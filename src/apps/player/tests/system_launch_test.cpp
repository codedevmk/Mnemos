#include "amiga500_adapter.hpp"
#include "amiga500_system.hpp"
#include "deflate.hpp"
#include "system_launch.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
    namespace fs = std::filesystem;

    using mnemos::apps::player::adapters::amiga500::amiga500_adapter;
    using mnemos::manifests::amiga500::amiga500_system;

    [[nodiscard]] std::optional<std::string> read_env(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char* value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (value == nullptr) {
            return std::nullopt;
        }
        return std::string{value};
    }

    [[nodiscard]] int set_env(const char* name, const std::string& value) {
#if defined(_WIN32)
        return _putenv_s(name, value.c_str());
#else
        return setenv(name, value.c_str(), 1);
#endif
    }

    [[nodiscard]] int unset_env(const char* name) {
#if defined(_WIN32)
        return _putenv_s(name, "");
#else
        return unsetenv(name);
#endif
    }

    class scoped_env final {
      public:
        explicit scoped_env(std::vector<const char*> names) {
            names_.reserve(names.size() + 10U);
            previous_.reserve(names.size() + 10U);
            bool saw_msx = false;
            bool saw_msx2 = false;
            const auto capture = [this](const char* name) {
                if (std::find(names_.begin(), names_.end(), name) != names_.end()) {
                    return;
                }
                names_.push_back(name);
                previous_.push_back(read_env(name));
                (void)unset_env(name);
            };

            for (const char* name : names) {
                const std::string_view env_name{name};
                saw_msx2 = saw_msx2 || env_name.rfind("MNEMOS_MSX2_", 0U) == 0U;
                saw_msx = saw_msx ||
                          (env_name.rfind("MNEMOS_MSX_", 0U) == 0U &&
                           env_name.rfind("MNEMOS_MSX2_", 0U) != 0U);
                capture(name);
            }

            if (saw_msx) {
                capture("MNEMOS_MSX_EXPANDED_SLOTS");
                capture("MNEMOS_MSX_RAM_SLOT");
                capture("MNEMOS_MSX_RAM_SIZE");
                capture("MNEMOS_MSX_DISK_SLOT");
                capture("MNEMOS_MSX_CART2_SLOT");
                capture("MNEMOS_MSX_LOGO_ROM");
            }
            if (saw_msx2) {
                capture("MNEMOS_MSX2_EXPANDED_SLOTS");
                capture("MNEMOS_MSX2_RAM_SLOT");
                capture("MNEMOS_MSX2_SUB_SLOT");
                capture("MNEMOS_MSX2_DISK_SLOT");
                capture("MNEMOS_MSX2_CART2_SLOT");
                capture("MNEMOS_MSX2_RAM_SIZE");
                capture("MNEMOS_MSX2_LOGO_ROM");
            }
        }

        scoped_env(const scoped_env&) = delete;
        scoped_env& operator=(const scoped_env&) = delete;

        ~scoped_env() {
            for (std::size_t i = 0; i < names_.size(); ++i) {
                if (previous_[i]) {
                    (void)set_env(names_[i].c_str(), *previous_[i]);
                } else {
                    (void)unset_env(names_[i].c_str());
                }
            }
        }

      private:
        std::vector<std::string> names_{};
        std::vector<std::optional<std::string>> previous_{};
    };

    [[nodiscard]] fs::path unique_test_dir() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        return fs::temp_directory_path() /
               ("mnemos_system_launch_test_" + std::to_string(static_cast<long long>(now)));
    }

    void write_image(const fs::path& path, std::size_t bytes, std::uint8_t fill) {
        fs::create_directories(path.parent_path());
        std::vector<std::uint8_t> data(bytes, fill);
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.good());
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        REQUIRE(out.good());
    }

    void write_image(const fs::path& path, const std::vector<std::uint8_t>& data) {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.good());
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        REQUIRE(out.good());
    }

    void append_u16_le(std::vector<std::uint8_t>& out, std::uint16_t value) {
        out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    }

    void append_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
        out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
    }

    void append_ascii(std::vector<std::uint8_t>& out, std::string_view text) {
        for (char ch : text) {
            out.push_back(static_cast<std::uint8_t>(ch));
        }
    }

    [[nodiscard]] std::vector<std::uint8_t> stored_zip(std::string_view entry_name,
                                                       std::span<const std::uint8_t> data) {
        REQUIRE(entry_name.size() <= 0xFFFFU);
        REQUIRE(data.size() <= 0xFFFFFFFFULL);

        const auto name_size = static_cast<std::uint16_t>(entry_name.size());
        const auto data_size = static_cast<std::uint32_t>(data.size());
        std::vector<std::uint8_t> zip;
        zip.reserve(30U + entry_name.size() + data.size() + 46U + entry_name.size() + 22U);

        append_u32_le(zip, 0x04034B50U);
        append_u16_le(zip, 20U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u32_le(zip, 0U);
        append_u32_le(zip, data_size);
        append_u32_le(zip, data_size);
        append_u16_le(zip, name_size);
        append_u16_le(zip, 0U);
        append_ascii(zip, entry_name);
        zip.insert(zip.end(), data.begin(), data.end());

        REQUIRE(zip.size() <= 0xFFFFFFFFULL);
        const auto central_offset = static_cast<std::uint32_t>(zip.size());
        append_u32_le(zip, 0x02014B50U);
        append_u16_le(zip, 20U);
        append_u16_le(zip, 20U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u32_le(zip, 0U);
        append_u32_le(zip, data_size);
        append_u32_le(zip, data_size);
        append_u16_le(zip, name_size);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u32_le(zip, 0U);
        append_u32_le(zip, 0U);
        append_ascii(zip, entry_name);

        REQUIRE(zip.size() - central_offset <= 0xFFFFFFFFULL);
        const auto central_size = static_cast<std::uint32_t>(zip.size() - central_offset);
        append_u32_le(zip, 0x06054B50U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 1U);
        append_u16_le(zip, 1U);
        append_u32_le(zip, central_size);
        append_u32_le(zip, central_offset);
        append_u16_le(zip, 0U);
        return zip;
    }

    [[nodiscard]] std::vector<std::uint8_t> msx_cas_image(std::uint8_t payload) {
        return {0x1FU, 0xA6U, 0xDEU, 0xBAU, 0xCCU, 0x13U, 0x7DU, 0x74U, payload};
    }

    [[nodiscard]] std::vector<std::uint8_t> msx_dsk_image(std::uint8_t marker) {
        constexpr std::size_t bytes_per_sector = 512U;
        constexpr std::size_t bytes = 80U * 2U * 9U * bytes_per_sector;
        std::vector<std::uint8_t> disk(bytes, 0xE5U);
        disk[0] = marker;
        return disk;
    }

    [[nodiscard]] std::vector<std::uint8_t> msx_banked_8k_cart(std::uint8_t base) {
        std::vector<std::uint8_t> cart(0x2000U * 8U, 0x00U);
        for (std::size_t bank = 0; bank < 8U; ++bank) {
            cart[bank * 0x2000U] = static_cast<std::uint8_t>(base + bank);
        }
        return cart;
    }

    [[nodiscard]] std::vector<std::uint8_t> packed_msx2_firmware() {
        std::vector<std::uint8_t> firmware(0x10000U, std::uint8_t{0x00U});
        std::fill_n(firmware.begin(), 0x8000U, std::uint8_t{0x22U});
        std::fill_n(firmware.begin() + 0x8000U, 0x4000U, std::uint8_t{0x33U});
        std::fill_n(firmware.begin() + 0xC000U, 0x4000U, std::uint8_t{0x44U});
        return firmware;
    }

    void append_out_immediate(std::vector<std::uint8_t>& program, std::uint8_t port,
                              std::uint8_t value) {
        program.push_back(0x3EU); // LD A,n
        program.push_back(value);
        program.push_back(0xD3U); // OUT (n),A
        program.push_back(port);
    }

    void append_vdp_vram_write(std::vector<std::uint8_t>& program, std::uint16_t address,
                               std::uint8_t value) {
        append_out_immediate(program, 0x99U, static_cast<std::uint8_t>(address & 0xFFU));
        append_out_immediate(program, 0x99U,
                             static_cast<std::uint8_t>(0x40U | ((address >> 8U) & 0x3FU)));
        append_out_immediate(program, 0x98U, value);
    }

    void append_vdp_register_write(std::vector<std::uint8_t>& program, std::uint8_t reg,
                                   std::uint8_t value) {
        append_out_immediate(program, 0x99U, value);
        append_out_immediate(program, 0x99U, static_cast<std::uint8_t>(0x80U | (reg & 0x3FU)));
    }

    [[nodiscard]] std::vector<std::uint8_t> generated_msx_boot_bios() {
        std::vector<std::uint8_t> bios(0x8000U, 0x76U);
        std::vector<std::uint8_t> program;
        program.reserve(96U);

        append_vdp_vram_write(program, 0x0008U, 0x80U);
        append_vdp_vram_write(program, 0x0800U, 0x01U);
        append_vdp_vram_write(program, 0x2000U, 0xF1U);
        append_vdp_register_write(program, 1U, 0x40U);
        append_vdp_register_write(program, 2U, 0x02U);
        append_vdp_register_write(program, 3U, 0x80U);
        append_vdp_register_write(program, 4U, 0x00U);
        program.push_back(0x76U);

        REQUIRE(program.size() <= bios.size());
        std::copy(program.begin(), program.end(), bios.begin());
        return bios;
    }

    [[nodiscard]] bool framebuffer_is_uniform(const mnemos::chips::frame_buffer_view& fb) {
        if (fb.pixels == nullptr || fb.width == 0U || fb.height == 0U) {
            return true;
        }
        const std::uint32_t first = fb.pixels[0];
        const std::uint32_t stride = fb.effective_stride();
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                if (row[x] != first) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] std::uint64_t framebuffer_digest(const mnemos::chips::frame_buffer_view& fb) {
        std::uint64_t hash = 1469598103934665603ULL;
        const auto mix = [&hash](std::uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ULL;
        };
        mix(fb.width);
        mix(fb.height);
        const std::uint32_t stride = fb.effective_stride();
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                mix(row[x]);
            }
        }
        return hash;
    }

    const mnemos::frontend_sdk::media_image_info*
    find_media(const mnemos::frontend_sdk::player_system& system, std::string_view id) {
        const auto& media = system.media_capabilities().media;
        const auto it = std::find_if(media.begin(), media.end(),
                                     [&](const auto& image) { return image.id == id; });
        return it == media.end() ? nullptr : &*it;
    }

    [[nodiscard]] bool has_spec(const mnemos::frontend_sdk::player_system& system,
                                std::string_view label, std::string_view value) {
        const auto& spec = system.system_spec();
        return std::any_of(spec.begin(), spec.end(), [&](const auto& field) {
            return field.label == label && field.value == value;
        });
    }


    [[nodiscard]] std::vector<std::uint8_t> tiny_kickstart() {
        std::vector<std::uint8_t> rom(amiga500_system::kickstart_window_size, 0x00U);
        const auto w16 = [&](std::size_t offset, std::uint16_t value) {
            rom[offset] = static_cast<std::uint8_t>(value >> 8U);
            rom[offset + 1U] = static_cast<std::uint8_t>(value);
        };
        const auto w32 = [&](std::size_t offset, std::uint32_t value) {
            rom[offset + 0U] = static_cast<std::uint8_t>(value >> 24U);
            rom[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
            rom[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
            rom[offset + 3U] = static_cast<std::uint8_t>(value);
        };
        w32(0x0000U, 0x0007F000U);
        w32(0x0004U, amiga500_system::kickstart_base + 0x0008U);
        w16(0x0008U, 0x46FCU); // MOVE #$2700,SR
        w16(0x000AU, 0x2700U);
        w16(0x000CU, 0x60FEU); // BRA.S self
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> tiny_adf() {
        std::vector<std::uint8_t> adf(amiga500_system::floppy_dd_size, 0x00U);
        adf[0] = 0x44U;
        adf[1] = 0x89U;
        return adf;
    }

    [[nodiscard]] std::vector<std::uint8_t>
    deflated_zip(std::string_view entry_name, std::span<const std::uint8_t> data) {
        const std::vector<std::uint8_t> payload = mnemos::compression::deflate_raw(data);
        REQUIRE(entry_name.size() <= 0xFFFFU);
        REQUIRE(payload.size() <= 0xFFFFFFFFULL);
        REQUIRE(data.size() <= 0xFFFFFFFFULL);

        const auto name_size = static_cast<std::uint16_t>(entry_name.size());
        const auto compressed_size = static_cast<std::uint32_t>(payload.size());
        const auto uncompressed_size = static_cast<std::uint32_t>(data.size());
        std::vector<std::uint8_t> zip;
        zip.reserve(30U + entry_name.size() + payload.size() + 46U + entry_name.size() + 22U);

        append_u32_le(zip, 0x04034B50U);
        append_u16_le(zip, 20U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 8U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u32_le(zip, 0U);
        append_u32_le(zip, compressed_size);
        append_u32_le(zip, uncompressed_size);
        append_u16_le(zip, name_size);
        append_u16_le(zip, 0U);
        append_ascii(zip, entry_name);
        zip.insert(zip.end(), payload.begin(), payload.end());

        const auto central_offset = static_cast<std::uint32_t>(zip.size());
        append_u32_le(zip, 0x02014B50U);
        append_u16_le(zip, 20U);
        append_u16_le(zip, 20U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 8U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u32_le(zip, 0U);
        append_u32_le(zip, compressed_size);
        append_u32_le(zip, uncompressed_size);
        append_u16_le(zip, name_size);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u32_le(zip, 0U);
        append_u32_le(zip, 0U);
        append_ascii(zip, entry_name);

        const auto central_size = static_cast<std::uint32_t>(zip.size() - central_offset);
        append_u32_le(zip, 0x06054B50U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 0U);
        append_u16_le(zip, 1U);
        append_u16_le(zip, 1U);
        append_u32_le(zip, central_size);
        append_u32_le(zip, central_offset);
        append_u16_le(zip, 0U);
        return zip;
    }

    [[nodiscard]] constexpr std::uint32_t i(std::uint8_t op, std::uint8_t rs, std::uint8_t rt,
                                            std::uint16_t imm) {
        return (static_cast<std::uint32_t>(op) << 26U) |
               (static_cast<std::uint32_t>(rs) << 21U) |
               (static_cast<std::uint32_t>(rt) << 16U) | imm;
    }

    void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8U));
    }

    void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
        put16(out, static_cast<std::uint16_t>(v));
        put16(out, static_cast<std::uint16_t>(v >> 16U));
    }

    void put_be32(std::vector<std::uint8_t>& v, std::size_t off, std::uint32_t x) {
        v[off] = static_cast<std::uint8_t>(x >> 24U);
        v[off + 1U] = static_cast<std::uint8_t>(x >> 16U);
        v[off + 2U] = static_cast<std::uint8_t>(x >> 8U);
        v[off + 3U] = static_cast<std::uint8_t>(x);
    }

    void put_be64(std::vector<std::uint8_t>& v, std::size_t off, std::uint64_t x) {
        for (int n = 7; n >= 0; --n) {
            v[off + static_cast<std::size_t>(n)] = static_cast<std::uint8_t>(x);
            x >>= 8U;
        }
    }

    [[nodiscard]] std::vector<std::uint8_t> make_bios() {
        std::vector<std::uint8_t> bios;
        put32(bios, i(0x09U, 0U, 1U, 0x1234U)); // ADDIU AT,R0,1234
        put32(bios, i(0x2BU, 0U, 1U, 0x0000U)); // SW AT,0(R0)
        put32(bios, i(0x04U, 0U, 0U, 0xFFFFU)); // BEQ R0,R0,$-4
        put32(bios, 0U);                         // delay-slot NOP
        return bios;
    }

    [[nodiscard]] std::vector<std::uint8_t>
    make_stored_zip(const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& entries) {
        std::vector<std::uint8_t> out;
        struct central final {
            std::string name;
            std::uint32_t size;
            std::uint32_t local_offset;
        };
        std::vector<central> directory;
        for (const auto& [name, data] : entries) {
            const auto local_offset = static_cast<std::uint32_t>(out.size());
            const auto size = static_cast<std::uint32_t>(data.size());
            put32(out, 0x04034B50U);
            put16(out, 20U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, 0U);
            put32(out, size);
            put32(out, size);
            put16(out, static_cast<std::uint16_t>(name.size()));
            put16(out, 0U);
            out.insert(out.end(), name.begin(), name.end());
            out.insert(out.end(), data.begin(), data.end());
            directory.push_back({name, size, local_offset});
        }
        const auto cd_offset = static_cast<std::uint32_t>(out.size());
        for (const central& c : directory) {
            put32(out, 0x02014B50U);
            put16(out, 20U);
            put16(out, 20U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, 0U);
            put32(out, c.size);
            put32(out, c.size);
            put16(out, static_cast<std::uint16_t>(c.name.size()));
            put16(out, 0U);
            put16(out, 0U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, c.local_offset);
            out.insert(out.end(), c.name.begin(), c.name.end());
        }
        const auto cd_size = static_cast<std::uint32_t>(out.size()) - cd_offset;
        put32(out, 0x06054B50U);
        put16(out, 0U);
        put16(out, 0U);
        put16(out, static_cast<std::uint16_t>(directory.size()));
        put16(out, static_cast<std::uint16_t>(directory.size()));
        put32(out, cd_size);
        put32(out, cd_offset);
        put16(out, 0U);
        return out;
    }

    [[nodiscard]] std::vector<std::uint8_t> make_none_block_chd() {
        constexpr std::uint32_t hunk_bytes = 4096U;
        constexpr std::uint32_t unit_bytes = 512U;
        const std::uint64_t hunk_data_offset = hunk_bytes;
        const std::uint64_t map_offset = hunk_data_offset + hunk_bytes;
        std::vector<std::uint8_t> chd(static_cast<std::size_t>(map_offset + 4U), 0);
        std::memcpy(chd.data(), "MComprHD", 8U);
        put_be32(chd, 8U, 124U);
        put_be32(chd, 12U, 5U);
        put_be64(chd, 32U, hunk_bytes);
        put_be64(chd, 40U, map_offset);
        put_be32(chd, 56U, hunk_bytes);
        put_be32(chd, 60U, unit_bytes);
        for (std::uint32_t n = 0U; n < hunk_bytes; ++n) {
            chd[static_cast<std::size_t>(hunk_data_offset) + n] =
                static_cast<std::uint8_t>(0xA5U ^ n);
        }
        put_be32(chd, static_cast<std::size_t>(map_offset), 1U);
        return chd;
    }

    [[nodiscard]] std::vector<std::uint8_t> make_package() {
        return make_stored_zip({{"readme.txt", {'G', '-', 'N', 'E', 'T'}},
                                {"card.chd", make_none_block_chd()}});
    }

    void write_bytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.good());
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        REQUIRE(out.good());
    }

    [[nodiscard]] const char* getenv_checked(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: test environment preservation
#endif
        const char* value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        return value;
    }

    void set_optional_env(const char* name, const std::optional<std::string>& value) {
#if defined(_WIN32)
        static_cast<void>(_putenv_s(name, value.has_value() ? value->c_str() : ""));
#else
        if (value.has_value()) {
            static_cast<void>(setenv(name, value->c_str(), 1));
        } else {
            static_cast<void>(unsetenv(name));
        }
#endif
    }

    class env_guard final {
      public:
        env_guard(const char* name, std::optional<std::string> value) : name_(name) {
            if (const char* previous = getenv_checked(name)) {
                previous_ = std::string{previous};
            }
            set_optional_env(name_, value);
        }

        ~env_guard() { set_optional_env(name_, previous_); }

        env_guard(const env_guard&) = delete;
        env_guard& operator=(const env_guard&) = delete;

      private:
        const char* name_{};
        std::optional<std::string> previous_{};
    };

} // namespace

TEST_CASE("system launch boots generated MSX BIOS to deterministic frames",
          "[apps][player][launch][msx][boot]") {
    scoped_env env(
        {"MNEMOS_MSX_BIOS", "MNEMOS_MSX_DISK_ROM", "MNEMOS_MSX_KANJI_ROM", "MNEMOS_MSX_CAS"});
    const fs::path dir = unique_test_dir();
    const fs::path bios = dir / "generated-msx-bios.rom";
    write_image(bios, generated_msx_boot_bios());

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(bios.string());
    options.system_arg = std::string{"msx"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(outcome.primary_media_path == bios.string());
    CHECK(find_media(*outcome.system, "cart") == nullptr);

    outcome.system->step_one_frame();
    outcome.system->step_one_frame();

    const auto fb = outcome.system->current_frame();
    REQUIRE(fb.pixels != nullptr);
    CHECK_FALSE(framebuffer_is_uniform(fb));
    CHECK(fb.pixels[0] == 0x00FFFFFFU);
    CHECK(fb.pixels[1] == 0x00000000U);
    CHECK(framebuffer_digest(fb) == 0x94B67168FBE6DCC4ULL);

    fs::remove_all(dir);
}

TEST_CASE("system launch boots generated MSX2 BIOS to deterministic frames",
          "[apps][player][launch][msx2][boot]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();
    const fs::path bios = dir / "generated-msx2-bios.rom";
    const fs::path cart = dir / "empty-cart.rom";
    write_image(bios, generated_msx_boot_bios());
    write_image(cart, 0x4000U, 0x00U);

    REQUIRE(set_env("MNEMOS_MSX2_BIOS", bios.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart.string());
    options.system_arg = std::string{"msx2"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(outcome.primary_media_path == cart.string());
    REQUIRE(find_media(*outcome.system, "cart") != nullptr);

    outcome.system->step_one_frame();
    outcome.system->step_one_frame();

    const auto fb = outcome.system->current_frame();
    REQUIRE(fb.pixels != nullptr);
    CHECK_FALSE(framebuffer_is_uniform(fb));
    CHECK(fb.pixels[0] == 0x00FFFFFFU);
    CHECK(fb.pixels[1] == 0x00000000U);
    CHECK(framebuffer_digest(fb) == 0x94B67168FBE6DCC4ULL);

    fs::remove_all(dir);
}

TEST_CASE("system launch applies MSX machine slot profile environment",
          "[apps][player][launch][msx][slots]") {
    scoped_env env(
        {"MNEMOS_MSX_BIOS", "MNEMOS_MSX_DISK_ROM", "MNEMOS_MSX_KANJI_ROM", "MNEMOS_MSX_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart = dir / "game.rom";
    const fs::path bios = dir / "msx-bios.rom";
    write_image(cart, 0x4000U, 0x11U);
    write_image(bios, 0x8000U, 0x22U);

    REQUIRE(set_env("MNEMOS_MSX_BIOS", bios.string()) == 0);
    REQUIRE(set_env("MNEMOS_MSX_EXPANDED_SLOTS", "0x8") == 0);
    REQUIRE(set_env("MNEMOS_MSX_RAM_SLOT", "3.2") == 0);
    REQUIRE(set_env("MNEMOS_MSX_RAM_SIZE", "256K") == 0);
    REQUIRE(set_env("MNEMOS_MSX_DISK_SLOT", "3.1") == 0);
    REQUIRE(set_env("MNEMOS_MSX_CART2_SLOT", "2.3") == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart.string());
    options.system_arg = std::string{"msx"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(has_spec(*outcome.system, "Slot Layout", "expanded=8 ram=3.2 disk=3.1 cart2=2.3"));
    CHECK(has_spec(*outcome.system, "RAM Mapper", "256 KiB"));

    fs::remove_all(dir);
}

TEST_CASE("system launch applies MSX2 machine slot and RAM profile environment",
          "[apps][player][launch][msx2][slots]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart = dir / "game.rom";
    const fs::path bios = dir / "msx2-bios.rom";
    write_image(cart, 0x4000U, 0x11U);
    write_image(bios, 0x8000U, 0x22U);

    REQUIRE(set_env("MNEMOS_MSX2_BIOS", bios.string()) == 0);
    REQUIRE(set_env("MNEMOS_MSX2_EXPANDED_SLOTS", "0,3") == 0);
    REQUIRE(set_env("MNEMOS_MSX2_RAM_SLOT", "3.1") == 0);
    REQUIRE(set_env("MNEMOS_MSX2_SUB_SLOT", "3.0") == 0);
    REQUIRE(set_env("MNEMOS_MSX2_DISK_SLOT", "2.0") == 0);
    REQUIRE(set_env("MNEMOS_MSX2_CART2_SLOT", "2.3") == 0);
    REQUIRE(set_env("MNEMOS_MSX2_RAM_SIZE", "256K") == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart.string());
    options.system_arg = std::string{"msx2"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(has_spec(*outcome.system, "Slot Layout",
                   "expanded=9 ram=3.1 sub=3.0 disk=2.0 cart2=2.3"));
    CHECK(has_spec(*outcome.system, "RAM Mapper", "256 KiB"));

    fs::remove_all(dir);
}

TEST_CASE("system launch mounts MSX primary CAS as cassette media",
          "[apps][player][launch][msx][cassette]") {
    scoped_env env(
        {"MNEMOS_MSX_BIOS", "MNEMOS_MSX_DISK_ROM", "MNEMOS_MSX_KANJI_ROM", "MNEMOS_MSX_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path tape = dir / "tiny.cas";
    const fs::path bios = dir / "msx-bios.rom";
    const std::vector<std::uint8_t> tape_image = msx_cas_image(0xC1U);

    write_image(tape, tape_image);
    write_image(bios, 0x8000U, 0x22U);

    REQUIRE(set_env("MNEMOS_MSX_BIOS", bios.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(tape.string());
    options.system_arg = std::string{"msx"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* tape_media = find_media(*outcome.system, "tape");
    REQUIRE(tape_media != nullptr);
    CHECK(tape_media->byte_count == tape_image.size());
    CHECK(tape_media->provider_id == "msx.cassette");
    CHECK(find_media(*outcome.system, "cart") == nullptr);
    CHECK(find_media(*outcome.system, "disk") == nullptr);
    CHECK(outcome.primary_media_path == tape.string());

    fs::remove_all(dir);
}

TEST_CASE("system launch mounts MSX cartridge with CAS environment media",
          "[apps][player][launch][msx][cassette]") {
    scoped_env env(
        {"MNEMOS_MSX_BIOS", "MNEMOS_MSX_DISK_ROM", "MNEMOS_MSX_KANJI_ROM", "MNEMOS_MSX_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart = dir / "game.rom";
    const fs::path bios = dir / "msx-bios.rom";
    const fs::path tape = dir / "resident.cas";
    const std::vector<std::uint8_t> tape_image = msx_cas_image(0xC3U);

    write_image(cart, 0x4000U, 0x11U);
    write_image(bios, 0x8000U, 0x22U);
    write_image(tape, tape_image);

    REQUIRE(set_env("MNEMOS_MSX_BIOS", bios.string()) == 0);
    REQUIRE(set_env("MNEMOS_MSX_CAS", tape.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart.string());
    options.system_arg = std::string{"msx"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* cart_media = find_media(*outcome.system, "cart");
    REQUIRE(cart_media != nullptr);
    CHECK(cart_media->byte_count == 0x4000U);
    const auto* tape_media = find_media(*outcome.system, "tape");
    REQUIRE(tape_media != nullptr);
    CHECK(tape_media->byte_count == tape_image.size());
    CHECK(tape_media->provider_id == "msx.cassette");
    CHECK(outcome.primary_media_path == cart.string());

    fs::remove_all(dir);
}

TEST_CASE("system launch mounts MSX Kanji ROM environment media",
          "[apps][player][launch][msx][kanji]") {
    scoped_env env(
        {"MNEMOS_MSX_BIOS", "MNEMOS_MSX_DISK_ROM", "MNEMOS_MSX_KANJI_ROM", "MNEMOS_MSX_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart = dir / "game.rom";
    const fs::path bios = dir / "msx-bios.rom";
    const fs::path kanji = dir / "msx-kanji.rom";

    write_image(cart, 0x4000U, 0x11U);
    write_image(bios, 0x8000U, 0x22U);
    write_image(kanji, 0x40000U, 0x33U);

    REQUIRE(set_env("MNEMOS_MSX_BIOS", bios.string()) == 0);
    REQUIRE(set_env("MNEMOS_MSX_KANJI_ROM", kanji.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart.string());
    options.system_arg = std::string{"msx"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* kanji_media = find_media(*outcome.system, "kanji");
    REQUIRE(kanji_media != nullptr);
    CHECK(kanji_media->byte_count == 0x40000U);
    CHECK(kanji_media->provider_id == "msx.kanji_rom");
    CHECK(has_spec(*outcome.system, "Kanji ROM", "JIS level 1+2"));
    CHECK(outcome.primary_media_path == cart.string());

    fs::remove_all(dir);
}

TEST_CASE("system launch mounts MSX logo ROM environment media",
          "[apps][player][launch][msx][firmware]") {
    scoped_env env(
        {"MNEMOS_MSX_BIOS", "MNEMOS_MSX_DISK_ROM", "MNEMOS_MSX_KANJI_ROM", "MNEMOS_MSX_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart = dir / "game.rom";
    const fs::path bios = dir / "msx-bios.rom";
    const fs::path logo = dir / "msx-logo.rom";

    write_image(cart, 0x4000U, 0x11U);
    write_image(bios, 0x8000U, 0x22U);
    write_image(logo, 0x4000U, 0x55U);

    REQUIRE(set_env("MNEMOS_MSX_BIOS", bios.string()) == 0);
    REQUIRE(set_env("MNEMOS_MSX_LOGO_ROM", logo.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart.string());
    options.system_arg = std::string{"msx"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* logo_media = find_media(*outcome.system, "logo_rom");
    REQUIRE(logo_media != nullptr);
    CHECK(logo_media->byte_count == 0x4000U);
    CHECK(logo_media->provider_id == "msx.adapter");
    CHECK(has_spec(*outcome.system, "Logo ROM", "slot 0 $8000-$BFFF"));
    CHECK(outcome.primary_media_path == cart.string());

    fs::remove_all(dir);
}

TEST_CASE("system launch passes MSX second cartridge mapper override",
          "[apps][player][launch][msx][mapper]") {
    scoped_env env(
        {"MNEMOS_MSX_BIOS", "MNEMOS_MSX_DISK_ROM", "MNEMOS_MSX_KANJI_ROM", "MNEMOS_MSX_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart1 = dir / "slot1.rom";
    const fs::path cart2 = dir / "slot2.rom";
    const fs::path bios = dir / "msx-bios.rom";

    write_image(cart1, 0x8000U, 0x11U);
    write_image(cart2, msx_banked_8k_cart(0x40U));
    write_image(bios, 0x8000U, 0x22U);

    REQUIRE(set_env("MNEMOS_MSX_BIOS", bios.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart1.string());
    options.rom_paths.push_back(cart2.string());
    options.system_arg = std::string{"msx"};
    options.mapper2_override = std::string{"ascii8"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* primary = find_media(*outcome.system, "cart");
    REQUIRE(primary != nullptr);
    CHECK(primary->byte_count == 0x8000U);
    const auto* secondary = find_media(*outcome.system, "cart2");
    REQUIRE(secondary != nullptr);
    CHECK(secondary->byte_count == 0x10000U);
    CHECK(has_spec(*outcome.system, "Cart Slot 2", "mounted"));
    CHECK(has_spec(*outcome.system, "Cart Slot 2 Mapper", "ASCII8"));

    fs::remove_all(dir);
}

TEST_CASE("system launch anchors MSX slot 2 SRAM saves beside the second cartridge",
          "[apps][player][launch][msx][mapper][sram]") {
    scoped_env env(
        {"MNEMOS_MSX_BIOS", "MNEMOS_MSX_DISK_ROM", "MNEMOS_MSX_KANJI_ROM", "MNEMOS_MSX_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart1 = dir / "slot1.rom";
    const fs::path cart2 = dir / "slot2.rom";
    const fs::path bios = dir / "msx-bios.rom";

    write_image(cart1, 0x8000U, 0x11U);
    write_image(cart2, msx_banked_8k_cart(0x40U));
    write_image(bios, 0x8000U, 0x22U);

    REQUIRE(set_env("MNEMOS_MSX_BIOS", bios.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart1.string());
    options.rom_paths.push_back(cart2.string());
    options.system_arg = std::string{"msx"};
    options.mapper2_override = std::string{"ascii8-sram8"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    CHECK(outcome.primary_media_path == cart1.string());
    CHECK(outcome.battery_media_path == cart2.string());
    REQUIRE(outcome.system->battery_ram().size() == 0x2000U);

    fs::remove_all(dir);
}

TEST_CASE("system launch mounts MSX primary DSK with disk interface ROM",
          "[apps][player][launch][msx][disk]") {
    scoped_env env(
        {"MNEMOS_MSX_BIOS", "MNEMOS_MSX_DISK_ROM", "MNEMOS_MSX_KANJI_ROM", "MNEMOS_MSX_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path disk = dir / "boot.dsk";
    const fs::path bios = dir / "msx-bios.rom";
    const fs::path disk_rom = dir / "msx-disk.rom";
    const std::vector<std::uint8_t> disk_image = msx_dsk_image(0xD1U);

    write_image(disk, disk_image);
    write_image(bios, 0x8000U, 0x22U);
    write_image(disk_rom, 0x4000U, 0x44U);

    REQUIRE(set_env("MNEMOS_MSX_BIOS", bios.string()) == 0);
    REQUIRE(set_env("MNEMOS_MSX_DISK_ROM", disk_rom.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(disk.string());
    options.system_arg = std::string{"msx"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* disk_media = find_media(*outcome.system, "disk");
    REQUIRE(disk_media != nullptr);
    CHECK(disk_media->byte_count == disk_image.size());
    CHECK(disk_media->provider_id == "msx.fdc");
    CHECK(find_media(*outcome.system, "cart") == nullptr);
    CHECK(find_media(*outcome.system, "tape") == nullptr);
    CHECK(outcome.primary_media_path == disk.string());

    fs::remove_all(dir);
}

TEST_CASE("system launch mounts zipped MSX primary DSK with disk interface ROM",
          "[apps][player][launch][msx][disk][zip]") {
    scoped_env env(
        {"MNEMOS_MSX_BIOS", "MNEMOS_MSX_DISK_ROM", "MNEMOS_MSX_KANJI_ROM", "MNEMOS_MSX_CAS"});
    const fs::path dir = unique_test_dir();

    const std::vector<std::uint8_t> disk_image = msx_dsk_image(0xD5U);
    const fs::path disk_zip = dir / "boot-disk.zip";
    const fs::path bios = dir / "msx-bios.rom";
    const fs::path disk_rom = dir / "msx-disk.rom";

    write_image(disk_zip, stored_zip("boot.dsk", disk_image));
    write_image(bios, 0x8000U, 0x22U);
    write_image(disk_rom, 0x4000U, 0x44U);

    REQUIRE(set_env("MNEMOS_MSX_BIOS", bios.string()) == 0);
    REQUIRE(set_env("MNEMOS_MSX_DISK_ROM", disk_rom.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(disk_zip.string());
    options.system_arg = std::string{"msx"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* disk_media = find_media(*outcome.system, "disk");
    REQUIRE(disk_media != nullptr);
    CHECK(disk_media->byte_count == disk_image.size());
    CHECK(disk_media->provider_id == "msx.fdc");
    CHECK(find_media(*outcome.system, "cart") == nullptr);
    CHECK(has_spec(*outcome.system, "Disk ROM", "WD1793 interface"));
    CHECK(outcome.primary_media_path == disk_zip.string());

    fs::remove_all(dir);
}

TEST_CASE("system launch rejects zipped MSX primary DSK without disk interface ROM",
          "[apps][player][launch][msx][disk][zip]") {
    scoped_env env(
        {"MNEMOS_MSX_BIOS", "MNEMOS_MSX_DISK_ROM", "MNEMOS_MSX_KANJI_ROM", "MNEMOS_MSX_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path disk_zip = dir / "boot-disk.zip";
    const fs::path bios = dir / "msx-bios.rom";

    write_image(disk_zip, stored_zip("boot.dsk", msx_dsk_image(0xD6U)));
    write_image(bios, 0x8000U, 0x22U);

    REQUIRE(set_env("MNEMOS_MSX_BIOS", bios.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(disk_zip.string());
    options.system_arg = std::string{"msx"};

    auto outcome = mnemos::apps::player::launch_system(options);
    CHECK(outcome.exit_code == 1);
    CHECK(outcome.system == nullptr);

    fs::remove_all(dir);
}

TEST_CASE("system launch rejects zipped MSX primary CAS without BIOS",
          "[apps][player][launch][msx][cassette][zip]") {
    scoped_env env(
        {"MNEMOS_MSX_BIOS", "MNEMOS_MSX_DISK_ROM", "MNEMOS_MSX_KANJI_ROM", "MNEMOS_MSX_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path tape_zip = dir / "program-tape.zip";
    write_image(tape_zip, stored_zip("program.cas", msx_cas_image(0xC5U)));

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(tape_zip.string());
    options.system_arg = std::string{"msx"};

    auto outcome = mnemos::apps::player::launch_system(options);
    CHECK(outcome.exit_code == 1);
    CHECK(outcome.system == nullptr);

    fs::remove_all(dir);
}

TEST_CASE("system launch accepts MSX2 firmware ROM alias environment variables",
          "[apps][player][launch][msx2]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart = dir / "tiny.rom";
    const fs::path bios = dir / "msx2-bios.rom";
    const fs::path sub_rom = dir / "msx2-sub.rom";
    const fs::path logo_rom = dir / "msx2-logo.rom";
    const fs::path disk_rom = dir / "msx2-disk.rom";

    write_image(cart, 0x4000U, 0x11U);
    write_image(bios, 0x8000U, 0x22U);
    write_image(sub_rom, 0x4000U, 0x33U);
    write_image(logo_rom, 0x4000U, 0x55U);
    write_image(disk_rom, 0x4000U, 0x44U);

    REQUIRE(set_env("MNEMOS_MSX2_BIOS", bios.string()) == 0);
    REQUIRE(set_env("MNEMOS_MSX2_SUB_ROM", sub_rom.string()) == 0);
    REQUIRE(set_env("MNEMOS_MSX2_LOGO_ROM", logo_rom.string()) == 0);
    REQUIRE(set_env("MNEMOS_MSX2_DISK_ROM", disk_rom.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart.string());
    options.system_arg = std::string{"msx2"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* sub = find_media(*outcome.system, "sub_bios");
    REQUIRE(sub != nullptr);
    CHECK(sub->byte_count == 0x4000U);
    const auto* logo = find_media(*outcome.system, "logo_rom");
    REQUIRE(logo != nullptr);
    CHECK(logo->byte_count == 0x4000U);
    const auto* disk = find_media(*outcome.system, "disk_rom");
    REQUIRE(disk != nullptr);
    CHECK(disk->byte_count == 0x4000U);
    CHECK(outcome.primary_media_path == cart.string());

    fs::remove_all(dir);
}

TEST_CASE("system launch accepts packed MSX2 firmware image",
          "[apps][player][launch][msx2][firmware]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart = dir / "tiny.rom";
    const fs::path firmware = dir / "packed-msx2-firmware.rom";

    write_image(cart, 0x4000U, 0x11U);
    write_image(firmware, packed_msx2_firmware());

    REQUIRE(set_env("MNEMOS_MSX2_FIRMWARE", firmware.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart.string());
    options.system_arg = std::string{"msx2"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* bios = find_media(*outcome.system, "bios");
    REQUIRE(bios != nullptr);
    CHECK(bios->byte_count == 0x8000U);
    const auto* sub = find_media(*outcome.system, "sub_bios");
    REQUIRE(sub != nullptr);
    CHECK(sub->byte_count == 0x4000U);
    const auto* disk = find_media(*outcome.system, "disk_rom");
    REQUIRE(disk != nullptr);
    CHECK(disk->byte_count == 0x4000U);
    CHECK(find_media(*outcome.system, "cart") != nullptr);
    CHECK(outcome.primary_media_path == cart.string());

    fs::remove_all(dir);
}

TEST_CASE("system launch splits packed MSX2 BIOS image", "[apps][player][launch][msx2][firmware]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart = dir / "tiny.rom";
    const fs::path bios_image = dir / "packed-as-bios.rom";

    write_image(cart, 0x4000U, 0x11U);
    write_image(bios_image, packed_msx2_firmware());

    REQUIRE(set_env("MNEMOS_MSX2_BIOS", bios_image.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart.string());
    options.system_arg = std::string{"msx2"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* bios = find_media(*outcome.system, "bios");
    REQUIRE(bios != nullptr);
    CHECK(bios->byte_count == 0x8000U);
    const auto* sub = find_media(*outcome.system, "sub_bios");
    REQUIRE(sub != nullptr);
    CHECK(sub->byte_count == 0x4000U);
    const auto* disk = find_media(*outcome.system, "disk_rom");
    REQUIRE(disk != nullptr);
    CHECK(disk->byte_count == 0x4000U);

    fs::remove_all(dir);
}

TEST_CASE("system launch mounts MSX2 primary CAS as cassette media",
          "[apps][player][launch][msx2][cassette]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path tape = dir / "tiny.cas";
    const fs::path bios = dir / "msx2-bios.rom";
    const std::vector<std::uint8_t> tape_image = msx_cas_image(0xC2U);

    write_image(tape, tape_image);
    write_image(bios, 0x8000U, 0x22U);

    REQUIRE(set_env("MNEMOS_MSX2_BIOS", bios.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(tape.string());
    options.system_arg = std::string{"msx2"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* tape_media = find_media(*outcome.system, "tape");
    REQUIRE(tape_media != nullptr);
    CHECK(tape_media->byte_count == tape_image.size());
    CHECK(tape_media->provider_id == "msx.cassette");
    CHECK(find_media(*outcome.system, "cart") == nullptr);
    CHECK(find_media(*outcome.system, "disk.0") == nullptr);
    CHECK(outcome.primary_media_path == tape.string());

    fs::remove_all(dir);
}

TEST_CASE("system launch mounts MSX2 cartridge with CAS environment media",
          "[apps][player][launch][msx2][cassette]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart = dir / "game.rom";
    const fs::path bios = dir / "msx2-bios.rom";
    const fs::path tape = dir / "resident.cas";
    const std::vector<std::uint8_t> tape_image = msx_cas_image(0xC4U);

    write_image(cart, 0x4000U, 0x11U);
    write_image(bios, 0x8000U, 0x22U);
    write_image(tape, tape_image);

    REQUIRE(set_env("MNEMOS_MSX2_BIOS", bios.string()) == 0);
    REQUIRE(set_env("MNEMOS_MSX2_CAS", tape.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart.string());
    options.system_arg = std::string{"msx2"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* cart_media = find_media(*outcome.system, "cart");
    REQUIRE(cart_media != nullptr);
    CHECK(cart_media->byte_count == 0x4000U);
    const auto* tape_media = find_media(*outcome.system, "tape");
    REQUIRE(tape_media != nullptr);
    CHECK(tape_media->byte_count == tape_image.size());
    CHECK(tape_media->provider_id == "msx.cassette");
    CHECK(outcome.primary_media_path == cart.string());

    fs::remove_all(dir);
}

TEST_CASE("system launch mounts MSX2 Kanji ROM environment media",
          "[apps][player][launch][msx2][kanji]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart = dir / "game.rom";
    const fs::path bios = dir / "msx2-bios.rom";
    const fs::path kanji = dir / "msx2-kanji.rom";

    write_image(cart, 0x4000U, 0x11U);
    write_image(bios, 0x8000U, 0x22U);
    write_image(kanji, 0x40000U, 0x33U);

    REQUIRE(set_env("MNEMOS_MSX2_BIOS", bios.string()) == 0);
    REQUIRE(set_env("MNEMOS_MSX2_KANJI_ROM", kanji.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart.string());
    options.system_arg = std::string{"msx2"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* kanji_media = find_media(*outcome.system, "kanji");
    REQUIRE(kanji_media != nullptr);
    CHECK(kanji_media->byte_count == 0x40000U);
    CHECK(kanji_media->provider_id == "msx.kanji_rom");
    CHECK(has_spec(*outcome.system, "Kanji ROM", "JIS level 1+2"));
    CHECK(outcome.primary_media_path == cart.string());

    fs::remove_all(dir);
}

TEST_CASE("system launch passes MSX2 second cartridge mapper override",
          "[apps][player][launch][msx2][mapper]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart1 = dir / "slot1.rom";
    const fs::path cart2 = dir / "slot2.rom";
    const fs::path bios = dir / "msx2-bios.rom";

    write_image(cart1, 0x8000U, 0x11U);
    write_image(cart2, msx_banked_8k_cart(0x50U));
    write_image(bios, 0x8000U, 0x22U);

    REQUIRE(set_env("MNEMOS_MSX2_BIOS", bios.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart1.string());
    options.rom_paths.push_back(cart2.string());
    options.system_arg = std::string{"msx2"};
    options.mapper2_override = std::string{"ascii8"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* primary = find_media(*outcome.system, "cart");
    REQUIRE(primary != nullptr);
    CHECK(primary->byte_count == 0x8000U);
    const auto* secondary = find_media(*outcome.system, "cart2");
    REQUIRE(secondary != nullptr);
    CHECK(secondary->byte_count == 0x10000U);
    CHECK(has_spec(*outcome.system, "Cart Slot 2", "mounted"));
    CHECK(has_spec(*outcome.system, "Cart Slot 2 Mapper", "ASCII8"));

    fs::remove_all(dir);
}

TEST_CASE("system launch anchors MSX2 slot 2 SRAM saves beside the second cartridge",
          "[apps][player][launch][msx2][mapper][sram]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path cart1 = dir / "slot1.rom";
    const fs::path cart2 = dir / "slot2.rom";
    const fs::path bios = dir / "msx2-bios.rom";

    write_image(cart1, 0x8000U, 0x11U);
    write_image(cart2, msx_banked_8k_cart(0x50U));
    write_image(bios, 0x8000U, 0x22U);

    REQUIRE(set_env("MNEMOS_MSX2_BIOS", bios.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(cart1.string());
    options.rom_paths.push_back(cart2.string());
    options.system_arg = std::string{"msx2"};
    options.mapper2_override = std::string{"ascii8-sram8"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    CHECK(outcome.primary_media_path == cart1.string());
    CHECK(outcome.battery_media_path == cart2.string());
    REQUIRE(outcome.system->battery_ram().size() == 0x2000U);

    fs::remove_all(dir);
}

TEST_CASE("system launch rejects MSX2 primary DSK without disk interface ROM",
          "[apps][player][launch][msx2][disk]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path disk = dir / "boot.dsk";
    const fs::path bios = dir / "msx2-bios.rom";

    write_image(disk, msx_dsk_image(0xD2U));
    write_image(bios, 0x8000U, 0x22U);

    REQUIRE(set_env("MNEMOS_MSX2_BIOS", bios.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(disk.string());
    options.system_arg = std::string{"msx2"};

    auto outcome = mnemos::apps::player::launch_system(options);
    CHECK(outcome.exit_code == 1);
    CHECK(outcome.system == nullptr);

    fs::remove_all(dir);
}

TEST_CASE("system launch rejects zipped MSX2 primary DSK without disk interface ROM",
          "[apps][player][launch][msx2][disk][zip]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path disk_zip = dir / "boot-disk.zip";
    const fs::path bios = dir / "msx2-bios.rom";

    write_image(disk_zip, stored_zip("boot.dsk", msx_dsk_image(0xD7U)));
    write_image(bios, 0x8000U, 0x22U);

    REQUIRE(set_env("MNEMOS_MSX2_BIOS", bios.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(disk_zip.string());
    options.system_arg = std::string{"msx2"};

    auto outcome = mnemos::apps::player::launch_system(options);
    CHECK(outcome.exit_code == 1);
    CHECK(outcome.system == nullptr);

    fs::remove_all(dir);
}

TEST_CASE("system launch mounts MSX2 primary DSK with packed firmware disk ROM",
          "[apps][player][launch][msx2][disk][firmware]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path disk = dir / "boot.dsk";
    const fs::path firmware = dir / "packed-msx2-firmware.rom";
    const std::vector<std::uint8_t> disk_image = msx_dsk_image(0xD4U);

    write_image(disk, disk_image);
    write_image(firmware, packed_msx2_firmware());

    REQUIRE(set_env("MNEMOS_MSX2_FIRMWARE", firmware.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(disk.string());
    options.system_arg = std::string{"msx2"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* disk_rom_media = find_media(*outcome.system, "disk_rom");
    REQUIRE(disk_rom_media != nullptr);
    CHECK(disk_rom_media->byte_count == 0x4000U);
    const auto* disk_media = find_media(*outcome.system, "disk.0");
    REQUIRE(disk_media != nullptr);
    CHECK(disk_media->byte_count == disk_image.size());
    CHECK(disk_media->provider_id == "msx2.wd1793");
    CHECK(find_media(*outcome.system, "cart") == nullptr);
    CHECK(outcome.primary_media_path == disk.string());

    fs::remove_all(dir);
}

TEST_CASE("system launch mounts MSX2 primary DSK with disk interface ROM",
          "[apps][player][launch][msx2][disk]") {
    scoped_env env({"MNEMOS_MSX2_BIOS", "MNEMOS_MSX2_FIRMWARE", "MNEMOS_MSX2_SUBROM",
                    "MNEMOS_MSX2_SUB_ROM", "MNEMOS_MSX2_DISKROM", "MNEMOS_MSX2_DISK_ROM",
                    "MNEMOS_MSX2_KANJI_ROM", "MNEMOS_MSX2_CAS"});
    const fs::path dir = unique_test_dir();

    const fs::path disk = dir / "boot.dsk";
    const fs::path bios = dir / "msx2-bios.rom";
    const fs::path disk_rom = dir / "msx2-disk.rom";
    const std::vector<std::uint8_t> disk_image = msx_dsk_image(0xD3U);

    write_image(disk, disk_image);
    write_image(bios, 0x8000U, 0x22U);
    write_image(disk_rom, 0x4000U, 0x44U);

    REQUIRE(set_env("MNEMOS_MSX2_BIOS", bios.string()) == 0);
    REQUIRE(set_env("MNEMOS_MSX2_DISK_ROM", disk_rom.string()) == 0);

    mnemos::apps::player::system_launch_options options{};
    options.rom_paths.push_back(disk.string());
    options.system_arg = std::string{"msx2"};

    auto outcome = mnemos::apps::player::launch_system(options);
    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);

    const auto* disk_rom_media = find_media(*outcome.system, "disk_rom");
    REQUIRE(disk_rom_media != nullptr);
    CHECK(disk_rom_media->byte_count == 0x4000U);
    const auto* disk_media = find_media(*outcome.system, "disk.0");
    REQUIRE(disk_media != nullptr);
    CHECK(disk_media->byte_count == disk_image.size());
    CHECK(disk_media->provider_id == "msx2.wd1793");
    CHECK(find_media(*outcome.system, "cart") == nullptr);
    CHECK(find_media(*outcome.system, "tape") == nullptr);
    CHECK(outcome.primary_media_path == disk.string());

    fs::remove_all(dir);
}


TEST_CASE("launch_system requires a G-NET BIOS for Taito G-NET packages",
          "[player][launch][taito_gnet]") {
    const auto root = std::filesystem::temp_directory_path() / "mnemos_taito_gnet_launch_test";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    const auto package_path = root / "synthetic_gnet.zip";
    write_bytes(package_path, make_package());

    env_guard bios_env("MNEMOS_TAITO_GNET_BIOS", std::nullopt);
    auto launch = mnemos::apps::player::launch_system(
        {.rom_paths = {package_path.string()}, .system_arg = std::string{"taito_gnet"}});
    CHECK(launch.exit_code == 1);
    CHECK(launch.system == nullptr);
}

TEST_CASE("launch_system boots Taito G-NET package with BIOS env",
          "[player][launch][taito_gnet]") {
    const auto root = std::filesystem::temp_directory_path() / "mnemos_taito_gnet_launch_test";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    const auto bios_path = root / "coh3002t.353";
    const auto package_path = root / "synthetic_gnet.zip";
    write_bytes(bios_path, make_bios());
    write_bytes(package_path, make_package());

    env_guard bios_env("MNEMOS_TAITO_GNET_BIOS", bios_path.string());
    auto launch = mnemos::apps::player::launch_system(
        {.rom_paths = {package_path.string()}, .system_arg = std::string{"gnet"}});

    REQUIRE(launch.exit_code == 0);
    REQUIRE(launch.system != nullptr);
    CHECK(launch.primary_media_path == package_path.string());
    CHECK(launch.system->system_spec()[1].value == "Taito G-NET / Sony ZN-2");
    CHECK(launch.system->media_capabilities().media.size() == 2U);

    launch.system->step_one_frame();
    CHECK(launch.system->current_frame().pixels != nullptr);
}

TEST_CASE("player launch boots Amiga500 from Kickstart env without disk media",
          "[apps][player][launch][amiga500]") {
    scoped_env env({"MNEMOS_AMIGA500_KICKSTART", "MNEMOS_AMIGA500_KEYBOARD_LAYOUT"});
    const fs::path dir = unique_test_dir();
    const fs::path rom_path = dir / "kick13.rom";
    write_image(rom_path, tiny_kickstart());
    REQUIRE(set_env("MNEMOS_AMIGA500_KICKSTART", rom_path.string()) == 0);
    REQUIRE(set_env("MNEMOS_AMIGA500_KEYBOARD_LAYOUT", "azerty") == 0);

    auto outcome =
        mnemos::apps::player::launch_system({.system_arg = std::string{"amiga500"}});

    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(outcome.primary_media_path.empty());
    CHECK(outcome.system->media_count() == 0U);
    CHECK(has_spec(*outcome.system, "BIOS", rom_path.stem().string()));
    CHECK(has_spec(*outcome.system, "Keyboard", "AZERTY"));

    fs::remove_all(dir);
}

TEST_CASE("player launch boots Amiga500+ from its Kickstart env without disk media",
          "[apps][player][launch][amiga500plus]") {
    scoped_env env({"MNEMOS_AMIGA500PLUS_KICKSTART", "MNEMOS_AMIGA500_KEYBOARD_LAYOUT"});
    const fs::path dir = unique_test_dir();
    const fs::path rom_path = dir / "kick20.rom";
    write_image(rom_path, tiny_kickstart());
    REQUIRE(set_env("MNEMOS_AMIGA500PLUS_KICKSTART", rom_path.string()) == 0);

    auto outcome =
        mnemos::apps::player::launch_system({.system_arg = std::string{"amiga500plus"}});

    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(outcome.primary_media_path.empty());
    CHECK(outcome.system->media_count() == 0U);
    CHECK(has_spec(*outcome.system, "System", "Amiga 500+"));
    CHECK(has_spec(*outcome.system, "Chip RAM", "1 MiB"));
    auto* adapter = dynamic_cast<amiga500_adapter*>(outcome.system.get());
    REQUIRE(adapter != nullptr);
    CHECK(adapter->system().chip_ram.size() == amiga500_system::chip_ram_size_1m);

    fs::remove_all(dir);
}

TEST_CASE("player launch boots Amiga600 from its Kickstart env without disk media",
          "[apps][player][launch][amiga600]") {
    scoped_env env({"MNEMOS_AMIGA600_KICKSTART", "MNEMOS_AMIGA500_KEYBOARD_LAYOUT"});
    const fs::path dir = unique_test_dir();
    const fs::path rom_path = dir / "kick20.rom";
    write_image(rom_path, tiny_kickstart());
    REQUIRE(set_env("MNEMOS_AMIGA600_KICKSTART", rom_path.string()) == 0);

    auto outcome = mnemos::apps::player::launch_system({.system_arg = std::string{"amiga600"}});

    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(outcome.primary_media_path.empty());
    CHECK(outcome.system->media_count() == 0U);
    CHECK(has_spec(*outcome.system, "System", "Amiga 600"));
    CHECK(has_spec(*outcome.system, "Chip RAM", "1 MiB"));
    auto* adapter = dynamic_cast<amiga500_adapter*>(outcome.system.get());
    REQUIRE(adapter != nullptr);
    CHECK(adapter->system().chip_ram.size() == amiga500_system::chip_ram_size_1m);

    fs::remove_all(dir);
}

TEST_CASE("player launch boots Amiga2000 from its Kickstart env without disk media",
          "[apps][player][launch][amiga2000]") {
    scoped_env env({"MNEMOS_AMIGA2000_KICKSTART", "MNEMOS_AMIGA500_KEYBOARD_LAYOUT"});
    const fs::path dir = unique_test_dir();
    const fs::path rom_path = dir / "kick13.rom";
    write_image(rom_path, tiny_kickstart());
    REQUIRE(set_env("MNEMOS_AMIGA2000_KICKSTART", rom_path.string()) == 0);

    auto outcome = mnemos::apps::player::launch_system({.system_arg = std::string{"amiga2000"}});

    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(outcome.primary_media_path.empty());
    CHECK(outcome.system->media_count() == 0U);
    CHECK(has_spec(*outcome.system, "System", "Amiga 2000"));
    CHECK(has_spec(*outcome.system, "Chip RAM", "512 KiB"));
    auto* adapter = dynamic_cast<amiga500_adapter*>(outcome.system.get());
    REQUIRE(adapter != nullptr);
    CHECK(adapter->system().chip_ram.size() == amiga500_system::chip_ram_size);

    fs::remove_all(dir);
}

TEST_CASE("player launch applies Amiga2000 ECS/1MiB model override",
          "[apps][player][launch][amiga2000]") {
    scoped_env env({"MNEMOS_AMIGA2000_KICKSTART", "MNEMOS_AMIGA500_KEYBOARD_LAYOUT",
                    "MNEMOS_AMIGA2000_MODEL"});
    const fs::path dir = unique_test_dir();
    const fs::path rom_path = dir / "kick20.rom";
    write_image(rom_path, tiny_kickstart());
    REQUIRE(set_env("MNEMOS_AMIGA2000_KICKSTART", rom_path.string()) == 0);

    auto outcome = mnemos::apps::player::launch_system(
        {.system_arg = std::string{"amiga2000"},
         .amiga_model_override = std::string{"ecs-1m"}});

    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(has_spec(*outcome.system, "System", "Amiga 2000"));
    CHECK(has_spec(*outcome.system, "Chip RAM", "1 MiB"));
    CHECK(has_spec(*outcome.system, "Configuration", "ECS / 1 MiB upgrade"));
    auto* adapter = dynamic_cast<amiga500_adapter*>(outcome.system.get());
    REQUIRE(adapter != nullptr);
    CHECK(adapter->system().chip_ram.size() == amiga500_system::chip_ram_size_1m);

    fs::remove_all(dir);
}

TEST_CASE("player launch applies Amiga2000 model env override",
          "[apps][player][launch][amiga2000]") {
    scoped_env env({"MNEMOS_AMIGA2000_KICKSTART", "MNEMOS_AMIGA500_KEYBOARD_LAYOUT",
                    "MNEMOS_AMIGA2000_MODEL"});
    const fs::path dir = unique_test_dir();
    const fs::path rom_path = dir / "kick20.rom";
    write_image(rom_path, tiny_kickstart());
    REQUIRE(set_env("MNEMOS_AMIGA2000_KICKSTART", rom_path.string()) == 0);
    REQUIRE(set_env("MNEMOS_AMIGA2000_MODEL", "ks2") == 0);

    auto outcome = mnemos::apps::player::launch_system({.system_arg = std::string{"amiga2000"}});

    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(has_spec(*outcome.system, "Chip RAM", "1 MiB"));
    CHECK(has_spec(*outcome.system, "Configuration", "ECS / 1 MiB upgrade"));

    fs::remove_all(dir);
}

TEST_CASE("player launch treats a zip-wrapped Amiga ADF as disk media",
          "[apps][player][launch][amiga500]") {
    scoped_env env({"MNEMOS_AMIGA500_KICKSTART", "MNEMOS_AMIGA500_KEYBOARD_LAYOUT"});
    const fs::path dir = unique_test_dir();
    const fs::path rom_path = dir / "kick13.rom";
    const fs::path disk_path = dir / "workbench.zip";
    write_image(rom_path, tiny_kickstart());
    const std::vector<std::uint8_t> disk_image = tiny_adf();
    write_image(disk_path, deflated_zip("Workbench.adf", disk_image));
    REQUIRE(set_env("MNEMOS_AMIGA500_KICKSTART", rom_path.string()) == 0);

    auto outcome = mnemos::apps::player::launch_system(
        {.rom_paths = {disk_path.string()}, .system_arg = std::string{"amiga500"}});

    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(outcome.primary_media_path == disk_path.string());
    CHECK(outcome.system->media_count() == 1U);
    auto* adapter = dynamic_cast<amiga500_adapter*>(outcome.system.get());
    REQUIRE(adapter != nullptr);
    CHECK(adapter->system().floppy_loaded());
    CHECK(adapter->system().floppy_size() == amiga500_system::floppy_dd_size);
    CHECK_FALSE(adapter->system().floppy_drives[0].change_latch);
    CHECK(has_spec(*outcome.system, "Disk", "Workbench"));

    fs::remove_all(dir);
}

TEST_CASE("player launch treats a zip-wrapped Amiga600 ADF as disk media",
          "[apps][player][launch][amiga600]") {
    scoped_env env({"MNEMOS_AMIGA600_KICKSTART", "MNEMOS_AMIGA500_KEYBOARD_LAYOUT"});
    const fs::path dir = unique_test_dir();
    const fs::path rom_path = dir / "kick20.rom";
    const fs::path disk_path = dir / "workbench.zip";
    write_image(rom_path, tiny_kickstart());
    const std::vector<std::uint8_t> disk_image = tiny_adf();
    write_image(disk_path, deflated_zip("Workbench.adf", disk_image));
    REQUIRE(set_env("MNEMOS_AMIGA600_KICKSTART", rom_path.string()) == 0);

    auto outcome = mnemos::apps::player::launch_system(
        {.rom_paths = {disk_path.string()}, .system_arg = std::string{"amiga600"}});

    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(outcome.primary_media_path == disk_path.string());
    CHECK(outcome.system->media_count() == 1U);
    auto* adapter = dynamic_cast<amiga500_adapter*>(outcome.system.get());
    REQUIRE(adapter != nullptr);
    CHECK(adapter->system().floppy_loaded());
    CHECK(adapter->system().floppy_size() == amiga500_system::floppy_dd_size);
    CHECK(has_spec(*outcome.system, "Disk", "Workbench"));

    fs::remove_all(dir);
}

TEST_CASE("player launch treats a zip-wrapped Amiga2000 ADF as disk media",
          "[apps][player][launch][amiga2000]") {
    scoped_env env({"MNEMOS_AMIGA2000_KICKSTART", "MNEMOS_AMIGA500_KEYBOARD_LAYOUT"});
    const fs::path dir = unique_test_dir();
    const fs::path rom_path = dir / "kick13.rom";
    const fs::path disk_path = dir / "workbench.zip";
    write_image(rom_path, tiny_kickstart());
    const std::vector<std::uint8_t> disk_image = tiny_adf();
    write_image(disk_path, deflated_zip("Workbench.adf", disk_image));
    REQUIRE(set_env("MNEMOS_AMIGA2000_KICKSTART", rom_path.string()) == 0);

    auto outcome = mnemos::apps::player::launch_system(
        {.rom_paths = {disk_path.string()}, .system_arg = std::string{"amiga2000"}});

    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(outcome.primary_media_path == disk_path.string());
    CHECK(outcome.system->media_count() == 1U);
    auto* adapter = dynamic_cast<amiga500_adapter*>(outcome.system.get());
    REQUIRE(adapter != nullptr);
    CHECK(adapter->system().floppy_loaded());
    CHECK(adapter->system().floppy_size() == amiga500_system::floppy_dd_size);
    CHECK(has_spec(*outcome.system, "Disk", "Workbench"));

    fs::remove_all(dir);
}

TEST_CASE("player launch passes keyboard layout override to Amiga500 adapter",
          "[apps][player][launch][amiga500]") {
    scoped_env env({"MNEMOS_AMIGA500_KICKSTART", "MNEMOS_AMIGA500_KEYBOARD_LAYOUT"});
    const fs::path dir = unique_test_dir();
    const fs::path rom_path = dir / "kick13.rom";
    write_image(rom_path, tiny_kickstart());

    auto outcome = mnemos::apps::player::launch_system({.rom_paths = {rom_path.string()},
                                                        .system_arg = std::string{"amiga500"},
                                                        .keyboard_layout_override =
                                                            std::string{"azerty"}});

    REQUIRE(outcome.exit_code == 0);
    REQUIRE(outcome.system != nullptr);
    CHECK(has_spec(*outcome.system, "Keyboard", "AZERTY"));

    fs::remove_all(dir);
}
