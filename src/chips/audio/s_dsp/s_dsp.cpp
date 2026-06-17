#include "s_dsp.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {
    namespace {
        [[nodiscard]] std::int32_t clamp16(std::int32_t v) noexcept {
            if (v > 32767) {
                return 32767;
            }
            if (v < -32768) {
                return -32768;
            }
            return v;
        }

        // Sign-extend an n-bit two's-complement value held in the low bits of `v`.
        [[nodiscard]] std::int32_t sign_extend(std::int32_t v, int bits) noexcept {
            const std::int32_t m = std::int32_t{1} << (bits - 1);
            return (v ^ m) - m;
        }

        // The envelope generator advances when a 15-bit free-running counter hits a
        // per-rate target. Index 0 means "never step"; the rest map to a period.
        // Period and phase offset are the datasheet's rate table (the same values
        // every S-DSP describes); rate 0x1F steps every sample.
        constexpr std::array<std::uint16_t, 32> kEnvRatePeriod = {
            0,  2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
            64, 48,   40,   32,   24,   20,  16,  12,  10,  8,   6,   5,   4,   3,   2,  1};

        // BRR IIR reconstruction filters: new = nibble + a*p0 + b*p1, with the
        // coefficients in 1/256ths (the datasheet's fixed-point taps).
        [[nodiscard]] std::int32_t brr_filter(int filter, std::int32_t cur, std::int32_t p0,
                                              std::int32_t p1) noexcept {
            switch (filter) {
            case 0:
                return cur;
            case 1:
                return cur + p0 + ((-p0) >> 4);
            case 2:
                return cur + (p0 << 1) + ((-((p0 << 1) + p0)) >> 5) - p1 + (p1 >> 4);
            default:
                return cur + (p0 << 1) + ((-((p0 << 1) + (p0 << 2))) >> 6) - p1 +
                       (((p1 << 1) + p1) >> 4);
            }
        }
    } // namespace

    chip_metadata s_dsp::metadata() const noexcept {
        return {
            .manufacturer = "Sony",
            .part_number = "S-DSP",
            .family = "PCM",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    // ---- register decode (ported verbatim from the Emu reference) ----

    void s_dsp::decode_voice(int v) noexcept {
        const int base = v << 4;
        voice& vo = voices_[static_cast<std::size_t>(v)];
        vo.vol_l = static_cast<std::int8_t>(regs_[base + vreg_vol_l]);
        vo.vol_r = static_cast<std::int8_t>(regs_[base + vreg_vol_r]);
        vo.pitch = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(regs_[base + vreg_p_l]) |
            ((static_cast<std::uint16_t>(regs_[base + vreg_p_h]) & 0x3FU) << 8U));
        vo.srcn = regs_[base + vreg_srcn];
        vo.adsr1 = regs_[base + vreg_adsr1];
        vo.adsr2 = regs_[base + vreg_adsr2];
        vo.gain = regs_[base + vreg_gain];
        vo.use_adsr = (vo.adsr2 & 0x80U) != 0U;
    }

    void s_dsp::decode_globals() noexcept {
        mvol_l_ = static_cast<std::int8_t>(regs_[reg_mvol_l]);
        mvol_r_ = static_cast<std::int8_t>(regs_[reg_mvol_r]);
        evol_l_ = static_cast<std::int8_t>(regs_[reg_evol_l]);
        evol_r_ = static_cast<std::int8_t>(regs_[reg_evol_r]);
        efb_ = static_cast<std::int8_t>(regs_[reg_efb]);
        pmon_ = regs_[reg_pmon];
        non_ = regs_[reg_non];
        eon_ = regs_[reg_eon];
        dir_page_ = regs_[reg_dir];
        esa_page_ = regs_[reg_esa];
        edl_ = static_cast<std::uint8_t>(regs_[reg_edl] & 0x0FU);
        flg_ = regs_[reg_flg];
        for (int i = 0; i < fir_taps; ++i) {
            fir_[static_cast<std::size_t>(i)] =
                static_cast<std::int8_t>(regs_[0x0FU | static_cast<std::uint8_t>(i << 4)]);
        }
    }

    std::uint8_t s_dsp::read_reg(std::uint8_t addr) const noexcept {
        addr &= 0x7FU;
        // ENDX shadows live status rather than the stored byte.
        if (addr == reg_endx) {
            return endx_;
        }
        // ENVX / OUTX per voice: the low nibble of addr selects which.
        const auto voice_idx = static_cast<std::uint8_t>((addr >> 4U) & 0x07U);
        const auto sub = static_cast<std::uint8_t>(addr & 0x0FU);
        if (sub == vreg_envx) {
            return voices_[voice_idx].envx;
        }
        if (sub == vreg_outx) {
            return voices_[voice_idx].outx;
        }
        return regs_[addr];
    }

    void s_dsp::write_reg(std::uint8_t addr, std::uint8_t value) noexcept {
        addr &= 0x7FU;

        // ENVX / OUTX are read-only on the bus; the byte stores but the read
        // returns the live snapshot.
        const auto sub = static_cast<std::uint8_t>(addr & 0x0FU);
        if (sub == vreg_envx || sub == vreg_outx) {
            regs_[addr] = value;
            return;
        }

        // An ENDX write clears the voice-end flag register (any value).
        if (addr == reg_endx) {
            endx_ = 0U;
            regs_[addr] = 0U;
            return;
        }

        regs_[addr] = value;

        // KON / KOFF are latches -- writes arm them for the next frame.
        if (addr == reg_kon) {
            kon_pending_ = value;
            return;
        }
        if (addr == reg_koff) {
            koff_pending_ = value;
            return;
        }

        // Voice-local writes refresh the decoded voice view.
        if ((addr & 0x0FU) <= static_cast<unsigned>(vreg_gain)) {
            decode_voice(static_cast<int>((addr >> 4U) & 0x07U));
        }
        // Any other write refreshes the globals (cheap; matches the reference).
        decode_globals();
    }

    std::uint8_t s_dsp::commit_kon_koff() noexcept {
        std::uint8_t newly_active = 0U;
        // KOFF first: a voice with both KON and KOFF pending gets release.
        for (int v = 0; v < voice_count; ++v) {
            const auto mask = static_cast<std::uint8_t>(1U << v);
            if ((koff_pending_ & mask) != 0U) {
                voices_[static_cast<std::size_t>(v)].phase = env_phase::release;
            }
        }
        for (int v = 0; v < voice_count; ++v) {
            const auto mask = static_cast<std::uint8_t>(1U << v);
            if ((kon_pending_ & mask) != 0U && (koff_pending_ & mask) == 0U) {
                voice& vo = voices_[static_cast<std::size_t>(v)];
                vo.active = true;
                vo.envx = 0U;
                vo.outx = 0U;
                vo.env_level = 0;
                // ADSR voices begin in attack; a GAIN voice's level is driven by
                // env_run() each sample, but the phase still starts clean.
                vo.phase = env_phase::attack;
                // Prime BRR playback at the voice's source-directory start address.
                vo.brr_addr = dir_entry(vo.srcn, /*loop=*/false);
                vo.brr_offset = 0U;
                vo.interp_pos = 0U;
                vo.brr_prev0 = 0;
                vo.brr_prev1 = 0;
                vo.brr_end = false;
                vo.started = false;
                vo.ring = {};
                endx_ = static_cast<std::uint8_t>(endx_ & ~mask);
                newly_active |= mask;
            }
        }
        kon_pending_ = 0U;
        koff_pending_ = 0U;
        return newly_active;
    }

    std::uint16_t s_dsp::dir_entry(std::uint8_t srcn, bool loop) const noexcept {
        // Directory entry: 4 bytes per source at DIR*256 + srcn*4 -> {start, loop}.
        const std::size_t base =
            (static_cast<std::size_t>(dir_page_) << 8U) + static_cast<std::size_t>(srcn) * 4U;
        const std::size_t off = loop ? 2U : 0U;
        const std::size_t lo = (base + off) & (aram_size - 1U);
        const std::size_t hi = (base + off + 1U) & (aram_size - 1U);
        return static_cast<std::uint16_t>(aram_[lo] | (aram_[hi] << 8U));
    }

    // ---- BRR sample decode ----

    void s_dsp::brr_advance_block(voice& v, int vi) noexcept {
        // Move to the next 9-byte block, honouring the END/LOOP flags of the block
        // we just finished. brr_addr points at the header of the CURRENT block.
        const std::uint8_t header = aram_[static_cast<std::size_t>(v.brr_addr) & (aram_size - 1U)];
        const bool end = (header & 0x01U) != 0U;
        const bool loop = (header & 0x02U) != 0U;
        if (end) {
            v.brr_end = true;
            endx_ = static_cast<std::uint8_t>(endx_ | (1U << static_cast<unsigned>(vi)));
            if (loop) {
                v.brr_addr = dir_entry(v.srcn, /*loop=*/true);
            } else {
                v.active = false;
                v.phase = env_phase::release;
                v.env_level = 0;
            }
        } else {
            v.brr_addr = static_cast<std::uint16_t>((v.brr_addr + 9U) & 0xFFFFU);
        }
        v.brr_offset = 0U;
    }

    std::int16_t s_dsp::brr_decode_next(voice& v, int vi) noexcept {
        // Decode one BRR nibble into a 15-bit signed sample, advancing through the
        // current block and rolling to the next block at its end.
        if (v.brr_offset >= 16) {
            brr_advance_block(v, vi);
            if (!v.active) {
                return 0;
            }
        }
        const std::size_t hdr_addr = static_cast<std::size_t>(v.brr_addr) & (aram_size - 1U);
        const std::uint8_t header = aram_[hdr_addr];
        const int shift = static_cast<int>((header >> 4U) & 0x0FU);
        const int filter = static_cast<int>((header >> 2U) & 0x03U);

        const std::size_t byte_addr =
            (hdr_addr + 1U + (static_cast<std::size_t>(v.brr_offset) >> 1U)) & (aram_size - 1U);
        const std::uint8_t data = aram_[byte_addr];
        std::int32_t nibble = (v.brr_offset & 1U) != 0U ? static_cast<std::int32_t>(data & 0x0FU)
                                                        : static_cast<std::int32_t>(data >> 4U);
        nibble = sign_extend(nibble, 4);

        // Apply the per-block shift (shift>=13 are documented as clamping to the
        // sign bit; treat >12 as a near-zero magnitude carrying the sign).
        std::int32_t s = (shift <= 12) ? ((nibble << shift) >> 1) : ((nibble >> 3) << 11);

        s = brr_filter(filter, s, v.brr_prev0, v.brr_prev1);
        s = std::clamp(s, -32768, 32767);

        v.brr_prev1 = v.brr_prev0;
        v.brr_prev0 = s;
        ++v.brr_offset;
        return static_cast<std::int16_t>(s);
    }

    // ---- envelope generator ----

    void s_dsp::env_run(voice& v) noexcept {
        // Internal envelope is 11-bit (0..0x7FF). ENVX exposes the top 7 bits.
        constexpr std::int32_t env_max = 0x7FF;

        auto rate_step = [&](int rate) -> bool {
            if (rate == 0) {
                return false;
            }
            const std::uint16_t period = kEnvRatePeriod[static_cast<std::size_t>(rate & 0x1F)];
            if (period == 0) {
                return false;
            }
            // A per-voice free-running counter reused via env_level's low bits would
            // entangle level + timing; instead derive the tick from a shared phase
            // accumulator so the cadence is deterministic and rate-correct.
            return (env_tick_ % period) == 0U;
        };

        if (v.phase == env_phase::release) {
            // Release: linear -1/256 of full scale per sample (datasheet fixed rate).
            v.env_level -= 8;
            if (v.env_level <= 0) {
                v.env_level = 0;
                v.active = false;
            }
        } else if (v.use_adsr) {
            const int attack_rate = (v.adsr1 & 0x0FU) * 2 + 1;
            const int decay_rate = ((v.adsr1 >> 4U) & 0x07U) * 2 + 16;
            const int sustain_rate = v.adsr2 & 0x1FU;
            const int sustain_level = ((v.adsr2 >> 5U) & 0x07U);
            const std::int32_t sustain_target = ((sustain_level + 1) * env_max) >> 3;
            switch (v.phase) {
            case env_phase::attack:
                if (rate_step(attack_rate)) {
                    v.env_level += (attack_rate == 31) ? 1024 : 32;
                }
                if (v.env_level >= env_max) {
                    v.env_level = env_max;
                    v.phase = env_phase::decay;
                }
                break;
            case env_phase::decay:
                if (rate_step(decay_rate)) {
                    v.env_level -= ((v.env_level >> 8) + 1);
                }
                if (v.env_level <= sustain_target) {
                    v.env_level = sustain_target;
                    v.phase = env_phase::sustain;
                }
                break;
            case env_phase::sustain:
                if (rate_step(sustain_rate)) {
                    v.env_level -= ((v.env_level >> 8) + 1);
                }
                if (v.env_level < 0) {
                    v.env_level = 0;
                }
                break;
            default:
                break;
            }
        } else {
            // GAIN modes: bit7=0 -> direct level (low 7 bits << 4); bit7=1 ->
            // a rate-driven ramp selected by bits 6:5.
            if ((v.gain & 0x80U) == 0U) {
                v.env_level = static_cast<std::int32_t>(v.gain & 0x7FU) << 4;
            } else {
                const int mode = (v.gain >> 5U) & 0x03U;
                const int rate = v.gain & 0x1FU;
                if (rate_step(rate)) {
                    switch (mode) {
                    case 0: // linear decrease
                        v.env_level -= 32;
                        break;
                    case 1: // exponential decrease
                        v.env_level -= ((v.env_level >> 8) + 1);
                        break;
                    case 2: // linear increase
                        v.env_level += 32;
                        break;
                    default: // bent-line increase
                        v.env_level += (v.env_level < 0x600) ? 32 : 8;
                        break;
                    }
                }
            }
        }

        v.env_level = std::clamp(v.env_level, 0, env_max);
        v.envx = static_cast<std::uint8_t>((v.env_level >> 4) & 0x7FU);
    }

    // ---- per-sample synthesis ----

    void s_dsp::step() noexcept {
        // FLG.RESET silences and halts everything; the key-on latch is applied at
        // the start of every native sample (the DSP's frame boundary).
        if ((flg_ & flg_reset) != 0U) {
            for (voice& v : voices_) {
                v.active = false;
                v.env_level = 0;
                v.envx = 0U;
                v.phase = env_phase::release;
            }
            kon_pending_ = 0U;
            koff_pending_ = 0U;
            last_left_ = 0;
            last_right_ = 0;
            return;
        }

        commit_kon_koff();
        ++env_tick_;

        // Advance the noise LFSR once per sample, clocked down by the FLG divider.
        const int noise_rate = flg_ & flg_noise_clk;
        const std::uint16_t noise_period =
            kEnvRatePeriod[static_cast<std::size_t>(noise_rate & 0x1F)];
        if (noise_period != 0 && (env_tick_ % noise_period) == 0U) {
            const std::uint32_t feedback = (static_cast<std::uint32_t>(noise_lfsr_) & 1U) ^
                                           ((static_cast<std::uint32_t>(noise_lfsr_) >> 1U) & 1U);
            noise_lfsr_ = static_cast<std::uint16_t>(
                ((static_cast<std::uint32_t>(noise_lfsr_) >> 1U) | (feedback << 14U)) & 0x7FFFU);
        }
        noise_ = static_cast<std::int16_t>(
            sign_extend(static_cast<std::int32_t>(noise_lfsr_ & 0x7FFFU), 15));

        std::int32_t mix_l = 0;
        std::int32_t mix_r = 0;
        std::int32_t echo_in_l = 0;
        std::int32_t echo_in_r = 0;
        std::int32_t prev_voice_out = 0; // for pitch modulation (uses previous voice)

        for (int vi = 0; vi < voice_count; ++vi) {
            voice& v = voices_[static_cast<std::size_t>(vi)];
            const auto mask = static_cast<std::uint8_t>(1U << vi);

            if (!v.active) {
                v.outx = 0U;
                prev_voice_out = 0;
                continue;
            }

            // Prime the interpolation ring on the first sample after key-on so the
            // first output is the sample at the BRR start address: ring[2] = s[0],
            // ring[3] = s[1]; the two older taps start cleared.
            if (!v.started) {
                v.ring[0] = 0;
                v.ring[1] = 0;
                v.ring[2] = brr_decode_next(v, vi);
                v.ring[3] = brr_decode_next(v, vi);
                v.started = true;
                v.interp_pos = 0U;
            }

            // Effective pitch: optionally modulated by the previous voice's output.
            std::uint32_t pitch = v.pitch;
            if (vi != 0 && (pmon_ & mask) != 0U) {
                const std::int32_t factor = (prev_voice_out >> 5) + 0x400;
                pitch =
                    (pitch * static_cast<std::uint32_t>(std::max<std::int32_t>(0, factor))) >> 10;
                pitch = std::min<std::uint32_t>(pitch, 0x3FFFU);
            }

            // Pitch resampling. The hardware bandlimits with a 4-point Gaussian
            // ROM; this port uses a deterministic linear interpolation between the
            // two newest ring samples (ring[2] = s[n], ring[3] = s[n+1]) keyed by
            // the 12-bit accumulator fraction (a documented simplification of the
            // hardware Gaussian).
            const std::int32_t frac = v.interp_pos & 0x0FFF;
            const std::int32_t a = v.ring[2];
            const std::int32_t b = v.ring[3];
            std::int32_t out = a + (((b - a) * frac) >> 12);
            out = std::clamp(out, -32768, 32767);

            // Noise substitution: when enabled the voice plays the LFSR instead of
            // its sample (still gated by the envelope).
            std::int32_t sample = (non_ & mask) != 0U ? noise_ : out;

            // Run the envelope and apply it (11-bit level, halved into 15-bit gain).
            env_run(v);
            sample = (sample * v.env_level) >> 11;
            v.outx = static_cast<std::uint8_t>((sample >> 7) & 0xFFU);
            prev_voice_out = sample;

            // Advance the pitch accumulator; each whole step pulls one new BRR
            // sample into the ring (FIFO of 4).
            const std::uint32_t next = static_cast<std::uint32_t>(v.interp_pos) + pitch;
            const std::uint32_t steps = next >> 12U;
            for (std::uint32_t s = 0; s < steps && v.active; ++s) {
                v.ring[0] = v.ring[1];
                v.ring[1] = v.ring[2];
                v.ring[2] = v.ring[3];
                v.ring[3] = brr_decode_next(v, vi);
            }
            v.interp_pos = static_cast<std::uint16_t>(next & 0x0FFFU);

            // Per-voice L/R volume (signed 8-bit), summed into the dry mix and,
            // when EON selects the voice, into the echo input.
            const std::int32_t sl = (sample * v.vol_l) >> 7;
            const std::int32_t sr = (sample * v.vol_r) >> 7;
            mix_l += sl;
            mix_r += sr;
            if ((eon_ & mask) != 0U) {
                echo_in_l += sl;
                echo_in_r += sr;
            }
        }

        // ---- echo / FIR path ----
        std::int32_t echo_out_l = 0;
        std::int32_t echo_out_r = 0;
        const std::size_t echo_frames =
            edl_ == 0U ? 1U : (static_cast<std::size_t>(edl_) * 2048U / 4U);
        const std::size_t echo_len = std::min(echo_frames, echo_max_frames);

        // FIR over the 8 most recent delay-line frames ending at the read cursor.
        for (int t = 0; t < fir_taps; ++t) {
            const std::size_t idx = (echo_pos_ + echo_len - static_cast<std::size_t>(fir_taps) +
                                     1U + static_cast<std::size_t>(t)) %
                                    echo_len;
            echo_out_l += (echo_buffer_[idx * 2U] * fir_[static_cast<std::size_t>(t)]) >> 7;
            echo_out_r += (echo_buffer_[idx * 2U + 1U] * fir_[static_cast<std::size_t>(t)]) >> 7;
        }
        echo_out_l = clamp16(echo_out_l);
        echo_out_r = clamp16(echo_out_r);

        // Write the new echo frame: dry echo input plus feedback, unless disabled.
        if ((flg_ & flg_echo_disable) == 0U) {
            const std::int32_t fb_l = (echo_out_l * efb_) >> 7;
            const std::int32_t fb_r = (echo_out_r * efb_) >> 7;
            echo_buffer_[echo_pos_ * 2U] = static_cast<std::int16_t>(clamp16(echo_in_l + fb_l));
            echo_buffer_[echo_pos_ * 2U + 1U] =
                static_cast<std::int16_t>(clamp16(echo_in_r + fb_r));
        }
        echo_pos_ = (echo_pos_ + 1U) % echo_len;

        // ---- master mix ----
        std::int32_t left = (mix_l * mvol_l_) >> 7;
        std::int32_t right = (mix_r * mvol_r_) >> 7;
        left += (echo_out_l * evol_l_) >> 7;
        right += (echo_out_r * evol_r_) >> 7;

        if ((flg_ & flg_mute) != 0U) {
            left = 0;
            right = 0;
        }

        last_left_ = static_cast<std::int16_t>(clamp16(left));
        last_right_ = static_cast<std::int16_t>(clamp16(right));
    }

    void s_dsp::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            step();
            buf_lr[i * 2U] = last_left_;
            buf_lr[i * 2U + 1U] = last_right_;
        }
    }

    void s_dsp::tick(std::uint64_t cycles) {
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

    std::size_t s_dsp::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void s_dsp::reset(reset_kind kind) {
        regs_ = {};
        voices_ = {};
        mvol_l_ = 0;
        mvol_r_ = 0;
        evol_l_ = 0;
        evol_r_ = 0;
        efb_ = 0;
        pmon_ = 0U;
        non_ = 0U;
        eon_ = 0U;
        dir_page_ = 0U;
        esa_page_ = 0U;
        edl_ = 0U;
        endx_ = 0U;
        kon_pending_ = 0U;
        koff_pending_ = 0U;
        fir_ = {};
        noise_ = 0;
        noise_lfsr_ = 0x4000U;
        noise_counter_ = 0U;
        echo_buffer_ = {};
        echo_pos_ = 0U;
        last_left_ = 0;
        last_right_ = 0;
        prescaler_ = 0;
        env_tick_ = 0U;

        // Real hardware boots with FLG.RESET | FLG.MUTE | FLG.ECHO_DISABLE set.
        regs_[reg_flg] = flg_reset | flg_mute | flg_echo_disable;
        decode_globals();

        // Audio RAM survives a soft/hard reset; only a cold power-on clears it.
        if (kind == reset_kind::power_on) {
            aram_.fill(0U);
        }
        sample_queue_.clear();
    }

    void s_dsp::save_state(state_writer& writer) const {
        writer.bytes(regs_);
        for (const auto& v : voices_) {
            writer.u8(static_cast<std::uint8_t>(v.vol_l));
            writer.u8(static_cast<std::uint8_t>(v.vol_r));
            writer.u16(v.pitch);
            writer.u8(v.srcn);
            writer.u8(v.adsr1);
            writer.u8(v.adsr2);
            writer.u8(v.gain);
            writer.u8(v.envx);
            writer.u8(v.outx);
            writer.boolean(v.use_adsr);
            writer.boolean(v.active);
            writer.u16(v.brr_addr);
            writer.u8(v.brr_offset);
            writer.u16(v.interp_pos);
            writer.boolean(v.brr_end);
            for (const auto r : v.ring) {
                writer.u16(static_cast<std::uint16_t>(r));
            }
            writer.u32(static_cast<std::uint32_t>(v.brr_prev0));
            writer.u32(static_cast<std::uint32_t>(v.brr_prev1));
            writer.boolean(v.started);
            writer.u8(static_cast<std::uint8_t>(v.phase));
            writer.u32(static_cast<std::uint32_t>(v.env_level));
        }
        writer.u8(static_cast<std::uint8_t>(mvol_l_));
        writer.u8(static_cast<std::uint8_t>(mvol_r_));
        writer.u8(static_cast<std::uint8_t>(evol_l_));
        writer.u8(static_cast<std::uint8_t>(evol_r_));
        writer.u8(static_cast<std::uint8_t>(efb_));
        writer.u8(pmon_);
        writer.u8(non_);
        writer.u8(eon_);
        writer.u8(dir_page_);
        writer.u8(esa_page_);
        writer.u8(edl_);
        writer.u8(flg_);
        writer.u8(endx_);
        writer.u8(kon_pending_);
        writer.u8(koff_pending_);
        for (const auto f : fir_) {
            writer.u8(static_cast<std::uint8_t>(f));
        }
        writer.bytes(aram_);
        writer.u16(static_cast<std::uint16_t>(noise_));
        writer.u16(noise_lfsr_);
        writer.u16(noise_counter_);
        for (const auto e : echo_buffer_) {
            writer.u16(static_cast<std::uint16_t>(e));
        }
        writer.u32(static_cast<std::uint32_t>(echo_pos_));
        writer.u32(env_tick_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
        writer.u16(static_cast<std::uint16_t>(last_left_));
        writer.u16(static_cast<std::uint16_t>(last_right_));
    }

    void s_dsp::load_state(state_reader& reader) {
        reader.bytes(regs_);
        for (auto& v : voices_) {
            v.vol_l = static_cast<std::int8_t>(reader.u8());
            v.vol_r = static_cast<std::int8_t>(reader.u8());
            v.pitch = reader.u16();
            v.srcn = reader.u8();
            v.adsr1 = reader.u8();
            v.adsr2 = reader.u8();
            v.gain = reader.u8();
            v.envx = reader.u8();
            v.outx = reader.u8();
            v.use_adsr = reader.boolean();
            v.active = reader.boolean();
            v.brr_addr = reader.u16();
            v.brr_offset = reader.u8();
            v.interp_pos = reader.u16();
            v.brr_end = reader.boolean();
            for (auto& r : v.ring) {
                r = static_cast<std::int16_t>(reader.u16());
            }
            v.brr_prev0 = static_cast<std::int32_t>(reader.u32());
            v.brr_prev1 = static_cast<std::int32_t>(reader.u32());
            v.started = reader.boolean();
            v.phase = static_cast<env_phase>(reader.u8());
            v.env_level = static_cast<std::int32_t>(reader.u32());
        }
        mvol_l_ = static_cast<std::int8_t>(reader.u8());
        mvol_r_ = static_cast<std::int8_t>(reader.u8());
        evol_l_ = static_cast<std::int8_t>(reader.u8());
        evol_r_ = static_cast<std::int8_t>(reader.u8());
        efb_ = static_cast<std::int8_t>(reader.u8());
        pmon_ = reader.u8();
        non_ = reader.u8();
        eon_ = reader.u8();
        dir_page_ = reader.u8();
        esa_page_ = reader.u8();
        edl_ = reader.u8();
        flg_ = reader.u8();
        endx_ = reader.u8();
        kon_pending_ = reader.u8();
        koff_pending_ = reader.u8();
        for (auto& f : fir_) {
            f = static_cast<std::int8_t>(reader.u8());
        }
        reader.bytes(aram_);
        noise_ = static_cast<std::int16_t>(reader.u16());
        noise_lfsr_ = reader.u16();
        noise_counter_ = reader.u16();
        for (auto& e : echo_buffer_) {
            e = static_cast<std::int16_t>(reader.u16());
        }
        echo_pos_ = static_cast<std::size_t>(reader.u32());
        env_tick_ = reader.u32();
        clock_divider_ = static_cast<int>(reader.u32());
        prescaler_ = static_cast<int>(reader.u32());
        last_left_ = static_cast<std::int16_t>(reader.u16());
        last_right_ = static_cast<std::int16_t>(reader.u16());
    }

    instrumentation::ichip_introspection& s_dsp::introspection() noexcept { return introspection_; }

    // ---- audio sample extraction ----

    std::span<const instrumentation::sample_view> s_dsp::audio_source_impl::samples() const {
        // The chip's native sample rate (32 kHz). Per-voice pitch is a playback
        // parameter, not a property of the stored BRR sample.
        constexpr std::uint32_t native_rate = 32000U;

        pcm_.clear();
        names_.clear();
        samples_.clear();

        struct meta final {
            std::uint16_t start;
            std::size_t off;
            std::size_t len;
            int loop;
        };
        std::vector<meta> metas;

        // Decode the distinct BRR sources the 8 voices reference (deduped by SRCN).
        std::array<bool, 256> seen{};
        for (int vi = 0; vi < voice_count; ++vi) {
            const std::uint8_t srcn = owner_->voices_[static_cast<std::size_t>(vi)].srcn;
            if (seen[srcn]) {
                continue;
            }
            seen[srcn] = true;

            const std::uint32_t start = owner_->dir_entry(srcn, false);
            const std::uint32_t loop = owner_->dir_entry(srcn, true);
            const std::size_t off = pcm_.size();

            // Walk BRR blocks from `start` until the END flag (or a bounded cap so a
            // malformed directory cannot run away).
            std::int32_t p0 = 0;
            std::int32_t p1 = 0;
            std::uint32_t addr = start;
            int loop_frame = -1;
            bool done = false;
            constexpr int max_blocks = 4096;
            for (int blk = 0; blk < max_blocks && !done; ++blk) {
                if (loop_frame < 0 && addr == loop) {
                    loop_frame = static_cast<int>(pcm_.size() - off);
                }
                const std::uint8_t header =
                    owner_->aram_[static_cast<std::size_t>(addr) & (aram_size - 1U)];
                const int shift = static_cast<int>((header >> 4) & 0x0F);
                const int filter = static_cast<int>((header >> 2) & 0x03);
                done = (header & 0x01) != 0;
                for (int n = 0; n < 16; ++n) {
                    const std::size_t byte_addr =
                        (static_cast<std::size_t>(addr) + 1U + static_cast<std::size_t>(n >> 1)) &
                        (aram_size - 1U);
                    const std::uint8_t data = owner_->aram_[byte_addr];
                    std::int32_t nib = (n & 1) != 0 ? static_cast<std::int32_t>(data & 0x0F)
                                                    : static_cast<std::int32_t>(data >> 4);
                    nib = sign_extend(nib, 4);
                    std::int32_t s = (shift <= 12) ? ((nib << shift) >> 1) : ((nib >> 3) << 11);
                    s = brr_filter(filter, s, p0, p1);
                    s = std::clamp(s, -32768, 32767);
                    p1 = p0;
                    p0 = s;
                    pcm_.push_back(static_cast<std::int16_t>(s));
                }
                addr = (addr + 9U) & 0xFFFFU;
            }
            const std::size_t len = pcm_.size() - off;
            if (len == 0U) {
                continue;
            }
            metas.push_back({.start = static_cast<std::uint16_t>(start),
                             .off = off,
                             .len = len,
                             .loop = loop_frame});
        }

        names_.reserve(metas.size());
        samples_.reserve(metas.size());
        for (std::size_t i = 0; i < metas.size(); ++i) {
            std::array<char, 16> buf{};
            std::snprintf(buf.data(), buf.size(), "sample_%04x", metas[i].start);
            names_.emplace_back(buf.data());
            samples_.push_back(instrumentation::sample_view{
                .name = names_[i],
                .frames = std::span<const std::int16_t>(pcm_).subspan(metas[i].off, metas[i].len),
                .sample_rate = native_rate,
                .channels = 1,
                .loop_start = metas[i].loop,
                .source_addr = metas[i].start});
        }
        return samples_;
    }

    std::span<const register_descriptor> s_dsp::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"FLG", flg_, 8U, fmt::flags};
        register_view_[1] = {"KON", regs_[reg_kon], 8U, fmt::flags};
        register_view_[2] = {"KOFF", regs_[reg_koff], 8U, fmt::flags};
        register_view_[3] = {"ENDX", endx_, 8U, fmt::flags};
        register_view_[4] = {"MVOL_L", static_cast<std::uint8_t>(mvol_l_), 8U, fmt::signed_integer};
        register_view_[5] = {"MVOL_R", static_cast<std::uint8_t>(mvol_r_), 8U, fmt::signed_integer};
        register_view_[6] = {"EON", eon_, 8U, fmt::flags};
        register_view_[7] = {"NON", non_, 8U, fmt::flags};
        register_view_[8] = {"DIR", static_cast<std::uint64_t>(dir_page_) << 8U, 16U,
                             fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto s_dsp_registration =
            register_factory("sony.s_dsp", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<s_dsp>(); });
    } // namespace

} // namespace mnemos::chips::audio
