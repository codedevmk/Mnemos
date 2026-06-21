#include "vrc6.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::vrc6;

    [[nodiscard]] std::int16_t peak_of(vrc6& chip, int ticks) {
        chip.enable_audio_capture(true);
        chip.tick(static_cast<std::uint64_t>(ticks));
        std::vector<std::int16_t> buf(chip.pending_samples() * 2U, 0);
        const std::size_t pairs = chip.drain_samples(buf.data(), chip.pending_samples());
        if (pairs == 0U) {
            return 0;
        }
        std::int16_t peak = 0;
        for (const std::int16_t s : buf) {
            peak = std::max(peak, static_cast<std::int16_t>(std::abs(s)));
        }
        return peak;
    }

    // Capture exactly `pairs` stereo frames of further output from a running chip.
    [[nodiscard]] std::vector<std::int16_t> capture(vrc6& chip, std::size_t pairs) {
        chip.enable_audio_capture(true);
        std::vector<std::int16_t> out;
        while (chip.pending_samples() < pairs) {
            chip.tick(1024);
        }
        out.resize(pairs * 2U, 0);
        chip.drain_samples(out.data(), pairs);
        return out;
    }

} // namespace

TEST_CASE("vrc6 registers in the chip factory", "[vrc6][audio]") {
    const auto chip = mnemos::chips::create_chip("konami.vrc6");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
    CHECK(chip->metadata().part_number == "VRC6");
}

TEST_CASE("vrc6 is silent until a channel is enabled", "[vrc6][audio]") {
    vrc6 chip;
    CHECK(peak_of(chip, 20000) == 0);
}

TEST_CASE("vrc6 pulse channel decodes registers and produces a tone", "[vrc6][audio]") {
    vrc6 chip;
    // Pulse 1: 50% duty, full volume, a mid period, enabled.
    chip.write_reg(0x9000U, 0x7FU); // duty 7 (50%), volume 15
    chip.write_reg(0x9001U, 0x40U); // period low
    chip.write_reg(0x9002U, 0x81U); // enable (bit 7) + period high -> $140
    CHECK(chip.pulse_period(0) == 0x140U);
    CHECK(chip.pulse_volume(0) == 15U);
    CHECK(peak_of(chip, 40000) > 1000); // a square wave, not silence
}

TEST_CASE("vrc6 sawtooth produces a rising ramp", "[vrc6][audio]") {
    vrc6 chip;
    chip.write_reg(0xB000U, 0x20U); // accumulator rate 32
    chip.write_reg(0xB001U, 0x40U); // period low
    chip.write_reg(0xB002U, 0x81U); // enable + period high
    CHECK(chip.saw_period() == 0x140U);
    CHECK(peak_of(chip, 40000) > 1000);
}

TEST_CASE("vrc6 halt silences the channels", "[vrc6][audio]") {
    vrc6 chip;
    chip.write_reg(0x9000U, 0x7FU); // a configured + enabled pulse
    chip.write_reg(0x9001U, 0x40U);
    chip.write_reg(0x9002U, 0x81U);
    chip.write_reg(0x9003U, 0x01U); // halt all channels before they run
    // Halted channels never clock, so the DAC stays flat and the DC blocker keeps
    // the output silent.
    CHECK(peak_of(chip, 40000) == 0);
}

TEST_CASE("vrc6 save_state/load_state round-trips the channels", "[vrc6][audio]") {
    vrc6 a;
    a.write_reg(0x9000U, 0x7FU);
    a.write_reg(0x9001U, 0x40U);
    a.write_reg(0x9002U, 0x81U);
    a.write_reg(0xB000U, 0x20U);
    a.tick(5000);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    vrc6 b;
    CHECK(b.pulse_period(0) != a.pulse_period(0));
    state_reader reader(blob);
    b.load_state(reader);
    CHECK(reader.ok());
    CHECK(b.pulse_period(0) == a.pulse_period(0));
    CHECK(b.pulse_volume(0) == a.pulse_volume(0));
}

// A restored chip must produce a bit-identical sample stream: every field that
// shapes future output (incl. the DC-blocker IIR state) is serialized.
TEST_CASE("vrc6 restore is sample-exact (determinism)", "[vrc6][audio]") {
    vrc6 a;
    a.write_reg(0x9000U, 0x7FU);
    a.write_reg(0x9001U, 0x40U);
    a.write_reg(0x9002U, 0x81U);
    a.write_reg(0xB000U, 0x20U);
    a.write_reg(0xB001U, 0x40U);
    a.write_reg(0xB002U, 0x81U);
    a.tick(5000); // warm the DC blocker + oscillator phase

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    constexpr std::size_t k_pairs = 4096;
    const std::vector<std::int16_t> from_a = capture(a, k_pairs);

    vrc6 b;
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    const std::vector<std::int16_t> from_b = capture(b, k_pairs);

    CHECK(from_a == from_b);
}
