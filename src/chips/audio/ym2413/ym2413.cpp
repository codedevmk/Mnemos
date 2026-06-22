#include "ym2413.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {

    namespace {

        // ---- Instrument mask ROM (15 presets) ----
        //
        // Each preset is 8 bytes, identical layout to the user-instrument
        // register window ($00..$07):
        //   [0] modulator: AM | VIB | EGT | KSR | MULTI(4)
        //   [1] carrier:   AM | VIB | EGT | KSR | MULTI(4)
        //   [2] modulator: KSL(2) | TL(6)
        //   [3] carrier:   KSL(2) | (reserved) | WF_carrier | WF_mod | FB(3)
        //   [4] modulator: AR(4) | DR(4)
        //   [5] carrier:   AR(4) | DR(4)
        //   [6] modulator: SL(4) | RR(4)
        //   [7] carrier:   SL(4) | RR(4)
        // Slot 0 is the user-programmable instrument (filled from $00..$07);
        // slots 1..15 are the fixed preset ROM.
        constexpr std::array<std::array<std::uint8_t, 8>, 15> preset_rom = {{
            {0x71, 0x61, 0x1E, 0x17, 0xD0, 0x78, 0x00, 0x17}, // 1: Violin
            {0x13, 0x41, 0x1A, 0x0D, 0xD8, 0xF7, 0x23, 0x13}, // 2: Guitar
            {0x13, 0x01, 0x99, 0x04, 0xF2, 0xF4, 0x11, 0x23}, // 3: Piano
            {0x21, 0x61, 0x1B, 0x07, 0xAF, 0x64, 0x40, 0x27}, // 4: Flute
            {0x22, 0x21, 0x1E, 0x06, 0xF0, 0x76, 0x08, 0x28}, // 5: Clarinet
            {0x31, 0x22, 0x16, 0x05, 0x90, 0x71, 0x00, 0x18}, // 6: Oboe
            {0x21, 0x61, 0x1D, 0x07, 0x82, 0x80, 0x10, 0x17}, // 7: Trumpet
            {0x23, 0x21, 0x2D, 0x16, 0xC0, 0x70, 0x07, 0x07}, // 8: Organ
            {0x61, 0x61, 0x1B, 0x06, 0x64, 0x65, 0x10, 0x17}, // 9: Horn
            {0x41, 0x61, 0x0B, 0x18, 0x85, 0xF0, 0x81, 0x07}, // A: Synth
            {0x13, 0x01, 0x83, 0x11, 0xFA, 0xE4, 0x10, 0x04}, // B: Harpsichord
            {0x17, 0xC1, 0x24, 0x07, 0xF8, 0xF8, 0x22, 0x12}, // C: Vibraphone
            {0x61, 0x50, 0x0C, 0x05, 0xC2, 0xF5, 0x20, 0x42}, // D: Synth Bass
            {0x01, 0x01, 0x55, 0x03, 0xE9, 0x90, 0x03, 0x02}, // E: Wood Bass
            {0x41, 0x41, 0x89, 0x03, 0xF1, 0xE4, 0xC0, 0x13}, // F: Electric Guitar
        }};

        // Frequency multiplier table: MULTI=0 maps to 0.5 (represented here as 1
        // with the final >>1; 1..15 map to the listed integer multipliers,
        // doubled so the shared >>1 yields the real value).
        constexpr std::array<std::uint8_t, 16> multi_table = {1,  2,  4,  6,  8,  10, 12, 14,
                                                              16, 18, 20, 20, 24, 24, 30, 30};

        // Key-Scale-Level base attenuation (the OPLL "KSL ROM"): higher notes are
        // progressively attenuated. Indexed by [block*16 + (F-Number >> 5)] -- block
        // (octave) and the F-Number's top 4 bits. Values are in 0.375 dB steps (one
        // step = 16 attenuation units in this chip's domain, where 256 units = 1
        // octave = 6 dB); they realise the full 6 dB/octave slope, which the KSL
        // field then scales down (>>0/1/2 for 6/3/1.5 dB-oct, off for field 0).
        constexpr std::array<std::uint8_t, 128> ksl_steps = {
            // OCT 0
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            // OCT 1
            0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 4, 5, 6, 7, 8,
            // OCT 2
            0, 0, 0, 0, 0, 3, 5, 7, 8, 10, 11, 12, 13, 14, 15, 16,
            // OCT 3
            0, 0, 0, 5, 8, 11, 13, 15, 16, 18, 19, 20, 21, 22, 23, 24,
            // OCT 4
            0, 0, 8, 13, 16, 19, 21, 23, 24, 26, 27, 28, 29, 30, 31, 32,
            // OCT 5
            0, 8, 16, 21, 24, 27, 29, 31, 32, 34, 35, 36, 37, 38, 39, 40,
            // OCT 6
            0, 16, 24, 29, 32, 35, 37, 39, 40, 42, 43, 44, 45, 46, 47, 48,
            // OCT 7
            0, 24, 32, 37, 40, 43, 45, 47, 48, 50, 51, 52, 53, 54, 55, 56};

        // KSL attenuation (in this chip's atten units) for a 2-bit field and a
        // base step from ksl_steps: field 0 = off; 1/2/3 = 1.5/3/6 dB per octave
        // (>>2 / >>1 / >>0 of the full-slope value). One step = 16 atten units.
        [[nodiscard]] std::int32_t ksl_atten(std::uint8_t field, std::uint8_t step) noexcept {
            if (field == 0U) {
                return 0;
            }
            return (static_cast<std::int32_t>(step) << 4) >> (3U - field);
        }

        // VIB (vibrato) phase-modulation table (verified on real OPLL): 8 F-Number
        // groups x 8 LFO phases. The value perturbs the channel's effective
        // F-Number when an operator enables vibrato; the LFO advances one phase
        // every 1024 native samples (~6 Hz).
        constexpr std::array<std::int8_t, 64> lfo_pm_table = {
            0, 0, 0, 0,  0,  0,  0, 0,  // F-Number group 0
            1, 0, 0, 0,  -1, 0,  0, 0,  // 1
            2, 1, 0, -1, -2, -1, 0, 1,  // 2
            3, 1, 0, -1, -3, -1, 0, 1,  // 3
            4, 2, 0, -2, -4, -2, 0, 2,  // 4
            5, 2, 0, -2, -5, -2, 0, 2,  // 5
            6, 3, 0, -3, -6, -3, 0, 3,  // 6
            7, 3, 0, -3, -7, -3, 0, 3}; // 7

        // AM (tremolo) advances one lfo_am entry every 64 native samples (~3.7 Hz).
        constexpr std::uint32_t am_samples_per_entry = 64U;
        // VIB advances one of the 8 phases every 1024 native samples (~6 Hz).
        constexpr std::uint32_t vib_samples_per_phase = 1024U;
        // Convert an lfo_am entry (0..26, 0.1875 dB units) to this core's atten
        // units: one chip unit = 8 atten units, and the (provisional, ear-tunable)
        // tremolo depth halves that -- x4 overall, a ~2.4 dB peak.
        constexpr std::int32_t am_depth_scale = 4;

        // ---- Output-pipeline tables (log-sine + exp), built once ----
        //
        // The OPLL stores a 256-entry quarter-sine LUT in the log domain (12-bit
        // -log2(sin) * 256) and an exp post-decode that reverses the log to a
        // linear sample. They are pure functions of math, identical for every
        // chip instance, so they are built once on first use.
        // The AM (tremolo) LFO table: a 210-entry triangle 0 -> 26 -> 0 the OPLL
        // steps through (one entry per 64 native samples => ~3.7 Hz). Values are in
        // the chip's "decibel" units (0.1875 dB each); the synthesis scales them
        // into this core's attenuation domain when an operator enables AM.
        constexpr std::size_t lfo_am_len = 210U;

        struct output_tables final {
            std::array<std::int16_t, 256> log_sine{};
            std::array<std::int16_t, 256> exp_lut{};
            std::array<std::uint8_t, lfo_am_len> lfo_am{};

            output_tables() noexcept {
                constexpr double pi = 3.14159265358979323846;
                for (int i = 0; i < 256; ++i) {
                    const double s = std::sin((i + 0.5) / 256.0 * pi / 2.0);
                    if (s <= 0.0) {
                        log_sine[static_cast<std::size_t>(i)] = 0x0FFF;
                    } else {
                        int iv = static_cast<int>(-std::log2(s) * 256.0 + 0.5);
                        iv = std::clamp(iv, 0, 0x0FFF);
                        log_sine[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(iv);
                    }
                }
                for (int i = 0; i < 256; ++i) {
                    const int iv = static_cast<int>(std::pow(2.0, i / 256.0) * 1024.0 + 0.5);
                    exp_lut[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(iv);
                }
                // Triangle: 7 zeros, rise 1..25 (four entries each), three at the 26
                // peak, then fall 25..1 (four each) -- 7 + 100 + 3 + 100 = 210.
                std::size_t k = 0;
                for (int i = 0; i < 7; ++i) {
                    lfo_am[k++] = 0U;
                }
                for (int v = 1; v <= 25; ++v) {
                    for (int r = 0; r < 4; ++r) {
                        lfo_am[k++] = static_cast<std::uint8_t>(v);
                    }
                }
                for (int r = 0; r < 3; ++r) {
                    lfo_am[k++] = 26U;
                }
                for (int v = 25; v >= 1; --v) {
                    for (int r = 0; r < 4; ++r) {
                        lfo_am[k++] = static_cast<std::uint8_t>(v);
                    }
                }
            }
        };

        [[nodiscard]] const output_tables& tables() noexcept {
            static const output_tables t{};
            return t;
        }

        // Convert a 13-bit log-domain attenuation (0.375 dB / step) to a linear
        // signed sample: the high 5 bits are a binary shift, the low 8 bits index
        // the half-precision exp2 table.
        [[nodiscard]] std::int32_t log_to_linear(std::uint16_t log_val) noexcept {
            if (log_val >= 0x1FFFU) {
                return 0; // fully attenuated
            }
            const int shift = (log_val >> 8U) & 0x1FU;
            const auto frac = static_cast<std::uint8_t>(255U - (log_val & 0xFFU));
            const std::int32_t mant = tables().exp_lut[frac]; // ~1024..2047
            return mant >> (shift > 14 ? 14 : shift);
        }

        // Read an 11-bit log-sine value (0..2047 across one full cycle), folding
        // the quarter-wave LUT: bit 10 = sign, bit 9 = mirror direction. Returns
        // the magnitude in bits 11-0 with the sign packed into bit 12.
        //
        // wf selects the operator waveform (OPLL has two): 0 = full sine, 1 = the
        // half-sine, whose negative half (sign bit set) is replaced by silence.
        [[nodiscard]] std::int32_t sine_log(std::uint16_t phase11, std::uint8_t wf) noexcept {
            if (wf != 0U && (phase11 & 0x400U) != 0U) {
                return 0x0FFF; // half-sine: the negative half is silenced (max atten)
            }
            auto qpos = static_cast<std::uint8_t>((phase11 >> 1U) & 0xFFU);
            if ((phase11 & 0x200U) != 0U) {
                qpos = static_cast<std::uint8_t>(255U - qpos);
            }
            std::int32_t mag = tables().log_sine[qpos];
            if ((phase11 & 0x400U) != 0U) {
                mag |= 0x1000; // sign bit
            }
            return mag;
        }

        // Envelope-rate step period: rate-N advances every 2^(15-N) EG ticks,
        // rate 0 is frozen, rate 15 is the fastest (every tick).
        [[nodiscard]] std::uint16_t eg_period(std::uint8_t rate) noexcept {
            if (rate == 0U) {
                return 0U; // frozen
            }
            if (rate >= 0xFU) {
                return 1U; // fastest
            }
            const int shift = 15 - static_cast<int>(rate);
            return static_cast<std::uint16_t>(1U << (shift < 0 ? 0 : shift));
        }

    } // namespace

    chip_metadata ym2413::metadata() const noexcept {
        return {
            .manufacturer = "Yamaha",
            .part_number = "YM2413",
            .family = "OPLL",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void ym2413::apply_instrument(channel_state& c,
                                  std::span<const std::uint8_t, 8> inst) noexcept {
        // byte[0] modulator: AM | VIB | EGT | KSR | MULTI(4)
        c.op[0].am = (inst[0] >> 7U) & 1U;
        c.op[0].vib = (inst[0] >> 6U) & 1U;
        c.op[0].egt = (inst[0] >> 5U) & 1U;
        c.op[0].ksr = (inst[0] >> 4U) & 1U;
        c.op[0].multi = inst[0] & 0xFU;
        // byte[1] carrier: same fields
        c.op[1].am = (inst[1] >> 7U) & 1U;
        c.op[1].vib = (inst[1] >> 6U) & 1U;
        c.op[1].egt = (inst[1] >> 5U) & 1U;
        c.op[1].ksr = (inst[1] >> 4U) & 1U;
        c.op[1].multi = inst[1] & 0xFU;
        // byte[2] modulator: KSL(2) | TL(6)
        c.op[0].ksl = (inst[2] >> 6U) & 0x3U;
        c.op[0].tl = inst[2] & 0x3FU;
        // byte[3] carrier: KSL(2) | (reserved) | WF_carrier | WF_mod | FB(3)
        c.op[1].ksl = (inst[3] >> 6U) & 0x3U;
        c.op[1].wf = (inst[3] >> 4U) & 1U;
        c.op[0].wf = (inst[3] >> 3U) & 1U;
        c.op[0].fb = inst[3] & 0x7U;
        c.op[1].fb = 0U; // carrier has no feedback
        c.op[1].tl = 0U; // carrier TL is replaced by channel volume
        // byte[4] modulator: AR | DR
        c.op[0].ar = (inst[4] >> 4U) & 0xFU;
        c.op[0].dr = inst[4] & 0xFU;
        // byte[5] carrier: AR | DR
        c.op[1].ar = (inst[5] >> 4U) & 0xFU;
        c.op[1].dr = inst[5] & 0xFU;
        // byte[6] modulator: SL | RR
        c.op[0].sl = (inst[6] >> 4U) & 0xFU;
        c.op[0].rr = inst[6] & 0xFU;
        // byte[7] carrier: SL | RR
        c.op[1].sl = (inst[7] >> 4U) & 0xFU;
        c.op[1].rr = inst[7] & 0xFU;
    }

    void ym2413::refresh_channel_instrument(int ch_index) noexcept {
        channel_state& c = channels_[static_cast<std::size_t>(ch_index)];
        if (c.instrument == 0U) {
            apply_instrument(c, std::span<const std::uint8_t, 8>(user_instrument_));
        } else if (c.instrument <= 15U) {
            apply_instrument(c, std::span<const std::uint8_t, 8>(
                                    preset_rom[static_cast<std::size_t>(c.instrument - 1U)]));
        }
    }

    void ym2413::write_data(std::uint8_t value) noexcept {
        const std::uint8_t regaddr = addr_latch_;

        if (regaddr < user_instrument_size) {
            user_instrument_[regaddr] = value;
            // Refresh any channel currently bound to the user instrument.
            for (int i = 0; i < channel_count; ++i) {
                if (channels_[static_cast<std::size_t>(i)].instrument == 0U) {
                    refresh_channel_instrument(i);
                }
            }
            return;
        }
        if (regaddr == reg_rhythm) {
            rhythm_ctrl_ = value;
            return;
        }
        if (regaddr == reg_test) {
            test_reg_ = value;
            return;
        }
        if (regaddr >= reg_fnum_low_base && regaddr <= reg_fnum_low_base + channel_count - 1) {
            const int ch = regaddr - reg_fnum_low_base;
            channel_state& c = channels_[static_cast<std::size_t>(ch)];
            c.f_number = static_cast<std::uint16_t>((c.f_number & 0x0100U) | value);
            return;
        }
        if (regaddr >= reg_control_base && regaddr <= reg_control_base + channel_count - 1) {
            const int ch = regaddr - reg_control_base;
            channel_state& c = channels_[static_cast<std::size_t>(ch)];
            c.f_number = static_cast<std::uint16_t>(
                (c.f_number & 0x00FFU) | (static_cast<std::uint16_t>(value & 0x01U) << 8U));
            c.block = static_cast<std::uint8_t>((value >> 1U) & 0x07U);
            const bool new_key = (value & 0x10U) != 0U;
            c.sustain = (value & 0x20U) != 0U;
            if (new_key && !c.prev_key_on) {
                // Key-on edge: kick both operators into the fast-damp step that
                // precedes attack so the previous note's tail does not bleed
                // through.
                for (auto& o : c.op) {
                    o.state = eg_state::damp;
                    o.phase = 0U;
                }
                refresh_channel_instrument(ch);
            } else if (!new_key && c.prev_key_on) {
                // Key-off edge: release.
                for (auto& o : c.op) {
                    o.state = eg_state::release;
                }
            }
            c.key_on = new_key;
            c.prev_key_on = new_key;
            return;
        }
        if (regaddr >= reg_inst_vol_base && regaddr <= reg_inst_vol_base + channel_count - 1) {
            const int ch = regaddr - reg_inst_vol_base;
            channel_state& c = channels_[static_cast<std::size_t>(ch)];
            c.instrument = static_cast<std::uint8_t>((value >> 4U) & 0x0FU);
            c.volume = static_cast<std::uint8_t>(value & 0x0FU);
            refresh_channel_instrument(ch);
            return;
        }
    }

    void ym2413::op_eg_tick(op_state& o, std::uint8_t keycode) noexcept {
        std::uint8_t rate = 0U;
        switch (o.state) {
        case eg_state::damp:
            rate = 12U; // fast damp
            break;
        case eg_state::attack:
            rate = o.ar;
            break;
        case eg_state::decay:
            rate = o.dr;
            break;
        case eg_state::sustain:
            rate = (o.egt != 0U) ? 0U : 5U;
            break;
        case eg_state::release:
            rate = o.rr;
            break;
        case eg_state::off:
            return; // frozen
        }
        if (rate == 0U) {
            return; // a zero rate is frozen regardless of key scaling
        }
        // Key-scale rate (KSR): the OPLL adds a rate-key-scale to the 4-bit rate so
        // higher notes have faster envelopes. RKS = ksr ? keycode : keycode>>2, and
        // the effective 6-bit rate is 4*rate + RKS. At this model's >>2 step
        // granularity ksr=0 leaves the rate unchanged (keycode>>2 never crosses a
        // step), so only ksr=1 operators key-scale; the per-step sub-pattern of the
        // exact OPL rate model is a later EG-accuracy refinement.
        const std::uint32_t rks =
            (o.ksr != 0U) ? keycode : static_cast<std::uint32_t>(keycode >> 2U);
        const std::uint32_t rate6 = std::min<std::uint32_t>(63U, (4U * rate) + rks);
        const std::uint16_t period = eg_period(static_cast<std::uint8_t>(rate6 >> 2U));
        // Global EG cadence: the shared eg_counter_ gates stepping per OPLL spec.
        // (The reference lifted this from a file-static into per-chip state so a
        // fresh chip is deterministic and instances are independent.)
        if (period == 0U || (eg_counter_ % period) != 0U) {
            return;
        }

        switch (o.state) {
        case eg_state::damp:
            if (o.eg_level < 0x1FFU) {
                o.eg_level = static_cast<std::uint16_t>(o.eg_level + 4U);
                if (o.eg_level >= 0x1FFU) {
                    o.eg_level = 0x1FFU;
                    o.state = eg_state::attack;
                }
            } else {
                o.state = eg_state::attack;
            }
            break;
        case eg_state::attack:
            // Attack uses an exponential curve toward 0 (peak loudness).
            if (o.eg_level > 0U) {
                std::uint16_t s = static_cast<std::uint16_t>((o.eg_level + 1U) >> 3U);
                if (s == 0U) {
                    s = 1U;
                }
                o.eg_level = (o.eg_level >= s) ? static_cast<std::uint16_t>(o.eg_level - s)
                                               : std::uint16_t{0};
                if (o.eg_level == 0U) {
                    o.state = eg_state::decay;
                }
            } else {
                o.state = eg_state::decay;
            }
            break;
        case eg_state::decay: {
            std::uint16_t sustain_target = static_cast<std::uint16_t>(o.sl << 3U);
            if (sustain_target >= 0x78U) {
                sustain_target = 0x1FFU;
            }
            if (o.eg_level < sustain_target) {
                o.eg_level = static_cast<std::uint16_t>(o.eg_level + 1U);
            } else {
                o.state = eg_state::sustain;
            }
            break;
        }
        case eg_state::sustain:
            // A percussive (egt=0) instrument decays slowly while sustained.
            if (o.egt == 0U && o.eg_level < 0x1FFU) {
                o.eg_level = static_cast<std::uint16_t>(o.eg_level + 1U);
            }
            break;
        case eg_state::release:
            if (o.eg_level < 0x1FFU) {
                o.eg_level = static_cast<std::uint16_t>(o.eg_level + 1U);
            } else {
                o.eg_level = 0x1FFU;
                o.state = eg_state::off;
            }
            break;
        case eg_state::off:
            break;
        }
    }

    std::int32_t ym2413::channel_sample(channel_state& c) noexcept {
        // Base phase increment from block + F-Number. At the native sample rate the
        // reference's Q16 unit-rate scaler is exactly 1.0, so the phase-per-sample
        // math reduces to the integer increment directly.
        const std::uint32_t base_inc = (static_cast<std::uint32_t>(c.f_number) << c.block) >> 1U;

        // Key-scale level: a per-note attenuation from block + the F-Number's top 4
        // bits, scaled per operator by its KSL field (added into each atten below).
        const std::uint8_t ksl_step =
            ksl_steps[(static_cast<std::size_t>(c.block) << 4U) | (c.f_number >> 5U)];

        // Current LFO outputs (the counters advance once per sample in step()).
        // AM (tremolo): an attenuation added to AM-enabled operators.
        const std::int32_t am_atten =
            static_cast<std::int32_t>(
                tables().lfo_am[(am_counter_ / am_samples_per_entry) % lfo_am_len]) *
            am_depth_scale;
        // VIB (vibrato): a small F-Number perturbation for vibrato-enabled operators,
        // chosen by the F-Number's top 3 bits and the current LFO phase.
        const std::uint32_t vib_phase = (vib_counter_ / vib_samples_per_phase) & 0x07U;
        const std::int32_t vib_offset =
            lfo_pm_table[static_cast<std::size_t>((((c.f_number >> 6U) & 0x07U) * 8U) + vib_phase)];

        // Phase increment for one operator, applying vibrato to the F-Number when the
        // operator enables it (the perturbation scales with the block/octave). With a
        // zero offset this reduces exactly to base_inc.
        const auto op_inc = [&](const op_state& o) -> std::uint32_t {
            std::uint32_t inc = base_inc;
            if (o.vib != 0U) {
                const std::int32_t fn = (static_cast<std::int32_t>(c.f_number) * 2) + vib_offset;
                inc = static_cast<std::uint32_t>((fn << c.block) >> 2);
            }
            return (inc * multi_table[o.multi]) >> 1U;
        };

        // Modulator (op[0]) phase + sample.
        op_state& m = c.op[0];
        m.phase += op_inc(m);
        auto pm = static_cast<std::uint16_t>((m.phase >> 9U) & 0x07FFU);
        const std::int32_t fb_avg = (m.feedback[0] + m.feedback[1]) >> 1;
        if (m.fb > 0U) {
            pm = static_cast<std::uint16_t>((pm + (fb_avg >> (7U - m.fb))) & 0x07FFU);
        }
        const std::int32_t m_log = sine_log(pm, m.wf);
        const std::int32_t m_atten = (m_log & 0x0FFF) + (static_cast<std::int32_t>(m.tl) << 5) +
                                     (static_cast<std::int32_t>(m.eg_level) << 4) +
                                     ksl_atten(m.ksl, ksl_step) + (m.am != 0U ? am_atten : 0);
        // Saturate (not wrap) when the summed attenuation exceeds the 13-bit range,
        // so a near-silent operator stays silent instead of aliasing back to loud.
        std::int32_t m_lin = log_to_linear(static_cast<std::uint16_t>(std::min(m_atten, 0x1FFF)));
        if ((m_log & 0x1000) != 0) {
            m_lin = -m_lin;
        }
        m.feedback[1] = m.feedback[0];
        m.feedback[0] = static_cast<std::int16_t>(m_lin);

        // Carrier (op[1]) phase + FM input from the modulator.
        op_state& cr = c.op[1];
        cr.phase += op_inc(cr);
        const auto pc = static_cast<std::uint16_t>(((cr.phase >> 9U) + (m_lin >> 1)) & 0x07FFU);
        const std::int32_t c_log = sine_log(pc, cr.wf);
        const std::int32_t c_atten = (c_log & 0x0FFF) + (static_cast<std::int32_t>(c.volume) << 7) +
                                     (static_cast<std::int32_t>(cr.eg_level) << 4) +
                                     ksl_atten(cr.ksl, ksl_step) + (cr.am != 0U ? am_atten : 0);
        std::int32_t out = log_to_linear(static_cast<std::uint16_t>(std::min(c_atten, 0x1FFF)));
        if ((c_log & 0x1000) != 0) {
            out = -out;
        }
        return out;
    }

    void ym2413::step() noexcept {
        ++eg_counter_;
        ++am_counter_; // AM/VIB LFOs advance once per native sample (not per channel)
        ++vib_counter_;
        std::int32_t mix = 0;
        for (auto& c : channels_) {
            // A channel contributes only while its carrier envelope is live.
            if (c.op[1].state == eg_state::off) {
                continue;
            }
            mix += channel_sample(c);
            // Advance the envelope generators after producing the sample, per the
            // OPLL spec (EG ticks each chip step). The key code (block + F-Number's
            // top bit) drives the key-scale-rate.
            const auto keycode =
                static_cast<std::uint8_t>((c.block << 1U) | ((c.f_number >> 8U) & 0x01U));
            op_eg_tick(c.op[0], keycode);
            op_eg_tick(c.op[1], keycode);
        }
        mix = std::clamp(mix, -32768, 32767);
        last_sample_ = static_cast<std::int16_t>(mix);
    }

    void ym2413::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            step();
            // Mono chip: the native sample is written to both stereo lanes.
            buf_lr[i * 2U] = last_sample_;
            buf_lr[i * 2U + 1U] = last_sample_;
        }
    }

    void ym2413::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (++prescaler_ >= clock_divider_) {
                prescaler_ = 0;
                step();
                if (audio_capture_) {
                    sample_queue_.push_back(last_sample_);
                    sample_queue_.push_back(last_sample_);
                }
            }
        }
    }

    std::size_t ym2413::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
        // The queue holds interleaved (L,R) int16; counts are in stereo pairs.
        const std::size_t avail_pairs = sample_queue_.size() / 2U;
        const std::size_t n = std::min(avail_pairs, max_pairs);
        if (n == 0U) {
            return 0U;
        }
        std::memcpy(out, sample_queue_.data(), n * 2U * sizeof(std::int16_t));
        sample_queue_.erase(sample_queue_.begin(),
                            sample_queue_.begin() + static_cast<std::ptrdiff_t>(n * 2U));
        return n;
    }

    void ym2413::reset(reset_kind /*kind*/) {
        user_instrument_ = {};
        rhythm_ctrl_ = 0U;
        test_reg_ = 0U;
        addr_latch_ = 0U;
        audio_select_ = 0U;
        am_counter_ = 0U;
        vib_counter_ = 0U;
        eg_counter_ = 0U;
        channels_ = {};
        for (auto& c : channels_) {
            // Power-up baseline: silent volume, both operators off + fully
            // attenuated.
            c.volume = 0xFU;
            for (auto& o : c.op) {
                o.state = eg_state::off;
                o.eg_level = 0x1FFU;
            }
        }
        last_sample_ = 0;
        prescaler_ = 0;
        sample_queue_.clear();
    }

    void ym2413::save_state(state_writer& writer) const {
        writer.bytes(user_instrument_);
        writer.u8(rhythm_ctrl_);
        writer.u8(test_reg_);
        writer.u8(addr_latch_);
        writer.u8(audio_select_);
        for (const auto& c : channels_) {
            writer.u16(c.f_number);
            writer.u8(c.block);
            writer.boolean(c.key_on);
            writer.boolean(c.sustain);
            writer.u8(c.instrument);
            writer.u8(c.volume);
            writer.boolean(c.prev_key_on);
            for (const auto& o : c.op) {
                writer.u8(o.am);
                writer.u8(o.vib);
                writer.u8(o.egt);
                writer.u8(o.ksr);
                writer.u8(o.multi);
                writer.u8(o.ksl);
                writer.u8(o.tl);
                writer.u8(o.fb);
                writer.u8(o.ar);
                writer.u8(o.dr);
                writer.u8(o.sl);
                writer.u8(o.rr);
                writer.u8(o.wf);
                writer.u32(o.phase);
                writer.u8(static_cast<std::uint8_t>(o.state));
                writer.u16(o.eg_level);
                writer.u16(static_cast<std::uint16_t>(o.feedback[0]));
                writer.u16(static_cast<std::uint16_t>(o.feedback[1]));
            }
        }
        writer.u32(am_counter_);
        writer.u32(vib_counter_);
        writer.u32(eg_counter_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
        writer.u16(static_cast<std::uint16_t>(last_sample_));
    }

    void ym2413::load_state(state_reader& reader) {
        reader.bytes(user_instrument_);
        rhythm_ctrl_ = reader.u8();
        test_reg_ = reader.u8();
        addr_latch_ = reader.u8();
        audio_select_ = reader.u8();
        for (auto& c : channels_) {
            c.f_number = reader.u16();
            c.block = reader.u8();
            c.key_on = reader.boolean();
            c.sustain = reader.boolean();
            c.instrument = reader.u8();
            c.volume = reader.u8();
            c.prev_key_on = reader.boolean();
            for (auto& o : c.op) {
                o.am = reader.u8();
                o.vib = reader.u8();
                o.egt = reader.u8();
                o.ksr = reader.u8();
                o.multi = reader.u8();
                o.ksl = reader.u8();
                o.tl = reader.u8();
                o.fb = reader.u8();
                o.ar = reader.u8();
                o.dr = reader.u8();
                o.sl = reader.u8();
                o.rr = reader.u8();
                o.wf = reader.u8();
                o.phase = reader.u32();
                o.state = static_cast<eg_state>(reader.u8());
                o.eg_level = reader.u16();
                o.feedback[0] = static_cast<std::int16_t>(reader.u16());
                o.feedback[1] = static_cast<std::int16_t>(reader.u16());
            }
        }
        am_counter_ = reader.u32();
        vib_counter_ = reader.u32();
        eg_counter_ = reader.u32();
        clock_divider_ = static_cast<int>(reader.u32());
        prescaler_ = static_cast<int>(reader.u32());
        last_sample_ = static_cast<std::int16_t>(reader.u16());
    }

    instrumentation::ichip_introspection& ym2413::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> ym2413::register_snapshot() noexcept {
        using fmt = register_value_format;
        // Surface the latched address + a representative channel's live state.
        const channel_state& c = channels_[0];
        register_view_[0] = {"ADDR", addr_latch_, 8U, fmt::unsigned_integer};
        register_view_[1] = {"RHYTHM", rhythm_ctrl_, 8U, fmt::flags};
        register_view_[2] = {"AUDIOSEL", audio_select_, 8U, fmt::flags};
        register_view_[3] = {"CH0_FNUM", c.f_number, 9U, fmt::unsigned_integer};
        register_view_[4] = {"CH0_BLOCK", c.block, 3U, fmt::unsigned_integer};
        register_view_[5] = {"CH0_INST", c.instrument, 4U, fmt::unsigned_integer};
        register_view_[6] = {"CH0_VOL", c.volume, 4U, fmt::unsigned_integer};
        register_view_[7] = {"CH0_KEYON", c.key_on ? 1U : 0U, 1U, fmt::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto ym2413_registration =
            register_factory("yamaha.ym2413", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<ym2413>(); });
    } // namespace

} // namespace mnemos::chips::audio
