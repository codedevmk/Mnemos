#include "scsp.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {
    namespace {
        // ADSR envelope-step period table: the 5-bit rate code selects how many
        // native samples pass between envelope steps. Rate $1F = fastest (1
        // sample/step), rate 0 = off.
        constexpr std::array<std::uint16_t, 32> adsr_rate_table = {
            0,  0,  8192, 6144, 4096, 3072, 2048, 1536, 1024, 768, 512, 384, 256, 192, 128, 96,
            64, 48, 32,   24,   16,   12,   8,    6,    4,    3,   2,   1,   1,   1,   1,   1};

        // LFO phase-increment table: samples per full cycle at the native rate,
        // 32 logarithmically-spaced steps (lower index = slower).
        constexpr std::array<std::uint32_t, 32> lfo_step_table = {
            229376, 180224, 131072, 98304, 65536, 49152, 32768, 24576, 16384, 12288, 8192,
            6144,   4096,   3072,   2048,  1536,  1024,  768,   512,   384,   256,   192,
            128,    96,     64,     48,    32,    24,    16,    12,    8,     6};

        // DISDL (direct-send level), 3-bit code -> Q15 gain, 6 dB/step.
        constexpr std::array<std::uint16_t, 8> disdl_table = {0x0000U, 0x0208U, 0x0409U, 0x0814U,
                                                              0x1020U, 0x2027U, 0x4027U, 0x8000U};

        // DIPAN (direct pan), 4-bit per-side attenuation -> Q15, 3 dB/step.
        // 0x8000 == unity gain after the >>15.
        constexpr std::array<std::uint16_t, 16> dipan_table = {
            0x8000U, 0x5A9DU, 0x4027U, 0x2D6BU, 0x2027U, 0x16C3U, 0x1020U, 0x0B68U,
            0x0814U, 0x05B6U, 0x0409U, 0x02DDU, 0x0208U, 0x016FU, 0x0104U, 0x0000U};

        // 10-bit envelope level -> Q15 linear multiplier. Modelled as a 6-bit
        // mantissa plus coarse exponent (not 1024 linear dB steps). TL uses the
        // same attenuation domain with its 8-bit value shifted left by two.
        [[nodiscard]] std::uint16_t env_to_linear(std::uint16_t env_level) noexcept {
            if (env_level >= 0x400U) {
                return 0U;
            }
            if (env_level == 0U) {
                return 0x8000U;
            }
            const std::uint32_t factor = ((static_cast<std::uint32_t>(env_level) & 0x3FU) ^ 0x7FU);
            const std::uint32_t shift = ((static_cast<std::uint32_t>(env_level) >> 6U) + 7U);
            const std::uint32_t q15 = (factor << 15U) >> shift;
            return q15 > 0x8000U ? 0x8000U : static_cast<std::uint16_t>(q15);
        }

        // LFO waveform sampler: `phase` is the low 16 bits of the LFO phase
        // accumulator (one full cycle per wrap); returns a signed 16-bit range.
        [[nodiscard]] std::int32_t lfo_sample(std::uint8_t wave, std::uint32_t phase) noexcept {
            const auto p = static_cast<std::uint16_t>(phase & 0xFFFFU);
            switch (wave & 0x3U) {
            case 0: // Sawtooth -- linear ramp -0x8000 -> +0x7FFF
                return static_cast<std::int32_t>(
                    static_cast<std::int16_t>(static_cast<std::int32_t>(p) - 0x8000));
            case 1: // Square -- +0x7FFF first half, -0x8000 second half
                return (p < 0x8000U) ? 0x7FFF : -0x8000;
            case 2: // Triangle -- symmetric ramp
                if (p < 0x4000U) {
                    return static_cast<std::int32_t>((p * 2U) - 0x8000U);
                }
                if (p < 0xC000U) {
                    return static_cast<std::int32_t>(0x7FFFU - ((p - 0x4000U) * 2U));
                }
                return static_cast<std::int32_t>(((p - 0xC000U) * 2U) - 0x8000U);
            case 3: { // Noise -- per-sample xorshift seeded by the phase accumulator
                std::uint32_t x = phase;
                x ^= x << 13U;
                x ^= x >> 17U;
                x ^= x << 5U;
                return static_cast<std::int32_t>(static_cast<std::int16_t>(x & 0xFFFFU));
            }
            default:
                return 0;
            }
        }

        // OCT/FNS pitch register -> Q16.16 phase increment.
        //   pitch[15:11] = OCT (signed 4-bit), pitch[10:0] = FNS (11-bit).
        // Base ratio (1024 + FNS) / 1024 in Q16.16; OCT shifts whole octaves.
        [[nodiscard]] std::uint32_t phase_increment(std::uint16_t pitch) noexcept {
            int oct = static_cast<int>((pitch >> 11U) & 0xFU);
            if ((oct & 0x8) != 0) {
                oct |= ~0xF; // sign-extend 4-bit
            }
            const auto fns = static_cast<std::uint32_t>(pitch & 0x7FFU);
            std::uint32_t ratio = (0x10000U * (1024U + fns)) / 1024U;
            if (oct > 0) {
                ratio <<= oct;
            } else if (oct < 0) {
                ratio >>= (-oct);
            }
            return ratio;
        }

        [[nodiscard]] std::int32_t clamp16(std::int32_t v) noexcept {
            if (v > 32767) {
                return 32767;
            }
            if (v < -32768) {
                return -32768;
            }
            return v;
        }
    } // namespace

    chip_metadata scsp::metadata() const noexcept {
        return {
            .manufacturer = "Yamaha",
            .part_number = "YMF292",
            .family = "PCM",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void scsp::decode_slot(int index) noexcept {
        const std::uint8_t* r = &regs_[static_cast<std::size_t>(index) * slot_reg_stride];
        slot& v = slots_[static_cast<std::size_t>(index)];

        const auto ctl =
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(r[0x00]) << 8U) | r[0x01]);
        const auto eg0 =
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(r[0x08]) << 8U) | r[0x09]);
        const auto eg1 =
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(r[0x0A]) << 8U) | r[0x0B]);
        const auto mod =
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(r[0x0E]) << 8U) | r[0x0F]);

        v.start_addr = (static_cast<std::uint32_t>(ctl & 0x000FU) << 16U) |
                       (static_cast<std::uint32_t>(r[2]) << 8U) | r[3];
        v.loop_start = static_cast<std::uint16_t>((static_cast<std::uint16_t>(r[4]) << 8U) | r[5]);
        v.loop_end = static_cast<std::uint16_t>((static_cast<std::uint16_t>(r[6]) << 8U) | r[7]);

        v.loop_control = static_cast<std::uint8_t>((ctl >> 5U) & 0x3U);
        v.source_control = static_cast<std::uint8_t>((ctl >> 7U) & 0x3U);
        v.source_bit_ctrl = static_cast<std::uint8_t>((ctl >> 9U) & 0x3U);
        v.sample_format = static_cast<std::uint8_t>((ctl >> 4U) & 0x1U);

        v.attack_rate = static_cast<std::uint8_t>(eg0 & 0x1FU);
        v.eg_hold = static_cast<std::uint8_t>((eg0 >> 5U) & 0x1U);
        v.decay1_rate = static_cast<std::uint8_t>((eg0 >> 6U) & 0x1FU);
        v.decay2_rate = static_cast<std::uint8_t>((eg0 >> 11U) & 0x1FU);
        v.release_rate = static_cast<std::uint8_t>(eg1 & 0x1FU);
        v.decay_level = static_cast<std::uint8_t>((eg1 >> 5U) & 0x1FU);
        v.key_rate_scale = static_cast<std::uint8_t>((eg1 >> 10U) & 0x0FU);
        v.loop_link = static_cast<std::uint8_t>((eg1 >> 14U) & 0x1U);

        v.total_level = r[0x0D];
        v.reverse_dir = static_cast<std::uint8_t>(r[0x0C] & 0x1U);
        v.loop_inhibit = static_cast<std::uint8_t>((r[0x0C] >> 1U) & 0x1U);

        v.pitch = static_cast<std::uint16_t>(((static_cast<std::uint16_t>(r[0x10]) & 0x7FU) << 8U) |
                                             r[0x11]);

        v.lfo_reset = static_cast<std::uint8_t>((r[0x12] >> 7U) & 1U);
        v.lfo_frequency = static_cast<std::uint8_t>((r[0x12] >> 2U) & 0x1FU);
        v.lfo_pitch_wave = static_cast<std::uint8_t>(r[0x12] & 0x3U);
        v.lfo_pitch_sens = static_cast<std::uint8_t>((r[0x13] >> 5U) & 0x7U);
        v.lfo_amp_wave = static_cast<std::uint8_t>((r[0x13] >> 3U) & 0x3U);
        v.lfo_amp_sens = static_cast<std::uint8_t>(r[0x13] & 0x7U);

        v.input_select = static_cast<std::uint8_t>((r[0x15] >> 3U) & 0x0FU);
        v.input_mix_level = static_cast<std::uint8_t>(r[0x15] & 0x7U);

        v.direct_send_level = static_cast<std::uint8_t>((r[0x16] >> 5U) & 0x7U);
        v.direct_pan = static_cast<std::uint8_t>(r[0x16] & 0x1FU);
        v.effect_send_level = static_cast<std::uint8_t>((r[0x17] >> 5U) & 0x7U);
        v.effect_pan = static_cast<std::uint8_t>(r[0x17] & 0x1FU);

        (void)mod; // FM-modulation (MDL/MDXSL/MDYSL) is not modelled in this port.
    }

    void scsp::key_on_kyonex() noexcept {
        for (int index = 0; index < slot_count; ++index) {
            const std::uint8_t b0 = regs_[static_cast<std::size_t>(index) * slot_reg_stride];
            const bool kyonb = (b0 & 0x08U) != 0U;
            slot& v = slots_[static_cast<std::size_t>(index)];
            if (kyonb) {
                v.active = true;
                v.env_state = 0U;
                v.env_level = 0x280U;
                v.env_sample_counter = 0U;
                if (v.loop_control == 2U) {
                    v.sample_pos = v.loop_end;
                    v.sample_dir = -1;
                } else {
                    v.sample_pos = 0U;
                    v.sample_dir = +1;
                }
                v.sample_pos_frac = 0U;
                v.lfo_phase = 0U;
            } else if (v.active) {
                v.env_state = 3U;
                v.env_sample_counter = 0U;
            }
        }
    }

    std::uint8_t scsp::read_reg(std::uint16_t offset) const noexcept {
        if (offset >= slot_reg_window) {
            return 0xFFU; // outside the modelled slot window -- open bus
        }
        return regs_[offset];
    }

    void scsp::write_reg(std::uint16_t offset, std::uint8_t value) noexcept {
        if (offset >= slot_reg_window) {
            return;
        }
        regs_[offset] = value;
        const int index = static_cast<int>(offset >> 5U);
        decode_slot(index);
        // A write to a slot's byte $00 with KYONEX (bit 4) set is the edge-trigger
        // that rescans every slot and keys voices on/off per their KYONB bit.
        if ((offset & 0x1FU) == 0U &&
            (regs_[static_cast<std::size_t>(index) * slot_reg_stride] & 0x10U) != 0U) {
            key_on_kyonex();
            regs_[static_cast<std::size_t>(index) * slot_reg_stride] &=
                static_cast<std::uint8_t>(~0x10U);
        }
    }

    std::int16_t scsp::fetch_sample(const slot& v) const noexcept {
        std::int16_t sample = 0;
        std::uint32_t addr = v.start_addr;
        if (v.sample_format != 0U) {
            addr = (addr + v.sample_pos) & (waveram_size - 1U);
        } else {
            addr = (addr + (v.sample_pos * 2U)) & (waveram_size - 1U);
        }

        if (v.source_control == 1U) {
            // Noise source -- hashed from the phase + position.
            std::uint32_t x = v.lfo_phase ^ (v.sample_pos * 0x45D9F3BU);
            x ^= x >> 16U;
            x *= 0x45D9F3BU;
            x ^= x >> 16U;
            sample = static_cast<std::int16_t>(static_cast<std::int32_t>((x & 0xFFFFU) ^ 0x8000U) -
                                               0x8000);
        } else if (v.source_control >= 2U) {
            sample = 0;
        } else if (v.sample_format != 0U) {
            // PCM8 -- signed 8-bit, expanded to 16-bit.
            const auto s = static_cast<std::int8_t>((*waveram_)[addr]);
            sample = static_cast<std::int16_t>(s << 8);
        } else {
            // PCM16 -- big-endian 16-bit signed in wave RAM.
            const std::uint32_t addr2 = (addr + 1U) & (waveram_size - 1U);
            const auto u = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>((*waveram_)[addr]) << 8U) | (*waveram_)[addr2]);
            sample = static_cast<std::int16_t>(u);
        }

        static constexpr std::array<std::uint16_t, 4> sample_xor = {0x0000U, 0x7FFFU, 0x8000U,
                                                                    0xFFFFU};
        auto bits = static_cast<std::uint16_t>(sample);
        bits ^= sample_xor[v.source_bit_ctrl & 0x3U];
        return static_cast<std::int16_t>(bits);
    }

    void scsp::voice_tick(slot& v) const noexcept {
        if (!v.active) {
            return;
        }
        // EGHOLD: attack produces an instant peak and holds there until release.
        if (v.env_state == 0U && v.eg_hold != 0U) {
            v.env_level = 0U;
            return;
        }

        std::uint8_t rate = 0U;
        switch (v.env_state) {
        case 0U:
            rate = v.attack_rate;
            break;
        case 1U:
            rate = v.decay1_rate;
            break;
        case 2U:
            rate = v.decay2_rate;
            break;
        case 3U:
            rate = v.release_rate;
            break;
        default:
            break;
        }

        // Key Rate Scaling -- higher pitch = faster envelope progression. KRS=0
        // and KRS=$F leave the raw rate untouched.
        if (v.key_rate_scale != 0xFU && v.key_rate_scale != 0U) {
            int eff = static_cast<int>(rate) + static_cast<int>(v.key_rate_scale) - 8;
            if (eff < 0) {
                eff = 0;
            }
            if (eff > 0x1F) {
                eff = 0x1F;
            }
            rate = static_cast<std::uint8_t>(eff);
        }

        const std::uint16_t step_period = adsr_rate_table[rate & 0x1FU];
        if (step_period == 0U) {
            return; // rate 0 / infinite period: envelope stays
        }

        ++v.env_sample_counter;
        if (v.env_sample_counter < step_period) {
            return;
        }
        v.env_sample_counter = 0U;

        if (v.env_state == 0U) {
            // Attack -- exponential delta = max(1, level >> 5).
            auto delta = static_cast<std::uint16_t>(v.env_level >> 5U);
            if (delta == 0U) {
                delta = 1U;
            }
            if (v.env_level > delta) {
                v.env_level = static_cast<std::uint16_t>(v.env_level - delta);
            } else {
                v.env_level = 0U;
                v.env_state = 1U;
                v.env_sample_counter = 0U;
            }
        } else if (v.env_state == 1U) {
            // Decay 1 -- linear rise toward decay_level.
            const auto dl =
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(v.decay_level) << 5U);
            if (v.env_level < dl) {
                v.env_level = static_cast<std::uint16_t>(v.env_level + 0x10U);
                if (v.env_level >= dl) {
                    v.env_state = 2U;
                    v.env_sample_counter = 0U;
                }
            } else {
                v.env_state = 2U;
                v.env_sample_counter = 0U;
            }
        } else if (v.env_state == 2U) {
            // Decay 2 / sustain -- slow linear rise to silence.
            if (v.env_level < 0x3C0U) {
                v.env_level = static_cast<std::uint16_t>(v.env_level + 0x4U);
                if (v.env_level >= 0x3C0U) {
                    v.env_level = 0x3FFU;
                    v.active = false;
                }
            } else {
                v.env_level = 0x3FFU;
                v.active = false;
            }
        } else {
            // Release -- faster linear rise to silence.
            if (v.env_level < 0x3C0U) {
                v.env_level = static_cast<std::uint16_t>(v.env_level + 0x20U);
                if (v.env_level >= 0x3C0U) {
                    v.env_level = 0x3FFU;
                    v.active = false;
                }
            } else {
                v.env_level = 0x3FFU;
                v.active = false;
            }
        }
    }

    void scsp::voice_step_pos(slot& v) const noexcept {
        if (!v.active) {
            return;
        }

        // LFO phase advance (one step per native sample).
        std::uint32_t lfo_cycle = lfo_step_table[v.lfo_frequency & 0x1FU];
        if (lfo_cycle == 0U) {
            lfo_cycle = 1U;
        }
        v.lfo_phase = (v.lfo_phase + 0x10000U / lfo_cycle) & 0xFFFFU;
        if (v.lfo_reset != 0U) {
            v.lfo_phase = 0U;
        }

        // Pitch-LFO modulation of the FNS portion.
        std::uint16_t pitch = v.pitch;
        if (v.lfo_pitch_sens != 0U) {
            const std::int32_t plfo = lfo_sample(v.lfo_pitch_wave, v.lfo_phase);
            const std::int32_t offset = (plfo * v.lfo_pitch_sens) >> 9;
            std::int32_t fns = static_cast<std::int32_t>(pitch & 0x7FFU) + offset;
            if (fns < 0) {
                fns = 0;
            }
            if (fns > 0x7FF) {
                fns = 0x7FF;
            }
            pitch = static_cast<std::uint16_t>((pitch & 0xF800U) |
                                               (static_cast<std::uint32_t>(fns) & 0x7FFU));
        }

        const std::uint32_t inc = phase_increment(pitch);
        v.sample_pos_frac += inc;
        std::uint32_t step = v.sample_pos_frac >> 16U;
        v.sample_pos_frac &= 0xFFFFU;

        if (v.sample_dir >= 0) {
            // Forward motion.
            v.sample_pos += step;
            if (v.sample_pos >= v.loop_end) {
                if (v.loop_control == 0U) {
                    v.active = false;
                    v.env_level = 0x3FFU;
                } else if (v.loop_control == 3U) {
                    // Alt: bounce off LEA; direction flips to reverse.
                    std::uint32_t over = v.sample_pos - v.loop_end;
                    const std::uint32_t span = static_cast<std::uint32_t>(v.loop_end) -
                                               static_cast<std::uint32_t>(v.loop_start);
                    if (span > 0U && over >= span) {
                        over %= span;
                    }
                    v.sample_pos = (v.loop_end > over) ? (v.loop_end - over) : v.loop_start;
                    v.sample_dir = -1;
                } else {
                    std::uint32_t span = static_cast<std::uint32_t>(v.loop_end) - v.loop_start;
                    if (span == 0U) {
                        span = 1U;
                    }
                    v.sample_pos = v.loop_start + ((v.sample_pos - v.loop_start) % span);
                }
            }
        } else {
            // Reverse motion -- decrement, handling underflow below 0.
            if (step > v.sample_pos) {
                step -= v.sample_pos;
                v.sample_pos = 0U;
                if (v.loop_control == 3U) {
                    v.sample_pos = v.loop_start + step;
                    v.sample_dir = +1;
                } else if (v.loop_control == 2U) {
                    std::uint32_t span = static_cast<std::uint32_t>(v.loop_end) - v.loop_start;
                    if (span == 0U) {
                        span = 1U;
                    }
                    v.sample_pos = v.loop_end - (step % span);
                } else {
                    v.active = false;
                    v.env_level = 0x3FFU;
                }
            } else {
                v.sample_pos -= step;
                if (v.sample_pos < v.loop_start) {
                    if (v.loop_control == 3U) {
                        std::uint32_t under = v.loop_start - v.sample_pos;
                        const std::uint32_t span = static_cast<std::uint32_t>(v.loop_end) -
                                                   static_cast<std::uint32_t>(v.loop_start);
                        if (span > 0U && under >= span) {
                            under %= span;
                        }
                        v.sample_pos = v.loop_start + under;
                        v.sample_dir = +1;
                    } else if (v.loop_control == 2U) {
                        std::uint32_t span = static_cast<std::uint32_t>(v.loop_end) - v.loop_start;
                        if (span == 0U) {
                            span = 1U;
                        }
                        const std::uint32_t under = v.loop_start - v.sample_pos;
                        v.sample_pos = v.loop_end - (under % span);
                    } else {
                        v.active = false;
                        v.env_level = 0x3FFU;
                    }
                }
            }
        }
    }

    void scsp::step() noexcept {
        std::int32_t acc_l = 0;
        std::int32_t acc_r = 0;

        for (slot& v : slots_) {
            voice_step_pos(v);
            voice_tick(v);
        }

        for (slot& v : slots_) {
            if (!v.active) {
                continue;
            }
            std::int16_t s = fetch_sample(v);

            // SDIR outputs raw slot data, bypassing EG/TL/ALFO.
            if (v.reverse_dir == 0U && v.lfo_amp_sens != 0U) {
                const std::int32_t alfo = lfo_sample(v.lfo_amp_wave, v.lfo_phase);
                std::int32_t gain = 0x8000 + ((alfo * v.lfo_amp_sens) >> 7);
                if (gain < 0) {
                    gain = 0;
                }
                if (gain > 0xFFFF) {
                    gain = 0xFFFF;
                }
                s = static_cast<std::int16_t>((static_cast<std::int32_t>(s) * gain) >> 15);
            }

            std::int32_t scaled = s;
            if (v.reverse_dir == 0U) {
                // Envelope (0..0x3FF -> full-volume .. silent).
                const std::uint16_t env = env_to_linear(v.env_level);
                scaled = (static_cast<std::int32_t>(s) * env) >> 15;
                // Total level (TL, 7-bit) reuses the envelope LUT at index TL*4.
                if (v.total_level != 0U) {
                    const std::uint16_t tl_mult =
                        env_to_linear(static_cast<std::uint16_t>(v.total_level << 2U));
                    scaled = (scaled * tl_mult) >> 15;
                }
            }

            // Direct-send level (3-bit dB LUT).
            const std::uint16_t disdl_mult = disdl_table[v.direct_send_level & 7U];
            const std::int32_t dsl = (scaled * disdl_mult) >> 15;

            // Pan (4-bit dB LUT) applied to one channel; bit 4 selects the side.
            const std::uint8_t pan_side = (v.direct_pan >> 4U) & 1U;
            const std::uint8_t pan_att = v.direct_pan & 0xFU;
            const std::uint16_t pan_mult = dipan_table[pan_att];
            std::int32_t left = dsl;
            std::int32_t right = dsl;
            if (pan_side == 0U) {
                right = (right * pan_mult) >> 15;
            } else {
                left = (left * pan_mult) >> 15;
            }

            acc_l += left;
            acc_r += right;
        }

        last_left_ = static_cast<std::int16_t>(clamp16(acc_l));
        last_right_ = static_cast<std::int16_t>(clamp16(acc_r));
    }

    void scsp::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            step();
            buf_lr[i * 2U] = last_left_;
            buf_lr[i * 2U + 1U] = last_right_;
        }
    }

    void scsp::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (++prescaler_ >= clock_divider_) {
                prescaler_ = 0;
                step();
                if (audio_capture_) {
                    sample_queue_.push_back(last_left_);
                    sample_queue_.push_back(last_right_);
                }
            }
        }
    }

    std::size_t scsp::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void scsp::reset(reset_kind kind) {
        regs_ = {};
        slots_ = {}; // each slot's sample_dir default-initialises to +1
        last_left_ = 0;
        last_right_ = 0;
        prescaler_ = 0;
        // Wave RAM survives a hard/soft reset on real hardware; only a cold
        // power-on clears it.
        if (kind == reset_kind::power_on) {
            waveram_->fill(0U);
        }
        sample_queue_.clear();
    }

    void scsp::save_state(state_writer& writer) const {
        writer.bytes(regs_);
        for (const auto& v : slots_) {
            writer.u32(v.start_addr);
            writer.u16(v.loop_start);
            writer.u16(v.loop_end);
            writer.u16(v.pitch);
            writer.u8(v.loop_control);
            writer.u8(v.sample_format);
            writer.u8(v.source_control);
            writer.u8(v.source_bit_ctrl);
            writer.u8(v.eg_hold);
            writer.u8(v.total_level);
            writer.u8(v.attack_rate);
            writer.u8(v.decay1_rate);
            writer.u8(v.decay2_rate);
            writer.u8(v.release_rate);
            writer.u8(v.decay_level);
            writer.u8(v.key_rate_scale);
            writer.u8(v.loop_link);
            writer.u8(v.lfo_reset);
            writer.u8(v.lfo_frequency);
            writer.u8(v.lfo_pitch_wave);
            writer.u8(v.lfo_pitch_sens);
            writer.u8(v.lfo_amp_wave);
            writer.u8(v.lfo_amp_sens);
            writer.u8(v.direct_send_level);
            writer.u8(v.direct_pan);
            writer.u8(v.effect_send_level);
            writer.u8(v.effect_pan);
            writer.u8(v.input_select);
            writer.u8(v.input_mix_level);
            writer.u8(v.reverse_dir);
            writer.u8(v.loop_inhibit);
            writer.boolean(v.active);
            writer.u8(v.env_state);
            writer.u16(v.env_level);
            writer.u16(v.env_sample_counter);
            writer.u32(v.sample_pos_frac);
            writer.u32(v.sample_pos);
            writer.u8(static_cast<std::uint8_t>(v.sample_dir));
            writer.u32(v.lfo_phase);
        }
        writer.bytes(*waveram_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
        writer.u16(static_cast<std::uint16_t>(last_left_));
        writer.u16(static_cast<std::uint16_t>(last_right_));
    }

    void scsp::load_state(state_reader& reader) {
        reader.bytes(regs_);
        for (auto& v : slots_) {
            v.start_addr = reader.u32();
            v.loop_start = reader.u16();
            v.loop_end = reader.u16();
            v.pitch = reader.u16();
            v.loop_control = reader.u8();
            v.sample_format = reader.u8();
            v.source_control = reader.u8();
            v.source_bit_ctrl = reader.u8();
            v.eg_hold = reader.u8();
            v.total_level = reader.u8();
            v.attack_rate = reader.u8();
            v.decay1_rate = reader.u8();
            v.decay2_rate = reader.u8();
            v.release_rate = reader.u8();
            v.decay_level = reader.u8();
            v.key_rate_scale = reader.u8();
            v.loop_link = reader.u8();
            v.lfo_reset = reader.u8();
            v.lfo_frequency = reader.u8();
            v.lfo_pitch_wave = reader.u8();
            v.lfo_pitch_sens = reader.u8();
            v.lfo_amp_wave = reader.u8();
            v.lfo_amp_sens = reader.u8();
            v.direct_send_level = reader.u8();
            v.direct_pan = reader.u8();
            v.effect_send_level = reader.u8();
            v.effect_pan = reader.u8();
            v.input_select = reader.u8();
            v.input_mix_level = reader.u8();
            v.reverse_dir = reader.u8();
            v.loop_inhibit = reader.u8();
            v.active = reader.boolean();
            v.env_state = reader.u8();
            v.env_level = reader.u16();
            v.env_sample_counter = reader.u16();
            v.sample_pos_frac = reader.u32();
            v.sample_pos = reader.u32();
            v.sample_dir = static_cast<std::int8_t>(reader.u8());
            v.lfo_phase = reader.u32();
        }
        reader.bytes(*waveram_);
        clock_divider_ = static_cast<int>(reader.u32());
        prescaler_ = static_cast<int>(reader.u32());
        last_left_ = static_cast<std::int16_t>(reader.u16());
        last_right_ = static_cast<std::int16_t>(reader.u16());
    }

    instrumentation::ichip_introspection& scsp::introspection() noexcept { return introspection_; }

    // ---- audio sample extraction ----

    std::span<const instrumentation::sample_view> scsp::audio_source_impl::samples() const {
        // The chip's native mix rate (~44.1 kHz). The stored waveform's natural
        // playback rate; per-slot pitch (OCT/FNS) is a playback parameter, not a
        // property of the sample.
        constexpr std::uint32_t native_rate = 44100U;

        const auto& wram = *owner_->waveram_;

        struct meta final {
            std::uint32_t start;
            std::size_t off;
            std::size_t len;
            int loop;
            std::uint8_t channels;
        };
        std::vector<meta> metas;

        pcm_.clear();
        // A sample is a wave-RAM region, not a slot: several slots may share one.
        // Decode the distinct regions the 32 slots reference (deduped by start
        // address) rather than 32 near-duplicates.
        for (int si = 0; si < scsp::slot_count; ++si) {
            const slot& v = owner_->slots_[static_cast<std::size_t>(si)];
            const std::uint32_t start = v.start_addr & (scsp::waveram_size - 1U);
            const bool seen = std::any_of(metas.begin(), metas.end(),
                                          [start](const meta& m) { return m.start == start; });
            if (seen || v.loop_end == 0U) {
                continue;
            }
            // PCM8 = 1 byte/frame, PCM16 = 2 bytes/frame (big-endian in wave RAM).
            // The slot plays from its start to LEA (loop end, in frames).
            const bool pcm8 = v.sample_format != 0U;
            const std::size_t frames = v.loop_end;
            const std::size_t off = pcm_.size();
            std::size_t produced = 0U;
            for (std::size_t f = 0; f < frames; ++f) {
                std::int16_t sample = 0;
                if (pcm8) {
                    const std::uint32_t a =
                        (start + static_cast<std::uint32_t>(f)) & (scsp::waveram_size - 1U);
                    sample = static_cast<std::int16_t>(static_cast<std::int8_t>(wram[a]) << 8);
                } else {
                    const std::uint32_t a =
                        (start + static_cast<std::uint32_t>(f) * 2U) & (scsp::waveram_size - 1U);
                    const std::uint32_t a2 = (a + 1U) & (scsp::waveram_size - 1U);
                    sample = static_cast<std::int16_t>((static_cast<std::uint16_t>(wram[a]) << 8U) |
                                                       wram[a2]);
                }
                pcm_.push_back(sample);
                ++produced;
            }
            if (produced == 0U) {
                continue;
            }
            int loop = -1;
            if (v.loop_control != 0U && v.loop_start < v.loop_end) {
                loop = static_cast<int>(v.loop_start);
            }
            metas.push_back(
                {.start = start, .off = off, .len = produced, .loop = loop, .channels = 1U});
        }

        // names_ is reserved up front so the string_views the samples hold into it
        // never dangle on reallocation; pcm_ is complete, so its spans are stable.
        names_.clear();
        names_.reserve(metas.size());
        samples_.clear();
        samples_.reserve(metas.size());
        for (std::size_t i = 0; i < metas.size(); ++i) {
            std::array<char, 20> buf{};
            std::snprintf(buf.data(), buf.size(), "sample_%05x", metas[i].start);
            names_.emplace_back(buf.data());
            samples_.push_back(instrumentation::sample_view{
                .name = names_[i],
                .frames = std::span<const std::int16_t>(pcm_).subspan(metas[i].off, metas[i].len),
                .sample_rate = native_rate,
                .channels = metas[i].channels,
                .loop_start = metas[i].loop,
                .source_addr = metas[i].start});
        }
        return samples_;
    }

    std::span<const register_descriptor> scsp::register_snapshot() noexcept {
        using fmt = register_value_format;
        // Surface slot 0's decoded state -- the same per-voice introspection the
        // PCM family exposes for its selected voice.
        const slot& v = slots_[0];
        register_view_[0] = {"SA", v.start_addr, 20U, fmt::unsigned_integer};
        register_view_[1] = {"LSA", v.loop_start, 16U, fmt::unsigned_integer};
        register_view_[2] = {"LEA", v.loop_end, 16U, fmt::unsigned_integer};
        register_view_[3] = {"PITCH", v.pitch, 16U, fmt::unsigned_integer};
        register_view_[4] = {"LPCTL", v.loop_control, 2U, fmt::unsigned_integer};
        register_view_[5] = {"TL", v.total_level, 8U, fmt::unsigned_integer};
        register_view_[6] = {"ENV", v.env_level, 10U, fmt::unsigned_integer};
        register_view_[7] = {"POS", v.sample_pos, 16U, fmt::unsigned_integer};
        register_view_[8] = {"DISDL", v.direct_send_level, 3U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto scsp_registration =
            register_factory("yamaha.scsp", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<scsp>(); });
    } // namespace

} // namespace mnemos::chips::audio
