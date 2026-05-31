// Pins down the DSP helpers shared by every player_system adapter's audio
// path: saturation, Q12 rounded scaling, linear/box interpolation. Pure
// math, no per-system knowledge.

#include "audio_resampler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using mnemos::dsp::clip_i16;
using mnemos::dsp::kMixerGainOne;
using mnemos::dsp::kOutputRate;
using mnemos::dsp::sample_channel_box;
using mnemos::dsp::sample_channel_linear;
using mnemos::dsp::scale_q12;

TEST_CASE("clip_i16 saturates to int16 bounds") {
    CHECK(clip_i16(0) == 0);
    CHECK(clip_i16(32767) == 32767);
    CHECK(clip_i16(-32768) == -32768);
    CHECK(clip_i16(100'000) == 32767);
    CHECK(clip_i16(-100'000) == -32768);
}

TEST_CASE("scale_q12 applies Q12 gain with half-away-from-zero rounding") {
    // gain = 1.0 (kMixerGainOne) -> identity
    CHECK(scale_q12(1234, kMixerGainOne) == 1234);
    CHECK(scale_q12(-1234, kMixerGainOne) == -1234);

    // gain = 0.5 (kMixerGainOne / 2)
    CHECK(scale_q12(1000, kMixerGainOne / 2) == 500);
    CHECK(scale_q12(-1000, kMixerGainOne / 2) == -500);

    // gain = 0 -> zero
    CHECK(scale_q12(12345, 0) == 0);

    // Rounding: 1 * 1.5 (Q12=6144 = 1.5*4096) -> 1.5 -> rounds to 2
    // via half-away-from-zero. (1*6144 + 2048) / 4096 = 2.
    CHECK(scale_q12(1, 6144) == 2);
    CHECK(scale_q12(-1, 6144) == -2);
}

TEST_CASE("kOutputRate is the player's fixed 48 kHz contract") { CHECK(kOutputRate == 48000U); }

TEST_CASE("sample_channel_linear interpolates between adjacent frames") {
    // Stereo (stride 2): L=100,200,400  R=10,20,40
    const std::array<std::int16_t, 6> stereo = {100, 10, 200, 20, 400, 40};

    // pos 0.0 -> first frame's left channel
    CHECK(sample_channel_linear(stereo.data(), 2, 0, 3, 0.0) == 100);
    // pos 0.5 between frames 0 and 1: L = 100 + (200-100)*0.5 = 150
    CHECK(sample_channel_linear(stereo.data(), 2, 0, 3, 0.5) == 150);
    // pos 1.5 between frames 1 and 2: L = 200 + (400-200)*0.5 = 300
    CHECK(sample_channel_linear(stereo.data(), 2, 0, 3, 1.5) == 300);
    // pos past end clamps to last sample
    CHECK(sample_channel_linear(stereo.data(), 2, 0, 3, 10.0) == 400);
    // Right channel still readable
    CHECK(sample_channel_linear(stereo.data(), 2, 1, 3, 0.0) == 10);
    CHECK(sample_channel_linear(stereo.data(), 2, 1, 3, 0.5) == 15);
}

TEST_CASE("sample_channel_linear handles single-frame source") {
    const std::int16_t lone[1] = {42};
    CHECK(sample_channel_linear(lone, 1, 0, 1, 0.0) == 42);
    CHECK(sample_channel_linear(lone, 1, 0, 1, 0.7) == 42);
}

TEST_CASE("sample_channel_linear returns 0 for null or empty source") {
    CHECK(sample_channel_linear(nullptr, 1, 0, 0, 0.0) == 0);
    const std::int16_t lone[1] = {42};
    CHECK(sample_channel_linear(lone, 1, 0, 0, 0.0) == 0);
}

TEST_CASE("sample_channel_box averages over an integer range") {
    // Mono: [100, 200, 300, 400]
    const std::array<std::int16_t, 4> mono = {100, 200, 300, 400};
    // [0, 2): box over indices 0 and 1 -> (100+200)/2 = 150
    CHECK(sample_channel_box(mono.data(), 1, 0, 4, 0.0, 2.0) == 150);
    // [1, 4): (200+300+400)/3 = 300
    CHECK(sample_channel_box(mono.data(), 1, 0, 4, 1.0, 4.0) == 300);
}

TEST_CASE("sample_channel_box falls back to linear for degenerate range") {
    const std::array<std::int16_t, 4> mono = {100, 200, 300, 400};
    // end <= start: falls back to sample_channel_linear at start.
    CHECK(sample_channel_box(mono.data(), 1, 0, 4, 1.5, 1.5) == 250); // interp at 1.5
    CHECK(sample_channel_box(mono.data(), 1, 0, 4, 2.0, 1.0) == 300); // 1.0 -> frame 1
}
