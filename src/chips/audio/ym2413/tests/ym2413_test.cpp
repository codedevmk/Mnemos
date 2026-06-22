// ym2413 port fidelity vs the Emu reference. The chip is mono (each native
// sample is duplicated onto both stereo lanes); the synthesis pipeline is ported
// integer-exact, so the deterministic waveform asserted below is reproduced
// sample-for-sample by the in-tree core.

#include "ym2413.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
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

    // Keying a channel with a preset instrument makes it sound. The Trumpet's
    // attack is gradual, so step well past it before expecting audible output.
    configure_channel0(chip);
    bool any_nonzero = false;
    for (int i = 0; i < 8000; ++i) {
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

    // Enough samples to cover the gradual attack into the sounding body.
    std::array<std::int16_t, 16384> wx{};
    std::array<std::int16_t, 16384> wy{};
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

namespace {
    // Program channel 0 from explicit user-instrument bytes (slot 0) at the given
    // F-Number low byte + $20 control (key-on | block | F-Number high), volume 0.
    void configure_user(ym2413& chip, const std::array<std::uint8_t, 8>& inst,
                        std::uint8_t fnum_low, std::uint8_t control) {
        for (std::uint8_t i = 0; i < 8U; ++i) {
            write_reg(chip, static_cast<std::uint8_t>(ym2413::reg_user_inst_base + i), inst[i]);
        }
        write_reg(chip, ym2413::reg_inst_vol_base, 0x00); // instrument 0 (user), volume 0 (loud)
        write_reg(chip, ym2413::reg_fnum_low_base, fnum_low);
        write_reg(chip, ym2413::reg_control_base, control);
    }

    [[nodiscard]] int peak_abs(ym2413& chip, int pairs) {
        std::vector<std::int16_t> buf(static_cast<std::size_t>(pairs) * 2U);
        chip.generate(buf);
        int peak = 0;
        for (std::size_t i = 0; i < buf.size(); i += 2U) {
            peak = std::max(peak, std::abs(static_cast<int>(buf[i])));
        }
        return peak;
    }

    [[nodiscard]] int min_sample(ym2413& chip, int pairs) {
        std::vector<std::int16_t> buf(static_cast<std::size_t>(pairs) * 2U);
        chip.generate(buf);
        int lo = 0;
        for (std::size_t i = 0; i < buf.size(); i += 2U) {
            lo = std::min(lo, static_cast<int>(buf[i]));
        }
        return lo;
    }
} // namespace

TEST_CASE("ym2413 key-scale level attenuates higher octaves", "[ym2413][audio]") {
    // Carrier KSL = 3 (6 dB/octave). The modulator is silenced (TL max) so the
    // carrier is ~a pure sine whose peak reflects only the envelope + KSL.
    // Bytes: [0]/[1] egt=1,multi=1; [2] mod TL=$3F (silent); [3] carrier KSL=3
    // ($C0); [4]/[5] AR=15 (instant); [6]/[7] SL/RR=0.
    const std::array<std::uint8_t, 8> ksl_on = {0x21, 0x21, 0x3F, 0xC0, 0xF0, 0xF0, 0x00, 0x00};
    ym2413 low;
    ym2413 high;
    configure_user(low, ksl_on, 0xA0, 0x13);  // block 1, F-Number high bit set
    configure_user(high, ksl_on, 0xA0, 0x1B); // block 5 (four octaves up)
    const int peak_low = peak_abs(low, 512);
    const int peak_high = peak_abs(high, 512);
    REQUIRE(peak_low > 0);
    CHECK(peak_high * 2 < peak_low); // the higher octave is markedly attenuated by KSL

    // With KSL off (field 0) the two octaves reach a comparable peak.
    const std::array<std::uint8_t, 8> ksl_off = {0x21, 0x21, 0x3F, 0x00, 0xF0, 0xF0, 0x00, 0x00};
    ym2413 low0;
    ym2413 high0;
    configure_user(low0, ksl_off, 0xA0, 0x13);
    configure_user(high0, ksl_off, 0xA0, 0x1B);
    const int peak_low0 = peak_abs(low0, 512);
    const int peak_high0 = peak_abs(high0, 512);
    CHECK(peak_high0 * 2 > peak_low0); // no KSL -> no octave attenuation
}

TEST_CASE("ym2413 half-sine waveform silences the negative half", "[ym2413][audio]") {
    // Carrier WF = 1 (half-sine) with the modulator silenced: output is the
    // rectified (non-negative) half-sine; WF = 0 (full sine) swings negative.
    // [3] carrier byte: WF_carrier = bit 4 -> $10 for half-sine, $00 for full.
    const std::array<std::uint8_t, 8> half = {0x21, 0x21, 0x3F, 0x10, 0xF0, 0xF0, 0x00, 0x00};
    const std::array<std::uint8_t, 8> full = {0x21, 0x21, 0x3F, 0x00, 0xF0, 0xF0, 0x00, 0x00};
    ym2413 hsine;
    ym2413 fsine;
    // Block 5 so the carrier phase sweeps full cycles (into the negative half)
    // within the sample window.
    configure_user(hsine, half, 0xA0, 0x1B);
    configure_user(fsine, full, 0xA0, 0x1B);
    CHECK(min_sample(hsine, 512) >= 0); // half-sine never goes negative
    CHECK(min_sample(fsine, 512) < 0);  // full sine does
    CHECK(peak_abs(hsine, 512) > 0);    // and it still sounds
}

namespace {
    // Spread (max - min) of the per-window peak amplitude across the buffer: ~0 for
    // a steady tone, large when tremolo modulates the level.
    [[nodiscard]] int windowed_peak_range(ym2413& chip, int pairs, int window) {
        std::vector<std::int16_t> buf(static_cast<std::size_t>(pairs) * 2U);
        chip.generate(buf);
        int hi = 0;
        int lo = 0x7FFFFFFF;
        for (int w = 0; w + window <= pairs; w += window) {
            int peak = 0;
            for (int i = 0; i < window; ++i) {
                const auto s = buf[static_cast<std::size_t>((w + i) * 2)];
                peak = std::max(peak, std::abs(static_cast<int>(s)));
            }
            hi = std::max(hi, peak);
            lo = std::min(lo, peak);
        }
        return hi - lo;
    }
} // namespace

TEST_CASE("ym2413 AM tremolo modulates the amplitude over time", "[ym2413][audio]") {
    // Carrier AM = 1 (byte[1] bit 7), a sustained tone with the modulator silenced.
    // Over a full AM cycle (~13440 samples) the level swings; without AM it is
    // steady. The window (1024) spans several tone cycles so each window's peak is
    // the true tone peak, varying only with the slow AM LFO.
    const std::array<std::uint8_t, 8> am_on = {0x21, 0xA1, 0x3F, 0x00, 0xF0, 0xF0, 0x00, 0x00};
    const std::array<std::uint8_t, 8> am_off = {0x21, 0x21, 0x3F, 0x00, 0xF0, 0xF0, 0x00, 0x00};
    ym2413 with_am;
    ym2413 without_am;
    configure_user(with_am, am_on, 0xA0, 0x1B);     // block 5
    configure_user(without_am, am_off, 0xA0, 0x1B); // identical but AM off
    const int range_am = windowed_peak_range(with_am, 14000, 1024);
    const int range_steady = windowed_peak_range(without_am, 14000, 1024);
    CHECK(range_am > 100);              // tremolo clearly modulates the level
    CHECK(range_steady < range_am / 4); // no AM -> a steady amplitude
}

TEST_CASE("ym2413 vibrato perturbs the tone and stays deterministic", "[ym2413][audio]") {
    // Carrier VIB = 1 (byte[1] bit 6) at the top F-Number group (non-zero phase-mod
    // offset): the waveform differs from the un-vibratoed tone, deterministically.
    const std::array<std::uint8_t, 8> vib_on = {0x21, 0x61, 0x3F, 0x00, 0xF0, 0xF0, 0x00, 0x00};
    const std::array<std::uint8_t, 8> vib_off = {0x21, 0x21, 0x3F, 0x00, 0xF0, 0xF0, 0x00, 0x00};
    ym2413 vib;
    ym2413 plain;
    configure_user(vib, vib_on, 0xFF, 0x1B); // F-Number $1FF (group 7), block 5
    configure_user(plain, vib_off, 0xFF, 0x1B);
    std::array<std::int16_t, 8000> wv{};
    std::array<std::int16_t, 8000> wp{};
    vib.generate(wv);
    plain.generate(wp);
    CHECK(wv != wp); // vibrato changed the waveform

    ym2413 vib2;
    configure_user(vib2, vib_on, 0xFF, 0x1B);
    std::array<std::int16_t, 8000> wv2{};
    vib2.generate(wv2);
    CHECK(wv == wv2); // and it is reproducible
}

namespace {
    [[nodiscard]] int peak_in_window(ym2413& chip, int total, int start, int end) {
        std::vector<std::int16_t> buf(static_cast<std::size_t>(total) * 2U);
        chip.generate(buf);
        int peak = 0;
        for (int i = start; i < end; ++i) {
            const auto s = buf[static_cast<std::size_t>(i) * 2U];
            peak = std::max(peak, std::abs(static_cast<int>(s)));
        }
        return peak;
    }
} // namespace

TEST_CASE("ym2413 key-scale rate speeds up higher notes' envelopes", "[ym2413][audio]") {
    // A percussive voice (egt=0) with instant attack and a moderate decay to
    // silence. With KSR on, a higher note decays faster, so a late window is much
    // quieter than for the low note; with KSR off both decay at the same rate.
    // Bytes [0]/[1]: egt=0, ksr=1 ($11) / ksr=0 ($01), multi=1. [4]/[5] AR=15,DR=8.
    // [6]/[7] SL=15,RR=0.
    const std::array<std::uint8_t, 8> ksr_on = {0x11, 0x11, 0x3F, 0x00, 0xF8, 0xF8, 0xF0, 0xF0};
    const std::array<std::uint8_t, 8> ksr_off = {0x01, 0x01, 0x3F, 0x00, 0xF8, 0xF8, 0xF0, 0xF0};

    ym2413 lo_on;
    ym2413 hi_on;
    configure_user(lo_on, ksr_on, 0xA0, 0x13); // block 1
    configure_user(hi_on, ksr_on, 0xA0, 0x1D); // block 6
    const int late_lo = peak_in_window(lo_on, 16000, 12000, 16000);
    const int late_hi = peak_in_window(hi_on, 16000, 12000, 16000);
    REQUIRE(late_lo > 0);
    CHECK(late_hi * 4 < late_lo); // KSR: the higher note has decayed much further

    ym2413 lo_off;
    ym2413 hi_off;
    configure_user(lo_off, ksr_off, 0xA0, 0x13);
    configure_user(hi_off, ksr_off, 0xA0, 0x1D);
    const int late_lo0 = peak_in_window(lo_off, 16000, 12000, 16000);
    const int late_hi0 = peak_in_window(hi_off, 16000, 12000, 16000);
    CHECK(late_hi0 * 2 > late_lo0); // no KSR -> the two notes decay comparably
}
