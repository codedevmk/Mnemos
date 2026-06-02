#include "sid_6581.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {
    namespace {

        // Per-voice register offsets (voice N base = N * 7).
        constexpr std::uint8_t voice_freq_lo = 0U;
        constexpr std::uint8_t voice_freq_hi = 1U;
        constexpr std::uint8_t voice_pw_lo = 2U;
        constexpr std::uint8_t voice_pw_hi = 3U;
        constexpr std::uint8_t voice_ctrl = 4U;
        constexpr std::uint8_t voice_ad = 5U;
        constexpr std::uint8_t voice_sr = 6U;

        // Absolute register addresses.
        constexpr std::uint8_t reg_fc_lo = 21U;
        constexpr std::uint8_t reg_fc_hi = 22U;
        constexpr std::uint8_t reg_res_filt = 23U;
        constexpr std::uint8_t reg_mode_vol = 24U;
        constexpr std::uint8_t reg_potx = 25U;
        constexpr std::uint8_t reg_poty = 26U;
        constexpr std::uint8_t reg_osc3 = 27U;
        constexpr std::uint8_t reg_env3 = 28U;

        constexpr std::uint8_t ctrl_gate = 0x01U;
        constexpr std::uint8_t ctrl_sync = 0x02U;
        constexpr std::uint8_t ctrl_ring = 0x04U;
        constexpr std::uint8_t ctrl_test = 0x08U;
        constexpr std::uint8_t ctrl_triangle = 0x10U;
        constexpr std::uint8_t ctrl_saw = 0x20U;
        constexpr std::uint8_t ctrl_pulse = 0x40U;
        constexpr std::uint8_t ctrl_noise = 0x80U;

        constexpr std::uint8_t res_filt_res_mask = 0xF0U;
        constexpr std::uint8_t res_filt_res_shift = 4U;
        constexpr std::uint8_t mode_vol_vol_mask = 0x0FU;
        constexpr std::uint8_t mode_vol_lp = 0x10U;
        constexpr std::uint8_t mode_vol_bp = 0x20U;
        constexpr std::uint8_t mode_vol_hp = 0x40U;
        constexpr std::uint8_t mode_vol_mute3 = 0x80U;

        constexpr std::uint32_t phase_mask = 0x00FFFFFFU;
        constexpr std::uint32_t phase_msb = 0x00800000U;
        constexpr std::uint32_t noise_trigger_bit = 0x00080000U; // phase bit 19
        constexpr std::uint32_t noise_seed = 0x007FFFFFU;

        constexpr std::int32_t fc_max = 2047;
        constexpr int filter_fixed_shift = 12; // Q20.12 inside the filter

        // φ2 cycles per envelope increment for each 4-bit rate code (datasheet).
        constexpr std::array<std::uint16_t, 16> k_rate_period = {
            9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126U, 3907U, 11720U, 19532U, 31251U,
        };

        [[nodiscard]] bool reg_is_write_only(std::uint8_t reg) {
            return reg <= 24U; // voice (0..20) + filter (21..24) registers
        }

        [[nodiscard]] bool reg_is_read_only(std::uint8_t reg) { return reg >= 25U && reg <= 28U; }

        [[nodiscard]] std::uint8_t exp_period_for(std::uint8_t envelope) {
            if (envelope >= 0xFFU) {
                return 1U;
            }
            if (envelope >= 0x5DU) {
                return 2U;
            }
            if (envelope >= 0x36U) {
                return 4U;
            }
            if (envelope >= 0x1AU) {
                return 8U;
            }
            if (envelope >= 0x0EU) {
                return 16U;
            }
            if (envelope >= 0x06U) {
                return 30U;
            }
            return 1U;
        }

        [[nodiscard]] std::uint16_t waveform_triangle(std::uint32_t phase, bool ring_flip) {
            std::uint32_t msb = (phase & phase_msb) != 0U ? 1U : 0U;
            if (ring_flip) {
                msb ^= 1U;
            }
            std::uint32_t bits = (phase >> 12U) & 0x07FFU;
            if (msb != 0U) {
                bits = (~bits) & 0x07FFU;
            }
            return static_cast<std::uint16_t>(bits << 1U);
        }

        [[nodiscard]] std::uint16_t waveform_sawtooth(std::uint32_t phase) {
            return static_cast<std::uint16_t>((phase >> 12U) & 0x0FFFU);
        }

        [[nodiscard]] std::uint16_t waveform_pulse(std::uint32_t phase, std::uint16_t pulse_width) {
            const auto upper = static_cast<std::uint16_t>((phase >> 12U) & 0x0FFFU);
            return upper < pulse_width ? 0x0000U : 0x0FFFU;
        }

        [[nodiscard]] std::uint16_t waveform_noise(std::uint32_t lfsr) {
            std::uint16_t out = 0U;
            if ((lfsr & (1U << 22U)) != 0U) {
                out = static_cast<std::uint16_t>(out | (1U << 11U));
            }
            if ((lfsr & (1U << 20U)) != 0U) {
                out = static_cast<std::uint16_t>(out | (1U << 10U));
            }
            if ((lfsr & (1U << 16U)) != 0U) {
                out = static_cast<std::uint16_t>(out | (1U << 9U));
            }
            if ((lfsr & (1U << 13U)) != 0U) {
                out = static_cast<std::uint16_t>(out | (1U << 8U));
            }
            if ((lfsr & (1U << 11U)) != 0U) {
                out = static_cast<std::uint16_t>(out | (1U << 7U));
            }
            if ((lfsr & (1U << 7U)) != 0U) {
                out = static_cast<std::uint16_t>(out | (1U << 6U));
            }
            if ((lfsr & (1U << 4U)) != 0U) {
                out = static_cast<std::uint16_t>(out | (1U << 5U));
            }
            if ((lfsr & (1U << 2U)) != 0U) {
                out = static_cast<std::uint16_t>(out | (1U << 4U));
            }
            return out;
        }

        // 23-bit LFSR feedback: bits 22 XOR 17 -> new bit 0.
        [[nodiscard]] std::uint32_t noise_shift(std::uint32_t lfsr) {
            const std::uint32_t feedback = ((lfsr >> 22U) ^ (lfsr >> 17U)) & 1U;
            return ((lfsr << 1U) | feedback) & noise_seed;
        }

        [[nodiscard]] std::uint8_t ring_source_of(std::uint8_t voice) {
            constexpr std::array<std::uint8_t, 3> map = {2U, 0U, 1U};
            return map[voice];
        }

        [[nodiscard]] int waveform_select_count(std::uint8_t ctrl) {
            int n = 0;
            if ((ctrl & ctrl_triangle) != 0U) {
                ++n;
            }
            if ((ctrl & ctrl_saw) != 0U) {
                ++n;
            }
            if ((ctrl & ctrl_pulse) != 0U) {
                ++n;
            }
            if ((ctrl & ctrl_noise) != 0U) {
                ++n;
            }
            return n;
        }

        [[nodiscard]] std::uint16_t combine_waveforms(sid_6581::variant variant,
                                                      std::uint16_t and_out, std::uint16_t or_out,
                                                      int select_count) {
            if (variant != sid_6581::variant::mos_8580 || select_count < 2) {
                return and_out;
            }
            // 8580 approximation: floor at the AND intersection, restore half of
            // the union bits the intersection cleared (documented, not measured).
            const auto restored = static_cast<std::uint16_t>((or_out & ~and_out) >> 1U);
            return static_cast<std::uint16_t>((and_out | restored) & 0x0FFFU);
        }

        void filter_range_for_variant(sid_6581::variant v, std::int32_t& min_hz,
                                      std::int32_t& max_hz) {
            if (v == sid_6581::variant::mos_8580) {
                min_hz = 30;
                max_hz = 12000;
            } else {
                min_hz = 220;
                max_hz = 18000;
            }
        }

    } // namespace

    chip_metadata sid_6581::metadata() const noexcept {
        return {
            .manufacturer = "MOS Technology",
            .part_number = "6581",
            .family = "SID",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void sid_6581::reset(reset_kind /*kind*/) {
        // variant_ and sample_rate_hz_ are machine configuration; preserved.
        regs_.fill(0U);
        voices_.fill(voice_state{}); // re-seeds each noise LFSR to all-ones
        filter_cutoff_ = 0U;
        filter_resonance_ = 0U;
        filter_route_ = 0U;
        filter_mode_ = 0U;
        volume_ = 0U;
        filter_lp_ = 0;
        filter_bp_ = 0;
        potx_ = 0U;
        poty_ = 0U;
        osc3_ = 0U;
        env3_ = 0U;
    }

    void sid_6581::set_sample_rate(std::int32_t rate_hz) noexcept {
        if (rate_hz > 0) {
            sample_rate_hz_ = rate_hz;
        }
    }

    void sid_6581::decode_voice(std::uint8_t voice_index) noexcept {
        voice_state& v = voices_[voice_index];
        const auto base = static_cast<std::uint8_t>(voice_index * 7U);
        v.frequency = static_cast<std::uint16_t>(
            regs_[base + voice_freq_lo] |
            (static_cast<std::uint16_t>(regs_[base + voice_freq_hi]) << 8U));
        v.pulse_width = static_cast<std::uint16_t>(
            regs_[base + voice_pw_lo] |
            (static_cast<std::uint16_t>(regs_[base + voice_pw_hi] & 0x0FU) << 8U));
        v.control = regs_[base + voice_ctrl];
        const std::uint8_t ad = regs_[base + voice_ad];
        v.attack = static_cast<std::uint8_t>((ad >> 4U) & 0x0FU);
        v.decay = static_cast<std::uint8_t>(ad & 0x0FU);
        const std::uint8_t sr = regs_[base + voice_sr];
        v.sustain = static_cast<std::uint8_t>((sr >> 4U) & 0x0FU);
        v.release = static_cast<std::uint8_t>(sr & 0x0FU);
    }

    void sid_6581::decode_filter_and_volume() noexcept {
        filter_cutoff_ = static_cast<std::uint16_t>(
            (regs_[reg_fc_lo] & 0x07U) | (static_cast<std::uint16_t>(regs_[reg_fc_hi]) << 3U));
        filter_resonance_ = static_cast<std::uint8_t>((regs_[reg_res_filt] & res_filt_res_mask) >>
                                                      res_filt_res_shift);
        filter_route_ = static_cast<std::uint8_t>(regs_[reg_res_filt] & 0x0FU);
        filter_mode_ = static_cast<std::uint8_t>(regs_[reg_mode_vol] & 0xF0U);
        volume_ = static_cast<std::uint8_t>(regs_[reg_mode_vol] & mode_vol_vol_mask);
    }

    std::uint8_t sid_6581::read(std::uint8_t address) const noexcept {
        const auto reg = static_cast<std::uint8_t>(address & 0x1FU);
        if (reg_is_write_only(reg)) {
            return 0xFFU; // write-only reads float high on the bus
        }
        return regs_[reg];
    }

    void sid_6581::write(std::uint8_t address, std::uint8_t value) noexcept {
        const auto reg = static_cast<std::uint8_t>(address & 0x1FU);
        if (reg_is_read_only(reg)) {
            return;
        }
        if (reg <= 20U && (reg % 7U) == voice_pw_hi) {
            value = static_cast<std::uint8_t>(value & 0x0FU); // PW high nibble only
        }
        regs_[reg] = value;
        if (reg <= 20U) {
            decode_voice(static_cast<std::uint8_t>(reg / 7U));
        } else if (reg <= 24U) {
            decode_filter_and_volume();
        }
    }

    void sid_6581::set_paddle_x(std::uint8_t value) noexcept {
        regs_[reg_potx] = value;
        potx_ = value;
    }

    void sid_6581::set_paddle_y(std::uint8_t value) noexcept {
        regs_[reg_poty] = value;
        poty_ = value;
    }

    void sid_6581::envelope_step(voice_state& v) noexcept {
        const bool gate_now = (v.control & ctrl_gate) != 0U;
        if (gate_now && !v.gate_prev) {
            v.phase = env_phase::attack;
            v.rate_counter = 0U;
        } else if (!gate_now && v.gate_prev) {
            if (v.phase != env_phase::idle) {
                v.phase = env_phase::release;
            }
        }
        v.gate_prev = gate_now;

        if (v.phase == env_phase::idle || v.phase == env_phase::sustain) {
            if (v.phase == env_phase::sustain) {
                const auto target = static_cast<std::uint8_t>(v.sustain * 0x11U);
                if (v.envelope > target) {
                    v.phase = env_phase::decay; // sustain dropped below the level
                }
            }
            return;
        }

        std::uint8_t rate_code = v.release;
        if (v.phase == env_phase::attack) {
            rate_code = v.attack;
        } else if (v.phase == env_phase::decay) {
            rate_code = v.decay;
        }
        const std::uint16_t period = k_rate_period[rate_code];
        v.rate_counter = static_cast<std::uint16_t>(v.rate_counter + 1U);
        if (v.rate_counter < period) {
            return;
        }
        v.rate_counter = 0U;

        if (v.phase == env_phase::attack) {
            if (v.envelope < 0xFFU) {
                ++v.envelope;
            }
            if (v.envelope == 0xFFU) {
                v.phase = env_phase::decay;
                v.exp_counter = 0U;
                v.exp_period = exp_period_for(v.envelope);
            }
            return;
        }

        v.exp_counter = static_cast<std::uint8_t>(v.exp_counter + 1U);
        if (v.exp_counter < v.exp_period) {
            return;
        }
        v.exp_counter = 0U;
        if (v.envelope > 0U) {
            --v.envelope;
        }
        v.exp_period = exp_period_for(v.envelope);

        if (v.phase == env_phase::decay) {
            const auto target = static_cast<std::uint8_t>(v.sustain * 0x11U);
            if (v.envelope <= target) {
                v.envelope = target;
                v.phase = env_phase::sustain;
            }
        } else if (v.phase == env_phase::release) {
            if (v.envelope == 0U) {
                v.phase = env_phase::idle;
            }
        }
    }

    void sid_6581::apply_noise_corruption(voice_state& v) const noexcept {
        // 6581: combining NOISE with another waveform progressively empties the
        // LFSR through the shared DAC ladder.
        if (variant_ != variant::mos_6581) {
            return;
        }
        const std::uint8_t ctrl = v.control;
        if ((ctrl & ctrl_noise) == 0U) {
            return;
        }
        if ((ctrl & (ctrl_triangle | ctrl_saw | ctrl_pulse)) == 0U) {
            return;
        }
        constexpr std::uint32_t clear_mask = (1U << 22U) | (1U << 20U) | (1U << 16U) | (1U << 13U) |
                                             (1U << 11U) | (1U << 7U) | (1U << 4U) | (1U << 2U);
        v.noise_lfsr &= ~clear_mask;
    }

    void sid_6581::waveform_step(std::uint8_t voice_index) noexcept {
        voice_state& v = voices_[voice_index];
        if ((v.control & ctrl_test) != 0U) {
            v.accumulator_prev = v.accumulator;
            v.accumulator = 0U;
            v.noise_lfsr = noise_seed;
            return;
        }

        v.accumulator_prev = v.accumulator;
        v.accumulator = (v.accumulator + v.frequency) & phase_mask;

        if ((v.control & ctrl_sync) != 0U) {
            const voice_state& src = voices_[ring_source_of(voice_index)];
            const bool src_msb_now = (src.accumulator & phase_msb) != 0U;
            const bool src_msb_prev = (src.accumulator_prev & phase_msb) != 0U;
            if (src_msb_now && !src_msb_prev) {
                v.accumulator = 0U;
            }
        }

        const bool bit19_now = (v.accumulator & noise_trigger_bit) != 0U;
        const bool bit19_prev = (v.accumulator_prev & noise_trigger_bit) != 0U;
        if (bit19_now && !bit19_prev) {
            v.noise_lfsr = noise_shift(v.noise_lfsr);
            apply_noise_corruption(v);
        }
    }

    void sid_6581::tick(std::uint64_t cycles) {
        for (std::uint64_t c = 0; c < cycles; ++c) {
            for (auto& v : voices_) {
                envelope_step(v);
            }
            for (std::uint8_t i = 0; i < voice_count; ++i) {
                waveform_step(i);
            }
            // Capture one mixed output sample per φ2 cycle when the host has
            // opted in. sample() advances the filter integrators, so this is
            // the chip's single sampling consumer -- nothing else calls it on
            // the run path. Skipped entirely when capture is off.
            if (audio_capture_) {
                sample_queue_.push_back(sample());
            }
        }
        env3_ = voices_[2].envelope;
        regs_[reg_env3] = env3_;
        osc3_ = static_cast<std::uint8_t>((waveform_output(2) >> 4U) & 0xFFU);
        regs_[reg_osc3] = osc3_;
    }

    std::size_t sid_6581::drain_samples(std::int16_t* out, std::size_t max_samples) noexcept {
        const std::size_t n = std::min(sample_queue_.size(), max_samples);
        if (n == 0U) {
            return 0U;
        }
        std::memcpy(out, sample_queue_.data(), n * sizeof(std::int16_t));
        sample_queue_.erase(sample_queue_.begin(),
                            sample_queue_.begin() + static_cast<std::ptrdiff_t>(n));
        return n;
    }

    std::uint8_t sid_6581::envelope_value(std::uint8_t voice) const noexcept {
        return voice < voice_count ? voices_[voice].envelope : std::uint8_t{0U};
    }

    sid_6581::env_phase sid_6581::envelope_phase(std::uint8_t voice) const noexcept {
        return voice < voice_count ? voices_[voice].phase : env_phase::idle;
    }

    std::uint16_t sid_6581::waveform_output(std::uint8_t voice) const noexcept {
        if (voice >= voice_count) {
            return 0U;
        }
        const voice_state& v = voices_[voice];
        const std::uint8_t ctrl = v.control;
        const bool triangle = (ctrl & ctrl_triangle) != 0U;
        const bool sawtooth = (ctrl & ctrl_saw) != 0U;
        const bool pulse = (ctrl & ctrl_pulse) != 0U;
        const bool noise = (ctrl & ctrl_noise) != 0U;
        if (!triangle && !sawtooth && !pulse && !noise) {
            return 0U;
        }

        bool ring_flip = false;
        if ((ctrl & ctrl_ring) != 0U && triangle) {
            const voice_state& src = voices_[ring_source_of(voice)];
            ring_flip = (src.accumulator & phase_msb) != 0U;
        }

        std::uint16_t and_out = 0x0FFFU;
        std::uint16_t or_out = 0x0000U;
        bool started = false;
        if (triangle) {
            const std::uint16_t w = waveform_triangle(v.accumulator, ring_flip);
            and_out = w;
            or_out = w;
            started = true;
        }
        if (sawtooth) {
            const std::uint16_t w = waveform_sawtooth(v.accumulator);
            and_out = started ? static_cast<std::uint16_t>(and_out & w) : w;
            or_out = static_cast<std::uint16_t>(or_out | w);
            started = true;
        }
        if (pulse) {
            const std::uint16_t w = waveform_pulse(v.accumulator, v.pulse_width);
            and_out = started ? static_cast<std::uint16_t>(and_out & w) : w;
            or_out = static_cast<std::uint16_t>(or_out | w);
            started = true;
        }
        if (noise) {
            const std::uint16_t w = waveform_noise(v.noise_lfsr);
            and_out = started ? static_cast<std::uint16_t>(and_out & w) : w;
            or_out = static_cast<std::uint16_t>(or_out | w);
        }

        const std::uint16_t out =
            combine_waveforms(variant_, and_out, or_out, waveform_select_count(ctrl));
        return static_cast<std::uint16_t>(out & 0x0FFFU);
    }

    std::uint32_t sid_6581::voice_phase(std::uint8_t voice) const noexcept {
        return voice < voice_count ? voices_[voice].accumulator : std::uint32_t{0U};
    }

    std::int32_t sid_6581::filter_cutoff_hz() const noexcept {
        std::int32_t min_hz = 0;
        std::int32_t max_hz = 0;
        filter_range_for_variant(variant_, min_hz, max_hz);
        const auto fc = static_cast<std::int32_t>(filter_cutoff_);
        return min_hz + (fc * (max_hz - min_hz)) / fc_max;
    }

    std::int32_t sid_6581::filter_frequency_coeff() const noexcept {
        std::int32_t min_hz = 0;
        std::int32_t max_hz = 0;
        filter_range_for_variant(variant_, min_hz, max_hz);
        const auto fc = static_cast<std::int32_t>(filter_cutoff_);
        const std::int32_t fc_hz = min_hz + (fc * (max_hz - min_hz)) / fc_max;
        std::int64_t scaled =
            (static_cast<std::int64_t>(fc_hz) << filter_fixed_shift) / sample_rate_hz_;
        scaled *= 2;
        if (scaled < 0) {
            scaled = 0;
        }
        if (scaled > (1 << filter_fixed_shift)) {
            scaled = (1 << filter_fixed_shift);
        }
        return static_cast<std::int32_t>(scaled);
    }

    std::int32_t sid_6581::filter_damping_coeff() const noexcept {
        const auto res = static_cast<std::int32_t>(filter_resonance_);
        return (1 << filter_fixed_shift) - ((res * (7 * (1 << filter_fixed_shift) / 8)) / 15);
    }

    std::int16_t sid_6581::sample() noexcept {
        std::int32_t routed_sum = 0;
        std::int32_t bypass_sum = 0;
        for (std::uint8_t i = 0; i < voice_count; ++i) {
            const voice_state& v = voices_[i];
            const auto wave = static_cast<std::int32_t>(waveform_output(i));
            const std::int32_t centred = wave - 0x800;
            const std::int32_t s = centred * static_cast<std::int32_t>(v.envelope);
            if ((filter_route_ & (1U << i)) != 0U) {
                routed_sum += s;
            } else {
                if (i == 2U && (filter_mode_ & mode_vol_mute3) != 0U) {
                    continue;
                }
                bypass_sum += s;
            }
        }

        const std::int32_t f = filter_frequency_coeff();
        const std::int32_t q = filter_damping_coeff();
        const auto bp_damped = static_cast<std::int32_t>(
            (static_cast<std::int64_t>(filter_bp_) * q) >> filter_fixed_shift);
        const std::int32_t hp = routed_sum - bp_damped - filter_lp_;
        filter_bp_ +=
            static_cast<std::int32_t>((static_cast<std::int64_t>(f) * hp) >> filter_fixed_shift);
        filter_lp_ += static_cast<std::int32_t>((static_cast<std::int64_t>(f) * filter_bp_) >>
                                                filter_fixed_shift);

        std::int32_t filter_out = 0;
        if ((filter_mode_ & mode_vol_lp) != 0U) {
            filter_out += filter_lp_;
        }
        if ((filter_mode_ & mode_vol_bp) != 0U) {
            filter_out += filter_bp_;
        }
        if ((filter_mode_ & mode_vol_hp) != 0U) {
            filter_out += hp;
        }

        const std::int32_t mixed = bypass_sum + filter_out;
        std::int32_t scaled = (mixed * static_cast<std::int32_t>(volume_)) / (15 * voice_count);
        if (scaled > 32767) {
            scaled = 32767;
        }
        if (scaled < -32768) {
            scaled = -32768;
        }
        return static_cast<std::int16_t>(scaled);
    }

    void sid_6581::save_state(state_writer& writer) const {
        writer.u8(static_cast<std::uint8_t>(variant_));
        writer.bytes(std::span<const std::uint8_t>(regs_));
        for (const voice_state& v : voices_) {
            writer.u16(v.frequency);
            writer.u16(v.pulse_width);
            writer.u8(v.control);
            writer.u8(v.attack);
            writer.u8(v.decay);
            writer.u8(v.sustain);
            writer.u8(v.release);
            writer.u8(static_cast<std::uint8_t>(v.phase));
            writer.u8(v.envelope);
            writer.boolean(v.gate_prev);
            writer.u16(v.rate_counter);
            writer.u8(v.exp_counter);
            writer.u8(v.exp_period);
            writer.u32(v.accumulator);
            writer.u32(v.accumulator_prev);
            writer.u32(v.noise_lfsr);
        }
        writer.u16(filter_cutoff_);
        writer.u8(filter_resonance_);
        writer.u8(filter_route_);
        writer.u8(filter_mode_);
        writer.u8(volume_);
        writer.u32(static_cast<std::uint32_t>(filter_lp_));
        writer.u32(static_cast<std::uint32_t>(filter_bp_));
        writer.u32(static_cast<std::uint32_t>(sample_rate_hz_));
        writer.u8(potx_);
        writer.u8(poty_);
        writer.u8(osc3_);
        writer.u8(env3_);
    }

    void sid_6581::load_state(state_reader& reader) {
        variant_ = static_cast<variant>(reader.u8());
        reader.bytes(std::span<std::uint8_t>(regs_));
        for (voice_state& v : voices_) {
            v.frequency = reader.u16();
            v.pulse_width = reader.u16();
            v.control = reader.u8();
            v.attack = reader.u8();
            v.decay = reader.u8();
            v.sustain = reader.u8();
            v.release = reader.u8();
            v.phase = static_cast<env_phase>(reader.u8());
            v.envelope = reader.u8();
            v.gate_prev = reader.boolean();
            v.rate_counter = reader.u16();
            v.exp_counter = reader.u8();
            v.exp_period = reader.u8();
            v.accumulator = reader.u32();
            v.accumulator_prev = reader.u32();
            v.noise_lfsr = reader.u32();
        }
        filter_cutoff_ = reader.u16();
        filter_resonance_ = reader.u8();
        filter_route_ = reader.u8();
        filter_mode_ = reader.u8();
        volume_ = reader.u8();
        filter_lp_ = static_cast<std::int32_t>(reader.u32());
        filter_bp_ = static_cast<std::int32_t>(reader.u32());
        sample_rate_hz_ = static_cast<std::int32_t>(reader.u32());
        potx_ = reader.u8();
        poty_ = reader.u8();
        osc3_ = reader.u8();
        env3_ = reader.u8();
    }

    instrumentation::ichip_introspection& sid_6581::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> sid_6581::register_snapshot() noexcept {
        register_view_[0] = {"V1_ENV", voices_[0].envelope, 8U,
                             register_value_format::unsigned_integer};
        register_view_[1] = {"V2_ENV", voices_[1].envelope, 8U,
                             register_value_format::unsigned_integer};
        register_view_[2] = {"V3_ENV", voices_[2].envelope, 8U,
                             register_value_format::unsigned_integer};
        register_view_[3] = {"VOL", volume_, 4U, register_value_format::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto sid_6581_registration =
            register_factory("mos.6581", chip_class::audio_synth, []() -> std::unique_ptr<ichip> {
                return std::make_unique<sid_6581>();
            });
    } // namespace

} // namespace mnemos::chips::audio
