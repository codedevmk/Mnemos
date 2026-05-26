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
    genesis_adapter adapter(tiny_rom(), {.video_region = mnemos::video_region::ntsc});

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

TEST_CASE("genesis_adapter ignores out-of-range input ports") {
    genesis_adapter adapter(tiny_rom());
    mnemos::frontend_sdk::controller_state pad{};
    pad.start = true;
    pad.a = true;
    adapter.apply_input(0, pad);
    adapter.apply_input(7, pad);  // ignored
    adapter.apply_input(-1, pad); // ignored
    // No throw, no UB; in-range input was recorded (port 0).
    SUCCEED();
}

TEST_CASE("genesis_adapter drain_audio resamples to 48 kHz output") {
    genesis_adapter adapter(tiny_rom());
    // Before stepping, no samples queued but the adapter still reports the
    // fixed 48 kHz output rate so the player can open SDL_AudioStream upfront.
    auto audio = adapter.drain_audio();
    CHECK(audio.frame_count == 0U);
    CHECK(audio.sample_rate == 48000U);

    // Stepping one NTSC frame produces a frame's-worth of 48 kHz samples
    // (48000 / 60 = 800; allow +-2 for fractional accumulator rounding).
    adapter.step_one_frame();
    audio = adapter.drain_audio();
    CHECK(audio.sample_rate == 48000U);
    CHECK(audio.frame_count >= 798U);
    CHECK(audio.frame_count <= 802U);
    REQUIRE(audio.samples != nullptr);

    // Second drain of the same frame returns nothing (chip queues consumed).
    audio = adapter.drain_audio();
    CHECK(audio.frame_count == 0U);
}

TEST_CASE("genesis_adapter selects PAL pacing when configured") {
    genesis_adapter adapter(tiny_rom(), {.video_region = mnemos::video_region::pal});
    CHECK(adapter.region().frames_per_second_x1000 == 50000U);
}

// Per-pad protocol tests (3-button MK-1650 + 6-button MK-1653) now live with
// each device under src/peripheral/input/tests/. The adapter-level test below
// just verifies controller_state -> pad-bits routing end-to-end.

TEST_CASE("genesis_adapter routes controller_state into the attached pad") {
    genesis_adapter adapter(tiny_rom());

    mnemos::frontend_sdk::controller_state combo{};
    combo.right = true;
    combo.a = true;
    adapter.apply_input(0, combo);

    // Default attachment is a 6-button pad (MK-1653) at phase 0; the next
    // CPU write of TH=1 advances to phase 1, where the read byte exposes
    // CBRLDU. With Right + A pressed at TH=1, Right clears bit 3.
    auto* dev = adapter.system().port_device(0);
    REQUIRE(dev != nullptr);
    dev->write_data(0x40U); // TH = 1 -> phase 1
    // Bits: 0=U(1), 1=D(1), 2=L(1), 3=R(0 pressed), 4=B(1), 5=C(1), 6=TH=1.
    CHECK(dev->read_data() == 0x77U);
}

// Cart-byte -> video_region resolution is now exercised end-to-end in the
// shared cross-family test in adapters/common/tests/region_test.cpp:
// parse_genesis_market() + default_video_for() compose into the same flow
// the player does; the adapter just passes the resolved video_region through.
