// ZX Spectrum player-adapter smoke test: construct, step frames, and confirm the
// adapter advertises its chips and a stable framebuffer geometry. No ROM needed
// (a synthetic all-NOP image); the real-ROM boot is covered by the manifest test.

#include "spectrum_adapter.hpp"

#include "ula.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::apps::player::adapters::spectrum::spectrum_adapter;
    using ula = mnemos::chips::video::ula;
} // namespace

TEST_CASE("spectrum adapter constructs and steps frames", "[apps][player][spectrum]") {
    spectrum_adapter adapter(std::vector<std::uint8_t>(0x4000U, 0x00U));

    const auto fb0 = adapter.current_frame();
    CHECK(fb0.width == static_cast<std::uint32_t>(ula::frame_width));
    CHECK(fb0.height == static_cast<std::uint32_t>(ula::frame_height));

    adapter.step_one_frame();
    adapter.step_one_frame();

    // Two chips advertised in scheduler order: ULA (frame source) then CPU.
    CHECK(adapter.chips().size() == 2U);
    CHECK(adapter.current_frame().pixels != nullptr);
}

TEST_CASE("spectrum adapter maps the pad to Kempston + cursor keys", "[apps][player][spectrum]") {
    spectrum_adapter adapter(std::vector<std::uint8_t>(0x4000U, 0x00U));

    mnemos::frontend_sdk::controller_state state{};
    state.right = true;
    state.a = true; // fire
    adapter.apply_input(0, state);

    auto& sys = adapter.system();
    // Kempston (port $1F, active-high): right (b0) + fire (b4).
    CHECK(sys.kempston == 0x11U);
    // Cursor key 8 (right) = row 4 bit 2, fire key 0 = row 4 bit 0; both pressed
    // clear their bits (active-low) -> 0xFF & ~0x04 & ~0x01 = 0xFA.
    CHECK(sys.keyboard_rows[4] == 0xFAU);

    // Releasing everything restores the idle state.
    adapter.apply_input(0, mnemos::frontend_sdk::controller_state{});
    CHECK(sys.kempston == 0x00U);
    CHECK(sys.keyboard_rows[4] == 0xFFU);
}
