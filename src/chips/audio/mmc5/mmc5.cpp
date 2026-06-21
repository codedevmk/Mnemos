#include "mmc5.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {

    namespace {
        // 5-bit length-load index -> length-counter value (the standard NES table,
        // shared with the 2A03).
        constexpr std::array<std::uint8_t, 32> k_length_table = {
            10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
            12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};

        // Per-duty 8-step square sequence (1 = high). Indexed [duty][step].
        constexpr std::array<std::array<std::uint8_t, 8>, 4> k_duty_table = {{
            {0, 1, 0, 0, 0, 0, 0, 0}, // 12.5%
            {0, 1, 1, 0, 0, 0, 0, 0}, // 25%
            {0, 1, 1, 1, 1, 0, 0, 0}, // 50%
            {1, 0, 0, 1, 1, 1, 1, 1}, // 25% negated
        }};

        constexpr std::int32_t k_channel_peak = 1500;
        // CPU cycles per 240 Hz envelope/length tick (NTSC 1.789773 MHz; MMC5 is an
        // NTSC-only part).
        constexpr int k_frame_period = 7457;

        [[nodiscard]] std::int16_t clip16(int v) noexcept {
            return static_cast<std::int16_t>(std::clamp(v, -32768, 32767));
        }
    } // namespace

    chip_metadata mmc5::metadata() const noexcept {
        return {
            .manufacturer = "Nintendo",
            .part_number = "MMC5",
            .family = "MMC",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void mmc5::decode_pulse(pulse_channel& p) noexcept {
        p.duty = static_cast<std::uint8_t>((p.r0 >> 6U) & 0x03U);
        p.length_halt = (p.r0 & 0x20U) != 0U;
        p.constant_volume = (p.r0 & 0x10U) != 0U;
        p.volume_env = static_cast<std::uint8_t>(p.r0 & 0x0FU);
        p.timer = static_cast<std::uint16_t>((p.r2 & 0xFFU) |
                                             (static_cast<std::uint16_t>(p.r3 & 0x07U) << 8U));
        p.length_load = static_cast<std::uint8_t>((p.r3 >> 3U) & 0x1FU);
    }

    void mmc5::write_reg(std::uint16_t addr, std::uint8_t value) noexcept {
        switch (addr) {
        case 0x5000U:
            pulse_[0].r0 = value;
            decode_pulse(pulse_[0]);
            break;
        case 0x5001U: // no sweep unit
            break;
        case 0x5002U:
            pulse_[0].r2 = value;
            decode_pulse(pulse_[0]);
            break;
        case 0x5003U:
            pulse_[0].r3 = value;
            decode_pulse(pulse_[0]);
            if (pulse_[0].enabled) {
                pulse_[0].length_counter = k_length_table[pulse_[0].length_load & 0x1FU];
            }
            pulse_[0].sequence_step = 0U; // a timer-high write restarts the duty phase
            pulse_[0].env_start = true;
            break;
        case 0x5004U:
            pulse_[1].r0 = value;
            decode_pulse(pulse_[1]);
            break;
        case 0x5005U:
            break;
        case 0x5006U:
            pulse_[1].r2 = value;
            decode_pulse(pulse_[1]);
            break;
        case 0x5007U:
            pulse_[1].r3 = value;
            decode_pulse(pulse_[1]);
            if (pulse_[1].enabled) {
                pulse_[1].length_counter = k_length_table[pulse_[1].length_load & 0x1FU];
            }
            pulse_[1].sequence_step = 0U;
            pulse_[1].env_start = true;
            break;
        case 0x5010U: // PCM control (mode bit 0, IRQ-enable bit 7); IRQ not wired
            pcm_control_ = value;
            break;
        case 0x5011U: // raw 8-bit PCM DAC; $00 is the IRQ sentinel, not a level
            if (value != 0U) {
                pcm_level_ = value;
            }
            break;
        case 0x5015U: // pulse enables (bit 0 / bit 1)
            pulse_[0].enabled = (value & 0x01U) != 0U;
            pulse_[1].enabled = (value & 0x02U) != 0U;
            if (!pulse_[0].enabled) {
                pulse_[0].length_counter = 0U;
            }
            if (!pulse_[1].enabled) {
                pulse_[1].length_counter = 0U;
            }
            break;
        default:
            break;
        }
    }

    std::uint8_t mmc5::read_status() const noexcept {
        return static_cast<std::uint8_t>((pulse_[0].length_counter > 0U ? 0x01U : 0x00U) |
                                         (pulse_[1].length_counter > 0U ? 0x02U : 0x00U));
    }

    // 240 Hz tick: clock both pulse envelopes and (at the same rate, twice the 2A03
    // half-frame rate) the length counters.
    void mmc5::clock_frame() noexcept {
        for (auto& p : pulse_) {
            if (p.env_start) {
                p.env_start = false;
                p.env_decay = 15U;
                p.env_divider = p.volume_env;
            } else if (p.env_divider == 0U) {
                p.env_divider = p.volume_env;
                if (p.env_decay > 0U) {
                    --p.env_decay;
                } else if (p.length_halt) {
                    p.env_decay = 15U; // loop
                }
            } else {
                --p.env_divider;
            }
            if (!p.length_halt && p.length_counter > 0U) {
                --p.length_counter;
            }
        }
    }

    void mmc5::advance_oscillators(int cpu_cycles) noexcept {
        for (int i = 0; i < cpu_cycles; ++i) {
            if (++frame_accum_ >= k_frame_period) {
                frame_accum_ = 0;
                clock_frame();
            }
            // Pulse timers run at the APU rate -- every other CPU cycle.
            apu_half_ = !apu_half_;
            if (!apu_half_) {
                continue;
            }
            for (auto& p : pulse_) {
                if (p.timer_counter == 0U) {
                    p.timer_counter = p.timer;
                    p.sequence_step = static_cast<std::uint8_t>((p.sequence_step + 1U) & 0x07U);
                } else {
                    --p.timer_counter;
                }
            }
        }
    }

    std::int16_t mmc5::mix() const noexcept {
        std::int32_t m = 0;
        for (const auto& p : pulse_) {
            const bool gated = p.enabled && p.length_counter > 0U && p.timer >= 8U;
            if (gated && k_duty_table[p.duty][p.sequence_step] != 0U) {
                const std::int32_t vol = p.constant_volume ? p.volume_env : p.env_decay;
                m += (k_channel_peak * vol) / 15;
            }
        }
        // Raw PCM: an 8-bit DAC centred on mid-scale, up to twice a 2A03 channel.
        // An untouched ($00) channel contributes nothing.
        if (pcm_level_ != 0U) {
            m += ((static_cast<std::int32_t>(pcm_level_) - 128) * k_channel_peak) / 64;
        }
        return clip16(m);
    }

    void mmc5::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            advance_oscillators(1);
            if (++sample_prescaler_ >= clock_divider_) {
                sample_prescaler_ = 0;
                // One-pole DC blocker: the pulse + PCM DACs carry a DC offset, so
                // AC-couple the output (silence -> 0).
                const double raw = static_cast<double>(mix());
                dc_ += 0.0008 * (raw - dc_);
                const std::int16_t s = clip16(static_cast<int>(raw - dc_));
                last_left_ = s;
                last_right_ = s;
                if (audio_capture_) {
                    sample_queue_.push_back(s);
                    sample_queue_.push_back(s);
                }
            }
        }
    }

    std::size_t mmc5::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void mmc5::reset(reset_kind /*kind*/) {
        pulse_ = {};
        pcm_level_ = 0U;
        pcm_control_ = 0U;
        apu_half_ = false;
        frame_accum_ = 0;
        last_left_ = 0;
        last_right_ = 0;
        dc_ = 0.0;
        sample_prescaler_ = 0;
        sample_queue_.clear();
    }

    void mmc5::save_state(state_writer& writer) const {
        for (const auto& p : pulse_) {
            writer.u8(p.r0);
            writer.u8(p.r2);
            writer.u8(p.r3);
            writer.u16(p.timer);
            writer.u16(p.timer_counter);
            writer.u8(p.length_counter);
            writer.u8(p.sequence_step);
            writer.boolean(p.enabled);
            writer.boolean(p.env_start);
            writer.u8(p.env_divider);
            writer.u8(p.env_decay);
        }
        writer.u8(pcm_level_);
        writer.u8(pcm_control_);
        writer.boolean(apu_half_);
        writer.u32(static_cast<std::uint32_t>(frame_accum_));
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(sample_prescaler_));
    }

    void mmc5::load_state(state_reader& reader) {
        for (auto& p : pulse_) {
            p.r0 = reader.u8();
            p.r2 = reader.u8();
            p.r3 = reader.u8();
            decode_pulse(p);
            p.timer = reader.u16();
            p.timer_counter = reader.u16();
            p.length_counter = reader.u8();
            p.sequence_step = reader.u8();
            p.enabled = reader.boolean();
            p.env_start = reader.boolean();
            p.env_divider = reader.u8();
            p.env_decay = reader.u8();
        }
        pcm_level_ = reader.u8();
        pcm_control_ = reader.u8();
        apu_half_ = reader.boolean();
        frame_accum_ = static_cast<int>(reader.u32());
        clock_divider_ = static_cast<int>(reader.u32());
        sample_prescaler_ = static_cast<int>(reader.u32());
    }

    instrumentation::ichip_introspection& mmc5::introspection() noexcept { return introspection_; }

    std::span<const register_descriptor> mmc5::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"P1_TIMER", pulse_[0].timer, 11U, fmt::unsigned_integer};
        register_view_[1] = {"P1_VOL", pulse_[0].volume_env, 4U, fmt::unsigned_integer};
        register_view_[2] = {"P2_TIMER", pulse_[1].timer, 11U, fmt::unsigned_integer};
        register_view_[3] = {"P2_VOL", pulse_[1].volume_env, 4U, fmt::unsigned_integer};
        register_view_[4] = {"PCM", pcm_level_, 8U, fmt::unsigned_integer};
        register_view_[5] = {"STATUS", read_status(), 2U, fmt::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto mmc5_registration =
            register_factory("nintendo.mmc5", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<mmc5>(); });
    } // namespace

} // namespace mnemos::chips::audio
