// ssg port fidelity. The Emu reference (chips/ssg) ships only the register
// surface + decode; the tone divider, 17-bit noise LFSR, and envelope shape
// machine are implemented here directly from the AY-3-8910 / YM2149 datasheet,
// so the golden block below is derived by tracing that datasheet-defined math.

#include "ssg.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::create_chip;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::ssg;

    // Configure a single audible tone on channel A: period 2, full fixed volume,
    // mixer enabling tone A only (noise + B/C silenced). With period 2 the square
    // output runs 1,1,1,0,0,0 (three steps high, three low) per cycle.
    void configure_tone_a(ssg& chip) {
        chip.write_reg(ssg::reg_a_freq_lo, 0x02);
        chip.write_reg(ssg::reg_a_freq_hi, 0x00); // tone period = 2
        chip.write_reg(ssg::reg_a_level, 0x0F);   // fixed volume 15, M = 0
        // Mixer active-low: clear bit 0 to enable tone A; leave noise (bits 3..5)
        // and tone B/C (bits 1..2) set = disabled.
        chip.write_reg(ssg::reg_mixer, 0xFE);
    }

} // namespace

TEST_CASE("ssg registers in the chip registry as an audio synth", "[ssg][audio]") {
    auto chip = create_chip("yamaha.ssg");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
}

TEST_CASE("ssg reset clears to a defined baseline", "[ssg][audio]") {
    ssg chip;
    // Power-on mixer is $FF (all paths inhibited); decoded period/volume are zero.
    REQUIRE(chip.read_reg(ssg::reg_mixer) == 0xFF);
    REQUIRE(chip.tone_period(0) == 0);
    REQUIRE(chip.tone_period(1) == 0);
    REQUIRE(chip.tone_period(2) == 0);
    REQUIRE(chip.volume(0) == 0);
    REQUIRE(chip.envelope_period() == 0);
    REQUIRE(chip.envelope_shape() == 0);
    REQUIRE(chip.noise_period() == 0);
    REQUIRE(chip.noise_lfsr() == 1U);

    // Dirty the chip, then reset and re-check the baseline.
    configure_tone_a(chip);
    std::array<std::int16_t, 32> warm{};
    chip.generate(warm);
    chip.reset(reset_kind::hard);
    REQUIRE(chip.read_reg(ssg::reg_mixer) == 0xFF);
    REQUIRE(chip.tone_period(0) == 0);
    REQUIRE(chip.noise_lfsr() == 1U);
    REQUIRE(chip.last_left() == 0);
    REQUIRE(chip.last_right() == 0);
}

TEST_CASE("ssg register writes decode into channel/global state", "[ssg][audio]") {
    ssg chip;
    chip.write_reg(ssg::reg_a_freq_lo, 0x34);
    chip.write_reg(ssg::reg_a_freq_hi, 0x0F); // only the low nibble is significant
    REQUIRE(chip.tone_period(0) == 0x0F34);
    REQUIRE(chip.read_reg(ssg::reg_a_freq_hi) == 0x0F);

    chip.write_reg(ssg::reg_noise, 0xFF);
    REQUIRE(chip.noise_period() == 0x1F); // 5-bit field

    chip.write_reg(ssg::reg_env_lo, 0xCD);
    chip.write_reg(ssg::reg_env_hi, 0xAB);
    REQUIRE(chip.envelope_period() == 0xABCD);

    chip.write_reg(ssg::reg_env_shape, 0xF4);
    REQUIRE(chip.envelope_shape() == 0x04); // low nibble

    // The ADDRESS/DATA port path latches the register, then reads it back.
    chip.address(ssg::reg_b_freq_lo);
    REQUIRE(chip.selected_register() == ssg::reg_b_freq_lo);
    chip.write(0x77);
    REQUIRE(chip.read() == 0x77);
    REQUIRE(chip.tone_period(1) == 0x0077);
}

TEST_CASE("ssg generates the datasheet-traced tone waveform bit-exact", "[ssg][audio]") {
    // vol_table[15] = 9100 (peak in ssg.cpp). With tone period 2 the square output
    // is high for steps 1..3, low for 4..6, repeating; only channel A contributes.
    constexpr std::int16_t hi = 9100;
    constexpr std::array<std::int16_t, 24> kGolden = {
        hi, hi, hi, hi, hi, hi, // step 1 (L,R)..step 3 high
        0,  0,  0,  0,  0,  0,  // steps 4..6 low
        hi, hi, hi, hi, hi, hi, // steps 7..9 high
        0,  0,  0,  0,  0,  0}; // steps 10..12 low

    ssg chip;
    configure_tone_a(chip);
    std::array<std::int16_t, 24> buf{};
    chip.generate(buf);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        INFO("sample " << i << (i % 2 == 0 ? " (L)" : " (R)"));
        REQUIRE(buf[i] == kGolden[i]);
    }
    // A mono chip mirrors the mix onto both lanes.
    REQUIRE(chip.last_left() == chip.last_right());
}

TEST_CASE("ssg save_state/load_state round-trips bit-identically", "[ssg][audio]") {
    ssg a;
    configure_tone_a(a);
    // Arm the noise LFSR + envelope as well so all runtime state is non-trivial.
    a.write_reg(ssg::reg_noise, 0x03);
    a.write_reg(ssg::reg_env_lo, 0x04);
    a.write_reg(ssg::reg_env_shape, 0x0E); // CONT|ALT|HOLD triangle-ish ramp
    a.write_reg(ssg::reg_b_level, ssg::level_env_mode);
    a.write_reg(ssg::reg_mixer, 0xC0); // enable tone+noise A/B/C, ports as outputs
    std::array<std::int16_t, 40> warm{};
    a.generate(warm);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    ssg b;
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    // From the restored state both chips must produce identical output.
    std::array<std::int16_t, 128> from_a{};
    std::array<std::int16_t, 128> from_b{};
    a.generate(from_a);
    b.generate(from_b);
    REQUIRE(from_a == from_b);
}

TEST_CASE("ssg audio capture counts and drains in stereo frames", "[ssg][audio]") {
    ssg chip;
    configure_tone_a(chip);    // an audible voice
    chip.set_clock_divider(1); // one native step per cycle for a clean count
    chip.enable_audio_capture(true);
    constexpr std::size_t frames = 10U;
    chip.tick(frames);

    // pending_samples() is in stereo frames (pairs) -- matching rf5c68 + the
    // player's add_source() contract -- NOT raw int16.
    REQUIRE(chip.pending_samples() == frames);

    std::array<std::int16_t, 16> buf{};
    const std::size_t got = chip.drain_samples(buf.data(), 3U);
    REQUIRE(got == 3U);
    REQUIRE(chip.pending_samples() == frames - 3U);
    bool any_nonzero = false; // an active voice must produce non-silent capture
    for (std::size_t i = 0; i < got * 2U; ++i) {
        any_nonzero = any_nonzero || (buf[i] != 0);
    }
    REQUIRE(any_nonzero);

    std::array<std::int16_t, 64> rest{};
    const std::size_t left = chip.drain_samples(rest.data(), 100U);
    REQUIRE(left == frames - 3U);
    REQUIRE(chip.pending_samples() == 0U);
}
