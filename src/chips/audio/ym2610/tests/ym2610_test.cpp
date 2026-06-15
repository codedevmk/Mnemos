// ym2610 (OPNB) top-level wrapper: registry round-trip, reset baseline, a
// register write driving the synthesis path into audible output, and a bit-exact
// save/load round-trip (which must restore the FM core plus every sub-chip).

#include "ym2610.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::ym2610;

    // Drive the SSG block through the YM2610 bank-A address/data ports: a tone on
    // channel A at full volume. The SSG mix is the simplest path to a guaranteed
    // non-silent output from a register write.
    void configure_ssg_tone(ym2610& chip) {
        // Bank A $00..$0F is the SSG. Address then data.
        const auto write_a = [&chip](std::uint8_t reg, std::uint8_t value) {
            chip.write_address_a(reg);
            chip.write_data_a(value);
        };
        write_a(0x00, 0x40); // channel A tone period low
        write_a(0x01, 0x00); // channel A tone period high
        write_a(0x07, 0x3E); // mixer: enable channel-A tone (active-low bit0 = 0)
        write_a(0x08, 0x0F); // channel A amplitude = full fixed volume
    }

    // Drive an FM channel (bank A, channel 1) into a sounding state: algorithm 7
    // (four parallel carriers), all four operators loud with a fast attack, a
    // mid-range pitch, then key all four slots on.
    void configure_fm_channel(ym2610& chip) {
        const auto write_a = [&chip](std::uint8_t reg, std::uint8_t value) {
            chip.write_address_a(reg);
            chip.write_data_a(value);
        };
        // Per-operator blocks for channel 1 (reg low nibble 0 selects ch within
        // the pair; slot is bits 3:2). Set DT/MUL, TL=0, fast AR, and a release.
        for (std::uint8_t slot = 0; slot < 4; ++slot) {
            const std::uint8_t base = static_cast<std::uint8_t>(slot << 2U);
            write_a(static_cast<std::uint8_t>(0x30 + base), 0x01); // DT=0, MUL=1
            write_a(static_cast<std::uint8_t>(0x40 + base), 0x00); // TL=0 (loud)
            write_a(static_cast<std::uint8_t>(0x50 + base), 0x1F); // KS=0, AR=31 (instant)
            write_a(static_cast<std::uint8_t>(0x60 + base), 0x00); // D1R=0
            write_a(static_cast<std::uint8_t>(0x70 + base), 0x00); // D2R=0
            write_a(static_cast<std::uint8_t>(0x80 + base), 0x0F); // D1L=0, RR=15
        }
        write_a(0xB0, 0x07); // feedback 0, algorithm 7 (four parallel carriers)
        write_a(0xB4, 0xC0); // L+R enable
        write_a(0xA4, 0x22); // block 4, F-number high bits
        write_a(0xA0, 0x80); // F-number low byte
        write_a(0x28, 0xF0); // key on all four slots of channel 1 (sel=0)
    }

} // namespace

TEST_CASE("ym2610 registers in the chip factory as an audio synth", "[ym2610][audio]") {
    const auto chip = mnemos::chips::create_chip("yamaha.ym2610");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
    REQUIRE(chip->metadata().part_number == "ym2610");
}

TEST_CASE("ym2610 reset clears state to a silent baseline", "[ym2610][audio]") {
    ym2610 chip;
    configure_fm_channel(chip);
    configure_ssg_tone(chip);
    std::array<std::int16_t, 64> warm{};
    chip.generate(warm);

    chip.reset(reset_kind::power_on);

    // After reset nothing is keyed/enabled, so the mix is silent.
    chip.step();
    REQUIRE(chip.last_left() == 0);
    REQUIRE(chip.last_right() == 0);
}

TEST_CASE("ym2610 an SSG register write produces output", "[ym2610][audio]") {
    ym2610 chip;
    // A silent chip with nothing programmed produces no output.
    chip.step();
    REQUIRE(chip.last_left() == 0);
    REQUIRE(chip.last_right() == 0);

    configure_ssg_tone(chip);
    bool any_nonzero = false;
    for (int i = 0; i < 256; ++i) {
        chip.step();
        any_nonzero = any_nonzero || (chip.last_left() != 0) || (chip.last_right() != 0);
    }
    REQUIRE(any_nonzero);
}

TEST_CASE("ym2610 an FM register write produces output", "[ym2610][audio]") {
    ym2610 chip;
    configure_fm_channel(chip);
    bool any_nonzero = false;
    for (int i = 0; i < 512; ++i) {
        chip.step();
        any_nonzero = any_nonzero || (chip.last_left() != 0) || (chip.last_right() != 0);
    }
    REQUIRE(any_nonzero);
}

TEST_CASE("ym2610 save_state/load_state round-trips bit-identically", "[ym2610][audio]") {
    ym2610 a;
    configure_fm_channel(a);
    configure_ssg_tone(a);
    // Advance a few samples so the FM phase/envelope + SSG accumulators are
    // non-trivial.
    std::array<std::int16_t, 80> warm{};
    a.generate(warm);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    ym2610 b;
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    // From the restored state both chips (FM core + every sub-chip) must produce
    // identical output.
    std::array<std::int16_t, 256> from_a{};
    std::array<std::int16_t, 256> from_b{};
    a.generate(from_a);
    b.generate(from_b);
    REQUIRE(from_a == from_b);
}

TEST_CASE("ym2610 audio capture counts and drains in stereo frames", "[ym2610][audio]") {
    ym2610 chip;
    configure_ssg_tone(chip);  // an audible source
    chip.set_clock_divider(1); // one (L,R) frame per ticked cycle
    chip.enable_audio_capture(true);
    constexpr std::size_t frames = 10U;
    chip.tick(frames);

    // pending_samples() is in stereo frames (pairs), matching the player's
    // add_source() contract -- NOT raw int16.
    REQUIRE(chip.pending_samples() == frames);

    std::array<std::int16_t, 16> buf{};
    const std::size_t got = chip.drain_samples(buf.data(), 3U);
    REQUIRE(got == 3U);
    REQUIRE(chip.pending_samples() == frames - 3U);

    std::array<std::int16_t, 64> rest{};
    const std::size_t left = chip.drain_samples(rest.data(), 100U);
    REQUIRE(left == frames - 3U);
    REQUIRE(chip.pending_samples() == 0U);
}
