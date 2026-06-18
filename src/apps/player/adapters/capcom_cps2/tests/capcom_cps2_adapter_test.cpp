#include "capcom_cps2_adapter.hpp"

#include "file.hpp"
#include "png_image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
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

TEST_CASE("capcom_cps2_adapter maps pads onto the board's active-low input words",
          "[capcom_cps2][adapter]") {
    std::vector<std::uint8_t> program(0x40U, 0x00U);
    capcom_cps2_adapter adapter(std::move(program));
    auto& machine = adapter.machine();
    CHECK(machine.input0 == 0xFFFFU); // idle: all released (active-low high)

    mnemos::frontend_sdk::controller_state p1{};
    p1.right = true;
    p1.a = true;
    p1.start = true;
    adapter.apply_input(0, p1);
    // P1 in the low byte: right clears bit0, button 1 clears bit4.
    CHECK((machine.input0 & 0x00FFU) == static_cast<std::uint16_t>(0xFFU & ~0x11U));
    CHECK((machine.input0 & 0xFF00U) == 0xFF00U);    // P2 untouched
    CHECK((machine.input_sys & 0x0100U) == 0x0000U); // START1 (IN2 bit 8) active-low
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
