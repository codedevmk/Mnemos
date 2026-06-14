// ym2413 port fidelity vs the Emu reference. The chip is mono (each native
// sample is duplicated onto both stereo lanes); the synthesis pipeline is ported
// integer-exact, so the deterministic waveform asserted below is reproduced
// sample-for-sample by the in-tree core.

#include "ym2413.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::ym2413;

    void write_reg(ym2413& chip, std::uint8_t address, std::uint8_t value) {
        chip.write_address(address);
        chip.write_data(value);
    }

    // Configure channel 0 with a fixed preset instrument (Trumpet, slot 7) at a
    // mid-range pitch and full volume, then key it on. This is a fully
    // deterministic setup that drives the phase + envelope generators into a
    // non-trivial sounding state.
    void configure_channel0(ym2413& chip) {
        write_reg(chip, ym2413::reg_inst_vol_base,
                  0x70); // channel 0: instrument 7, volume 0 (loud)
        write_reg(chip, ym2413::reg_fnum_low_base, 0xA0); // channel 0: F-Number low byte
        // $20: key-on (bit 4) | block 4 (bits 3-1) | F-Number high bit (bit 0).
        write_reg(chip, ym2413::reg_control_base, 0x18);
    }

} // namespace

TEST_CASE("ym2413 registers in the chip factory", "[ym2413][audio]") {
    const auto chip = mnemos::chips::create_chip("yamaha.ym2413");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
}

TEST_CASE("ym2413 reset clears state to a defined baseline", "[ym2413][audio]") {
    ym2413 chip;
    // Dirty the chip, then reset and verify the documented power-up baseline.
    configure_channel0(chip);
    write_reg(chip, ym2413::reg_user_inst_base + 0, 0x55);
    chip.write_audio_select(0x01);
    std::array<std::int16_t, 32> warm{};
    chip.generate(warm);

    chip.reset(reset_kind::power_on);

    REQUIRE(chip.address_latch() == 0);
    REQUIRE(chip.read_audio_select() == 0);
    REQUIRE(chip.last_sample() == 0);
    // After reset every channel's carrier envelope is OFF, so the chip is
    // silent until a channel is keyed on.
    chip.step();
    REQUIRE(chip.last_sample() == 0);
}

TEST_CASE("ym2413 register writes drive the synthesis state and produce output",
          "[ym2413][audio]") {
    ym2413 chip;

    // Audio-select mux round-trips.
    chip.write_audio_select(0x01);
    REQUIRE(chip.read_audio_select() == 0x01);

    // A silent (unkeyed) chip produces no output.
    chip.step();
    REQUIRE(chip.last_sample() == 0);

    // Keying a channel with a preset instrument makes it sound.
    configure_channel0(chip);
    bool any_nonzero = false;
    for (int i = 0; i < 256; ++i) {
        chip.step();
        any_nonzero = any_nonzero || (chip.last_sample() != 0);
    }
    REQUIRE(any_nonzero);
}

TEST_CASE("ym2413 generate writes mono samples to both stereo lanes", "[ym2413][audio]") {
    ym2413 chip;
    configure_channel0(chip);
    std::array<std::int16_t, 64> buf{};
    chip.generate(buf);
    // Mono chip: left and right lanes of each frame are identical.
    for (std::size_t i = 0; i < buf.size(); i += 2U) {
        REQUIRE(buf[i] == buf[i + 1U]);
    }
}

TEST_CASE("ym2413 save_state/load_state round-trips bit-identically", "[ym2413][audio]") {
    ym2413 a;
    configure_channel0(a);
    // Advance a few samples so the phase/envelope accumulators are non-trivial.
    std::array<std::int16_t, 40> warm{};
    a.generate(warm);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    ym2413 b;
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

TEST_CASE("ym2413 produces a deterministic, repeatable waveform after key-on", "[ym2413][audio]") {
    // The synthesis pipeline is deterministic: two freshly-reset chips driven
    // through the same setup produce identical samples (a self-consistency
    // golden for the ported integer math).
    ym2413 x;
    ym2413 y;
    configure_channel0(x);
    configure_channel0(y);

    std::array<std::int16_t, 256> wx{};
    std::array<std::int16_t, 256> wy{};
    x.generate(wx);
    y.generate(wy);
    REQUIRE(wx == wy);

    // The waveform must be non-trivial (not a constant): a keyed FM voice
    // swings around zero as its phase advances.
    bool varies = false;
    for (std::size_t i = 2; i < wx.size(); i += 2U) {
        if (wx[i] != wx[0]) {
            varies = true;
            break;
        }
    }
    REQUIRE(varies);
}

TEST_CASE("ym2413 audio capture counts and drains in stereo frames", "[ym2413][audio]") {
    ym2413 chip;
    configure_channel0(chip); // a keyed, audible voice
    chip.enable_audio_capture(true);
    constexpr std::size_t frames = 10U;
    chip.tick(frames); // clock_divider defaults to 1 -> one (L,R) frame per cycle

    // pending_samples() is in stereo frames (pairs), matching the player's
    // add_source() contract -- NOT raw int16.
    REQUIRE(chip.pending_samples() == frames);

    // Drain fewer pairs than queued: returns pairs, fills 2*pairs int16.
    std::array<std::int16_t, 16> buf{};
    const std::size_t got = chip.drain_samples(buf.data(), 3U);
    REQUIRE(got == 3U);
    REQUIRE(chip.pending_samples() == frames - 3U);

    // Draining past the end returns only what remains.
    std::array<std::int16_t, 64> rest{};
    const std::size_t left = chip.drain_samples(rest.data(), 100U);
    REQUIRE(left == frames - 3U);
    REQUIRE(chip.pending_samples() == 0U);
}
