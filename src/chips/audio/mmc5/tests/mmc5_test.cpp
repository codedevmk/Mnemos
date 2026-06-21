#include "mmc5.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::mmc5;

    [[nodiscard]] std::int16_t peak_of(mmc5& chip, int ticks) {
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
    [[nodiscard]] std::vector<std::int16_t> capture(mmc5& chip, std::size_t pairs) {
        chip.enable_audio_capture(true);
        std::vector<std::int16_t> out;
        while (chip.pending_samples() < pairs) {
            chip.tick(1024);
        }
        out.resize(pairs * 2U, 0);
        chip.drain_samples(out.data(), pairs);
        return out;
    }

    // Enable + programme pulse 1: 50% duty, full constant volume, a mid period.
    void program_pulse1(mmc5& chip) {
        chip.write_reg(0x5015U, 0x01U); // enable pulse 1
        chip.write_reg(0x5000U, 0xBFU); // duty 50% (10), constant vol, volume 15
        chip.write_reg(0x5002U, 0x40U); // timer low
        chip.write_reg(0x5003U, 0x08U); // timer high -> $040, length load
    }

} // namespace

TEST_CASE("mmc5 registers in the chip factory", "[mmc5][audio]") {
    const auto chip = mnemos::chips::create_chip("nintendo.mmc5");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
    CHECK(chip->metadata().part_number == "MMC5");
}

TEST_CASE("mmc5 is silent until a channel is enabled", "[mmc5][audio]") {
    mmc5 chip;
    CHECK(peak_of(chip, 40000) == 0);
}

TEST_CASE("mmc5 pulse channel decodes registers and produces a tone", "[mmc5][audio]") {
    mmc5 chip;
    program_pulse1(chip);
    CHECK(chip.pulse_timer(0) == 0x040U);
    CHECK(chip.pulse_length(0) > 0U);
    CHECK(peak_of(chip, 40000) > 1000);
}

TEST_CASE("mmc5 length counter silences the pulse when it runs out", "[mmc5][audio]") {
    mmc5 chip;
    // Length-load index 2 ($08>>3 = 1 -> table[1] = 254)... use the shortest entry:
    // index 3 -> table[3] = 2. With env/length running (not halted) it ticks down.
    chip.write_reg(0x5015U, 0x01U);
    chip.write_reg(0x5000U, 0x1FU); // duty 0, NOT halted, constant vol 15
    chip.write_reg(0x5002U, 0x40U);
    chip.write_reg(0x5003U, 0x18U); // length index 3 -> count 2 (very short)
    CHECK(chip.pulse_length(0) == 2U);
    // Run well past 2 frame ticks (2*7457 cyc) so the length counter reaches 0.
    chip.tick(40000);
    CHECK(chip.pulse_length(0) == 0U);
}

TEST_CASE("mmc5 raw PCM DAC produces output", "[mmc5][audio]") {
    mmc5 chip;
    chip.write_reg(0x5011U, 0xF0U); // a strong DAC level
    CHECK(chip.pcm_level() == 0xF0U);
    chip.write_reg(0x5011U, 0x00U);   // $00 is the IRQ sentinel, not a level
    CHECK(chip.pcm_level() == 0xF0U); // held
    CHECK(peak_of(chip, 8000) > 0);
}

TEST_CASE("mmc5 status reports the running length counters", "[mmc5][audio]") {
    mmc5 chip;
    CHECK(chip.read_status() == 0x00U);
    program_pulse1(chip);
    CHECK((chip.read_status() & 0x01U) != 0U);
}

TEST_CASE("mmc5 save_state/load_state round-trips the channels", "[mmc5][audio]") {
    mmc5 a;
    program_pulse1(a);
    a.write_reg(0x5011U, 0x80U);
    a.tick(5000);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    mmc5 b;
    CHECK(b.pulse_timer(0) != a.pulse_timer(0));
    state_reader reader(blob);
    b.load_state(reader);
    CHECK(reader.ok());
    CHECK(b.pulse_timer(0) == a.pulse_timer(0));
    CHECK(b.pcm_level() == a.pcm_level());
}

// A restored chip must produce a bit-identical sample stream: every field that
// shapes future output (incl. the DC-blocker IIR state) is serialized.
TEST_CASE("mmc5 restore is sample-exact (determinism)", "[mmc5][audio]") {
    mmc5 a;
    program_pulse1(a);
    a.write_reg(0x5011U, 0x80U);
    a.tick(5000); // warm the DC blocker + oscillator/frame state

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    constexpr std::size_t k_pairs = 4096;
    const std::vector<std::int16_t> from_a = capture(a, k_pairs);

    mmc5 b;
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    const std::vector<std::int16_t> from_b = capture(b, k_pairs);

    CHECK(from_a == from_b);
}
