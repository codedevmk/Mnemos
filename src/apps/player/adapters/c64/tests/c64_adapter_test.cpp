// Adapter smoke. The C64 player_system adapter is a thin shell over
// build_c64_runtime() + a scheduler. This test boots it on synthetic
// zero-filled system ROMs (the 6510 runs from a null reset vector, exactly
// like the manifest parity test's always-on path), steps a few frames, and
// checks the player_system contract holds without needing a real ROM set.

#include "c64_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using mnemos::apps::player::adapters::c64::c64_adapter;
    using mnemos::manifests::c64::c64_config;

    // Correctly-sized zero ROMs: BASIC 8K, KERNAL 8K, CHARGEN 4K.
    c64_adapter make_adapter(c64_config config = {}) {
        return c64_adapter(std::vector<std::uint8_t>(0x2000U, 0x00U),
                           std::vector<std::uint8_t>(0x2000U, 0x00U),
                           std::vector<std::uint8_t>(0x1000U, 0x00U), config);
    }

} // namespace

TEST_CASE("c64_adapter advances frames and reports region") {
    c64_adapter adapter = make_adapter({.video_region = c64_config::region::pal});

    CHECK(adapter.region().frames_per_second_x1000 == 50000U);
    CHECK(adapter.frames_stepped() == 0U);

    // The VIC-II sizes its framebuffer lazily on the first rendered line, so a
    // complete frame only exists after the first step (the player guards on
    // src_w > 0 for exactly this reason).
    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 2U);

    const auto fb = adapter.current_frame();
    CHECK(fb.width > 0U);
    CHECK(fb.height > 0U);
}

TEST_CASE("c64_adapter selects NTSC pacing when configured") {
    c64_adapter adapter = make_adapter({.video_region = c64_config::region::ntsc});
    CHECK(adapter.region().frames_per_second_x1000 == 60000U);
}

TEST_CASE("c64_adapter publishes a System/Region status spec") {
    c64_adapter adapter = make_adapter();
    const auto& spec = adapter.system_spec();
    REQUIRE(spec.size() >= 2U);
    CHECK(spec[0].label == "System");
    CHECK(spec[0].value == "Commodore 64");
    CHECK(spec[1].label == "Region");
}

TEST_CASE("c64_adapter enumerates its chips in scheduler order") {
    c64_adapter adapter = make_adapter();
    const auto chips = adapter.chips();
    REQUIRE(chips.size() == 5U);
    for (auto* c : chips) {
        CHECK(c != nullptr);
    }
}

TEST_CASE("c64_adapter ignores out-of-range input ports") {
    c64_adapter adapter = make_adapter();
    mnemos::frontend_sdk::controller_state pad{};
    pad.a = true;
    adapter.apply_input(0, pad);
    adapter.apply_input(7, pad);  // ignored
    adapter.apply_input(-1, pad); // ignored
    SUCCEED();
}

TEST_CASE("c64_adapter drain_audio resamples to 48 kHz output") {
    c64_adapter adapter = make_adapter({.video_region = c64_config::region::ntsc});
    // Before stepping, no samples queued, but the adapter still reports the
    // fixed 48 kHz output rate so the player can open SDL_AudioStream upfront.
    auto audio = adapter.drain_audio();
    CHECK(audio.frame_count == 0U);
    CHECK(audio.sample_rate == 48000U);

    // Stepping one NTSC frame produces ~one frame's-worth of 48 kHz samples
    // (48000 / 60 = 800; allow +-2 for fractional-accumulator rounding).
    adapter.step_one_frame();
    audio = adapter.drain_audio();
    CHECK(audio.sample_rate == 48000U);
    CHECK(audio.frame_count >= 798U);
    CHECK(audio.frame_count <= 802U);
    REQUIRE(audio.samples != nullptr);

    // Second drain of the same frame returns nothing (SID queue consumed).
    audio = adapter.drain_audio();
    CHECK(audio.frame_count == 0U);
}
