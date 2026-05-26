#include "sn76489.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>

namespace mnemos::chips::audio {
    namespace {
        // -2 dB per step from peak 8191 (four channels stay within int16); index 15
        // is hard-muted per the datasheet.
        constexpr std::array<std::int16_t, 16> vol_table = {
            8191, 6508, 5170, 4108, 3263, 2592, 2060, 1636, 1300, 1033, 820, 652, 518, 411, 327, 0};
        // Noise rate dividers for rates 0..2 (rate 3 borrows tone channel 2).
        constexpr std::array<std::uint16_t, 3> noise_rate = {0x10U, 0x20U, 0x40U};

        constexpr double pi = 3.14159265358979323846;

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

    chip_metadata sn76489::metadata() const noexcept {
        return {
            .manufacturer = "Texas Instruments",
            .part_number = "SN76489",
            .family = "PSG",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void sn76489::write(std::uint8_t value) noexcept {
        if ((value & 0x80U) != 0U) {
            // Latch + data.
            const int ch = (value >> 5U) & 3;
            const bool is_vol = ((value >> 4U) & 1U) != 0U;
            const auto data = static_cast<std::uint8_t>(value & 0x0FU);
            latched_ch_ = static_cast<std::uint8_t>(ch);
            latched_vol_ = is_vol;
            if (is_vol) {
                volume_[static_cast<std::size_t>(ch)] = data;
            } else if (ch < 3) {
                tone_[static_cast<std::size_t>(ch)] = static_cast<std::uint16_t>(
                    (tone_[static_cast<std::size_t>(ch)] & 0x3F0U) | data);
            } else {
                noise_mode_ = static_cast<std::uint8_t>(data & 0x07U);
                lfsr_ = 0x8000U;
            }
        } else {
            // Data byte for the latched register.
            const int ch = latched_ch_;
            const auto data = static_cast<std::uint8_t>(value & 0x3FU);
            if (latched_vol_) {
                volume_[static_cast<std::size_t>(ch)] = static_cast<std::uint8_t>(data & 0x0FU);
            } else if (ch < 3) {
                tone_[static_cast<std::size_t>(ch)] = static_cast<std::uint16_t>(
                    (tone_[static_cast<std::size_t>(ch)] & 0x00FU) | (data << 4U));
            } else {
                noise_mode_ = static_cast<std::uint8_t>(data & 0x07U);
                lfsr_ = 0x8000U;
            }
        }
    }

    std::int16_t sn76489::step() noexcept {
        std::int32_t sample = 0;

        for (std::size_t ch = 0; ch < 3; ++ch) {
            if (tone_[ch] == 0U || tone_[ch] == 1U) {
                sample += vol_table[volume_[ch]]; // period 0/1 holds the output high
                continue;
            }
            counter_[ch] = static_cast<std::uint16_t>(counter_[ch] - 1U);
            if (counter_[ch] == 0U) {
                counter_[ch] = tone_[ch];
                polarity_[ch] = static_cast<std::int8_t>(-polarity_[ch]);
            }
            sample += polarity_[ch] > 0 ? vol_table[volume_[ch]]
                                        : static_cast<std::int16_t>(-vol_table[volume_[ch]]);
        }

        // Noise channel.
        const int rate = noise_mode_ & 3;
        const std::uint16_t period = rate < 3 ? noise_rate[static_cast<std::size_t>(rate)]
                                              : (tone_[2] != 0U ? tone_[2] : 1U);
        counter_[3] = static_cast<std::uint16_t>(counter_[3] - 1U);
        if (counter_[3] == 0U) {
            counter_[3] = period;
            polarity_[3] = static_cast<std::int8_t>(-polarity_[3]);
            // The LFSR clocks on the positive edge only (every other expiry).
            if (polarity_[3] > 0) {
                if ((noise_mode_ & 0x04U) != 0U) {
                    const auto fb =
                        static_cast<std::uint16_t>(((lfsr_ ^ (lfsr_ >> 3U)) & 1U) << 15U);
                    lfsr_ = static_cast<std::uint16_t>((lfsr_ >> 1U) | fb);
                } else {
                    const auto fb = static_cast<std::uint16_t>((lfsr_ & 1U) << 15U);
                    lfsr_ = static_cast<std::uint16_t>((lfsr_ >> 1U) | fb);
                }
            }
        }
        sample += (lfsr_ & 1U) != 0U ? vol_table[volume_[3]]
                                     : static_cast<std::int16_t>(-vol_table[volume_[3]]);

        sample = clamp16(sample);
        if (lp_alpha_q15_ != 0) {
            std::int32_t y = lp_state_;
            y += (lp_alpha_q15_ * (sample - y)) >> 15;
            lp_state_ = y;
            sample = y;
        }
        last_sample_ = static_cast<std::int16_t>(sample);
        return last_sample_;
    }

    void sn76489::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            if (++prescaler_ >= clock_divider_) {
                prescaler_ = 0;
                const auto s = step();
                if (audio_capture_) {
                    sample_queue_.push_back(s);
                }
            }
        }
    }

    std::size_t sn76489::drain_samples(std::int16_t* out, std::size_t max_samples) noexcept {
        const std::size_t n = std::min(sample_queue_.size(), max_samples);
        if (n == 0U) {
            return 0U;
        }
        std::memcpy(out, sample_queue_.data(), n * sizeof(std::int16_t));
        sample_queue_.erase(sample_queue_.begin(),
                            sample_queue_.begin() + static_cast<std::ptrdiff_t>(n));
        return n;
    }

    void sn76489::set_lowpass_cutoff_hz(int sample_rate_hz, int cutoff_hz) noexcept {
        if (sample_rate_hz <= 0 || cutoff_hz <= 0) {
            lp_alpha_q15_ = 0;
            lp_state_ = 0;
            return;
        }
        const double fs = static_cast<double>(sample_rate_hz);
        const double fc = static_cast<double>(cutoff_hz);
        const double rc = 1.0 / (2.0 * pi * fc);
        const double dt = 1.0 / fs;
        double alpha = dt / (dt + rc);
        if (alpha < 0.0) {
            alpha = 0.0;
        }
        if (alpha > 1.0) {
            alpha = 1.0;
        }
        auto q15 = static_cast<std::int32_t>(alpha * 32767.0 + 0.5);
        if (q15 == 0) {
            q15 = 1;
        }
        lp_alpha_q15_ = q15;
        lp_state_ = 0;
    }

    void sn76489::reset(reset_kind /*kind*/) {
        // The reset clears the digital core but leaves the analog filter config.
        const std::int32_t saved_alpha = lp_alpha_q15_;
        tone_ = {};
        counter_ = {1U, 1U, 1U, 1U};
        polarity_ = {1, 1, 1, 1};
        volume_ = {0x0FU, 0x0FU, 0x0FU, 0x0FU};
        noise_mode_ = 0U;
        lfsr_ = 0x8000U;
        latched_ch_ = 0U;
        latched_vol_ = false;
        prescaler_ = 0;
        last_sample_ = 0;
        lp_alpha_q15_ = saved_alpha;
        lp_state_ = 0;
    }

    void sn76489::save_state(state_writer& writer) const {
        for (const auto v : tone_) {
            writer.u16(v);
        }
        for (const auto v : counter_) {
            writer.u16(v);
        }
        for (const auto v : polarity_) {
            writer.u8(static_cast<std::uint8_t>(v));
        }
        for (const auto v : volume_) {
            writer.u8(v);
        }
        writer.u8(noise_mode_);
        writer.u16(lfsr_);
        writer.u8(latched_ch_);
        writer.boolean(latched_vol_);
        writer.u32(static_cast<std::uint32_t>(lp_alpha_q15_));
        writer.u32(static_cast<std::uint32_t>(lp_state_));
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
        writer.u16(static_cast<std::uint16_t>(last_sample_));
    }

    void sn76489::load_state(state_reader& reader) {
        for (auto& v : tone_) {
            v = reader.u16();
        }
        for (auto& v : counter_) {
            v = reader.u16();
        }
        for (auto& v : polarity_) {
            v = static_cast<std::int8_t>(reader.u8());
        }
        for (auto& v : volume_) {
            v = reader.u8();
        }
        noise_mode_ = reader.u8();
        lfsr_ = reader.u16();
        latched_ch_ = reader.u8();
        latched_vol_ = reader.boolean();
        lp_alpha_q15_ = static_cast<std::int32_t>(reader.u32());
        lp_state_ = static_cast<std::int32_t>(reader.u32());
        clock_divider_ = static_cast<int>(reader.u32());
        prescaler_ = static_cast<int>(reader.u32());
        last_sample_ = static_cast<std::int16_t>(reader.u16());
    }

    instrumentation::ichip_introspection& sn76489::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> sn76489::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"TONE0", tone_[0], 10U, fmt::unsigned_integer};
        register_view_[1] = {"TONE1", tone_[1], 10U, fmt::unsigned_integer};
        register_view_[2] = {"TONE2", tone_[2], 10U, fmt::unsigned_integer};
        register_view_[3] = {"VOL0", volume_[0], 4U, fmt::unsigned_integer};
        register_view_[4] = {"VOL1", volume_[1], 4U, fmt::unsigned_integer};
        register_view_[5] = {"VOL2", volume_[2], 4U, fmt::unsigned_integer};
        register_view_[6] = {"VOL3", volume_[3], 4U, fmt::unsigned_integer};
        register_view_[7] = {"NOISE", noise_mode_, 3U, fmt::unsigned_integer};
        register_view_[8] = {"LFSR", lfsr_, 16U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto sn76489_registration =
            register_factory("ti.sn76489", chip_class::audio_synth, []() -> std::unique_ptr<ichip> {
                return std::make_unique<sn76489>();
            });
    } // namespace

} // namespace mnemos::chips::audio
