#include "capcom_cps2_adapter.hpp"

#include "file.hpp"
#include "png_image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    namespace cps2 = mnemos::manifests::capcom_cps2;
    using mnemos::apps::player::adapters::capcom_cps2::capcom_cps2_adapter;

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
    CHECK(adapter.region().frames_per_second_x1000 == 59600U);
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

    CHECK(adapter.memory_views().size() == 10U);
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
    adapter.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.left = true;
    p2.c = true;
    p2.y = true;
    p2.select = true;
    adapter.apply_input(1, p2);

    // P1 in the low byte: right clears bit0, button 1 clears bit4.
    // P2 in the high byte: left clears bit1, button 3 clears bit6.
    const std::uint8_t p1_main = static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x10U);
    const std::uint8_t p2_main = static_cast<std::uint8_t>(0xFFU & ~0x02U & ~0x40U);
    CHECK(machine.input0 ==
          static_cast<std::uint16_t>((static_cast<std::uint16_t>(p2_main) << 8U) | p1_main));
    CHECK((machine.input0 & 0x00FFU) == static_cast<std::uint16_t>(0xFFU & ~0x11U));
    // Buttons 4/5/6 are exposed through the second input word: P1 low byte,
    // P2 high byte.
    const std::uint8_t p1_extra = static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x04U);
    const std::uint8_t p2_extra = static_cast<std::uint8_t>(0xFFU & ~0x02U);
    CHECK(machine.input1 ==
          static_cast<std::uint16_t>((static_cast<std::uint16_t>(p2_extra) << 8U) | p1_extra));
    CHECK(machine.bus().read16_be(cps2::cps_io_base + 0x10U) == machine.input1);
    // IN2 layout: START1-4 in bits 8-11, COIN1-4 in bits 12-15.
    CHECK((machine.input_sys & 0x0100U) == 0x0000U); // START1 (bit 8) active-low
    CHECK((machine.input_sys & 0x2000U) == 0x0000U); // COIN2 (bit 13, P2 select) active-low
    CHECK((machine.input_sys & 0x00FFU) == 0x00FFU); // low byte (EEPROM/unused) untouched
}

// Data-gated (never committed), game-agnostic: MNEMOS_CPS2_SET points at a zip of
// the authentic CPS2 dump files plus a "game.toml" copy of the matching set
// declaration and the 20-byte board key (a "key" region in the toml, or a
// keys/<set>.key sidecar beside the zip). The board must decrypt + boot + light a
// frame. MNEMOS_CPS2_FRAMES overrides the warm-up; MNEMOS_CPS2_PNG dumps a frame.
TEST_CASE("capcom_cps2_adapter boots a real CPS2 set", "[capcom_cps2][adapter][data]") {
    const char* set_env = opt_env("MNEMOS_CPS2_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_CPS2_SET to a CPS2 set zip (game.toml + key inside)");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    // Pass the path so clone sets resolve the parent and the key sidecar is found.
    capcom_cps2_adapter adapter(std::move(*bytes), "cps2", nullptr, {}, set_env);
    auto& machine = adapter.machine();
    REQUIRE(machine.rom_set().issues.empty()); // CRC-verified load
    REQUIRE(machine.executable());             // the board key decrypted the program

    int warmup_frames = 600;
    if (const char* frames_env = opt_env("MNEMOS_CPS2_FRAMES")) {
        const int parsed = std::atoi(frames_env);
        if (parsed > 0) {
            warmup_frames = parsed;
        }
    }

    bool frame_lit = false;
    for (int frame = 0; frame < warmup_frames; ++frame) {
        adapter.step_one_frame();
        if (!frame_lit) {
            const auto view = adapter.current_frame();
            for (std::uint32_t i = 1; i < view.width * view.height; ++i) {
                if (view.pixels[i] != view.pixels[0]) {
                    frame_lit = true;
                    break;
                }
            }
        }
    }

    if (const char* png = opt_env("MNEMOS_CPS2_PNG")) {
        CHECK(write_png(adapter.current_frame(), png));
    }

    CHECK(frame_lit);
    CHECK(machine.vblank_irq_raised() > 0U);
}
