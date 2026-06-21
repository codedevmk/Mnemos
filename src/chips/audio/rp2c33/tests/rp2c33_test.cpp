#include "rp2c33.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::rp2c33;

    // Program a square-ish waveform, full direct volume, and a mid-range pitch, so
    // the channel produces a non-trivial signal.
    void configure_tone(rp2c33& chip) {
        chip.write_reg(rp2c33::reg_master, 0x80U); // enable wave-RAM writes (master full)
        for (std::uint16_t i = 0; i < rp2c33::wave_size; ++i) {
            const std::uint8_t s = (i < 32U) ? 0x3FU : 0x00U; // high half / low half
            chip.write_reg(static_cast<std::uint16_t>(rp2c33::reg_wave_base + i), s);
        }
        chip.write_reg(rp2c33::reg_master, 0x00U);  // lock the wave RAM, master full
        chip.write_reg(rp2c33::reg_vol_env, 0xA0U); // direct volume, gain 32
        chip.write_reg(rp2c33::reg_freq_lo, 0xFFU); // frequency low
        chip.write_reg(rp2c33::reg_freq_hi, 0x04U); // frequency high -> $4FF, wave not halted
    }

} // namespace

TEST_CASE("rp2c33 registers in the chip factory", "[rp2c33][audio]") {
    const auto chip = mnemos::chips::create_chip("ricoh.rp2c33");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
    CHECK(chip->metadata().part_number == "RP2C33");
}

TEST_CASE("rp2c33 is silent after reset", "[rp2c33][audio]") {
    rp2c33 chip;
    chip.enable_audio_capture(true);
    chip.tick(10000);
    std::vector<std::int16_t> buf(chip.pending_samples() * 2U, 1);
    const std::size_t pairs = chip.drain_samples(buf.data(), chip.pending_samples());
    REQUIRE(pairs > 0U);
    std::int16_t peak = 0;
    for (const std::int16_t s : buf) {
        peak = std::max(peak, static_cast<std::int16_t>(std::abs(s)));
    }
    CHECK(peak == 0); // no waveform programmed -> silence
}

TEST_CASE("rp2c33 decodes the wave frequency and produces audio when keyed", "[rp2c33][audio]") {
    rp2c33 chip;
    chip.enable_audio_capture(true);
    configure_tone(chip);
    CHECK(chip.wave_frequency() == 0x04FFU);
    CHECK(chip.volume_gain() == 32U);

    chip.tick(40000); // advance the wave accumulator across the table several times
    std::vector<std::int16_t> buf(chip.pending_samples() * 2U, 0);
    const std::size_t pairs = chip.drain_samples(buf.data(), chip.pending_samples());
    REQUIRE(pairs > 0U);
    std::int16_t peak = 0;
    for (const std::int16_t s : buf) {
        peak = std::max(peak, static_cast<std::int16_t>(std::abs(s)));
    }
    CHECK(peak > 1000); // the square wave at full volume, not silence
}

TEST_CASE("rp2c33 save_state/load_state round-trips the channel", "[rp2c33][audio]") {
    rp2c33 a;
    configure_tone(a);
    a.tick(5000);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    rp2c33 b;
    CHECK(b.wave_frequency() != a.wave_frequency()); // fresh chip differs
    state_reader reader(blob);
    b.load_state(reader);
    CHECK(reader.ok());
    CHECK(b.wave_frequency() == a.wave_frequency());
    CHECK(b.volume_gain() == a.volume_gain());
    CHECK(b.wave_sample(0) == a.wave_sample(0));
    CHECK(b.wave_sample(40) == a.wave_sample(40));
}
