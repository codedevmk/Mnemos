#include "vrc6.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {

    namespace {
        [[nodiscard]] std::int16_t clip16(int v) noexcept {
            return static_cast<std::int16_t>(std::clamp(v, -32768, 32767));
        }
    } // namespace

    chip_metadata vrc6::metadata() const noexcept {
        return {
            .manufacturer = "Konami",
            .part_number = "VRC6",
            .family = "VRC",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void vrc6::write_reg(std::uint16_t addr, std::uint8_t value) noexcept {
        const auto set_period_lo = [](std::uint16_t& p, std::uint8_t v) {
            p = static_cast<std::uint16_t>((p & 0x0F00U) | v);
        };
        const auto set_period_hi = [](std::uint16_t& p, std::uint8_t v) {
            p = static_cast<std::uint16_t>((p & 0x00FFU) |
                                           (static_cast<std::uint16_t>(v & 0x0FU) << 8U));
        };
        switch (addr & 0xF003U) {
        case 0x9000U: // pulse 1 control
            pulse_[0].volume = static_cast<std::uint8_t>(value & 0x0FU);
            pulse_[0].duty = static_cast<std::uint8_t>((value >> 4U) & 0x07U);
            pulse_[0].mode = (value & 0x80U) != 0U;
            break;
        case 0x9001U:
            set_period_lo(pulse_[0].period, value);
            break;
        case 0x9002U:
            set_period_hi(pulse_[0].period, value);
            pulse_[0].enabled = (value & 0x80U) != 0U;
            break;
        case 0x9003U: // global control
            halt_ = (value & 0x01U) != 0U;
            freq_shift_ = (value & 0x04U) != 0U ? 8U : ((value & 0x02U) != 0U ? 4U : 0U);
            break;
        case 0xA000U: // pulse 2 control
            pulse_[1].volume = static_cast<std::uint8_t>(value & 0x0FU);
            pulse_[1].duty = static_cast<std::uint8_t>((value >> 4U) & 0x07U);
            pulse_[1].mode = (value & 0x80U) != 0U;
            break;
        case 0xA001U:
            set_period_lo(pulse_[1].period, value);
            break;
        case 0xA002U:
            set_period_hi(pulse_[1].period, value);
            pulse_[1].enabled = (value & 0x80U) != 0U;
            break;
        case 0xB000U: // sawtooth accumulator rate
            saw_.rate = static_cast<std::uint8_t>(value & 0x3FU);
            break;
        case 0xB001U:
            set_period_lo(saw_.period, value);
            break;
        case 0xB002U:
            set_period_hi(saw_.period, value);
            saw_.enabled = (value & 0x80U) != 0U;
            break;
        default:
            break;
        }
    }

    void vrc6::clock_pulse(pulse_channel& p) noexcept {
        if (!p.enabled) {
            p.output = 0U;
            p.duty_step = 15U;
            return;
        }
        if (p.counter == 0U) {
            p.counter = static_cast<std::uint16_t>(p.period >> freq_shift_);
            p.duty_step = static_cast<std::uint8_t>((p.duty_step - 1U) & 0x0FU); // 15 -> 0
        } else {
            --p.counter;
        }
        // Output the volume during the high part of the duty (the low duty_step+1
        // counts), or always in mode (duty-ignore / DC) -- otherwise silence.
        p.output = (p.mode || p.duty_step <= p.duty) ? p.volume : 0U;
    }

    void vrc6::clock_saw() noexcept {
        if (!saw_.enabled) {
            saw_.output = 0U;
            saw_.accum = 0U;
            saw_.step = 0U;
            return;
        }
        if (saw_.counter == 0U) {
            saw_.counter = static_cast<std::uint16_t>(saw_.period >> freq_shift_);
            // The accumulator advances on every other divider step: seven active
            // steps per ramp -- six add the 6-bit rate, the seventh resets to zero.
            if ((saw_.step & 1U) == 0U) {
                const std::uint8_t active = static_cast<std::uint8_t>(saw_.step >> 1U); // 0..6
                if (active >= 6U) {
                    saw_.accum = 0U;
                } else {
                    saw_.accum = static_cast<std::uint8_t>(saw_.accum + saw_.rate);
                }
            }
            saw_.step = static_cast<std::uint8_t>((saw_.step + 1U) % 14U);
            saw_.output = static_cast<std::uint8_t>((saw_.accum >> 3U) & 0x1FU); // top 5 bits
        } else {
            --saw_.counter;
        }
    }

    std::int16_t vrc6::mix() const noexcept {
        // Linear 6-bit DAC: two 4-bit pulses + one 5-bit saw sum to 0..61, scaled
        // into the int16 range. The DC offset is removed by the high-pass in tick().
        const int sum = static_cast<int>(pulse_[0].output) + static_cast<int>(pulse_[1].output) +
                        static_cast<int>(saw_.output);
        return clip16(sum * 500);
    }

    void vrc6::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (!halt_) {
                clock_pulse(pulse_[0]);
                clock_pulse(pulse_[1]);
                clock_saw();
            }
            if (++sample_prescaler_ >= clock_divider_) {
                sample_prescaler_ = 0;
                // One-pole DC blocker: the DAC sum is unsigned (0..61), so subtract
                // the slow-moving average to AC-couple the output (silence -> 0).
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

    std::size_t vrc6::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void vrc6::reset(reset_kind /*kind*/) {
        pulse_ = {};
        saw_ = {};
        halt_ = false;
        freq_shift_ = 0U;
        last_left_ = 0;
        last_right_ = 0;
        dc_ = 0.0;
        sample_prescaler_ = 0;
        sample_queue_.clear();
    }

    void vrc6::save_state(state_writer& writer) const {
        for (const auto& p : pulse_) {
            writer.u16(p.period);
            writer.u16(p.counter);
            writer.u8(p.volume);
            writer.u8(p.duty);
            writer.boolean(p.mode);
            writer.boolean(p.enabled);
            writer.u8(p.duty_step);
            writer.u8(p.output);
        }
        writer.u16(saw_.period);
        writer.u16(saw_.counter);
        writer.u8(saw_.rate);
        writer.boolean(saw_.enabled);
        writer.u8(saw_.accum);
        writer.u8(saw_.step);
        writer.u8(saw_.output);
        writer.boolean(halt_);
        writer.u8(freq_shift_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(sample_prescaler_));
    }

    void vrc6::load_state(state_reader& reader) {
        for (auto& p : pulse_) {
            p.period = reader.u16();
            p.counter = reader.u16();
            p.volume = reader.u8();
            p.duty = reader.u8();
            p.mode = reader.boolean();
            p.enabled = reader.boolean();
            p.duty_step = reader.u8();
            p.output = reader.u8();
        }
        saw_.period = reader.u16();
        saw_.counter = reader.u16();
        saw_.rate = reader.u8();
        saw_.enabled = reader.boolean();
        saw_.accum = reader.u8();
        saw_.step = reader.u8();
        saw_.output = reader.u8();
        halt_ = reader.boolean();
        freq_shift_ = reader.u8();
        clock_divider_ = static_cast<int>(reader.u32());
        sample_prescaler_ = static_cast<int>(reader.u32());
    }

    instrumentation::ichip_introspection& vrc6::introspection() noexcept { return introspection_; }

    std::span<const register_descriptor> vrc6::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"P1_PERIOD", pulse_[0].period, 12U, fmt::unsigned_integer};
        register_view_[1] = {"P1_VOL", pulse_[0].volume, 4U, fmt::unsigned_integer};
        register_view_[2] = {"P2_PERIOD", pulse_[1].period, 12U, fmt::unsigned_integer};
        register_view_[3] = {"P2_VOL", pulse_[1].volume, 4U, fmt::unsigned_integer};
        register_view_[4] = {"SAW_PERIOD", saw_.period, 12U, fmt::unsigned_integer};
        register_view_[5] = {"SAW_RATE", saw_.rate, 6U, fmt::unsigned_integer};
        register_view_[6] = {"HALT", halt_ ? 1U : 0U, 1U, fmt::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto vrc6_registration =
            register_factory("konami.vrc6", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<vrc6>(); });
    } // namespace

} // namespace mnemos::chips::audio
