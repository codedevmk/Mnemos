#include "taito_f2_adapter.hpp"

#include "adapter_registry.hpp"
#include "file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
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
    CHECK(adapter.media_capabilities().media[0].provider_id == "taito_f2.adapter");
}

TEST_CASE("taito_f2_adapter publishes board memory views", "[taito_f2][adapter]") {
    taito_f2_adapter adapter(make_program(), "smoke");

    CHECK(adapter.memory_views().size() == 7U);
    CHECK(adapter.memory_views()[0]->name() == "work_ram");
    CHECK(adapter.memory_views()[0]->bytes().size() == taito::work_ram_size);
    CHECK(adapter.memory_views()[1]->name() == "palette_ram");
    CHECK(adapter.memory_views()[2]->name() == "tile_ram");
    CHECK(adapter.memory_views()[3]->name() == "tile_ram_secondary");
    CHECK(adapter.memory_views()[4]->name() == "sprite_ram");
    CHECK(adapter.memory_views()[5]->name() == "roz_ram");
    CHECK(adapter.memory_views()[5]->bytes().size() == taito::dondokod_roz_ram_size);
    CHECK(adapter.memory_views()[6]->name() == "z80_ram");
}

TEST_CASE("taito_f2_adapter maps pads onto active-low board inputs", "[taito_f2][adapter]") {
    taito_f2_adapter adapter(make_program(), "smoke");

    mnemos::frontend_sdk::controller_state p1{};
    p1.up = true;
    p1.a = true;
    p1.start = true;
    p1.select = true;
    adapter.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.right = true;
    adapter.apply_input(1, p2);

    CHECK(adapter.machine().input_p1 == static_cast<std::uint8_t>(0xFFU & ~0x08U & ~0x10U));
    CHECK(adapter.machine().input_p2 == static_cast<std::uint8_t>(0xFFU & ~0x01U));
    CHECK(adapter.machine().input_system == static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x10U));
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

TEST_CASE("taito_f2_adapter loads a Dino Rex sprite-extension game.toml profile",
          "[taito_f2][adapter][sprite_ext]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "dinorex"
board = "taito_f2"
taito_f2_map = "dinorex"
taito_f2_palette_format = "xrgb_555"
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
    CHECK(adapter.machine().params.palette_format == taito::taito_f2_palette_format::xrgb_555);
    REQUIRE(adapter.machine().params.sprite_extension_base.has_value());
    REQUIRE(adapter.machine().params.sprite_extension_size.has_value());
    CHECK(*adapter.machine().params.sprite_extension_base == taito::dinorex_sprite_extension_base);
    CHECK(*adapter.machine().params.sprite_extension_size == taito::dinorex_sprite_extension_size);
    CHECK(adapter.machine().video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::extension_low_as_high);
    CHECK(adapter.machine().video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::xrgb_555);
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
    CHECK(adapter.machine().video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
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
taito_f2_palette_format = "rgbx_444"
taito_f2_sprite_policy = "banked"
taito_f2_sprite_buffering = "partial_delayed"
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
    CHECK(adapter.machine().params.sprite_buffering ==
          taito::taito_f2_sprite_buffering::partial_delayed);
    CHECK(adapter.machine().params.palette_format == taito::taito_f2_palette_format::rgbx_444);
    CHECK(adapter.machine().video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
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
    adapter.step_one_frame();

    const auto chunk = adapter.drain_audio();
    CHECK(chunk.frame_count > 0U);
    CHECK(chunk.sample_rate == taito::ym2610_clock_hz /
                                   mnemos::chips::audio::ym2610::default_clock_divider);
    REQUIRE(chunk.samples != nullptr);
    CHECK(adapter.drain_audio().frame_count == 0U);
}

TEST_CASE("taito_f2_adapter boots a real Gun Frontier set", "[taito_f2][adapter][data]") {
    // Data-gated (never committed): MNEMOS_TAITO_F2_GUNFRONT_SET points at a
    // zip of the authentic dump files plus a "game.toml" copy of
    // src/manifests/taito_f2/games/gunfront.toml. This verifies the real F2
    // decode path: CRC-clean declarative load, vertical presentation metadata,
    // Gun Frontier map selection, vblank IRQ generation, and a non-blank frame.
    const char* set_env = opt_env("MNEMOS_TAITO_F2_GUNFRONT_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_TAITO_F2_GUNFRONT_SET to gunfront.zip with game.toml inside");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    taito_f2_adapter adapter(std::move(*bytes), "gunfront");
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
        const auto view = adapter.current_frame();
        for (std::uint32_t i = 1; i < view.width * view.height; ++i) {
            if (view.pixels[i] != view.pixels[0]) {
                frame_lit = true;
                break;
            }
        }
    }
    CHECK(machine.vblank_irq_raised > 0U);
    CHECK(frame_lit);
}
