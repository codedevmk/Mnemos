#include "capcom_cps2_adapter.hpp"

#include "cps2_crypto.hpp"
#include "cps2_game_manifests.hpp"
#include "eeprom_93c46.hpp"
#include "file.hpp"
#include "png_image.hpp"
#include "save_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    namespace cps2 = mnemos::manifests::capcom_cps2;
    using mnemos::apps::player::adapters::capcom_cps2::capcom_cps2_adapter;

    struct temp_directory final {
        std::filesystem::path path;

        explicit temp_directory(std::string_view stem) {
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            path = std::filesystem::temp_directory_path() /
                   (std::string(stem) + "_" + std::to_string(ticks));
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
            std::filesystem::create_directories(path, ec);
        }

        ~temp_directory() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    };

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

    void put16_le(std::vector<std::uint8_t>& out, std::uint16_t value) {
        out.push_back(static_cast<std::uint8_t>(value));
        out.push_back(static_cast<std::uint8_t>(value >> 8U));
    }

    void put32_le(std::vector<std::uint8_t>& out, std::uint32_t value) {
        put16_le(out, static_cast<std::uint16_t>(value));
        put16_le(out, static_cast<std::uint16_t>(value >> 16U));
    }

    std::array<std::uint8_t, cps2::crypto_key_size> sample_key() {
        std::array<std::uint8_t, cps2::crypto_key_size> k{};
        for (std::size_t i = 0; i < k.size(); ++i) {
            k[i] = static_cast<std::uint8_t>(i * 7U + 3U);
        }
        return k;
    }

    std::vector<std::uint8_t> plain_program(std::uint16_t opcode = 0x707FU) {
        std::vector<std::uint8_t> p(0x40U, 0x00U);
        const auto w16 = [&p](std::size_t a, std::uint16_t v) {
            p[a] = static_cast<std::uint8_t>(v >> 8U);
            p[a + 1U] = static_cast<std::uint8_t>(v);
        };
        w16(0x0U, 0x00FFU);
        w16(0x2U, 0x0000U); // SSP = 0x00FF0000
        w16(0x4U, 0x0000U);
        w16(0x6U, 0x0008U); // PC = 0x00000008
        w16(0x8U, opcode);
        return p;
    }

    std::vector<std::uint8_t>
    encrypted_program(const std::array<std::uint8_t, cps2::crypto_key_size>& k,
                      std::uint16_t opcode = 0x707FU) {
        const std::vector<std::uint8_t> plain = plain_program(opcode);
        cps2::cps2_crypto_key key{};
        REQUIRE(cps2::decode_key(k, key));
        std::vector<std::uint8_t> enc(plain.size());
        REQUIRE(cps2::encrypt_opcodes(plain, enc, key));
        return enc;
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
            put32_le(out, 0x04034B50U);
            put16_le(out, 20U);
            put16_le(out, 0U);
            put16_le(out, 0U);
            put32_le(out, 0U);
            put32_le(out, 0U); // crc (unchecked by the reader)
            put32_le(out, size);
            put32_le(out, size);
            put16_le(out, static_cast<std::uint16_t>(name.size()));
            put16_le(out, 0U);
            out.insert(out.end(), name.begin(), name.end());
            out.insert(out.end(), data.begin(), data.end());
            directory.push_back({name, size, local_offset});
        }
        const auto cd_offset = static_cast<std::uint32_t>(out.size());
        for (const central& c : directory) {
            put32_le(out, 0x02014B50U);
            put16_le(out, 20U);
            put16_le(out, 20U);
            put16_le(out, 0U);
            put16_le(out, 0U);
            put32_le(out, 0U);
            put32_le(out, 0U);
            put32_le(out, c.size);
            put32_le(out, c.size);
            put16_le(out, static_cast<std::uint16_t>(c.name.size()));
            put16_le(out, 0U);
            put16_le(out, 0U);
            put16_le(out, 0U);
            put16_le(out, 0U);
            put32_le(out, 0U);
            put32_le(out, c.local_offset);
            out.insert(out.end(), c.name.begin(), c.name.end());
        }
        const auto cd_size = static_cast<std::uint32_t>(out.size()) - cd_offset;
        put32_le(out, 0x06054B50U);
        put16_le(out, 0U);
        put16_le(out, 0U);
        put16_le(out, static_cast<std::uint16_t>(directory.size()));
        put16_le(out, static_cast<std::uint16_t>(directory.size()));
        put32_le(out, cd_size);
        put32_le(out, cd_offset);
        put16_le(out, 0U);
        return out;
    }

    [[nodiscard]] std::vector<std::uint8_t> make_profile_zip(std::string_view name,
                                                             std::uint8_t players,
                                                             std::string_view input) {
        std::string manifest =
            "[set]\n"
            "schema = \"mnemos-romset/1\"\n"
            "name = \"" +
            std::string{name} +
            "\"\n"
            "board = \"capcom_cps2\"\n"
            "players = " +
            std::to_string(players) +
            "\n"
            "input = \"" +
            std::string{input} +
            "\"\n"
            "\n"
            "[[region]]\n"
            "name = \"maincpu\"\n"
            "size = 64\n"
            "\n"
            "[[region.file]]\n"
            "name = \"prog\"\n"
            "offset = 0\n";
        return make_stored_zip({
            {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
            {"prog", std::vector<std::uint8_t>(64U, 0x00U)},
        });
    }

    [[nodiscard]] std::vector<std::uint8_t>
    make_keyed_save_zip(std::string_view name,
                        const std::array<std::uint8_t, cps2::crypto_key_size>& key,
                        std::uint16_t opcode = 0x60FEU) {
        std::string manifest =
            "[set]\n"
            "schema = \"mnemos-romset/1\"\n"
            "name = \"" +
            std::string{name} +
            "\"\n"
            "board = \"capcom_cps2\"\n"
            "\n"
            "[[region]]\n"
            "name = \"maincpu\"\n"
            "size = 64\n"
            "\n"
            "[[region.file]]\n"
            "name = \"prog\"\n"
            "offset = 0\n";
        const std::string key_name = std::string{name} + ".key";
        return make_stored_zip({
            {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
            {"prog", encrypted_program(key, opcode)},
            {key_name, std::vector<std::uint8_t>(key.begin(), key.end())},
        });
    }

    // Dump a framebuffer (0x00RRGGBB) to a PNG so a human can eyeball the render.
    bool write_png(const mnemos::chips::frame_buffer_view& fb, const std::string& path) {
        if (fb.pixels == nullptr || fb.width == 0U || fb.height == 0U) {
            return false;
        }
        std::vector<std::uint32_t> packed(static_cast<std::size_t>(fb.width) * fb.height, 0U);
        const std::uint32_t stride = fb.effective_stride();
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                packed[static_cast<std::size_t>(y) * fb.width + x] =
                    fb.pixels[static_cast<std::size_t>(y) * stride + x];
            }
        }
        return mnemos::graphics::images::png_image(fb.width, fb.height, std::move(packed))
            .write(path);
    }

    [[nodiscard]] bool frame_is_lit(const mnemos::chips::frame_buffer_view& view) noexcept {
        if (view.pixels == nullptr || view.width == 0U || view.height == 0U) {
            return false;
        }
        const std::uint32_t first = view.pixels[0];
        const std::uint32_t stride = view.effective_stride();
        for (std::uint32_t y = 0; y < view.height; ++y) {
            const std::uint32_t* row = view.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < view.width; ++x) {
                if (row[x] != first) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] int warmup_frames_from_env(const char* primary_name, int fallback) {
        if (const char* primary = opt_env(primary_name)) {
            const int parsed = std::atoi(primary);
            if (parsed > 0) {
                return parsed;
            }
        }
        if (const char* shared = opt_env("MNEMOS_CPS2_FRAMES")) {
            const int parsed = std::atoi(shared);
            if (parsed > 0) {
                return parsed;
            }
        }
        return fallback;
    }

    [[nodiscard]] bool run_until_lit(capcom_cps2_adapter& adapter, int warmup_frames) {
        bool lit = false;
        for (int frame = 0; frame < warmup_frames; ++frame) {
            adapter.step_one_frame();
            if (!lit && frame_is_lit(adapter.current_frame())) {
                lit = true;
            }
        }
        return lit;
    }
} // namespace

TEST_CASE("capcom_cps2_adapter constructs from a bare program and reports board state",
          "[capcom_cps2][adapter]") {
    // A bare (non-zip) binary is treated as the encrypted 68000 program. With no
    // board key it is a non-executable blocker, but the adapter still constructs,
    // exposes a valid 384x224 framebuffer, and steps frames without crashing.
    std::vector<std::uint8_t> program(0x40U, 0x00U);
    capcom_cps2_adapter adapter(std::move(program), "test");
    CHECK_FALSE(adapter.machine().executable());
    const auto fb = adapter.current_frame();
    CHECK(fb.width == 384U);
    CHECK(fb.height == 224U);
    REQUIRE(fb.pixels != nullptr);
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 1U);
    CHECK(adapter.region().frames_per_second_x1000 == 59637U);

    const auto& session = adapter.session_capabilities();
    REQUIRE(session.input_ports.size() == 2U);
    CHECK(session.input_ports[0].format == mnemos::frontend_sdk::input_device_format::arcade_panel);
    CHECK(session.input_ports[1].device_id == "cps2.panel.p2");
    CHECK(session.deterministic_frame_input);
    CHECK(session.max_input_delay_frames == 8U);

    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 1U);
    CHECK(media.media[0].id == "rom_set");
    CHECK(media.media[0].label == "test");
    CHECK(media.media[0].residency == mnemos::frontend_sdk::media_residency::resident);
    CHECK(media.media[0].byte_count == 0x40U);
    CHECK(media.media[0].provider_id == "cps2.adapter");
    CHECK(media.media[0].cache_hint == "resident");

    auto battery = adapter.battery_ram();
    REQUIRE(battery.size() == mnemos::chips::storage::eeprom_93c46::size_bytes);
    CHECK(battery[0] == 0xFFU);
    battery[0] = 0x42U;
    CHECK(adapter.machine().eeprom().bytes()[0] == 0x42U);
}

TEST_CASE("capcom_cps2_adapter publishes board memory views", "[capcom_cps2][adapter]") {
    std::vector<std::uint8_t> program(0x40U, 0x00U);
    capcom_cps2_adapter adapter(std::move(program), "test");

    const auto expect_view = [&adapter](std::string_view name, std::size_t expected_bytes) {
        bool saw = false;
        for (const auto* view : adapter.memory_views()) {
            REQUIRE(view != nullptr);
            if (view->name() == name) {
                saw = true;
                CHECK(view->bytes().size() == expected_bytes);
            }
        }
        CHECK(saw);
    };

    CHECK(adapter.memory_views().size() == 11U);
    expect_view("main_work_ram", cps2::main_ram_size);
    expect_view("video_ram", cps2::video_ram_size);
    expect_view("object_ram", cps2::object_ram_size);
    expect_view("extra_ram", cps2::extra_ram_size);
    expect_view("control_registers", cps2::control_reg_size);
    expect_view("extra_control", cps2::extra_ctrl_size);
    expect_view("cps_registers", cps2::cps_reg_size);
    expect_view("qsound_shared_ram", cps2::qsound_shared_size);
    expect_view("z80_ram", cps2::z80_ram_window);
    expect_view("qsound_work_ram", cps2::z80_work_window);
    expect_view("development_dips", cps2::development_dip_size);
}

TEST_CASE("capcom_cps2_adapter exposes CPS2 bus diagnostics registers",
          "[capcom_cps2][adapter]") {
    std::vector<std::uint8_t> program(0x40U, 0x00U);
    capcom_cps2_adapter adapter(std::move(program), "test");

    const auto chips = adapter.chips();
    REQUIRE(chips.size() == 5U);
    REQUIRE(chips.back() != nullptr);
    CHECK(chips.back()->metadata().part_number == std::string_view{"CPS2_BUS"});

    auto* registers = chips.back()->introspection().registers();
    REQUIRE(registers != nullptr);
    const auto snapshot = registers->registers();
    CHECK(snapshot.size() == 77U);

    bool saw_palette_source = false;
    bool saw_palette_control = false;
    bool saw_layer_control = false;
    bool saw_command_counter = false;
    bool saw_main_cycles = false;
    bool saw_sound_cycles = false;
    bool saw_snapshot_tail = false;
    for (const auto& reg : snapshot) {
        if (reg.name == "PAL_SRC") {
            saw_palette_source = true;
            CHECK(reg.bit_width == 32U);
        } else if (reg.name == "CPSB_PALCTRL") {
            saw_palette_control = true;
            CHECK(reg.bit_width == 16U);
        } else if (reg.name == "CPSB_LAYER") {
            saw_layer_control = true;
            CHECK(reg.bit_width == 16U);
        } else if (reg.name == "CMD68K_W") {
            saw_command_counter = true;
            CHECK(reg.value == 0U);
        } else if (reg.name == "MAINCYC") {
            saw_main_cycles = true;
            CHECK(reg.bit_width == 64U);
        } else if (reg.name == "SNDCYC") {
            saw_sound_cycles = true;
            CHECK(reg.bit_width == 64U);
        } else if (reg.name == "SNAP15") {
            saw_snapshot_tail = true;
            CHECK(reg.bit_width == 8U);
        }
    }
    CHECK(saw_palette_source);
    CHECK(saw_palette_control);
    CHECK(saw_layer_control);
    CHECK(saw_command_counter);
    CHECK(saw_main_cycles);
    CHECK(saw_sound_cycles);
    CHECK(saw_snapshot_tail);
}

TEST_CASE("capcom_cps2_adapter drains native QSound at the CPS2 frame cadence",
          "[capcom_cps2][adapter][audio]") {
    std::vector<std::uint8_t> program(0x40U, 0x00U);
    capcom_cps2_adapter adapter(std::move(program), "audio");

    const auto initial = adapter.drain_audio();
    CHECK(initial.frame_count == 0U);
    CHECK(initial.sample_rate == mnemos::chips::audio::qsound::native_sample_rate);

    adapter.step_one_frame();
    const auto chunk = adapter.drain_audio();
    const std::uint32_t expected = static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(mnemos::chips::audio::qsound::native_sample_rate) *
        cps2::refresh_hz_den / cps2::refresh_hz_num);
    CHECK(chunk.sample_rate == mnemos::chips::audio::qsound::native_sample_rate);
    CHECK(chunk.frame_count == expected);
    REQUIRE(chunk.samples != nullptr);

    const auto empty = adapter.drain_audio();
    CHECK(empty.frame_count == 0U);
    CHECK(empty.sample_rate == mnemos::chips::audio::qsound::native_sample_rate);

    for (int i = 0; i < 179; ++i) {
        adapter.step_one_frame();
    }
    const auto accumulated = adapter.drain_audio();
    const std::uint32_t expected_total = static_cast<std::uint32_t>(
        180ULL * mnemos::chips::audio::qsound::native_sample_rate * cps2::refresh_hz_den /
        cps2::refresh_hz_num);
    CHECK(accumulated.frame_count + chunk.frame_count == expected_total);
}

TEST_CASE("capcom_cps2_adapter threads game.toml orientation into the video region",
          "[capcom_cps2][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "vertical_test"
board = "capcom_cps2"
orientation = "vertical"

[[region]]
name = "maincpu"
size = 64

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::vector<std::uint8_t>(64U, 0x00U)},
    });

    capcom_cps2_adapter adapter(zip, "vertical_test");

    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    const auto* main_region = adapter.machine().rom_set().region("maincpu");
    REQUIRE(main_region != nullptr);
    CHECK(main_region->size() == 64U);
}

TEST_CASE("capcom_cps2_adapter discovers a family-compatible board key inside the zip",
          "[capcom_cps2][adapter]") {
    const auto key = sample_key();
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "1944"
board = "capcom_cps2"
orientation = "vertical"

[[region]]
name = "maincpu"
size = 64

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", encrypted_program(key)},
        {"1944u.key", std::vector<std::uint8_t>(key.begin(), key.end())},
    });

    capcom_cps2_adapter adapter(zip, "zip_key");

    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    CHECK(adapter.machine().rom_set().issues.empty());
    REQUIRE(adapter.machine().executable());
    const auto* key_region = adapter.machine().rom_set().region("key");
    REQUIRE(key_region != nullptr);
    CHECK(*key_region == std::vector<std::uint8_t>(key.begin(), key.end()));
    CHECK(adapter.machine().cpu().cpu_registers().pc == 0x00000008U);
}

TEST_CASE("capcom_cps2_adapter discovers a compatible board key inside the parent zip",
          "[capcom_cps2][adapter]") {
    const auto key = sample_key();
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "1944_mn"
board = "capcom_cps2"
parent = "1944"
orientation = "vertical"

[[region]]
name = "maincpu"
size = 64

[[region.file]]
name = "prog"
offset = 0
)";
    const auto clone_zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
    });
    const auto parent_zip = make_stored_zip({
        {"prog", encrypted_program(key)},
        {"1944u.key", std::vector<std::uint8_t>(key.begin(), key.end())},
    });

    temp_directory dir("mnemos_cps2_parent_key");
    const std::filesystem::path clone_path = dir.path / "1944_mn.zip";
    const std::filesystem::path parent_path = dir.path / "1944.zip";
    REQUIRE(mnemos::io::write_file(clone_path.string(), clone_zip));
    REQUIRE(mnemos::io::write_file(parent_path.string(), parent_zip));

    capcom_cps2_adapter adapter(clone_zip, "parent_zip_key", nullptr, {}, clone_path.string());

    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    CHECK(adapter.machine().rom_set().issues.empty());
    REQUIRE(adapter.machine().executable());
    const auto* key_region = adapter.machine().rom_set().region("key");
    REQUIRE(key_region != nullptr);
    CHECK(*key_region == std::vector<std::uint8_t>(key.begin(), key.end()));
    CHECK(adapter.machine().cpu().cpu_registers().pc == 0x00000008U);
}

TEST_CASE("capcom_cps2_adapter does not infer vertical orientation from the set name",
          "[capcom_cps2][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "1944"
board = "capcom_cps2"

[[region]]
name = "maincpu"
size = 64

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::vector<std::uint8_t>(64U, 0x00U)},
    });

    capcom_cps2_adapter adapter(zip, "manifest_orientation_default");

    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
}

TEST_CASE("capcom_cps2_adapter resolves a checked-in game manifest by zip stem",
          "[capcom_cps2][adapter]") {
    const auto key = sample_key();
    const auto zip = make_stored_zip({
        {"cps2_synth.prog", encrypted_program(key)},
        {"cps2_synth.key", std::vector<std::uint8_t>(key.begin(), key.end())},
        {"cps2_synth.gfx", std::vector<std::uint8_t>(0x80U, 0x00U)},
        {"cps2_synth.snd", std::vector<std::uint8_t>(0x8000U, 0x00U)},
        {"cps2_synth.qsnd", std::vector<std::uint8_t>(0x1000U, 0x00U)},
    });

    temp_directory dir("mnemos_cps2_embedded_manifest");
    const std::filesystem::path set_path = dir.path / "cps2_synth.zip";
    REQUIRE(mnemos::io::write_file(set_path.string(), zip));

    capcom_cps2_adapter adapter(zip, "embedded_manifest", nullptr, {}, set_path.string());

    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    CHECK(adapter.machine().rom_set().issues.empty());
    REQUIRE(adapter.machine().executable());
    const auto* main_region = adapter.machine().rom_set().region("maincpu");
    REQUIRE(main_region != nullptr);
    CHECK(main_region->size() == cps2::main_rom_size);
    const auto* key_region = adapter.machine().rom_set().region("key");
    REQUIRE(key_region != nullptr);
    CHECK(*key_region == std::vector<std::uint8_t>(key.begin(), key.end()));
    CHECK(adapter.machine().cpu().cpu_registers().pc == 0x00000008U);
}

TEST_CASE("capcom_cps2_adapter reports checked-in orientation by zip stem",
          "[capcom_cps2][adapter]") {
    const auto empty_zip = make_stored_zip({});

    temp_directory dir("mnemos_cps2_checked_in_orientation");
    const std::filesystem::path vertical_path = dir.path / "19xx.zip";
    const std::filesystem::path horizontal_path = dir.path / "1944.zip";
    REQUIRE(mnemos::io::write_file(vertical_path.string(), empty_zip));
    REQUIRE(mnemos::io::write_file(horizontal_path.string(), empty_zip));

    capcom_cps2_adapter vertical(empty_zip, "checked_in_vertical", nullptr, {},
                                 vertical_path.string());
    capcom_cps2_adapter horizontal(empty_zip, "checked_in_horizontal", nullptr, {},
                                   horizontal_path.string());

    CHECK(vertical.region().orientation ==
          mnemos::frontend_sdk::display_orientation::vertical_counterclockwise);
    CHECK(horizontal.region().orientation ==
          mnemos::frontend_sdk::display_orientation::horizontal);
}

TEST_CASE("capcom_cps2_adapter rejects a game.toml for another board", "[capcom_cps2][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "wrong_board"
board = "capcom_cps1"

[[region]]
name = "maincpu"
size = 64

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::vector<std::uint8_t>(64U, 0x00U)},
    });

    capcom_cps2_adapter adapter(zip, "wrong_board");

    const auto* main_region = adapter.machine().rom_set().region("maincpu");
    REQUIRE(main_region != nullptr);
    CHECK(main_region->empty());
    CHECK_FALSE(adapter.machine().executable());
}

TEST_CASE("capcom_cps2_adapter maps pads onto the board's active-low input words",
          "[capcom_cps2][adapter]") {
    std::vector<std::uint8_t> program(0x40U, 0x00U);
    capcom_cps2_adapter adapter(std::move(program));
    auto& machine = adapter.machine();
    CHECK(machine.input0 == 0xFFFFU); // idle: all released (active-low high)
    CHECK(machine.input1 == 0xFFFFU);

    mnemos::frontend_sdk::controller_state p1{};
    p1.right = true;
    p1.a = true;
    p1.x = true;
    p1.z = true;
    p1.start = true;
    p1.test = true;
    adapter.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.left = true;
    p2.c = true;
    p2.x = true;
    p2.y = true;
    p2.z = true;
    p2.select = true;
    p2.service = true;
    adapter.apply_input(1, p2);

    // P1 in the low byte: right clears bit0, button 1 clears bit4.
    // P2 in the high byte: left clears bit1, button 3 clears bit6.
    const std::uint8_t p1_main = static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x10U);
    const std::uint8_t p2_main = static_cast<std::uint8_t>(0xFFU & ~0x02U & ~0x40U);
    CHECK(machine.input0 ==
          static_cast<std::uint16_t>((static_cast<std::uint16_t>(p2_main) << 8U) | p1_main));
    CHECK((machine.input0 & 0x00FFU) == static_cast<std::uint16_t>(0xFFU & ~0x11U));
    // CPS-2 two-row fighters put P1 B4/B5/B6 on IN1 bits 0/1/2, P2 B4/B5 on
    // bits 4/5, and P2 B6 on IN2 bit 14.
    const std::uint16_t extra =
        static_cast<std::uint16_t>(0xFFFFU & ~0x0001U & ~0x0004U & ~0x0010U & ~0x0020U);
    CHECK(machine.input1 == extra);
    CHECK(machine.bus().read16_be(cps2::cps_io_base + 0x10U) == machine.input1);
    // IN2 layout: test/service in bits 1/2, START1-4 in bits 8-11,
    // COIN1-4 in bits 12-15, except six-button P2 B6 replaces bit 14.
    CHECK((machine.input_sys & 0x0002U) == 0x0000U); // test switch active-low
    CHECK((machine.input_sys & 0x0004U) == 0x0000U); // service credit active-low
    CHECK((machine.input_sys & 0x0100U) == 0x0000U); // START1 (bit 8) active-low
    CHECK((machine.input_sys & 0x2000U) == 0x0000U); // COIN2 (bit 13, P2 select) active-low
    CHECK((machine.input_sys & 0x4000U) == 0x0000U); // P2 BUTTON6, not COIN3
    CHECK((machine.input_sys & 0x00FFU) == 0x00F9U); // bit0 kept for EEPROM overlay
}

TEST_CASE("capcom_cps2_adapter supports SFA3 ticket-dispenser six-button wiring",
          "[capcom_cps2][adapter][input]") {
    const auto zip = make_profile_zip("sfa3b", 2U, "cps2_2p6bt");
    capcom_cps2_adapter adapter(zip, "sfa3b");

    mnemos::frontend_sdk::controller_state p1{};
    p1.x = true;
    adapter.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.z = true;
    adapter.apply_input(1, p2);

    auto& machine = adapter.machine();
    CHECK((machine.input1 & 0x0001U) == 0x0000U);  // P1 B4
    CHECK((machine.input1 & 0x2000U) == 0x0000U);  // ticket-empty line remains inactive
    CHECK((machine.input_sys & 0x4000U) == 0x0000U); // P2 B6 stays on IN2 bit 14
}

TEST_CASE("capcom_cps2_adapter maps declared four-player panels onto the second input word",
          "[capcom_cps2][adapter][input]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "four_player_test"
board = "capcom_cps2"
players = 4
input = "four_player_four_button"

[[region]]
name = "maincpu"
size = 64

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::vector<std::uint8_t>(64U, 0x00U)},
    });

    capcom_cps2_adapter adapter(zip, "four_player_test");
    const auto& session = adapter.session_capabilities();
    REQUIRE(session.input_ports.size() == 4U);
    CHECK(session.input_ports[2].port_index == 2U);
    CHECK(session.input_ports[2].player_slot == 3U);
    CHECK(session.input_ports[2].device_id == "cps2.panel.p3");
    CHECK(session.input_ports[3].label == "Player 4 Panel");

    mnemos::frontend_sdk::controller_state p3{};
    p3.down = true;
    p3.b = true;
    p3.x = true;
    p3.start = true;
    adapter.apply_input(2, p3);

    mnemos::frontend_sdk::controller_state p4{};
    p4.up = true;
    p4.c = true;
    p4.x = true;
    p4.select = true;
    adapter.apply_input(3, p4);

    auto& machine = adapter.machine();
    CHECK(machine.input0 == 0xFFFFU); // P1/P2 idle.
    const std::uint8_t p3_main = static_cast<std::uint8_t>(0xFFU & ~0x04U & ~0x20U & ~0x80U);
    const std::uint8_t p4_main = static_cast<std::uint8_t>(0xFFU & ~0x08U & ~0x40U & ~0x80U);
    CHECK(machine.input1 ==
          static_cast<std::uint16_t>((static_cast<std::uint16_t>(p4_main) << 8U) | p3_main));
    CHECK(machine.bus().read16_be(cps2::cps_io_base + 0x10U) == machine.input1);
    CHECK((machine.input_sys & 0x0400U) == 0x0000U); // START3 active-low
    CHECK((machine.input_sys & 0x8000U) == 0x0000U); // COIN4 active-low
    CHECK((machine.input_sys & 0x0100U) == 0x0100U); // START1 remains released

    adapter.apply_input(4, p3); // out-of-range port ignored.
    CHECK(machine.input1 ==
          static_cast<std::uint16_t>((static_cast<std::uint16_t>(p4_main) << 8U) | p3_main));
}

TEST_CASE("capcom_cps2_adapter honors explicit plain two-player input panels",
          "[capcom_cps2][adapter][input]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "two_player_test"
board = "capcom_cps2"
players = 2
input = "two_player"

[[region]]
name = "maincpu"
size = 64

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", std::vector<std::uint8_t>(64U, 0x00U)},
    });

    capcom_cps2_adapter adapter(zip, "two_player_test");
    const auto& session = adapter.session_capabilities();
    REQUIRE(session.input_ports.size() == 2U);

    mnemos::frontend_sdk::controller_state p1{};
    p1.right = true;
    p1.x = true;
    p1.y = true;
    p1.z = true;
    p1.start = true;
    adapter.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.x = true;
    p2.select = true;
    adapter.apply_input(1, p2);

    auto& machine = adapter.machine();
    CHECK(machine.input0 == 0xFFFEU); // P1 right only; extra buttons are not wired.
    CHECK(machine.input1 == 0xFFFFU);
    CHECK((machine.input_sys & 0x0100U) == 0x0000U); // START1 active-low
    CHECK((machine.input_sys & 0x2000U) == 0x0000U); // COIN2 active-low
}

TEST_CASE("capcom_cps2_adapter honors reduced-button two-player profiles",
          "[capcom_cps2][adapter][input]") {
    SECTION("two buttons") {
        const auto zip = make_profile_zip("two_button_test", 2U, "two_player_two_button");
        capcom_cps2_adapter adapter(zip, "two_button_test");

        mnemos::frontend_sdk::controller_state p1{};
        p1.a = true;
        p1.b = true;
        p1.c = true;
        p1.x = true;
        adapter.apply_input(0, p1);

        mnemos::frontend_sdk::controller_state p2{};
        p2.a = true;
        p2.b = true;
        p2.c = true;
        p2.x = true;
        adapter.apply_input(1, p2);

        auto& machine = adapter.machine();
        CHECK((machine.input0 & 0x0010U) == 0x0000U); // P1 BUTTON1
        CHECK((machine.input0 & 0x0020U) == 0x0000U); // P1 BUTTON2
        CHECK((machine.input0 & 0x0040U) == 0x0040U); // P1 BUTTON3 unused
        CHECK((machine.input0 & 0x0080U) == 0x0080U); // P1 BUTTON4 unused
        CHECK((machine.input0 & 0x1000U) == 0x0000U); // P2 BUTTON1
        CHECK((machine.input0 & 0x2000U) == 0x0000U); // P2 BUTTON2
        CHECK((machine.input0 & 0x4000U) == 0x4000U); // P2 BUTTON3 unused
        CHECK((machine.input0 & 0x8000U) == 0x8000U); // P2 BUTTON4 unused
        CHECK(machine.input1 == 0xFFFFU);
    }

    SECTION("one button") {
        const auto zip = make_profile_zip("one_button_test", 2U, "two_player_one_button");
        capcom_cps2_adapter adapter(zip, "one_button_test");

        mnemos::frontend_sdk::controller_state p1{};
        p1.a = true;
        p1.b = true;
        adapter.apply_input(0, p1);

        CHECK((adapter.machine().input0 & 0x0010U) == 0x0000U); // P1 BUTTON1
        CHECK((adapter.machine().input0 & 0x0020U) == 0x0020U); // P1 BUTTON2 unused
    }
}

TEST_CASE("capcom_cps2_adapter honors four-player button-count profiles",
          "[capcom_cps2][adapter][input]") {
    SECTION("four players four buttons") {
        const auto zip = make_profile_zip("four_player_four_button_test", 4U,
                                          "four_player_four_button");
        capcom_cps2_adapter adapter(zip, "four_player_four_button_test");

        mnemos::frontend_sdk::controller_state p1{};
        p1.a = true;
        p1.b = true;
        p1.c = true;
        p1.x = true;
        adapter.apply_input(0, p1);

        mnemos::frontend_sdk::controller_state p4{};
        p4.a = true;
        p4.b = true;
        p4.c = true;
        p4.x = true;
        adapter.apply_input(3, p4);

        CHECK((adapter.machine().input0 & 0x00F0U) == 0x0000U);
        CHECK((adapter.machine().input1 & 0xF000U) == 0x0000U);
    }

    SECTION("four players two buttons") {
        const auto zip = make_profile_zip("four_player_two_button_test", 4U,
                                          "four_player_two_button");
        capcom_cps2_adapter adapter(zip, "four_player_two_button_test");

        mnemos::frontend_sdk::controller_state p1{};
        p1.a = true;
        p1.b = true;
        p1.c = true;
        p1.x = true;
        adapter.apply_input(0, p1);

        mnemos::frontend_sdk::controller_state p4{};
        p4.a = true;
        p4.b = true;
        p4.c = true;
        p4.x = true;
        adapter.apply_input(3, p4);

        CHECK((adapter.machine().input0 & 0x0030U) == 0x0000U);
        CHECK((adapter.machine().input0 & 0x00C0U) == 0x00C0U);
        CHECK((adapter.machine().input1 & 0x3000U) == 0x0000U);
        CHECK((adapter.machine().input1 & 0xC000U) == 0xC000U);
    }
}

TEST_CASE("capcom_cps2_adapter honors Cyberbots' CPS2 four-button wiring",
          "[capcom_cps2][adapter][input]") {
    const auto zip = make_profile_zip("cybots_profile_test", 2U, "cybots_four_button");
    capcom_cps2_adapter adapter(zip, "cybots_profile_test");

    mnemos::frontend_sdk::controller_state p1{};
    p1.a = true;
    p1.b = true;
    p1.c = true;
    p1.x = true;
    adapter.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.a = true;
    p2.b = true;
    p2.c = true;
    p2.x = true;
    adapter.apply_input(1, p2);

    CHECK((adapter.machine().input0 & 0x0070U) == 0x0000U);
    CHECK((adapter.machine().input0 & 0x7000U) == 0x0000U);
    CHECK((adapter.machine().input0 & 0x8080U) == 0x8080U); // B4 is not on IN0.
    CHECK((adapter.machine().input1 & 0x0001U) == 0x0000U); // P1 B4
    CHECK((adapter.machine().input1 & 0x0010U) == 0x0000U); // P2 B4
}

TEST_CASE("capcom_cps2_adapter honors CPS2 analog paddle profiles",
          "[capcom_cps2][adapter][input]") {
    SECTION("Eco Fighters spinner profile") {
        const auto zip = make_profile_zip("ecofghtr_profile_test", 2U, "ecofighters_spinner");
        capcom_cps2_adapter adapter(zip, "ecofghtr_profile_test");

        mnemos::frontend_sdk::controller_state p1{};
        p1.paddle = 0x0123U;
        adapter.apply_input(0, p1);

        mnemos::frontend_sdk::controller_state p2{};
        p2.paddle = 0x0456U;
        adapter.apply_input(1, p2);

        auto& machine = adapter.machine();
        CHECK(machine.input1 == 0xFFEFU); // spinner config bit active-low.
        machine.bus().write8(0x804040U, 0x01U);
        CHECK(machine.bus().read16_be(cps2::cps_io_base) == 0x5623U);
        machine.bus().write8(0x804040U, 0x00U);
        CHECK((machine.bus().read16_be(cps2::cps_io_base) & 0x2020U) == 0x2020U);
    }

    SECTION("Puzz Loop 2 paddle profile") {
        const auto zip = make_profile_zip("pzloop2_profile_test", 2U, "puzz_loop_2_paddle");
        capcom_cps2_adapter adapter(zip, "pzloop2_profile_test");

        mnemos::frontend_sdk::controller_state p1{};
        p1.a = true;
        p1.paddle = 0x019AU;
        adapter.apply_input(0, p1);

        mnemos::frontend_sdk::controller_state p2{};
        p2.paddle = 0x02BCU;
        adapter.apply_input(1, p2);

        auto& machine = adapter.machine();
        CHECK(machine.analog_dial(0U) == 0x009AU);
        CHECK(machine.analog_dial(1U) == 0x00BCU);
        CHECK(machine.bus().read16_be(cps2::cps_io_base) == 0xBC9AU);
        machine.bus().write8(cps2::sound_reset_port, 0x02U);
        CHECK((machine.bus().read16_be(cps2::cps_io_base) & 0x0010U) == 0x0000U);
    }
}

TEST_CASE("capcom_cps2_adapter applies Mars Matrix coin-lockout polarity",
          "[capcom_cps2][adapter][input]") {
    const auto zip = make_profile_zip("mmatrix", 2U, "two_player_one_button");
    capcom_cps2_adapter adapter(zip, "mmatrix");
    auto& machine = adapter.machine();

    machine.bus().write8(cps2::sound_reset_port, 0x00U);
    CHECK_FALSE(machine.coin_lockout(0U));
    CHECK_FALSE(machine.coin_lockout(3U));
    machine.bus().write8(cps2::sound_reset_port, 0x90U);
    CHECK(machine.coin_lockout(0U));
    CHECK_FALSE(machine.coin_lockout(1U));
    CHECK_FALSE(machine.coin_lockout(2U));
    CHECK(machine.coin_lockout(3U));
}

TEST_CASE("capcom_cps2_adapter applies DIP override to the development switch window",
          "[capcom_cps2][adapter]") {
    std::vector<std::uint8_t> program(0x40U, 0x00U);
    capcom_cps2_adapter adapter(std::move(program), "dip", nullptr, std::uint16_t{0xA5C3U});
    auto& machine = adapter.machine();

    REQUIRE(machine.development_dips().size() == cps2::development_dip_size);
    CHECK(machine.development_dips()[0] == 0xC3U);
    CHECK(machine.development_dips()[1] == 0xA5U);
    CHECK(machine.development_dips()[2] == 0xFFU);
    CHECK(machine.bus().read16_be(cps2::development_dip_base) == 0xC3A5U);
    CHECK(machine.bus().read16_be(cps2::development_dip_base + 2U) == 0xFFFFU);
}

TEST_CASE("capcom_cps2_adapter save target round-trips adapter and board state",
          "[capcom_cps2][adapter][save]") {
    const auto key = sample_key();
    const auto zip = make_keyed_save_zip("save_state_test", key);

    capcom_cps2_adapter live(zip, "save_state_test");
    REQUIRE(live.machine().executable());
    CHECK(live.session_capabilities().save_state_supported);
    CHECK(live.session_capabilities().frame_exact_save_state);
    const mnemos::runtime::save_target target =
        mnemos::apps::player::adapters::capcom_cps2::build_save_target(live);
    CHECK(target.manifest_id.rfind("capcom_cps2:", 0U) == 0U);
    CHECK(target.manifest_id != "capcom_cps2");

    mnemos::frontend_sdk::controller_state p1{};
    p1.right = true;
    p1.a = true;
    p1.x = true;
    p1.start = true;
    p1.test = true;
    p1.paddle = 0x0123U;
    live.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.left = true;
    p2.c = true;
    p2.z = true;
    p2.select = true;
    p2.service = true;
    p2.paddle = 0x0456U;
    live.apply_input(1, p2);

    live.step_one_frame();
    live.step_one_frame();
    REQUIRE(live.drain_audio().frame_count > 0U);
    const std::uint16_t saved_input0 = live.machine().input0;
    const std::uint16_t saved_input1 = live.machine().input1;
    const std::uint16_t saved_input_sys = live.machine().input_sys;
    const std::uint64_t saved_frames = live.frames_stepped();
    const std::uint64_t saved_vblank = live.machine().vblank_irq_raised();

    const std::vector<std::uint8_t> saved = live.save_state();
    REQUIRE(!saved.empty());

    live.step_one_frame();
    const std::vector<std::uint8_t> reference = live.save_state();

    capcom_cps2_adapter restored(zip, "save_state_test");
    const mnemos::runtime::load_result load = restored.load_state(saved);
    REQUIRE(load.ok());
    CHECK(load.master_cycle > 0U);
    CHECK(restored.frames_stepped() == saved_frames);
    CHECK(restored.machine().vblank_irq_raised() == saved_vblank);
    CHECK(restored.machine().input0 == saved_input0);
    CHECK(restored.machine().input1 == saved_input1);
    CHECK(restored.machine().input_sys == saved_input_sys);
    CHECK(restored.drain_audio().frame_count == 0U);

    restored.step_one_frame();
    CHECK(restored.save_state() == reference);
}

TEST_CASE("capcom_cps2_adapter rejects save states from another resident set",
          "[capcom_cps2][adapter][save]") {
    const auto key = sample_key();
    const auto source_zip = make_keyed_save_zip("save_state_source", key);
    const auto other_zip = make_keyed_save_zip("save_state_other", key);

    capcom_cps2_adapter source(source_zip, "source");
    capcom_cps2_adapter other(other_zip, "other");
    REQUIRE(source.machine().executable());
    REQUIRE(other.machine().executable());

    const std::vector<std::uint8_t> saved = source.save_state();
    REQUIRE(!saved.empty());

    const mnemos::runtime::load_result result = other.load_state(saved);
    CHECK(result.status == mnemos::runtime::load_status::manifest_mismatch);
}

// Data-gated (never committed), game-agnostic: MNEMOS_CPS2_SET points at an
// authentic CPS2 set zip. The adapter resolves checked-in games/<set>.toml by
// zip stem unless the zip carries an explicit game.toml, and the 20-byte board
// key can be a .key entry inside the zip/parent zip, a "key" region in the toml,
// or a keys/<set>.key sidecar beside the zip. The board must decrypt + boot +
// light a frame. MNEMOS_CPS2_FRAMES overrides the warm-up; MNEMOS_CPS2_PNG dumps
// a frame.
TEST_CASE("capcom_cps2_adapter boots a real CPS2 set", "[capcom_cps2][adapter][data]") {
    const char* set_env = opt_env("MNEMOS_CPS2_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_CPS2_SET to a CPS2 set zip");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    // Pass the path so clone sets resolve the parent and the key sidecar is found.
    capcom_cps2_adapter adapter(std::move(*bytes), "cps2", nullptr, {}, set_env);
    auto& machine = adapter.machine();
    REQUIRE(machine.rom_set().issues.empty()); // CRC-verified load
    REQUIRE(machine.executable());             // the board key decrypted the program

    const bool frame_lit = run_until_lit(adapter, warmup_frames_from_env("MNEMOS_CPS2_FRAMES", 600));

    if (const char* png = opt_env("MNEMOS_CPS2_PNG")) {
        CHECK(write_png(adapter.current_frame(), png));
    }

    CHECK(frame_lit);
    CHECK(machine.vblank_irq_raised() > 0U);
}

TEST_CASE("capcom_cps2_adapter boots every available real CPS2 set in the matrix directory",
          "[capcom_cps2][adapter][data][matrix]") {
    const char* dir_env = opt_env("MNEMOS_CPS2_SET_DIR");
    if (dir_env == nullptr) {
        SKIP("set MNEMOS_CPS2_SET_DIR to a directory containing CPS2 zip files");
    }

    namespace fs = std::filesystem;
    const fs::path set_dir{dir_env};
    REQUIRE(fs::is_directory(set_dir));

    std::vector<fs::path> available_sets;
    const auto add_set = [&available_sets](fs::path set_path) {
        set_path = set_path.lexically_normal();
        if (std::find(available_sets.begin(), available_sets.end(), set_path) ==
            available_sets.end()) {
            available_sets.push_back(std::move(set_path));
        }
    };
    for (const cps2::cps2_game_manifest_view& entry : cps2::cps2_game_manifest_catalog) {
        if (entry.set_id == "cps2_synth") {
            continue;
        }
        const fs::path set_path = set_dir / (std::string(entry.set_id) + ".zip");
        if (fs::is_regular_file(set_path)) {
            add_set(set_path);
        }
    }
    // Include clone/self-describing zips that are not yet in the checked-in catalog.
    for (const fs::directory_entry& entry : fs::directory_iterator(set_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".zip") {
            continue;
        }
        add_set(entry.path());
    }
    if (available_sets.empty()) {
        SKIP("MNEMOS_CPS2_SET_DIR contains no CPS2 zip files");
    }
    std::sort(available_sets.begin(), available_sets.end());

    const int warmup_frames = warmup_frames_from_env("MNEMOS_CPS2_MATRIX_FRAMES", 600);
    const char* png_dir = opt_env("MNEMOS_CPS2_MATRIX_PNG_DIR");

    for (const fs::path& set_path : available_sets) {
        INFO(set_path.string());
        auto bytes = mnemos::io::read_file(set_path.string());
        REQUIRE(bytes.has_value());

        capcom_cps2_adapter adapter(std::move(*bytes), set_path.stem().string(), nullptr, {},
                                    set_path.string());
        auto& machine = adapter.machine();
        CHECK(machine.rom_set().issues.empty());
        REQUIRE(machine.executable());

        const bool frame_lit = run_until_lit(adapter, warmup_frames);
        if (png_dir != nullptr) {
            const fs::path png_path =
                fs::path(png_dir) / (set_path.stem().string() + ".png");
            CHECK(write_png(adapter.current_frame(), png_path.string()));
        }
        CHECK(frame_lit);
        CHECK(machine.vblank_irq_raised() > 0U);
    }
}
