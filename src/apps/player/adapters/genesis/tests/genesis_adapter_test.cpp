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

TEST_CASE("genesis_adapter routes controller_state through the system pad protocol") {
    using namespace mnemos::manifests::genesis;
    genesis_adapter adapter(tiny_rom());
    auto& sys = adapter.system();

    // Idle (no buttons pressed). On a 3-button Genesis pad with TH=high
    // (default), all buttons read as active-low '1' bits, so the byte is
    // 0x40 | 0x3F = 0x7F (bit 6 = TH high reflection, bits 0-5 = no buttons).
    mnemos::frontend_sdk::controller_state idle{};
    adapter.apply_input(0, idle);
    sys.pad_th[0] = true;
    CHECK(sys.read_pad_port(0) == 0x7FU);

    // Press Start. With TH high, Start isn't visible (bank shows B/C/dpad).
    // The byte must still report all other bits idle.
    mnemos::frontend_sdk::controller_state with_start{};
    with_start.start = true;
    adapter.apply_input(0, with_start);
    sys.pad_th[0] = true;
    CHECK(sys.read_pad_port(0) == 0x7FU); // TH high -> Start invisible

    // Toggle TH low. Start now visible at bit 5; bits 2,3 always 0 so a
    // 3-button pad is identifiable (left+right always "pressed" in this bank).
    sys.pad_th[0] = false;
    // Expected byte: TH=0 (no 0x40), Start press clears bit 5, bits 2/3 = 0.
    // Idle for U/D/A: bits 0,1,4 = 1. So byte = 0b00010011 = 0x13.
    CHECK(sys.read_pad_port(0) == 0x13U);

    // Press D-pad Right + button A with TH low.
    mnemos::frontend_sdk::controller_state combo{};
    combo.right = true;
    combo.a = true;
    adapter.apply_input(0, combo);
    sys.pad_th[0] = false;
    // TH low: bits 2,3 = 0 (L/R show as pressed regardless); A clears bit 4.
    // U/D idle (bits 0,1 = 1); Start idle (bit 5 = 1). Byte = 0b00100011 = 0x23.
    CHECK(sys.read_pad_port(0) == 0x23U);

    // Same combo with TH high: now Right is visible at bit 3 (cleared).
    sys.pad_th[0] = true;
    // Bits: 0=U(1), 1=D(1), 2=L(1), 3=R(0 pressed), 4=B(1), 5=C(1), 6=TH(1) = 0x77.
    CHECK(sys.read_pad_port(0) == 0x77U);
}

TEST_CASE("genesis_adapter 6-button extended-read protocol") {
    using namespace mnemos::manifests::genesis;
    genesis_adapter adapter(tiny_rom());
    auto& sys = adapter.system();

    // Press the extended buttons (X/Y/Z + Mode) so phase 7 has visible state.
    mnemos::frontend_sdk::controller_state s{};
    s.x = true;
    s.y = true;
    s.z = true;
    s.mode = true;
    adapter.apply_input(0, s);

    // Start fresh: V-blank reset puts phase at 0.
    sys.pad_reset_phases();
    sys.pad_th[0] = false;

    // The protocol pulses TH high/low; each transition advances the phase.
    // Walk all 8 phases and check the bytes the 68K would observe.
    auto pulse = [&](bool th) {
        sys.pad_write_th(0, th);
        return sys.read_pad_port(0);
    };

    // Phase 1 (TH=1): standard CBRLDU bank, no buttons pressed -> 0x7F.
    CHECK(pulse(true) == 0x7FU);
    // Phase 2 (TH=0): standard SA00DU bank -> 0x33 (no presses).
    CHECK(pulse(false) == 0x33U);
    // Phase 3 (TH=1): still standard CBRLDU.
    CHECK(pulse(true) == 0x7FU);
    // Phase 4 (TH=0): still standard SA00DU.
    CHECK(pulse(false) == 0x33U);
    // Phase 5 (TH=1): still standard.
    CHECK(pulse(true) == 0x7FU);
    // Phase 6 (TH=0): 6-button id -- bits 3..0 must all be 0 (dpad zeroed).
    //   Bits 5/4 still S/A idle (=1); bits 3..0 = 0000. Byte = 0x30.
    CHECK(pulse(false) == 0x30U);
    // Phase 7 (TH=1): extended bank C B Mode X Y Z. We pressed all four
    //   extended buttons, so bits 3..0 (Mode/X/Y/Z) are 0; bits 5/4 (C/B)
    //   are idle (=1). Byte = 0b01110000 = 0x70.
    CHECK(pulse(true) == 0x70U);
    // Phase 0 (wrap): back to standard SA00DU.
    CHECK(pulse(false) == 0x33U);
}

// Cart-byte -> video_region resolution is now exercised end-to-end in the
// shared cross-family test in adapters/common/tests/region_test.cpp:
// parse_genesis_market() + default_video_for() compose into the same flow
// the player does; the adapter just passes the resolved video_region through.
