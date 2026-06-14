// adpcm_b decode fidelity. The golden PCM block is the deterministic output of
// the integer ADPCM-B delta machine (datasheet step/quantiser tables) run
// through the identical scenario built in configure_voice() below.

#include "adpcm_b.hpp"

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
    using mnemos::chips::audio::adpcm_b;

    // A short packed-nibble waveform at byte 0x10..0x13 (each byte = two 4-bit
    // ADPCM nibbles, high nibble first), END at 0x13, full L/R pan, TL=0, and
    // unity DELTA-N so exactly one nibble decodes per native step. The chip is
    // keyed via CTRL.START.
    void configure_voice(adpcm_b& chip) {
        constexpr std::array<std::uint8_t, 4> wave = {0x12, 0x34, 0x56, 0x78};
        const auto rom = chip.sample_rom();
        for (std::size_t i = 0; i < wave.size(); ++i) {
            rom[0x10 + i] = wave[i];
        }
        chip.write_reg(adpcm_b::reg_start_lo, 0x10);
        chip.write_reg(adpcm_b::reg_start_hi, 0x00); // start = 0x0010
        chip.write_reg(adpcm_b::reg_end_lo, 0x13);
        chip.write_reg(adpcm_b::reg_end_hi, 0x00); // end = 0x0013
        chip.write_reg(adpcm_b::reg_delta_lo, 0x00);
        chip.write_reg(adpcm_b::reg_delta_hi, 0x01); // DELTA-N = 0x0100 -> unity
        chip.write_reg(adpcm_b::reg_pan_tl,
                       adpcm_b::pan_left | adpcm_b::pan_right); // full pan, TL=0
        chip.write_reg(adpcm_b::reg_ctrl, adpcm_b::ctrl_start); // key on
    }

    // Bit-exact stereo (L,R) output for the scenario above, 8 frames.
    constexpr std::array<std::int16_t, 16> kPcmGolden = {
        46, 46, 124, 124, 233, 233, 373, 373, 578, 578, 965, 965, 1858, 1858, 1716, 1716};

} // namespace

TEST_CASE("adpcm_b registers in the chip registry as an audio synth", "[adpcm_b][audio]") {
    auto chip = create_chip("yamaha.adpcm_b");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
}

TEST_CASE("adpcm_b decodes the ADPCM-B delta stream bit-exactly", "[adpcm_b][audio]") {
    adpcm_b chip;
    configure_voice(chip);
    std::array<std::int16_t, 16> buf{};
    chip.generate(buf);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        INFO("sample " << i << (i % 2 == 0 ? " (L)" : " (R)"));
        REQUIRE(buf[i] == kPcmGolden[i]);
    }
}

TEST_CASE("adpcm_b reset clears state to a defined baseline", "[adpcm_b][audio]") {
    adpcm_b chip;
    configure_voice(chip);
    // Advance so runtime state is non-trivial, then a soft reset must silence it.
    std::array<std::int16_t, 8> warm{};
    chip.generate(warm);
    chip.reset(reset_kind::soft);

    REQUIRE(chip.read_reg(adpcm_b::reg_ctrl) == 0x00);
    REQUIRE(chip.read_reg(adpcm_b::reg_status) == 0x00);
    chip.step();
    REQUIRE(chip.last_left() == 0);
    REQUIRE(chip.last_right() == 0);
}

TEST_CASE("adpcm_b register writes read back and CTRL.START keys the voice", "[adpcm_b][audio]") {
    adpcm_b chip;
    chip.write_reg(adpcm_b::reg_pan_tl, 0xC5); // L+R pan, TL=5
    REQUIRE(chip.read_reg(adpcm_b::reg_pan_tl) == 0xC5);
    chip.write_reg(adpcm_b::reg_delta_lo, 0x34);
    chip.write_reg(adpcm_b::reg_delta_hi, 0x12);
    REQUIRE(chip.read_reg(adpcm_b::reg_delta_lo) == 0x34);
    REQUIRE(chip.read_reg(adpcm_b::reg_delta_hi) == 0x12);
    // STATUS ($0B) is read-only -- a write is dropped, reads return the flag byte.
    chip.write_reg(adpcm_b::reg_status, 0xFF);
    REQUIRE(chip.read_reg(adpcm_b::reg_status) == 0x00);
    // Reserved index returns open bus.
    REQUIRE(chip.read_reg(0x0F) == 0xFF);

    // A keyed, audible voice produces non-zero output after CTRL.START.
    configure_voice(chip);
    chip.step();
    REQUIRE(chip.last_left() != 0);
    REQUIRE(chip.last_right() != 0);

    // CTRL.RESET stops the voice and silences output.
    chip.write_reg(adpcm_b::reg_ctrl, adpcm_b::ctrl_reset);
    chip.step();
    REQUIRE(chip.last_left() == 0);
    REQUIRE(chip.last_right() == 0);
}

TEST_CASE("adpcm_b save_state/load_state round-trips bit-identically", "[adpcm_b][audio]") {
    adpcm_b a;
    configure_voice(a);
    // Advance a few samples so the decoder accumulator/cursor are non-trivial.
    std::array<std::int16_t, 6> warm{};
    a.generate(warm);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    adpcm_b b;
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    // From the restored state both chips must produce identical output.
    std::array<std::int16_t, 32> from_a{};
    std::array<std::int16_t, 32> from_b{};
    a.generate(from_a);
    b.generate(from_b);
    REQUIRE(from_a == from_b);
}

TEST_CASE("adpcm_b stops at END and latches EOS without repeat", "[adpcm_b][audio]") {
    adpcm_b chip;
    configure_voice(chip); // 4 bytes = 8 nibbles, repeat OFF
    std::array<std::int16_t, 32> buf{};
    chip.generate(buf); // far more steps than the stream has nibbles

    // EOS latched and BUSY cleared once the stream ran past END.
    REQUIRE((chip.read_reg(adpcm_b::reg_status) & adpcm_b::status_eos) != 0);
    REQUIRE((chip.read_reg(adpcm_b::reg_status) & adpcm_b::status_busy) == 0);
    // After the voice stopped, output settles to silence (the voice is inactive).
    chip.step();
    REQUIRE(chip.last_left() == 0);
    REQUIRE(chip.last_right() == 0);
}

TEST_CASE("adpcm_b audio capture counts and drains in stereo frames", "[adpcm_b][audio]") {
    adpcm_b chip;
    configure_voice(chip); // an enabled, audible voice
    chip.enable_audio_capture(true);
    constexpr std::size_t frames = 6U;
    chip.tick(frames); // clock_divider defaults to 1 -> one (L,R) frame per cycle

    // pending_samples() is in stereo frames (pairs), matching the player's
    // add_source() contract -- NOT raw int16.
    REQUIRE(chip.pending_samples() == frames);

    // Drain fewer pairs than queued: returns pairs, fills 2*pairs int16.
    std::array<std::int16_t, 16> buf{};
    const std::size_t got = chip.drain_samples(buf.data(), 2U);
    REQUIRE(got == 2U);
    REQUIRE(chip.pending_samples() == frames - 2U);
    bool any_nonzero = false; // an active voice must produce non-silent capture
    for (std::size_t i = 0; i < got * 2U; ++i) {
        any_nonzero = any_nonzero || (buf[i] != 0);
    }
    REQUIRE(any_nonzero);

    // Draining past the end returns only what remains.
    std::array<std::int16_t, 64> rest{};
    const std::size_t left = chip.drain_samples(rest.data(), 100U);
    REQUIRE(left == frames - 2U);
    REQUIRE(chip.pending_samples() == 0U);
}
