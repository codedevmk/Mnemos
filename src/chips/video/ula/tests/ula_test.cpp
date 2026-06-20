// ZX Spectrum ULA unit tests: exact-pixel bitmap/attribute rendering, the flat
// border, the FLASH attribute swap, the 50 Hz /INT pulse timing, and save/load.
// No ROM or CPU needed -- the ULA is driven directly.

#include "ula.hpp"

#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

    using mnemos::chips::video::ula;

    constexpr std::uint32_t white_normal = 0xC0C0C0U;
    constexpr std::uint32_t black = 0x000000U;
    constexpr std::uint32_t red_normal = 0xC00000U;

    // Index into the ULA framebuffer for screen pixel (sx, sy) in 0..255 / 0..191.
    std::uint32_t screen_px(const ula& u, int sx, int sy) {
        const auto fb = u.framebuffer();
        const auto x = static_cast<std::uint32_t>(ula::screen_x_offset + sx);
        const auto y = static_cast<std::uint32_t>(ula::screen_y_offset + sy);
        return fb.pixels[y * fb.effective_stride() + x];
    }

} // namespace

TEST_CASE("ula renders ink/paper from bitmap + attribute bytes", "[chips][video][ula]") {
    std::array<std::uint8_t, ula::screen_ram_bytes> ram{};
    // Cell (0,0): bitmap offset 0, attribute offset 0x1800. paper = white(7),
    // ink = black(0): attr = paper<<3 = 0x38. Left 4 pixels ink, right 4 paper.
    ram[0] = 0xF0U;
    ram[0x1800] = 0x38U;

    ula u;
    u.set_screen_ram(ram);
    u.render_frame();

    for (int x = 0; x < 4; ++x) {
        CHECK(screen_px(u, x, 0) == black); // ink
    }
    for (int x = 4; x < 8; ++x) {
        CHECK(screen_px(u, x, 0) == white_normal); // paper
    }
}

TEST_CASE("ula border fills the frame around the screen", "[chips][video][ula]") {
    std::array<std::uint8_t, ula::screen_ram_bytes> ram{};
    ula u;
    u.set_screen_ram(ram);
    u.set_border(2); // red
    u.render_frame();

    const auto fb = u.framebuffer();
    CHECK(fb.width == static_cast<std::uint32_t>(ula::frame_width));
    CHECK(fb.height == static_cast<std::uint32_t>(ula::frame_height));
    CHECK(fb.pixels[0] == red_normal);                        // top-left border
    CHECK(fb.pixels[fb.width * fb.height - 1] == red_normal); // bottom-right border
    CHECK(u.border() == 2);
}

TEST_CASE("ula bright attribute selects the bright palette", "[chips][video][ula]") {
    std::array<std::uint8_t, ula::screen_ram_bytes> ram{};
    ram[0] = 0xFFU;      // all ink
    ram[0x1800] = 0x42U; // ink = red(2), bright (bit6)
    ula u;
    u.set_screen_ram(ram);
    u.render_frame();
    CHECK(screen_px(u, 0, 0) == 0xFF0000U); // bright red
}

TEST_CASE("ula FLASH swaps ink/paper every 16 frames", "[chips][video][ula]") {
    std::array<std::uint8_t, ula::screen_ram_bytes> ram{};
    ram[0] = 0x00U;      // all paper
    ram[0x1800] = 0xB8U; // FLASH (bit7), paper = white(7), ink = black(0)
    ula u;
    u.set_screen_ram(ram);

    u.tick(ula::tstates_per_frame); // frame 1: flash phase off
    CHECK(u.frame_index() == 1U);
    CHECK(screen_px(u, 0, 0) == white_normal);

    for (int i = 0; i < 15; ++i) {
        u.tick(ula::tstates_per_frame); // reach frame 16: flash phase on
    }
    CHECK(u.frame_index() == 16U);
    CHECK(screen_px(u, 0, 0) == black); // ink/paper swapped
}

TEST_CASE("ula captures a mid-frame border change per scanline", "[chips][video][ula]") {
    // The beam draws as it sweeps, so changing the border partway through a frame
    // splits the border into stripes (the basis of rainbow loaders / border FX).
    constexpr std::uint32_t green_normal = 0x00C000U;
    std::array<std::uint8_t, ula::screen_ram_bytes> ram{};
    ula u;
    u.set_screen_ram(ram);

    u.set_border(2);                        // red for the top of the frame
    u.tick(ula::tstates_per_frame / 2);     // sweep the upper half
    u.set_border(4);                        // green for the bottom
    u.tick(ula::tstates_per_frame / 2 + 1); // finish the frame (cross the boundary)

    REQUIRE(u.frame_index() == 1U);
    const auto fb = u.framebuffer();
    // A top-border row was painted while the border was red; a bottom-border row
    // while it was green. Whole-frame rendering could only show one colour.
    CHECK(fb.pixels[10U * fb.effective_stride()] == red_normal);
    CHECK(fb.pixels[300U * fb.effective_stride()] == green_normal);
}

TEST_CASE("ula pulses /INT for 32 T-states at each frame start", "[chips][video][ula]") {
    ula u;
    bool line = false;
    int rising = 0;
    u.set_irq_callback([&](bool asserted) {
        line = asserted;
        if (asserted) {
            ++rising;
        }
    });

    u.tick(ula::tstates_per_frame); // crosses the first frame boundary
    CHECK(u.frame_index() == 1U);
    CHECK(line); // /INT asserted at frame start
    CHECK(rising == 1);

    u.tick(ula::irq_pulse_tstates); // 32 T-states later it releases
    CHECK_FALSE(line);

    u.tick(ula::tstates_per_frame - ula::irq_pulse_tstates); // next frame boundary
    CHECK(u.frame_index() == 2U);
    CHECK(line);
    CHECK(rising == 2);
}

TEST_CASE("ula save/load round-trips its timing + border state", "[chips][video][ula]") {
    ula u;
    u.set_border(5);
    u.tick(1000); // advance into a frame

    std::vector<std::uint8_t> buf;
    mnemos::chips::state_writer writer(buf);
    u.save_state(writer);

    ula restored;
    mnemos::chips::state_reader reader(buf);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.border() == 5);
    CHECK(restored.frame_index() == u.frame_index());
}
