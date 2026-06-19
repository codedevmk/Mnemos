// 1-bit beeper tests: a toggled speaker yields a swinging (AC) signal, a held
// level yields a constant (DC) one, and the timing produces ~output_rate samples
// per second. Ticked one CPU cycle at a time (the per-cycle scheduler contract).

#include "beeper.hpp"

#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::audio::beeper;

    std::vector<std::int16_t> drain(beeper& b) {
        std::vector<std::int16_t> out(b.pending_samples());
        if (!out.empty()) {
            b.drain_samples(out.data(), out.size());
        }
        return out;
    }
} // namespace

TEST_CASE("beeper: a toggled speaker produces a swinging signal", "[chips][audio][beeper]") {
    beeper b;
    b.enable_audio_capture(true);
    // ~1 kHz square: flip every cpu_clock / (2 * 1000) cycles, for ~0.1 s.
    constexpr int half_period = beeper::default_cpu_clock_hz / (2 * 1000);
    for (int half = 0; half < 200; ++half) {
        b.set_speaker((half & 1) != 0);
        for (int t = 0; t < half_period; ++t) {
            b.tick(1);
        }
    }
    const auto buf = drain(b);
    REQUIRE(buf.size() > 1000U); // ~4800 samples for 0.1 s at 48 kHz
    std::int16_t mn = 32767;
    std::int16_t mx = -32768;
    for (const std::int16_t s : buf) {
        mn = std::min(mn, s);
        mx = std::max(mx, s);
    }
    CHECK(mn < -1000); // the signal swings both ways (it is real audio)
    CHECK(mx > 1000);
}

TEST_CASE("beeper: a held level is constant (DC)", "[chips][audio][beeper]") {
    beeper b;
    b.enable_audio_capture(true);
    b.set_speaker(true);
    for (int t = 0; t < 350'000; ++t) { // ~0.1 s held high
        b.tick(1);
    }
    const auto buf = drain(b);
    REQUIRE(!buf.empty());
    for (const std::int16_t s : buf) {
        CHECK(s == beeper::default_amplitude); // all-high window -> +amplitude
    }
}

TEST_CASE("beeper: nothing is captured while capture is off", "[chips][audio][beeper]") {
    beeper b;
    b.set_speaker(true);
    for (int t = 0; t < 100'000; ++t) {
        b.tick(1);
    }
    CHECK(b.pending_samples() == 0U);
}

TEST_CASE("beeper: save/load round-trips the speaker level", "[chips][audio][beeper]") {
    beeper b;
    b.set_speaker(true);
    b.enable_audio_capture(true);
    b.tick(100);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    b.save_state(writer);

    beeper restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    // The level survives; continuing to capture from the same level yields +amp.
    restored.enable_audio_capture(true);
    for (int t = 0; t < 1000; ++t) {
        restored.tick(1);
    }
    const auto buf = drain(restored);
    REQUIRE(!buf.empty());
    CHECK(buf.front() == beeper::default_amplitude);
}
