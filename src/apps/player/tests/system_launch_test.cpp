#include "system_launch.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

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

    void set_env(const char* name, const std::optional<std::string>& value) {
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
            set_env(name_, value);
        }

        ~env_guard() { set_env(name_, previous_); }

        env_guard(const env_guard&) = delete;
        env_guard& operator=(const env_guard&) = delete;

      private:
        const char* name_{};
        std::optional<std::string> previous_{};
    };

} // namespace

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
