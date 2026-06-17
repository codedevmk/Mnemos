// s_dsp port fidelity. The register surface + KON/KOFF latch are ported verbatim
// from the Emu reference; the synthesis core (BRR decode, envelope, voice mix) is
// the datasheet-documented next stage, exercised here with a deterministic golden
// derived by hand from a filter-0 BRR block (see configure_voice0()).

#include "s_dsp.hpp"

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
    using mnemos::chips::audio::s_dsp;

    // Lay a single filter-0 BRR block in audio RAM and a one-entry sample directory
    // pointing at it, then key voice 0 on at unity pitch, full volume, and a direct
    // GAIN level so the output is exact and hand-computable.
    //
    // Block at 0x0200, directory page 0x04 (-> 0x0400). Filter 0 means each nibble
    // decodes to (nibble << shift) >> 1 with no IIR history; shift 8 gives
    // sample = nibble << 7. The data ramps 1,2,3,4,... so decoded samples are
    // 128, 256, 384, 512, ...
    void configure_voice0(s_dsp& chip) {
        const auto ram = chip.aram();

        // Sample directory entry 0 at page 0x04: {start=0x0200, loop=0x0200}.
        ram[0x0400] = 0x00; // start low
        ram[0x0401] = 0x02; // start high -> 0x0200
        ram[0x0402] = 0x00; // loop low
        ram[0x0403] = 0x02; // loop high

        // BRR block header: shift 8, filter 0, loop 0, end 1.
        ram[0x0200] = static_cast<std::uint8_t>((8U << 4U) | (0U << 2U) | 0x01U); // 0x81
        // 8 data bytes -> nibbles 1..16 -> samples 128,256,...,2048.
        for (std::uint8_t i = 0; i < 8U; ++i) {
            const auto hi = static_cast<std::uint8_t>((2U * i + 1U) & 0x0FU);
            const auto lo = static_cast<std::uint8_t>((2U * i + 2U) & 0x0FU);
            ram[0x0201 + i] = static_cast<std::uint8_t>((hi << 4U) | lo);
        }

        // Unmute + un-reset (keep echo disabled); DIR page, full master volume.
        chip.write_reg(s_dsp::reg_flg, s_dsp::flg_echo_disable); // 0x20
        chip.write_reg(s_dsp::reg_dir, 0x04);
        chip.write_reg(s_dsp::reg_mvol_l, 0x7F);
        chip.write_reg(s_dsp::reg_mvol_r, 0x7F);

        // Voice 0: full L/R volume, unity pitch (0x1000 -> 1 sample/step), SRCN 0,
        // direct GAIN at max level.
        chip.write_reg(s_dsp::vreg_vol_l, 0x7F);
        chip.write_reg(s_dsp::vreg_vol_r, 0x7F);
        chip.write_reg(s_dsp::vreg_p_l, 0x00);
        chip.write_reg(s_dsp::vreg_p_h, 0x10); // pitch = 0x1000
        chip.write_reg(s_dsp::vreg_srcn, 0x00);
        chip.write_reg(s_dsp::vreg_adsr2, 0x00); // ADSR2 bit7 = 0 -> use GAIN
        chip.write_reg(s_dsp::vreg_gain, 0x7F);  // direct GAIN, level 0x7F0

        // Key voice 0 on.
        chip.write_reg(s_dsp::reg_kon, 0x01);
    }

} // namespace

TEST_CASE("s_dsp registers in the factory and reports its class", "[s_dsp][audio]") {
    auto chip = create_chip("sony.s_dsp");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
    REQUIRE(chip->metadata().manufacturer == "Sony");
}

TEST_CASE("s_dsp reset clears state to a defined baseline", "[s_dsp][audio]") {
    s_dsp chip;
    // Real hardware boots with FLG.RESET | FLG.MUTE | FLG.ECHO_DISABLE set.
    REQUIRE(chip.read_reg(s_dsp::reg_flg) ==
            (s_dsp::flg_reset | s_dsp::flg_mute | s_dsp::flg_echo_disable));
    // No voice has ended; the live ENDX status reads zero.
    REQUIRE(chip.read_reg(s_dsp::reg_endx) == 0x00);
    // A reset (FLG.MUTE set) chip is silent.
    chip.step();
    REQUIRE(chip.last_left() == 0);
    REQUIRE(chip.last_right() == 0);
}

TEST_CASE("s_dsp register writes read back and decode KON/ENVX/ENDX", "[s_dsp][audio]") {
    s_dsp chip;
    // Plain register read-back.
    chip.write_reg(s_dsp::reg_eon, 0x55);
    REQUIRE(chip.read_reg(s_dsp::reg_eon) == 0x55);

    // ENVX ($x8) is read-only: it reflects the live envelope snapshot, not the
    // stored byte. After key-on with a level, the snapshot is non-zero.
    configure_voice0(chip);
    chip.step();
    REQUIRE(chip.read_reg(s_dsp::vreg_envx) != 0x00);

    // An ENDX write clears the voice-end flags.
    chip.write_reg(s_dsp::reg_endx, 0xFF);
    REQUIRE(chip.read_reg(s_dsp::reg_endx) == 0x00);
}

TEST_CASE("s_dsp synthesises BRR voice 0 sample-for-sample", "[s_dsp][audio]") {
    s_dsp chip;
    configure_voice0(chip);

    std::array<std::int16_t, 8> buf{};
    chip.generate(buf);

    // Hand-computed from the filter-0 ramp:
    //   s[0]=128 -> envelope (128*2032)>>11 = 127 -> *vol *mvol = 125
    //   s[1]=256 -> (256*2032)>>11 = 254 -> *vol *mvol = 250
    REQUIRE(buf[0] == 125); // L0
    REQUIRE(buf[1] == 125); // R0
    REQUIRE(buf[2] == 250); // L1
    REQUIRE(buf[3] == 250); // R1
    // The ramp keeps rising for the next steps (non-trivial, monotone here).
    REQUIRE(buf[4] > buf[2]);
    REQUIRE(buf[6] > buf[4]);
}

TEST_CASE("s_dsp save_state/load_state round-trips bit-identically", "[s_dsp][audio]") {
    s_dsp a;
    configure_voice0(a);
    // Advance a few samples so runtime accumulator / envelope state is non-trivial.
    std::array<std::int16_t, 12> warm{};
    a.generate(warm);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    s_dsp b;
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    // From the restored state both chips must produce identical output.
    std::array<std::int16_t, 64> from_a{};
    std::array<std::int16_t, 64> from_b{};
    a.generate(from_a);
    b.generate(from_b);
    REQUIRE(from_a == from_b);
}

TEST_CASE("s_dsp audio capture counts and drains in stereo frames", "[s_dsp][audio]") {
    s_dsp chip;
    configure_voice0(chip); // an enabled, audible voice
    chip.enable_audio_capture(true);
    constexpr std::size_t frames = 10U;
    chip.tick(frames); // clock_divider defaults to 1 -> one (L,R) frame per cycle

    // pending_samples() is in stereo frames (pairs), NOT raw int16.
    REQUIRE(chip.pending_samples() == frames);

    // Drain fewer pairs than queued: returns pairs, fills 2*pairs int16.
    std::array<std::int16_t, 16> buf{};
    const std::size_t got = chip.drain_samples(buf.data(), 3U);
    REQUIRE(got == 3U);
    REQUIRE(chip.pending_samples() == frames - 3U);
    bool any_nonzero = false; // an active voice must produce non-silent capture
    for (std::size_t i = 0; i < got * 2U; ++i) {
        any_nonzero = any_nonzero || (buf[i] != 0);
    }
    REQUIRE(any_nonzero);

    // Draining past the end returns only what remains.
    std::array<std::int16_t, 64> rest{};
    const std::size_t left = chip.drain_samples(rest.data(), 100U);
    REQUIRE(left == frames - 3U);
    REQUIRE(chip.pending_samples() == 0U);
}
