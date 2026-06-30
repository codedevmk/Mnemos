#include "taito_f2_adapter.hpp"

#include "adapter_registry.hpp"
#include "file.hpp"
#include "rom_set_toml.hpp"
#include "taito_f2_game_manifests.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

    namespace taito = mnemos::manifests::taito_f2;
    using mnemos::apps::player::adapters::taito_f2::taito_f2_adapter;

    void poke16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value);
    }

    void poke32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 24U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value >> 16U);
        bytes[at + 2U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 3U] = static_cast<std::uint8_t>(value);
    }

    [[nodiscard]] std::uint32_t read32_le(std::span<const std::uint8_t> bytes,
                                          std::size_t at) {
        return static_cast<std::uint32_t>(bytes[at]) |
               (static_cast<std::uint32_t>(bytes[at + 1U]) << 8U) |
               (static_cast<std::uint32_t>(bytes[at + 2U]) << 16U) |
               (static_cast<std::uint32_t>(bytes[at + 3U]) << 24U);
    }

    [[nodiscard]] std::uint16_t read16_le(std::span<const std::uint8_t> bytes,
                                          std::size_t at) {
        return static_cast<std::uint16_t>(bytes[at]) |
               static_cast<std::uint16_t>(bytes[at + 1U] << 8U);
    }

    [[nodiscard]] std::vector<std::uint8_t> make_program() {
        std::vector<std::uint8_t> rom(taito::main_rom_size, 0xFFU);
        poke32(rom, 0x0U, taito::work_ram_base + taito::work_ram_size);
        poke32(rom, 0x4U, 0x00000400U);
        poke16(rom, 0x400U, 0x33FCU);
        poke16(rom, 0x402U, 0x4242U);
        poke32(rom, 0x404U, taito::work_ram_base);
        poke16(rom, 0x408U, 0x60FEU);
        return rom;
    }

    void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8U));
    }

    void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
        put16(out, static_cast<std::uint16_t>(v));
        put16(out, static_cast<std::uint16_t>(v >> 16U));
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

    [[nodiscard]] std::vector<std::uint8_t>
    make_minimal_profile_zip(std::string_view set_name, std::string_view map) {
        std::string board_profile;
        if (map == "growl" || map == "ninjak") {
            board_profile += "players = 4\n";
        }

        if (map == "growl" || map == "solfigtr") {
            board_profile += "taito_f2_input_profile = \"split_tmp82c265\"\n";
        } else if (map == "ninjak") {
            board_profile += "taito_f2_input_profile = \"te7750_quad\"\n";
        } else {
            board_profile += "taito_f2_input_profile = \"standard\"\n";
        }

        if (map == "metalb" || map == "footchmp" || map == "deadconx") {
            board_profile += "taito_f2_tc0480scp_profile = \"";
            board_profile += std::string{map};
            board_profile += "\"\n";
        } else {
            if (map == "quizhq") {
                board_profile += "taito_f2_text_gfx_source = \"program_1bpp\"\n";
                board_profile += "taito_f2_text_gfx_base = 0x80000\n";
            } else if (map == "qtorimon") {
                board_profile += "taito_f2_text_gfx_source = \"program_1bpp\"\n";
                board_profile += "taito_f2_text_gfx_base = 0x40000\n";
            } else {
                board_profile += "taito_f2_text_gfx_source = \"tc0100scn_ram_2bpp\"\n";
            }
            board_profile += "taito_f2_tc0100scn_bg_x_offset = 16\n";
            board_profile += "taito_f2_tc0100scn_text_x_offset = 23\n";
            board_profile += "taito_f2_tc0480scp_profile = \"none\"\n";
        }

        if (map == "dondokod") {
            board_profile += "taito_f2_roz_x_offset = -16\n";
            board_profile += "taito_f2_roz_y_offset = 0\n";
        } else if (map == "pulirula") {
            board_profile += "taito_f2_roz_x_offset = -10\n";
            board_profile += "taito_f2_roz_y_offset = 16\n";
        }

        const std::string manifest = std::string{R"(
[set]
schema = "mnemos-romset/1"
name = ")"} + std::string{set_name} +
                                     R"("
board = "taito_f2"
taito_f2_map = ")" +
                                     std::string{map} +
                                     R"("
taito_f2_palette_format = "rgbx_444"
taito_f2_sprite_policy = "banked"
taito_f2_sprite_buffering = "partial_delayed"
)" + board_profile +
                                     R"(

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0
)";
        return make_stored_zip({
            {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
            {"prog", make_program()},
        });
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_decl
    require_embedded_decl(std::string_view set_name) {
        const std::string_view toml = taito::taito_f2_game_manifest_toml(set_name);
        REQUIRE_FALSE(toml.empty());
        const auto parsed = mnemos::manifests::common::parse_rom_set_decl(toml, "embedded");
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": "
                              << error.message);
        }
        REQUIRE(parsed.ok());
        REQUIRE(parsed.value.has_value());
        return *parsed.value;
    }

    [[nodiscard]] std::vector<std::pair<std::string, std::vector<std::uint8_t>>>
    placeholder_entries_for(const mnemos::manifests::common::rom_set_decl& decl,
                            std::uint8_t fill) {
        std::map<std::string, std::size_t, std::less<>> sizes;
        for (const auto& region : decl.regions) {
            for (const auto& file : region.files) {
                const std::size_t size = file.size == 0U ? 1U : file.size;
                auto [it, inserted] = sizes.emplace(file.name, size);
                if (!inserted) {
                    it->second = std::max(it->second, size);
                }
            }
        }

        std::vector<std::pair<std::string, std::vector<std::uint8_t>>> entries;
        entries.reserve(sizes.size());
        for (const auto& [name, size] : sizes) {
            entries.emplace_back(name, std::vector<std::uint8_t>(size, fill));
        }
        return entries;
    }

    [[nodiscard]] bool has_only_crc_issues(
        const std::vector<mnemos::manifests::common::rom_load_issue>& issues) {
        return !issues.empty() && std::all_of(issues.begin(), issues.end(), [](const auto& issue) {
                   return issue.message.find("crc32 mismatch") != std::string::npos;
               });
    }

    [[nodiscard]] bool current_frame_has_variation(const taito_f2_adapter& adapter) {
        const auto view = adapter.current_frame();
        if (view.pixels == nullptr || view.width == 0U || view.height == 0U) {
            return false;
        }
        const std::uint32_t first = view.pixels[0] & 0x00FFFFFFU;
        const std::uint32_t stride = view.effective_stride();
        for (std::uint32_t y = 0; y < view.height; ++y) {
            const std::uint32_t* row = view.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < view.width; ++x) {
                if ((row[x] & 0x00FFFFFFU) != first) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] bool
    audio_chunk_has_nonzero_sample(const mnemos::frontend_sdk::audio_chunk& chunk) noexcept {
        for (std::size_t i = 0; i < static_cast<std::size_t>(chunk.frame_count) * 2U; ++i) {
            if (chunk.samples[i] != 0) {
                return true;
            }
        }
        return false;
    }

} // namespace

TEST_CASE("taito_f2_adapter boots a bare program through the registry",
          "[taito_f2][adapter]") {
    mnemos::frontend_sdk::adapter_options options{};
    options.rom = make_program();
    options.display_name = "smoke";
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("taito_f2", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<taito_f2_adapter&>(*system);

    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 2U);
    CHECK(adapter.machine().video.frame_index() == 2U);
    CHECK(adapter.machine().work_ram[0] == 0x42U);
    CHECK(adapter.machine().work_ram[1] == 0x42U);
    CHECK(adapter.current_frame().width == 320U);
    CHECK(adapter.current_frame().height == 224U);
    CHECK(adapter.system_spec()[1].value == "Taito F2");

    const auto& session = adapter.session_capabilities();
    REQUIRE(session.input_ports.size() == 2U);
    CHECK(session.input_ports[0].device_id == "taito_f2.panel.p1");
    CHECK(session.deterministic_frame_input);
    CHECK(session.save_state_supported);
    CHECK(session.frame_exact_save_state);
    CHECK(adapter.media_capabilities().media[0].provider_id == "taito_f2.adapter");
}

TEST_CASE("taito_f2_adapter publishes board memory views", "[taito_f2][adapter]") {
    taito_f2_adapter adapter(make_program(), "smoke");

    CHECK(adapter.memory_views().size() == 25U);
    CHECK(adapter.memory_views()[0]->name() == "work_ram");
    CHECK(adapter.memory_views()[0]->bytes().size() == taito::work_ram_size);
    CHECK(adapter.memory_views()[1]->name() == "palette_ram");
    CHECK(adapter.memory_views()[2]->name() == "tile_ram");
    CHECK(adapter.memory_views()[3]->name() == "tile_ram_secondary");
    CHECK(adapter.memory_views()[4]->name() == "sprite_ram");
    CHECK(adapter.memory_views()[5]->name() == "sprite_latched_ram");
    CHECK(adapter.memory_views()[5]->bytes().size() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_bytes);
    CHECK(adapter.memory_views()[6]->name() == "sprite_rendered_ram");
    CHECK(adapter.memory_views()[6]->bytes().size() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_bytes);
    CHECK(adapter.memory_views()[7]->name() == "roz_ram");
    CHECK(adapter.memory_views()[7]->bytes().size() == taito::dondokod_roz_ram_size);
    CHECK(adapter.memory_views()[8]->name() == "z80_ram");
    CHECK(adapter.memory_views()[9]->name() == "sound_bank_state");
    REQUIRE(adapter.memory_views()[9]->bytes().size() == taito::sound_bank_state_bytes);
    CHECK(adapter.memory_views()[9]->bytes()[0] == 0U);
    CHECK(adapter.memory_views()[9]->bytes()[1] == 0U);
    CHECK(adapter.memory_views()[9]->bytes()[2] == 1U);
    CHECK(adapter.memory_views()[9]->bytes()[3] == 1U);
    CHECK(adapter.memory_views()[10]->name() == "io_output_regs");
    REQUIRE(adapter.memory_views()[10]->bytes().size() == taito::io_output_reg_count);
    CHECK(adapter.memory_views()[11]->name() == "io_output_state");
    REQUIRE(adapter.memory_views()[11]->bytes().size() == taito::io_output_state_bytes);
    CHECK(adapter.memory_views()[11]->bytes()[0] == 0U);
    CHECK(adapter.memory_views()[12]->name() == "palette_write_state");
    REQUIRE(adapter.memory_views()[12]->bytes().size() == taito::palette_write_state_bytes);
    CHECK(adapter.memory_views()[12]->bytes()[0] == 0U);
    CHECK(adapter.memory_views()[12]->bytes()[1] ==
          static_cast<std::uint8_t>(taito::taito_f2_palette_format::xbgr_555));
    CHECK(adapter.memory_views()[12]->bytes()[20] == 0U);
    CHECK(adapter.memory_views()[13]->name() == "video_regs_raw");
    CHECK(adapter.memory_views()[13]->bytes().size() ==
          taito::video_reg_count * sizeof(std::uint16_t));
    CHECK(adapter.memory_views()[14]->name() == "video_regs_secondary_raw");
    CHECK(adapter.memory_views()[14]->bytes().size() ==
          taito::video_reg_count * sizeof(std::uint16_t));
    CHECK(adapter.memory_views()[15]->name() == "sprite_bank_regs_raw");
    CHECK(adapter.memory_views()[15]->bytes().size() ==
          mnemos::chips::video::taito_f2_video::sprite_bank_count *
              sizeof(std::uint16_t));
    CHECK(adapter.memory_views()[16]->name() == "priority_regs_raw");
    CHECK(adapter.memory_views()[16]->bytes().size() ==
          mnemos::chips::video::taito_f2_video::priority_reg_count *
              sizeof(std::uint16_t));
    CHECK(adapter.memory_views()[17]->name() == "roz_control_regs_raw");
    CHECK(adapter.memory_views()[17]->bytes().size() ==
          mnemos::chips::video::taito_f2_video::roz_control_reg_count *
              sizeof(std::uint16_t));
    CHECK(adapter.memory_views()[18]->name() == "tc0480scp_control_regs_raw");
    CHECK(adapter.memory_views()[18]->bytes().size() ==
          mnemos::chips::video::taito_f2_video::tc0480scp_control_reg_count *
              sizeof(std::uint16_t));
    CHECK(adapter.memory_views()[19]->name() == "irq_state");
    REQUIRE(adapter.memory_views()[19]->bytes().size() == taito::irq_state_bytes);
    CHECK(adapter.memory_views()[19]->bytes()[0] == 5U);
    CHECK(adapter.memory_views()[19]->bytes()[1] == 6U);
    CHECK(adapter.memory_views()[19]->bytes()[20] == 0U);
    CHECK(adapter.memory_views()[20]->name() == "board_profile_state");
    REQUIRE(adapter.memory_views()[20]->bytes().size() == taito::board_profile_state_bytes);
    const auto board_profile = adapter.memory_views()[20]->bytes();
    CHECK(board_profile[0] == 1U);
    CHECK(board_profile[1] == 0U);
    CHECK(board_profile[2] == 2U);
    CHECK(board_profile[20] == 0xBFU);
    CHECK((board_profile[21] & 0x02U) != 0U);
    CHECK((board_profile[21] & 0x08U) == 0U);
    CHECK(board_profile[22] == taito::z80_sound_bank_mask);
    CHECK(board_profile[23] == 1U);
    CHECK(read32_le(board_profile, 24U) == taito::m68k_clock_hz);
    CHECK(read32_le(board_profile, 28U) == taito::z80_clock_hz);
    CHECK(read32_le(board_profile, 32U) == taito::ym2610_clock_hz);
    CHECK(read32_le(board_profile, 40U) ==
          mnemos::chips::video::taito_f2_video::visible_width);
    CHECK(read32_le(board_profile, 44U) ==
          mnemos::chips::video::taito_f2_video::visible_height);
    CHECK(read32_le(board_profile, 60U) == taito::z80_fixed_rom_window);
    CHECK(read32_le(board_profile, 64U) == taito::z80_bank_window);
    CHECK(read32_le(board_profile, 68U) == taito::z80_ram_size);
    CHECK(read32_le(board_profile, 72U) == taito::z80_fixed_rom_window);
    CHECK(read32_le(board_profile, 76U) == 1U);
    CHECK(adapter.memory_views()[21]->name() == "sound_reset_state");
    REQUIRE(adapter.memory_views()[21]->bytes().size() == taito::sound_reset_state_bytes);
    CHECK(adapter.memory_views()[21]->bytes()[0] == 1U);
    CHECK(adapter.memory_views()[21]->bytes()[1] == 0U);
    CHECK(adapter.memory_views()[21]->bytes()[23] == 1U);
    CHECK(adapter.memory_views()[22]->name() == "watchdog_state");
    REQUIRE(adapter.memory_views()[22]->bytes().size() == taito::watchdog_state_bytes);
    CHECK(adapter.memory_views()[22]->bytes()[0] == 1U);
    CHECK(adapter.memory_views()[22]->bytes()[1] == 0U);
    CHECK(adapter.memory_views()[23]->name() == "main_bus_state");
    REQUIRE(adapter.memory_views()[23]->bytes().size() == taito::main_bus_state_bytes);
    CHECK(adapter.memory_views()[23]->bytes()[0] == 1U);
    CHECK(adapter.memory_views()[23]->bytes()[1] == 1U);
    CHECK(adapter.memory_views()[23]->bytes()[38] == 1U);
    CHECK(adapter.memory_views()[23]->bytes()[39] == 0U);
    CHECK(adapter.memory_views()[24]->name() == "io_access_state");
    REQUIRE(adapter.memory_views()[24]->bytes().size() == taito::io_access_state_bytes);
    CHECK(adapter.memory_views()[24]->bytes()[0] == 1U);
    CHECK(adapter.memory_views()[24]->bytes()[1] == 0U);
}

TEST_CASE("taito_f2_adapter debug probe records palette readback",
          "[taito_f2][adapter][palette]") {
    taito_f2_adapter adapter(make_program(), "smoke");

    adapter.machine().main_bus.write16_be(taito::palette_ram_base + 8U, 0x1234U);
    CHECK(adapter.machine().palette_read_count == 0U);

    CHECK(adapter.run_debug_probe("palette-readback"));
    CHECK_FALSE(adapter.run_debug_probe("unknown-probe"));

    REQUIRE(adapter.memory_views()[12]->bytes().size() == taito::palette_write_state_bytes);
    const auto palette_state = adapter.memory_views()[12]->bytes();
    CHECK(adapter.machine().palette_read_count == 2U);
    CHECK(palette_state[20] == 1U);
    CHECK(read32_le(palette_state, 24U) == 2U);
    CHECK(read32_le(palette_state, 28U) == taito::palette_ram_base + 9U);
    CHECK(read16_le(palette_state, 32U) == 0x1234U);
    CHECK(read16_le(palette_state, 34U) == 4U);
}

TEST_CASE("taito_f2_adapter exposes TC0140SYT sound communication diagnostics",
          "[taito_f2][adapter]") {
    taito_f2_adapter adapter(make_program(), "smoke");

    const auto chips = adapter.chips();
    REQUIRE(chips.size() == 5U);
    REQUIRE(chips[4] != nullptr);
    const auto md = chips[4]->metadata();
    CHECK(md.manufacturer == "Taito");
    CHECK(md.part_number == "TC0140SYT");
    CHECK(md.klass == mnemos::chips::chip_class::bus_controller);

    auto* regs = chips[4]->introspection().registers();
    REQUIRE(regs != nullptr);
    const auto snapshot = regs->registers();
    REQUIRE(snapshot.size() >= 14U);
    CHECK(snapshot[0].name == "MPORT");
    CHECK(snapshot[6].name == "STATUS");
    CHECK(snapshot[13].name == "CMDNMI");
}

TEST_CASE("taito_f2_adapter maps pads onto active-low board inputs", "[taito_f2][adapter]") {
    taito_f2_adapter adapter(make_program(), "smoke");

    mnemos::frontend_sdk::controller_state p1{};
    p1.up = true;
    p1.a = true;
    p1.start = true;
    p1.select = true;
    p1.service = true;
    adapter.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.right = true;
    p2.test = true;
    adapter.apply_input(1, p2);

    CHECK(adapter.machine().input_p1 == static_cast<std::uint8_t>(0xFFU & ~0x08U & ~0x10U));
    CHECK(adapter.machine().input_p2 == static_cast<std::uint8_t>(0xFFU & ~0x01U));
    CHECK(adapter.machine().input_system ==
          static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x04U & ~0x08U & ~0x10U));
}

TEST_CASE("taito_f2_adapter exposes four-player panels for multiplayer boards",
          "[taito_f2][adapter][input]") {
    taito_f2_adapter growl(make_minimal_profile_zip("growl", "growl"), "growl");
    const auto& growl_session = growl.session_capabilities();
    REQUIRE(growl_session.input_ports.size() == 4U);
    CHECK(growl_session.input_ports[2].port_index == 2U);
    CHECK(growl_session.input_ports[2].player_slot == 3U);
    CHECK(growl_session.input_ports[2].device_id == "taito_f2.panel.p3");
    CHECK(growl_session.input_ports[3].label == "Player 4 Panel");

    taito_f2_adapter ninjak(make_minimal_profile_zip("ninjak", "ninjak"), "ninjak");
    REQUIRE(ninjak.session_capabilities().input_ports.size() == 4U);

    taito_f2_adapter solfigtr(make_minimal_profile_zip("solfigtr", "solfigtr"), "solfigtr");
    REQUIRE(solfigtr.session_capabilities().input_ports.size() == 2U);

    mnemos::frontend_sdk::controller_state p1{};
    p1.start = true;
    p1.select = true;
    p1.test = true;
    growl.apply_input(0, p1);

    CHECK(growl.machine().input_p1 == static_cast<std::uint8_t>(0xFFU & ~0x80U));
    CHECK(growl.machine().input_system ==
          static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x04U & ~0x10U));
    growl.apply_input(0, {});

    mnemos::frontend_sdk::controller_state p3{};
    p3.down = true;
    p3.b = true;
    p3.start = true;
    p3.select = true;
    p3.service = true;
    growl.apply_input(2, p3);

    mnemos::frontend_sdk::controller_state p4{};
    p4.up = true;
    p4.c = true;
    p4.start = true;
    p4.select = true;
    p4.service = true;
    growl.apply_input(3, p4);

    CHECK(growl.machine().input_p1 == 0xFFU);
    CHECK(growl.machine().input_p2 == 0xFFU);
    CHECK(growl.machine().input_p3 ==
          static_cast<std::uint8_t>(0xFFU & ~0x02U & ~0x20U & ~0x80U));
    CHECK(growl.machine().input_p4 ==
          static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x40U & ~0x80U));
    CHECK(growl.machine().input_system == 0xFFU);
    CHECK(growl.machine().input_coin_extension ==
          static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x02U & ~0x04U & ~0x08U));

    mnemos::frontend_sdk::controller_state n1{};
    n1.start = true;
    n1.select = true;
    n1.service = true;
    n1.test = true;
    ninjak.apply_input(0, n1);

    mnemos::frontend_sdk::controller_state n3{};
    n3.start = true;
    n3.select = true;
    n3.service = true;
    ninjak.apply_input(2, n3);

    mnemos::frontend_sdk::controller_state n4{};
    n4.start = true;
    n4.select = true;
    n4.service = true;
    ninjak.apply_input(3, n4);

    CHECK(ninjak.machine().input_system ==
          static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x04U & ~0x08U & ~0x10U &
                                    ~0x40U & ~0x80U));
    CHECK(ninjak.machine().input_coin_extension ==
          static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x02U & ~0x08U & ~0x10U));
    CHECK(ninjak.machine().main_bus.read8(taito::ninjak_input_base + 14U) ==
          ninjak.machine().input_coin_extension);

    growl.apply_input(4, p3); // out-of-range port ignored.
    CHECK(growl.machine().input_p3 ==
          static_cast<std::uint8_t>(0xFFU & ~0x02U & ~0x20U & ~0x80U));

    const std::vector<std::uint8_t> snapshot = growl.save_state();
    REQUIRE_FALSE(snapshot.empty());

    taito_f2_adapter restored(make_minimal_profile_zip("growl", "growl"), "growl");
    REQUIRE(restored.load_state(snapshot).ok());
    CHECK(restored.machine().input_p3 == growl.machine().input_p3);
    CHECK(restored.machine().input_p4 == growl.machine().input_p4);
    CHECK(restored.machine().input_system == growl.machine().input_system);
    CHECK(restored.machine().input_coin_extension == growl.machine().input_coin_extension);
}

TEST_CASE("taito_f2_adapter whole-player save-state round-trips",
          "[taito_f2][adapter][save]") {
    taito_f2_adapter source(make_program(), "smoke");

    mnemos::frontend_sdk::controller_state p1{};
    p1.up = true;
    p1.start = true;
    p1.select = true;
    p1.a = true;
    source.apply_input(0, p1);
    source.step_one_frame();
    source.step_one_frame();

    const std::vector<std::uint8_t> snapshot = source.save_state();
    REQUIRE_FALSE(snapshot.empty());
    const auto saved_work_ram = source.machine().work_ram;
    const std::uint8_t saved_input_p1 = source.machine().input_p1;
    const std::uint8_t saved_input_system = source.machine().input_system;
    const std::uint64_t saved_vblank = source.machine().vblank_irq_raised;

    source.step_one_frame();
    const std::vector<std::uint8_t> reference = source.save_state();

    taito_f2_adapter restored(make_program(), "smoke");
    restored.step_one_frame();
    const mnemos::runtime::load_result loaded = restored.load_state(snapshot);
    REQUIRE(loaded.ok());
    CHECK(loaded.master_cycle > 0U);
    CHECK(restored.frames_stepped() == 2U);
    CHECK(restored.machine().work_ram == saved_work_ram);
    CHECK(restored.machine().input_p1 == saved_input_p1);
    CHECK(restored.machine().input_system == saved_input_system);
    CHECK(restored.machine().vblank_irq_raised == saved_vblank);

    restored.step_one_frame();
    CHECK(restored.save_state() == reference);
}

TEST_CASE("taito_f2_adapter loads a declarative game.toml zip", "[taito_f2][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "taito_synth"
board = "taito_f2"
orientation = "vertical"
taito_f2_map = "gunfront"
taito_f2_palette_format = "rgbx_444"
taito_f2_sprite_policy = "banked"
taito_f2_sprite_buffering = "partial_delayed"
taito_f2_sprite_extension_base = 0xC00000
taito_f2_sprite_extension_size = 0x2000
taito_f2_sprite_active_area = "y_word_bit0"
taito_f2_sprite_hide_pixels = 3
taito_f2_sprite_flip_hide_pixels = -3

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    taito_f2_adapter adapter(zip, "taito_synth");
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::gunfront);
    CHECK(adapter.machine().params.sprite_policy == taito::taito_f2_sprite_policy::banked);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::partial_delayed);
    CHECK(adapter.machine().params.palette_format == taito::taito_f2_palette_format::rgbx_444);
    CHECK(adapter.machine().params.sprite_active_area ==
          taito::taito_f2_sprite_active_area::y_word_bit0);
    CHECK(adapter.machine().params.sprite_hide_pixels == 3);
    CHECK(adapter.machine().params.sprite_flip_hide_pixels == -3);
    REQUIRE(adapter.machine().params.sprite_extension_base.has_value());
    REQUIRE(adapter.machine().params.sprite_extension_size.has_value());
    CHECK(*adapter.machine().params.sprite_extension_base == 0xC00000U);
    CHECK(*adapter.machine().params.sprite_extension_size == 0x2000U);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter loads a Liquid Kids game.toml profile", "[taito_f2][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "liquidk"
board = "taito_f2"
taito_f2_map = "liquidk"
taito_f2_palette_format = "rgbx_444"
taito_f2_sprite_buffering = "partial_delayed"
taito_f2_sprite_hide_pixels = 3
taito_f2_sprite_flip_hide_pixels = 3

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    taito_f2_adapter adapter(zip, "liquidk");
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::liquidk);
    CHECK(adapter.machine().params.sprite_policy == taito::taito_f2_sprite_policy::standard);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::partial_delayed);
    CHECK(adapter.machine().params.palette_format == taito::taito_f2_palette_format::rgbx_444);
    CHECK(adapter.machine().params.sprite_hide_pixels == 3);
    CHECK(adapter.machine().params.sprite_flip_hide_pixels == 3);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter loads a Dondoko Don game.toml profile",
          "[taito_f2][adapter][roz]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "dondokod"
board = "taito_f2"
taito_f2_map = "dondokod"
taito_f2_palette_format = "rgbx_444"
taito_f2_sprite_buffering = "partial_delayed"
taito_f2_sprite_hide_pixels = 3
taito_f2_sprite_flip_hide_pixels = 3

[[region]]
name = "maincpu"
size = 0x080000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "roz"
size = 0x20

[[region.file]]
name = "roz"
offset = 0
)";
    auto program = make_program();
    program.resize(0x080000U);
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::move(program)},
        {"roz", std::vector<std::uint8_t>(0x20U, 0x11U)},
    });

    taito_f2_adapter adapter(zip, "dondokod");

    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::dondokod);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::partial_delayed);
    CHECK(adapter.machine().params.palette_format == taito::taito_f2_palette_format::rgbx_444);
    CHECK(adapter.machine().params.sprite_hide_pixels == 3);
    CHECK(adapter.machine().params.sprite_flip_hide_pixels == 3);
    REQUIRE(adapter.machine().roms.regions.contains("roz"));
    CHECK(adapter.machine().roms.regions.at("roz").size() == 0x20U);
}

TEST_CASE("taito_f2_adapter loads a Pulirula game.toml profile",
          "[taito_f2][adapter][roz]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "pulirula"
board = "taito_f2"
taito_f2_map = "pulirula"
taito_f2_palette_format = "rgbx_444"
taito_f2_sprite_policy = "extension_2"
taito_f2_sprite_buffering = "partial_delayed"
taito_f2_sprite_extension_base = 0x600000
taito_f2_sprite_extension_size = 0x4000
taito_f2_sprite_hide_pixels = 3
taito_f2_sprite_flip_hide_pixels = 3

[[region]]
name = "maincpu"
size = 0x0c0000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "roz"
size = 0x20

[[region.file]]
name = "roz"
offset = 0
)";
    auto program = make_program();
    program.resize(0x0c0000U);
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::move(program)},
        {"roz", std::vector<std::uint8_t>(0x20U, 0x11U)},
    });

    taito_f2_adapter adapter(zip, "pulirula");

    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::pulirula);
    CHECK(adapter.machine().params.sprite_policy == taito::taito_f2_sprite_policy::extension_2);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::partial_delayed);
    CHECK(adapter.machine().params.palette_format == taito::taito_f2_palette_format::rgbx_444);
    CHECK(adapter.machine().params.sprite_hide_pixels == 3);
    CHECK(adapter.machine().params.sprite_flip_hide_pixels == 3);
    REQUIRE(adapter.machine().params.sprite_extension_base.has_value());
    REQUIRE(adapter.machine().params.sprite_extension_size.has_value());
    CHECK(*adapter.machine().params.sprite_extension_base == taito::pulirula_sprite_extension_base);
    CHECK(*adapter.machine().params.sprite_extension_size == taito::sprite_extension_ram_size);
    CHECK(adapter.machine().video.current_roz_variant() ==
          mnemos::chips::video::taito_f2_video::roz_variant::tc0430grw);
    REQUIRE(adapter.machine().roms.regions.contains("roz"));
    CHECK(adapter.machine().roms.regions.at("roz").size() == 0x20U);
}

TEST_CASE("taito_f2_adapter loads a Metal Black TC0480SCP game.toml profile",
          "[taito_f2][adapter][tc0480scp]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "metalb"
board = "taito_f2"
taito_f2_map = "metalb"
taito_f2_palette_format = "rgbx_444"
taito_f2_sprite_buffering = "partial_delayed"
taito_f2_sprite_hide_pixels = 3
taito_f2_sprite_flip_hide_pixels = 3

[[region]]
name = "maincpu"
size = 0x0c0000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "tiles"
size = 0x20

[[region.file]]
name = "tiles"
offset = 0
)";
    auto program = make_program();
    program.resize(0x0c0000U);
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::move(program)},
        {"tiles", std::vector<std::uint8_t>(0x20U, 0x44U)},
    });

    taito_f2_adapter adapter(zip, "metalb");

    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::metalb);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::partial_delayed);
    CHECK(adapter.machine().params.palette_format == taito::taito_f2_palette_format::rgbx_444);
    CHECK(adapter.machine().video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0480scp);
    REQUIRE(adapter.machine().roms.regions.contains("tiles"));
    CHECK(adapter.machine().roms.regions.at("tiles").size() == 0x20U);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter loads a Football Champ TC0190FMC game.toml profile",
          "[taito_f2][adapter][tc0190fmc]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "footchmp"
board = "taito_f2"
taito_f2_map = "footchmp"
taito_f2_palette_format = "rgbx_444"
taito_f2_sprite_policy = "banked"
taito_f2_sprite_active_area = "y_word_bit0"
taito_f2_sprite_buffering = "full_delayed"
taito_f2_sprite_hide_pixels = 3
taito_f2_sprite_flip_hide_pixels = 3

[[region]]
name = "maincpu"
size = 0x080000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "tiles"
size = 0x20

[[region.file]]
name = "tiles"
offset = 0
)";
    auto program = make_program();
    program.resize(0x080000U);
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::move(program)},
        {"tiles", std::vector<std::uint8_t>(0x20U, 0x44U)},
    });

    taito_f2_adapter adapter(zip, "footchmp");

    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::footchmp);
    CHECK(adapter.machine().params.sprite_policy == taito::taito_f2_sprite_policy::banked);
    CHECK(adapter.machine().params.sprite_active_area ==
          taito::taito_f2_sprite_active_area::y_word_bit0);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::full_delayed);
    CHECK(adapter.machine().video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0480scp);
    CHECK(adapter.machine().video.current_tc0480scp_priority_model() ==
          mnemos::chips::video::taito_f2_video::tc0480scp_priority_model::
              deadconx_footchmp);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter loads a Dead Connection TC0190FMC game.toml profile",
          "[taito_f2][adapter][tc0190fmc]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "deadconx"
board = "taito_f2"
taito_f2_map = "deadconx"
taito_f2_palette_format = "rgbx_444"
taito_f2_sprite_policy = "banked"
taito_f2_sprite_active_area = "y_word_bit0"
taito_f2_sprite_buffering = "full_delayed"
taito_f2_sprite_hide_pixels = 3
taito_f2_sprite_flip_hide_pixels = 3

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "tiles"
size = 0x20

[[region.file]]
name = "tiles"
offset = 0

[[region]]
name = "sprites"
size = 0x20

[[region.file]]
name = "sprites"
offset = 0

[[region]]
name = "adpcma"
size = 0x20

[[region.file]]
name = "adpcma"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
        {"tiles", std::vector<std::uint8_t>(0x20U, 0x44U)},
        {"sprites", std::vector<std::uint8_t>(0x20U, 0x55U)},
        {"adpcma", std::vector<std::uint8_t>(0x20U, 0x66U)},
    });

    taito_f2_adapter adapter(zip, "deadconx");

    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::deadconx);
    CHECK(adapter.machine().params.sprite_policy == taito::taito_f2_sprite_policy::banked);
    CHECK(adapter.machine().params.sprite_active_area ==
          taito::taito_f2_sprite_active_area::y_word_bit0);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::full_delayed);
    CHECK(adapter.machine().video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0480scp);
    CHECK(adapter.machine().video.current_tc0480scp_priority_model() ==
          mnemos::chips::video::taito_f2_video::tc0480scp_priority_model::
              deadconx_footchmp);
    REQUIRE(adapter.machine().roms.regions.contains("adpcma"));
    CHECK(adapter.machine().roms.regions.at("adpcma").size() == 0x20U);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter loads a Dino Rex shifted-window game.toml profile",
          "[taito_f2][adapter][video]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "dinorex"
board = "taito_f2"
taito_f2_map = "dinorex"
taito_f2_palette_format = "rrrr_gggg_bbbb_rgbx"
taito_f2_sprite_policy = "extension_3"
taito_f2_sprite_buffering = "immediate"
taito_f2_sprite_extension_base = 0x400000
taito_f2_sprite_extension_size = 0x1000
taito_f2_sprite_hide_pixels = 3
taito_f2_sprite_flip_hide_pixels = 3

[[region]]
name = "maincpu"
size = 0x300000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "tiles"
size = 0x20

[[region.file]]
name = "tiles"
offset = 0

[[region]]
name = "sprites"
size = 0x20

[[region.file]]
name = "sprites"
offset = 0

[[region]]
name = "adpcma"
size = 0x20

[[region.file]]
name = "adpcma"
offset = 0

[[region]]
name = "adpcmb"
size = 0x20

[[region.file]]
name = "adpcmb"
offset = 0
)";
    auto program = make_program();
    program.resize(0x300000U, 0xFFU);
    poke32(program, 0x404U, taito::dinorex_work_ram_base);
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::move(program)},
        {"tiles", std::vector<std::uint8_t>(0x20U, 0x44U)},
        {"sprites", std::vector<std::uint8_t>(0x20U, 0x55U)},
        {"adpcma", std::vector<std::uint8_t>(0x20U, 0x66U)},
        {"adpcmb", std::vector<std::uint8_t>(0x20U, 0x77U)},
    });

    taito_f2_adapter adapter(zip, "dinorex");

    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::dinorex);
    CHECK(adapter.machine().params.sprite_policy == taito::taito_f2_sprite_policy::extension_3);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::immediate);
    CHECK(adapter.machine().params.palette_format ==
          taito::taito_f2_palette_format::rrrr_gggg_bbbb_rgbx);
    REQUIRE(adapter.machine().params.sprite_extension_base.has_value());
    REQUIRE(adapter.machine().params.sprite_extension_size.has_value());
    CHECK(*adapter.machine().params.sprite_extension_base == taito::dinorex_sprite_extension_base);
    CHECK(*adapter.machine().params.sprite_extension_size == taito::dinorex_sprite_extension_size);
    CHECK(adapter.machine().video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::extension_low_as_high);
    CHECK(adapter.machine().video.sprite_palette_bank_base() == 0U);
    CHECK(adapter.machine().video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::rrrr_gggg_bbbb_rgbx);
    REQUIRE(adapter.machine().roms.regions.contains("adpcma"));
    REQUIRE(adapter.machine().roms.regions.contains("adpcmb"));
    CHECK(adapter.machine().roms.regions.at("adpcma").size() == 0x20U);
    CHECK(adapter.machine().roms.regions.at("adpcmb").size() == 0x20U);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter loads a Thunder Fox dual-TC0100SCN game.toml profile",
          "[taito_f2][adapter][tc0100scn]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "thundfox"
board = "taito_f2"
taito_f2_map = "thundfox"
taito_f2_palette_format = "rgbx_444"
taito_f2_sprite_buffering = "partial_delayed_thundfox"
taito_f2_sprite_hide_pixels = 3
taito_f2_sprite_flip_hide_pixels = -3

[[region]]
name = "maincpu"
size = 0x080000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "tiles"
size = 0x20

[[region.file]]
name = "tiles"
offset = 0

[[region]]
name = "tiles_secondary"
size = 0x20

[[region.file]]
name = "tiles_secondary"
offset = 0

[[region]]
name = "sprites"
size = 0x20

[[region.file]]
name = "sprites"
offset = 0

[[region]]
name = "adpcma"
size = 0x20

[[region.file]]
name = "adpcma"
offset = 0

[[region]]
name = "adpcmb"
size = 0x20

[[region.file]]
name = "adpcmb"
offset = 0
)";
    auto program = make_program();
    program.resize(0x080000U, 0xFFU);
    poke32(program, 0x404U, taito::thundfox_work_ram_base);
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::move(program)},
        {"tiles", std::vector<std::uint8_t>(0x20U, 0x44U)},
        {"tiles_secondary", std::vector<std::uint8_t>(0x20U, 0x45U)},
        {"sprites", std::vector<std::uint8_t>(0x20U, 0x55U)},
        {"adpcma", std::vector<std::uint8_t>(0x20U, 0x66U)},
        {"adpcmb", std::vector<std::uint8_t>(0x20U, 0x77U)},
    });

    taito_f2_adapter adapter(zip, "thundfox");

    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::thundfox);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::partial_delayed_thundfox);
    CHECK(adapter.machine().params.palette_format == taito::taito_f2_palette_format::rgbx_444);
    CHECK(adapter.machine().params.sprite_hide_pixels == 3);
    CHECK(adapter.machine().params.sprite_flip_hide_pixels == -3);
    CHECK(adapter.machine().video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::dual_tc0100scn);
    CHECK(adapter.machine().video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::
              partial_delayed_thundfox);
    REQUIRE(adapter.machine().roms.regions.contains("tiles_secondary"));
    CHECK(adapter.machine().roms.regions.at("tiles_secondary").size() == 0x20U);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter loads a Quiz Chikyu partial-buffer game.toml profile",
          "[taito_f2][adapter][video]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "qzchikyu"
board = "taito_f2"
taito_f2_map = "qzchikyu"
taito_f2_palette_format = "xrgb_555"
taito_f2_sprite_buffering = "partial_delayed_qzchikyu"
taito_f2_sprite_hide_pixels = 0
taito_f2_sprite_flip_hide_pixels = 4

[[region]]
name = "maincpu"
size = 0x180000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "tiles"
size = 0x20

[[region.file]]
name = "tiles"
offset = 0

[[region]]
name = "sprites"
size = 0x20

[[region.file]]
name = "sprites"
offset = 0

[[region]]
name = "adpcma"
size = 0x20

[[region.file]]
name = "adpcma"
offset = 0
)";
    auto program = make_program();
    program.resize(0x180000U, 0xFFU);
    poke32(program, 0x404U, taito::qzchikyu_work_ram_base);
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::move(program)},
        {"tiles", std::vector<std::uint8_t>(0x20U, 0x44U)},
        {"sprites", std::vector<std::uint8_t>(0x20U, 0x55U)},
        {"adpcma", std::vector<std::uint8_t>(0x20U, 0x66U)},
    });

    taito_f2_adapter adapter(zip, "qzchikyu");

    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::qzchikyu);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::partial_delayed_qzchikyu);
    CHECK(adapter.machine().params.palette_format == taito::taito_f2_palette_format::xrgb_555);
    CHECK(adapter.machine().params.sprite_hide_pixels == 0);
    CHECK(adapter.machine().params.sprite_flip_hide_pixels == 4);
    CHECK(adapter.machine().video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
    CHECK(adapter.machine().video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::
              partial_delayed_qzchikyu);
    REQUIRE(adapter.machine().roms.regions.contains("adpcma"));
    CHECK(adapter.machine().roms.regions.at("adpcma").size() == 0x20U);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter loads a Quiz Quest shifted-map game.toml profile",
          "[taito_f2][adapter][video]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "qzquest"
board = "taito_f2"
taito_f2_map = "qzquest"
taito_f2_palette_format = "xrgb_555"
taito_f2_sprite_buffering = "partial_delayed"

[[region]]
name = "maincpu"
size = 0x180000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "tiles"
size = 0x20

[[region.file]]
name = "tiles"
offset = 0

[[region]]
name = "sprites"
size = 0x20

[[region.file]]
name = "sprites"
offset = 0

[[region]]
name = "adpcma"
size = 0x20

[[region.file]]
name = "adpcma"
offset = 0
)";
    auto program = make_program();
    program.resize(0x180000U, 0xFFU);
    poke32(program, 0x404U, taito::qzquest_work_ram_base);
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::move(program)},
        {"tiles", std::vector<std::uint8_t>(0x20U, 0x44U)},
        {"sprites", std::vector<std::uint8_t>(0x20U, 0x55U)},
        {"adpcma", std::vector<std::uint8_t>(0x20U, 0x66U)},
    });

    taito_f2_adapter adapter(zip, "qzquest");

    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::qzquest);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::partial_delayed);
    CHECK(adapter.machine().params.palette_format == taito::taito_f2_palette_format::xrgb_555);
    CHECK(adapter.machine().video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
    CHECK(adapter.machine().video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::partial_delayed);
    REQUIRE(adapter.machine().roms.regions.contains("adpcma"));
    CHECK(adapter.machine().roms.regions.at("adpcma").size() == 0x20U);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter loads a Quiz Torimon shifted-IO game.toml profile",
          "[taito_f2][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "qtorimon"
board = "taito_f2"
taito_f2_map = "qtorimon"
taito_f2_palette_format = "xbgr_555"
taito_f2_sprite_buffering = "partial_delayed"

[[region]]
name = "maincpu"
size = 0x080000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "tiles"
size = 0x20
fill = 0xFF

[[region]]
name = "sprites"
size = 0x20

[[region.file]]
name = "sprites"
offset = 0

[[region]]
name = "adpcma"
size = 0x20

[[region.file]]
name = "adpcma"
offset = 0
)";
    auto program = make_program();
    program.resize(0x080000U, 0xFFU);
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::move(program)},
        {"sprites", std::vector<std::uint8_t>(0x20U, 0x55U)},
        {"adpcma", std::vector<std::uint8_t>(0x20U, 0x66U)},
    });

    taito_f2_adapter adapter(zip, "qtorimon");

    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::qtorimon);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::partial_delayed);
    CHECK(adapter.machine().params.palette_format == taito::taito_f2_palette_format::xbgr_555);
    CHECK(adapter.machine().params.text_gfx_source ==
          taito::taito_f2_text_gfx_source::program_1bpp);
    CHECK(adapter.machine().params.text_gfx_base == taito::qtorimon_program_text_gfx_base);
    CHECK(adapter.machine().video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
    CHECK(adapter.machine().video.current_text_gfx_source() ==
          mnemos::chips::video::taito_f2_video::text_gfx_source::program_1bpp);
    CHECK(adapter.machine().video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::partial_delayed);
    REQUIRE(adapter.machine().roms.regions.contains("adpcma"));
    CHECK(adapter.machine().roms.regions.at("adpcma").size() == 0x20U);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter loads a Growl TC0190FMC game.toml profile",
          "[taito_f2][adapter][tc0190fmc]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "growl"
board = "taito_f2"
taito_f2_map = "growl"
taito_f2_palette_format = "rrrr_gggg_bbbb_rgbx"
taito_f2_sprite_policy = "banked"
taito_f2_sprite_active_area = "y_word_bit0"
taito_f2_sprite_buffering = "immediate"
taito_f2_sprite_hide_pixels = 3
taito_f2_sprite_flip_hide_pixels = 3
taito_f2_tc0100scn_positive_text_y_origin = 24

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "tiles"
size = 0x20

[[region.file]]
name = "tiles"
offset = 0

[[region]]
name = "sprites"
size = 0x20

[[region.file]]
name = "sprites"
offset = 0

[[region]]
name = "adpcmb"
size = 0x20

[[region.file]]
name = "adpcmb"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
        {"tiles", std::vector<std::uint8_t>(0x20U, 0x44U)},
        {"sprites", std::vector<std::uint8_t>(0x20U, 0x55U)},
        {"adpcmb", std::vector<std::uint8_t>(0x20U, 0x66U)},
    });

    taito_f2_adapter adapter(zip, "growl");

    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::growl);
    CHECK(adapter.machine().params.sprite_policy == taito::taito_f2_sprite_policy::banked);
    CHECK(adapter.machine().params.sprite_active_area ==
          taito::taito_f2_sprite_active_area::y_word_bit0);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::immediate);
    CHECK(adapter.machine().params.palette_format ==
          taito::taito_f2_palette_format::rrrr_gggg_bbbb_rgbx);
    CHECK(adapter.machine().params.tc0100scn_positive_text_y_origin == 24);
    CHECK(adapter.machine().video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
    CHECK(adapter.machine().video.current_sprite_active_area_source() ==
          mnemos::chips::video::taito_f2_video::sprite_active_area_source::y_word_bit0);
    CHECK(adapter.machine().video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::rrrr_gggg_bbbb_rgbx);
    CHECK(adapter.machine().video.tc0100scn_positive_text_y_origin_offset() == 24);
    CHECK(adapter.machine().video.sprite_palette_bank_base() == 0U);
    REQUIRE(adapter.machine().roms.regions.contains("adpcmb"));
    CHECK(adapter.machine().roms.regions.at("adpcmb").size() == 0x20U);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter loads a Ninja Kids TC0190FMC game.toml profile",
          "[taito_f2][adapter][tc0190fmc]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "ninjak"
board = "taito_f2"
taito_f2_map = "ninjak"
taito_f2_palette_format = "rgbx_444"
taito_f2_sprite_policy = "banked"
taito_f2_sprite_buffering = "partial_delayed"
taito_f2_sprite_hide_pixels = 0
taito_f2_sprite_flip_hide_pixels = 0

[[region]]
name = "maincpu"
size = 0x080000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "tiles"
size = 0x20

[[region.file]]
name = "tiles"
offset = 0

[[region]]
name = "sprites"
size = 0x20

[[region.file]]
name = "sprites"
offset = 0

[[region]]
name = "adpcmb"
size = 0x20

[[region.file]]
name = "adpcmb"
offset = 0
)";
    auto program = make_program();
    program.resize(0x080000U);
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::move(program)},
        {"tiles", std::vector<std::uint8_t>(0x20U, 0x44U)},
        {"sprites", std::vector<std::uint8_t>(0x20U, 0x55U)},
        {"adpcmb", std::vector<std::uint8_t>(0x20U, 0x66U)},
    });

    taito_f2_adapter adapter(zip, "ninjak");

    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::ninjak);
    CHECK(adapter.machine().params.sprite_policy == taito::taito_f2_sprite_policy::banked);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::partial_delayed);
    CHECK(adapter.machine().params.sprite_hide_pixels == 0);
    CHECK(adapter.machine().params.sprite_flip_hide_pixels == 0);
    CHECK(adapter.machine().video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
    REQUIRE(adapter.machine().roms.regions.contains("adpcmb"));
    CHECK(adapter.machine().roms.regions.at("adpcmb").size() == 0x20U);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter loads a Solitary Fighter TC0190FMC game.toml profile",
          "[taito_f2][adapter][tc0190fmc]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "solfigtr"
board = "taito_f2"
taito_f2_map = "solfigtr"
taito_f2_palette_format = "rgbx_444"
taito_f2_sprite_policy = "banked"
taito_f2_sprite_buffering = "partial_delayed"
taito_f2_sprite_hide_pixels = 3
taito_f2_sprite_flip_hide_pixels = -3

[[region]]
name = "maincpu"
size = 0x080000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "tiles"
size = 0x20

[[region.file]]
name = "tiles"
offset = 0

[[region]]
name = "sprites"
size = 0x20

[[region.file]]
name = "sprites"
offset = 0

[[region]]
name = "adpcma"
size = 0x20

[[region.file]]
name = "adpcma"
offset = 0
)";
    auto program = make_program();
    program.resize(0x080000U);
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::move(program)},
        {"tiles", std::vector<std::uint8_t>(0x20U, 0x44U)},
        {"sprites", std::vector<std::uint8_t>(0x20U, 0x55U)},
        {"adpcma", std::vector<std::uint8_t>(0x20U, 0x66U)},
    });

    taito_f2_adapter adapter(zip, "solfigtr");

    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::solfigtr);
    CHECK(adapter.machine().params.sprite_policy == taito::taito_f2_sprite_policy::banked);
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::partial_delayed);
    CHECK(adapter.machine().params.sprite_hide_pixels == 3);
    CHECK(adapter.machine().params.sprite_flip_hide_pixels == -3);
    REQUIRE(adapter.machine().roms.regions.contains("adpcma"));
    CHECK(adapter.machine().roms.regions.at("adpcma").size() == 0x20U);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter merges a clone set with its parent zip",
          "[taito_f2][adapter][clone]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "liquidku"
board = "taito_f2"
parent = "liquidk"
taito_f2_map = "liquidk"
taito_f2_palette_format = "rgbx_444"
taito_f2_sprite_buffering = "partial_delayed"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "shared_prog"
offset = 0
size = 0x100000

[[region.file]]
name = "clone_patch"
offset = 0x20
size = 4
)";
    const std::vector<std::uint8_t> clone_patch{0xDEU, 0xADU, 0xBEU, 0xEFU};
    const auto clone_zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"clone_patch", clone_patch},
    });
    const auto parent_zip = make_stored_zip({{"shared_prog", make_program()}});

    const std::filesystem::path dir =
        std::filesystem::current_path() / "taito_f2_clone_parent_test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    REQUIRE_FALSE(ec);

    const std::filesystem::path parent_path = dir / "liquidk.zip";
    {
        std::ofstream out(parent_path, std::ios::binary);
        REQUIRE(out.good());
        out.write(reinterpret_cast<const char*>(parent_zip.data()),
                  static_cast<std::streamsize>(parent_zip.size()));
        REQUIRE(out.good());
    }

    const std::filesystem::path clone_path = dir / "liquidku.zip";
    taito_f2_adapter adapter(clone_zip, "liquidku", nullptr, {}, clone_path.string());

    REQUIRE(adapter.machine().roms.issues.empty());
    const auto& main = adapter.machine().roms.regions.at("maincpu");
    CHECK(main[0x20U] == 0xDEU);
    CHECK(main[0x21U] == 0xADU);
    CHECK(main[0x22U] == 0xBEU);
    CHECK(main[0x23U] == 0xEFU);
    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::liquidk);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("taito_f2_adapter resolves checked-in manifests for standard set zips",
          "[taito_f2][adapter]") {
    const auto decl = require_embedded_decl("gunfront");
    const auto zip = make_stored_zip(placeholder_entries_for(decl, 0x22U));

    const auto root =
        std::filesystem::temp_directory_path() / "mnemos_taito_f2_embedded_manifest";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    const auto rom_path = (root / "gunfront.zip").string();
    REQUIRE(mnemos::io::write_file(rom_path, zip));
    auto bytes = mnemos::io::read_file(rom_path);
    REQUIRE(bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*bytes);
    options.display_name = "gunfront";
    options.rom_path = rom_path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("taito_f2", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<taito_f2_adapter&>(*system);

    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::gunfront);
    CHECK(adapter.machine().params.sprite_policy == taito::taito_f2_sprite_policy::banked);
    REQUIRE(adapter.machine().roms.region("maincpu") != nullptr);
    REQUIRE(adapter.machine().roms.region("tiles") != nullptr);
    CHECK(adapter.machine().roms.region("maincpu")->front() == 0x22U);
    CHECK(adapter.machine().roms.region("tiles")->front() == 0x22U);
    CHECK(has_only_crc_issues(adapter.machine().roms.issues));
}

TEST_CASE("taito_f2_adapter resolves checked-in manifests inside title-wrapper zips",
          "[taito_f2][adapter][clone]") {
    const auto parent_decl = require_embedded_decl("gunfront");
    const auto parent_set_zip = make_stored_zip(placeholder_entries_for(parent_decl, 0x22U));
    const auto clone_set_zip =
        make_stored_zip({{"c71-11.ic39", std::vector<std::uint8_t>(0x20000U, 0x33U)}});
    const auto parent_wrapper = make_stored_zip({{"gunfront.zip", parent_set_zip}});
    const auto clone_wrapper = make_stored_zip({{"gunfrontj.zip", clone_set_zip}});

    const auto root =
        std::filesystem::temp_directory_path() / "mnemos_taito_f2_wrapper_manifest";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    const auto parent_path = (root / "Gun-Frontier_Arcade_EN.zip").string();
    const auto clone_path = (root / "Gun-Frontier_Arcade_JA.zip").string();
    REQUIRE(mnemos::io::write_file(parent_path, parent_wrapper));
    REQUIRE(mnemos::io::write_file(clone_path, clone_wrapper));
    auto bytes = mnemos::io::read_file(clone_path);
    REQUIRE(bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*bytes);
    options.display_name = "gunfrontj";
    options.rom_path = clone_path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("taito_f2", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<taito_f2_adapter&>(*system);

    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    CHECK(adapter.machine().params.address_map == taito::taito_f2_address_map::gunfront);
    CHECK(adapter.machine().params.sprite_policy == taito::taito_f2_sprite_policy::banked);
    const auto* main = adapter.machine().roms.region("maincpu");
    REQUIRE(main != nullptr);
    REQUIRE(main->size() > 0x40001U);
    CHECK((*main)[0] == 0x22U);
    CHECK((*main)[0x40001U] == 0x33U);
    const auto* tiles = adapter.machine().roms.region("tiles");
    REQUIRE(tiles != nullptr);
    CHECK(tiles->front() == 0x22U);
    CHECK(has_only_crc_issues(adapter.machine().roms.issues));
}

TEST_CASE("taito_f2_adapter rejects a game.toml for another board", "[taito_f2][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "wrong"
board = "capcom_cps1"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    taito_f2_adapter adapter(zip, "wrong");
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0U);
}

TEST_CASE("taito_f2_adapter drains YM2610-clocked audio", "[taito_f2][adapter][audio]") {
    taito_f2_adapter adapter(make_program(), "smoke");
    const auto initial = adapter.drain_audio();
    CHECK(initial.frame_count == 0U);
    CHECK(initial.sample_rate == taito::ym2610_clock_hz /
                                     mnemos::chips::audio::ym2610::default_clock_divider);

    adapter.step_one_frame();

    const auto chunk = adapter.drain_audio();
    CHECK(chunk.frame_count > 0U);
    CHECK(chunk.sample_rate == taito::ym2610_clock_hz /
                                   mnemos::chips::audio::ym2610::default_clock_divider);
    REQUIRE(chunk.samples != nullptr);
    CHECK(adapter.drain_audio().frame_count == 0U);
}

TEST_CASE("taito_f2_adapter boots a real Gun Frontier set", "[taito_f2][adapter][data]") {
    // Data-gated (never committed): MNEMOS_TAITO_F2_GUNFRONT_SET points at an
    // authentic gunfront/gunfrontj set zip or one-level title wrapper. The
    // adapter resolves the checked-in manifest from the path.
    const char* set_env = opt_env("MNEMOS_TAITO_F2_GUNFRONT_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_TAITO_F2_GUNFRONT_SET to a gunfront/gunfrontj set or wrapper zip");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    taito_f2_adapter adapter(std::move(*bytes), "gunfront", nullptr, {}, set_env);
    auto& machine = adapter.machine();
    REQUIRE(machine.roms.issues.empty());
    CHECK(machine.params.address_map == taito::taito_f2_address_map::gunfront);
    CHECK(machine.params.sprite_policy == taito::taito_f2_sprite_policy::banked);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);

    const auto& main_region = machine.roms.regions.at("maincpu");
    REQUIRE(main_region.size() == taito::main_rom_size);
    const std::uint32_t reset_pc = (static_cast<std::uint32_t>(main_region[4]) << 24U) |
                                   (static_cast<std::uint32_t>(main_region[5]) << 16U) |
                                   (static_cast<std::uint32_t>(main_region[6]) << 8U) |
                                   main_region[7];
    REQUIRE(reset_pc < main_region.size());

    bool frame_lit = false;
    for (int frame = 0; frame < 600 && !frame_lit; ++frame) {
        adapter.step_one_frame();
        frame_lit = current_frame_has_variation(adapter);
    }
    CHECK(machine.vblank_irq_raised > 0U);
    CHECK(frame_lit);
}

TEST_CASE("taito_f2_adapter boots a real Dino Rex set", "[taito_f2][adapter][data]") {
    const char* set_env = opt_env("MNEMOS_TAITO_F2_DINOREX_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_TAITO_F2_DINOREX_SET to dinorex.zip");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    taito_f2_adapter adapter(std::move(*bytes), "dinorex", nullptr, {}, set_env);
    auto& machine = adapter.machine();
    REQUIRE(machine.roms.issues.empty());
    CHECK(machine.params.address_map == taito::taito_f2_address_map::dinorex);
    CHECK(machine.params.sprite_policy == taito::taito_f2_sprite_policy::extension_3);
    CHECK(machine.params.palette_format == taito::taito_f2_palette_format::rrrr_gggg_bbbb_rgbx);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    CHECK(machine.video.sprite_palette_bank_base() == 0U);

    bool frame_lit = false;
    for (int frame = 0; frame < 600 && !frame_lit; ++frame) {
        adapter.step_one_frame();
        frame_lit = current_frame_has_variation(adapter);
    }

    const auto regs = machine.main_cpu.cpu_registers();
    INFO("pc=0x" << std::hex << regs.pc << " sr=0x" << regs.sr << " vblank="
                 << std::dec << machine.vblank_irq_raised << " irq_ack="
                 << machine.vblank_irq_acked << " layer=0x" << std::hex
                 << machine.video_regs[6] << " display=0x" << machine.video_regs[7]);
    CHECK(machine.vblank_irq_raised > 0U);
    CHECK(frame_lit);
}

TEST_CASE("taito_f2_adapter renders a real Dino Rex sound command stream",
          "[taito_f2][adapter][data][audio]") {
    const char* set_env = opt_env("MNEMOS_TAITO_F2_DINOREX_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_TAITO_F2_DINOREX_SET to dinorex.zip");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    auto write_command = [](taito::taito_f2_system& machine, std::uint8_t command) {
        machine.main_bus.write8(taito::dinorex_sound_comm_base, 0x00U);
        machine.main_bus.write8(taito::dinorex_sound_comm_base + 2U, command);
        machine.main_bus.write8(taito::dinorex_sound_comm_base + 2U,
                                static_cast<std::uint8_t>((command >> 4U) | (command << 4U)));
    };
    taito_f2_adapter adapter(std::move(*bytes), "dinorex", nullptr, {}, set_env);
    REQUIRE(adapter.machine().roms.issues.empty());

    for (int frame = 0; frame < 240; ++frame) {
        adapter.step_one_frame();
        (void)adapter.drain_audio();
    }

    bool audible = false;
    std::uint8_t audible_command = 0xFFU;
    constexpr std::uint8_t command_stream[] = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU,
    };
    for (const std::uint8_t command : command_stream) {
        write_command(adapter.machine(), command);
        for (int frame = 0; frame < 45 && !audible; ++frame) {
            adapter.step_one_frame();
            audible = audio_chunk_has_nonzero_sample(adapter.drain_audio());
        }
        if (audible) {
            audible_command = command;
            break;
        }
    }

    INFO("audible_command=0x" << std::hex << static_cast<unsigned>(audible_command));
    CHECK(audible);
}

TEST_CASE("taito_f2_adapter renders real Dino Rex audio during normal execution",
          "[taito_f2][adapter][data][audio]") {
    const char* set_env = opt_env("MNEMOS_TAITO_F2_DINOREX_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_TAITO_F2_DINOREX_SET to dinorex.zip");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    taito_f2_adapter adapter(std::move(*bytes), "dinorex", nullptr, {}, set_env);
    REQUIRE(adapter.machine().roms.issues.empty());

    bool audible = false;
    int audible_frame = -1;
    for (int frame = 0; frame < 3600 && !audible; ++frame) {
        mnemos::frontend_sdk::controller_state p1{};
        if (frame >= 30 && frame < 90) {
            p1.select = true;
        }
        if (frame >= 120 && frame < 180) {
            p1.start = true;
        }
        if (frame >= 220 && frame < 420) {
            p1.a = true;
        }
        if (frame >= 500 && frame < 700) {
            p1.b = true;
        }
        if (frame >= 780 && frame < 980) {
            p1.c = true;
        }
        if (frame >= 1060 && frame < 1260) {
            p1.right = true;
        }
        adapter.apply_input(0, p1);
        adapter.step_one_frame();
        audible = audio_chunk_has_nonzero_sample(adapter.drain_audio());
        if (audible) {
            audible_frame = frame;
        }
    }

    INFO("audible_frame=" << audible_frame);
    CHECK(audible);
}

TEST_CASE("taito_f2_adapter keeps real Growl on its mapped vblank handler",
          "[taito_f2][adapter][data]") {
    const char* set_env = opt_env("MNEMOS_TAITO_F2_GROWL_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_TAITO_F2_GROWL_SET to growl.zip");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    taito_f2_adapter adapter(std::move(*bytes), "growl", nullptr, {}, set_env);
    auto& machine = adapter.machine();
    REQUIRE(machine.roms.issues.empty());
    CHECK(machine.params.sprite_active_area ==
          taito::taito_f2_sprite_active_area::y_word_bit0);
    CHECK(machine.params.sprite_buffering == taito::taito_f2_sprite_buffering::immediate);
    CHECK(machine.params.palette_format == taito::taito_f2_palette_format::rrrr_gggg_bbbb_rgbx);
    CHECK(machine.video.current_sprite_active_area_source() ==
          mnemos::chips::video::taito_f2_video::sprite_active_area_source::y_word_bit0);
    CHECK(machine.video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::immediate);
    CHECK(machine.video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::rrrr_gggg_bbbb_rgbx);
    const auto& program = machine.roms.regions.at("maincpu");

    for (int frame = 0; frame < 900; ++frame) {
        mnemos::frontend_sdk::controller_state p1{};
        if (frame >= 30 && frame < 90) {
            p1.select = true;
        }
        if (frame >= 120 && frame < 180) {
            p1.start = true;
        }
        if (frame >= 220 && frame < 420) {
            p1.a = true;
        }
        adapter.apply_input(0, p1);
        adapter.step_one_frame();
        (void)adapter.drain_audio();
    }

    const auto m68k = machine.main_cpu.cpu_registers();
    INFO("pc=0x" << std::hex << m68k.pc << " sr=0x" << m68k.sr << " vblank="
                 << std::dec << machine.vblank_irq_raised << " irq_ack="
                 << machine.vblank_irq_acked);
    CHECK(machine.vblank_irq_acked > 100U);
    CHECK((m68k.pc & 1U) == 0U);
    CHECK(m68k.pc < program.size());
}

TEST_CASE("taito_f2_adapter produces real Growl audio after start",
          "[taito_f2][adapter][data][audio]") {
    const char* set_env = opt_env("MNEMOS_TAITO_F2_GROWL_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_TAITO_F2_GROWL_SET to growl.zip");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    taito_f2_adapter adapter(std::move(*bytes), "growl", nullptr, {}, set_env);
    REQUIRE(adapter.machine().roms.issues.empty());

    bool audible = false;
    int audible_frame = -1;
    for (int frame = 0; frame < 1800 && !audible; ++frame) {
        mnemos::frontend_sdk::controller_state p1{};
        if (frame >= 30 && frame < 90) {
            p1.select = true;
        }
        if (frame >= 120 && frame < 180) {
            p1.start = true;
        }
        if (frame >= 220 && frame < 420) {
            p1.a = true;
        }
        adapter.apply_input(0, p1);
        adapter.step_one_frame();
        audible = audio_chunk_has_nonzero_sample(adapter.drain_audio());
        if (audible) {
            audible_frame = frame;
        }
    }

    INFO("audible_frame=" << audible_frame);
    CHECK(audible);
}
