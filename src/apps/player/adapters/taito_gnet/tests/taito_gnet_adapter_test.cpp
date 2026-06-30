#include "taito_gnet_adapter.hpp"

#include "adapter_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

    namespace gnet = mnemos::apps::player::adapters::taito_gnet;

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

    [[nodiscard]] bool current_frame_has_variation(const gnet::taito_gnet_adapter& adapter) {
        const auto view = adapter.current_frame();
        if (view.pixels == nullptr || view.width == 0U || view.height == 0U) {
            return false;
        }
        const std::uint32_t first = view.pixels[0] & 0x00FFFFFFU;
        const std::uint32_t stride = view.effective_stride();
        for (std::uint32_t y = 0U; y < view.height; ++y) {
            const std::uint32_t* row = view.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0U; x < view.width; ++x) {
                if ((row[x] & 0x00FFFFFFU) != first) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] const char* opt_env(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in test data path
#endif
        const char* value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        return (value != nullptr && *value != '\0') ? value : nullptr;
    }

    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    read_file_bytes(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        const std::vector<char> raw{std::istreambuf_iterator<char>{in},
                                    std::istreambuf_iterator<char>{}};
        return std::vector<std::uint8_t>{raw.begin(), raw.end()};
    }

} // namespace

TEST_CASE("taito_gnet_adapter requires BIOS image in registry options",
          "[taito_gnet][adapter]") {
    gnet::force_link();
    mnemos::frontend_sdk::adapter_options options{};
    options.rom = make_package();
    options.display_name = "synthetic";
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("taito_gnet", std::move(options));
    CHECK(system == nullptr);
}

TEST_CASE("taito_gnet_adapter boots a board shell through the registry",
          "[taito_gnet][adapter]") {
    gnet::force_link();
    mnemos::frontend_sdk::adapter_options options{};
    options.rom = make_package();
    options.bios_images.push_back(make_bios());
    options.display_name = "synthetic";

    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("taito_gnet", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<gnet::taito_gnet_adapter&>(*system);

    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 1U);
    CHECK(adapter.machine().main_ram[0] == 0x34U);
    CHECK(adapter.machine().main_ram[1] == 0x12U);
    CHECK(adapter.machine().flash_card_count() == 1U);
    CHECK(adapter.machine().flash_card_data(0)[0] == 0xA5U);
    CHECK(adapter.machine().gpu_status ==
          mnemos::manifests::taito_gnet::taito_gnet_system::gpu_reset_status);
    CHECK(adapter.system_spec()[1].value == "Taito G-NET / Sony ZN-2");
    CHECK(adapter.system_spec()[4].value ==
          "board smoke: CPU/RAM/scratchpad/COP2 latch/GPU-OTC DMA/timer IRQ/flash/registers only");
    CHECK(adapter.region().frames_per_second_x1000 == 59826U);
    CHECK(adapter.current_frame().width == 320U);
    CHECK(adapter.current_frame().height == 240U);
    CHECK(current_frame_has_variation(adapter));

    REQUIRE(adapter.chips().size() == 1U);
    CHECK(adapter.chips()[0]->metadata().part_number == "R3000A");
    REQUIRE(adapter.memory_views().size() == 9U);
    CHECK(adapter.memory_views()[0]->name() == "main_ram");
    CHECK(adapter.memory_views()[1]->name() == "scratchpad");
    CHECK(adapter.memory_views()[2]->name() == "gpu_vram");
    CHECK(adapter.memory_views()[8]->name() == "flash_card_0");

    const auto& session = adapter.session_capabilities();
    CHECK(session.save_state_supported);
    CHECK(session.frame_exact_save_state);
    CHECK(session.input_ports.empty());

    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 2U);
    CHECK(media.media[0].id == "bios");
    CHECK(media.media[1].id == "flash_card_0");
    CHECK(media.media[1].provider_id == "taito_gnet.chd_flash_card");
}

TEST_CASE("taito_gnet_adapter whole-player save-state round-trips",
          "[taito_gnet][adapter][save]") {
    gnet::taito_gnet_adapter source(make_bios(), make_package(), "synthetic");
    source.step_one_frame();

    const std::vector<std::uint8_t> snapshot = source.save_state();
    REQUIRE_FALSE(snapshot.empty());
    const auto saved_pc = source.machine().cpu.cpu_registers().pc;
    const auto saved_card0 = source.machine().flash_cards.front().media.data[0];

    source.machine().main_ram[0] = 0U;
    source.machine().flash_cards.front().media.data[0] = 0U;
    source.step_one_frame();

    gnet::taito_gnet_adapter restored(make_bios(), make_package(), "synthetic");
    REQUIRE(restored.load_state(snapshot).ok());
    CHECK(restored.frames_stepped() == 1U);
    CHECK(restored.machine().main_ram[0] == 0x34U);
    CHECK(restored.machine().flash_cards.front().media.data[0] == saved_card0);
    CHECK(restored.machine().cpu.cpu_registers().pc == saved_pc);
}

TEST_CASE("taito_gnet_adapter assembles a real BIOS and CHD package",
          "[taito_gnet][adapter][data]") {
    const char* bios_env = opt_env("MNEMOS_TAITO_GNET_BIOS");
    const char* package_env = opt_env("MNEMOS_TAITO_GNET_PACKAGE");
    if (bios_env == nullptr || package_env == nullptr) {
        SKIP("set MNEMOS_TAITO_GNET_BIOS and MNEMOS_TAITO_GNET_PACKAGE for a real adapter smoke");
    }

    auto bios = read_file_bytes(bios_env);
    if (!bios) {
        SKIP("MNEMOS_TAITO_GNET_BIOS could not be read");
    }
    auto package = read_file_bytes(package_env);
    if (!package) {
        SKIP("MNEMOS_TAITO_GNET_PACKAGE could not be read");
    }

    gnet::taito_gnet_adapter adapter(std::move(*bios), std::move(*package), "gnet-data");
    CHECK(adapter.machine().flash_card_count() >= 1U);
    CHECK(adapter.machine().cpu.cpu_registers().pc == mnemos::chips::cpu::r3000a::reset_vector);
    CHECK(adapter.media_capabilities().media.size() >= 2U);
    CHECK(adapter.current_frame().pixels != nullptr);
}
