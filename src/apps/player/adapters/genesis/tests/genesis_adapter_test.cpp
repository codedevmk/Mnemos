// Adapter smoke. The Genesis player_system adapter is a thin shell over
// assemble_genesis() + a scheduler; this test boots it on a minimal ROM
// that just spins in place, steps a few frames, and checks the
// player_system contract holds.

#include "genesis_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

    using mnemos::apps::player::adapters::genesis::genesis_adapter;
    using mnemos::manifests::genesis::genesis_config;

    // Minimal 68000 boot: SSP, PC at $0008, MOVE.W #$2000,SR then BRA.S *
    // (the system just powers on and spins; the VDP still raster-times frames).
    std::vector<std::uint8_t> tiny_rom() {
        std::vector<std::uint8_t> rom(0x100, 0x00);
        const auto w16 = [&](std::size_t off, std::uint16_t v) {
            rom[off] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 1U] = static_cast<std::uint8_t>(v);
        };
        const auto w32 = [&](std::size_t off, std::uint32_t v) {
            rom[off + 0U] = static_cast<std::uint8_t>(v >> 24U);
            rom[off + 1U] = static_cast<std::uint8_t>(v >> 16U);
            rom[off + 2U] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 3U] = static_cast<std::uint8_t>(v);
        };
        w32(0x00, 0x00FFF000U); // SSP
        w32(0x04, 0x00000008U); // PC
        w16(0x08, 0x46FC);
        w16(0x0A, 0x2000); // MOVE.W #$2000,SR
        w16(0x0C, 0x60FE); // BRA.S *
        return rom;
    }

} // namespace

TEST_CASE("genesis_adapter advances frames and reports region") {
    genesis_adapter adapter(tiny_rom(), {.video_region = genesis_config::region::ntsc});

    CHECK(adapter.region().frames_per_second_x1000 == 60000U);
    CHECK(adapter.frames_stepped() == 0U);

    const auto fb_before = adapter.current_frame();
    REQUIRE(fb_before.width > 0U);
    REQUIRE(fb_before.height > 0U);

    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 2U);

    // current_frame() must remain accessible after stepping.
    const auto fb_after = adapter.current_frame();
    CHECK(fb_after.width == fb_before.width);
    CHECK(fb_after.height == fb_before.height);
}

TEST_CASE("genesis_adapter records input but commit 3 keeps audio silent") {
    genesis_adapter adapter(tiny_rom());

    mnemos::frontend_sdk::controller_state pad{};
    pad.start = true;
    pad.a = true;
    adapter.apply_input(0, pad);
    // No observable bus effect yet (commit 5 wires the MMIO). Just verify
    // out-of-range ports are ignored cleanly.
    adapter.apply_input(7, pad);  // ignored
    adapter.apply_input(-1, pad); // ignored

    const auto audio = adapter.drain_audio();
    CHECK(audio.samples == nullptr);
    CHECK(audio.frame_count == 0U);
    CHECK(audio.sample_rate == 0U);
}

TEST_CASE("genesis_adapter selects PAL pacing when configured") {
    genesis_adapter adapter(tiny_rom(), {.video_region = genesis_config::region::pal});
    CHECK(adapter.region().frames_per_second_x1000 == 50000U);
}
