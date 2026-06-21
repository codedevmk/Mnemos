#include "n163.hpp"

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
    using mnemos::chips::audio::n163;

    [[nodiscard]] std::int16_t peak_of(n163& chip, int ticks) {
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
    [[nodiscard]] std::vector<std::int16_t> capture(n163& chip, std::size_t pairs) {
        chip.enable_audio_capture(true);
        std::vector<std::int16_t> out;
        while (chip.pending_samples() < pairs) {
            chip.tick(1024);
        }
        out.resize(pairs * 2U, 0);
        chip.drain_samples(out.data(), pairs);
        return out;
    }

    // Programme channel 8 (registers $78-$7F) as a single active channel playing a
    // short square wave at full volume, plus a two-sample [15,0,15,0] waveform at the
    // start of RAM.
    void program_tone(n163& chip) {
        chip.write_address(0xF8U); // addr $78, auto-increment
        chip.write_data(0x00U);    // $78 frequency low
        chip.write_data(0x00U);    // $79 phase low
        chip.write_data(0x40U);    // $7A frequency mid  -> freq $4000
        chip.write_data(0x00U);    // $7B phase mid
        chip.write_data(0xFCU);    // $7C freq high 0 + length code -> 4 samples
        chip.write_data(0x00U);    // $7D phase high
        chip.write_data(0x00U);    // $7E wave address 0
        chip.write_data(0x0FU);    // $7F volume 15, channel count 0 -> 1 channel
        chip.write_address(0x80U); // addr $00, auto-increment
        chip.write_data(0x0FU);    // samples 0,1 = 15,0
        chip.write_data(0x0FU);    // samples 2,3 = 15,0
    }

} // namespace

TEST_CASE("n163 registers in the chip factory", "[n163][audio]") {
    const auto chip = mnemos::chips::create_chip("namco.163");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
    CHECK(chip->metadata().part_number == "163");
}

TEST_CASE("n163 is silent with empty wave RAM", "[n163][audio]") {
    n163 chip;
    // Default RAM is zero -> every sample reads 0 -> the centred output is constant
    // and the DC blocker keeps it at silence.
    chip.write_address(0xFFU); // addr $7F: 1 active channel, volume 0
    CHECK(peak_of(chip, 40000) == 0);
}

TEST_CASE("n163 address port auto-increments only when armed", "[n163][audio]") {
    n163 chip;
    chip.write_address(0x80U); // addr $00, auto-increment on
    chip.write_data(0xABU);
    chip.write_data(0xCDU);
    CHECK(chip.ram(0) == 0xABU);
    CHECK(chip.ram(1) == 0xCDU);

    chip.write_address(0x00U); // addr $00, auto-increment off
    CHECK(chip.read_data() == 0xABU);
    CHECK(chip.read_data() == 0xABU); // pointer held
}

TEST_CASE("n163 reports the active channel count from $7F", "[n163][audio]") {
    n163 chip;
    chip.write_address(0x7FU);
    chip.write_data(0x40U); // bits 6-4 = 4 -> 5 channels
    CHECK(chip.active_channels() == 5U);
}

TEST_CASE("n163 plays a wavetable tone", "[n163][audio]") {
    n163 chip;
    program_tone(chip);
    CHECK(chip.active_channels() == 1U);
    CHECK(peak_of(chip, 40000) > 1000);
}

TEST_CASE("n163 mute silences the output", "[n163][audio]") {
    n163 chip;
    program_tone(chip);
    chip.set_enabled(false); // $E000 bit 6: sound circuit off
    CHECK(peak_of(chip, 40000) == 0);
}

TEST_CASE("n163 save_state/load_state round-trips the sound RAM", "[n163][audio]") {
    n163 a;
    program_tone(a);
    a.tick(5000);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    n163 b;
    CHECK(b.ram(0x7FU) != a.ram(0x7FU));
    state_reader reader(blob);
    b.load_state(reader);
    CHECK(reader.ok());
    CHECK(b.ram(0x7FU) == a.ram(0x7FU));
    CHECK(b.ram(0x7AU) == a.ram(0x7AU));
    CHECK(b.active_channels() == a.active_channels());
}

// A restored chip must produce a bit-identical sample stream: every field that
// shapes future output (incl. the DC-blocker IIR state) is serialized.
TEST_CASE("n163 restore is sample-exact (determinism)", "[n163][audio]") {
    n163 a;
    program_tone(a);
    a.tick(5000); // warm the DC blocker + oscillator phase to a non-trivial state

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    constexpr std::size_t k_pairs = 4096;
    const std::vector<std::int16_t> from_a = capture(a, k_pairs);

    n163 b;
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    const std::vector<std::int16_t> from_b = capture(b, k_pairs);

    CHECK(from_a == from_b);
}
