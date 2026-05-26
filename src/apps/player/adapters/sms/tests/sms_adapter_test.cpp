// Adapter smoke. The SMS player_system adapter is a thin shell over
// assemble_sms() + a scheduler; this test boots it on a minimal ROM
// that just halts the Z80, steps a few frames, and checks the
// player_system contract holds.

#include "sms_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

    using mnemos::apps::player::adapters::sms::sms_adapter;

    // 32 KiB ROM whose entry-point code at $0000 disables interrupts and
    // halts the Z80 in place. The VDP still raster-times frames; the CPU is
    // just a passenger so the adapter can be exercised without a real game.
    std::vector<std::uint8_t> tiny_rom() {
        std::vector<std::uint8_t> rom(0x8000U, 0x00U);
        rom[0x0000U] = 0xF3U; // DI
        rom[0x0001U] = 0x76U; // HALT
        return rom;
    }

} // namespace

TEST_CASE("sms_adapter advances frames and reports region") {
    sms_adapter adapter(tiny_rom(), {.video_region = mnemos::video_region::ntsc});

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

TEST_CASE("sms_adapter ignores out-of-range input ports") {
    sms_adapter adapter(tiny_rom());
    mnemos::frontend_sdk::controller_state pad{};
    pad.a = true;
    adapter.apply_input(0, pad);
    adapter.apply_input(7, pad);  // ignored
    adapter.apply_input(-1, pad); // ignored
    SUCCEED();
}

TEST_CASE("sms_adapter drain_audio resamples to 48 kHz output") {
    sms_adapter adapter(tiny_rom());
    // Before stepping, no samples queued, but the adapter still reports the
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

    // Second drain of the same frame returns nothing (PSG queue consumed).
    audio = adapter.drain_audio();
    CHECK(audio.frame_count == 0U);
}

TEST_CASE("sms_adapter selects PAL pacing when configured") {
    sms_adapter adapter(tiny_rom(), {.video_region = mnemos::video_region::pal});
    CHECK(adapter.region().frames_per_second_x1000 == 50000U);
}

TEST_CASE("sms_adapter routes controller_state through the attached pad") {
    sms_adapter adapter(tiny_rom());
    auto& sys = adapter.system();

    // Idle: pad's 6 input pins all idle (active-low byte = 0x3F in low bits,
    // 0xC0 in the two high bits the pad doesn't drive) -> 0xFF.
    mnemos::frontend_sdk::controller_state idle{};
    adapter.apply_input(0, idle);
    REQUIRE(sys.port_device(0) != nullptr);
    CHECK(sys.port_device(0)->read_data() == 0xFFU);

    // Press dpad up + A -> Button 1. Up clears bit 0; Button 1 clears bit 4.
    mnemos::frontend_sdk::controller_state combo{};
    combo.up = true;
    combo.a = true;
    adapter.apply_input(0, combo);
    CHECK(sys.port_device(0)->read_data() == 0xEEU);

    // Port 1 routes independently. Right + B -> Button 2.
    mnemos::frontend_sdk::controller_state right_b2{};
    right_b2.right = true;
    right_b2.b = true;
    adapter.apply_input(1, right_b2);
    CHECK(sys.port_device(1)->read_data() == 0xD7U);
    CHECK(sys.port_device(0)->read_data() == 0xEEU); // unchanged
}

// Cart-byte -> video_region resolution is exercised end-to-end in
// adapters/common/tests/region_test.cpp (parse_sega8_target + default_video_for).
// The SMS adapter just passes the resolved video_region through.
