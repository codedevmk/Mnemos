#include "scc.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {

    namespace {
        constexpr std::uint32_t k_state_version = 1U;

        [[nodiscard]] std::int16_t clamp16(std::int32_t value) noexcept {
            if (value > 32767) {
                return 32767;
            }
            if (value < -32768) {
                return -32768;
            }
            return static_cast<std::int16_t>(value);
        }
    } // namespace

    scc::scc() {
        introspection_.with_registers([this] { return register_snapshot(); })
            .with_reg_writes([this](instrumentation::reg_write_trace::callback cb) {
                reg_write_callback_ = std::move(cb);
            });
        reset(reset_kind::power_on);
    }

    chip_metadata scc::metadata() const noexcept {
        return {
            .manufacturer = "Konami",
            .part_number = "K051649",
            .family = "SCC",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void scc::note_write(std::uint16_t port, std::uint8_t value) {
        if (reg_write_callback_) {
            reg_write_callback_({.port = port, .value = value});
        }
    }

    std::uint8_t scc::waveform(int waveform, int index) const noexcept {
        if (waveform < 0 || waveform >= waveform_count || index < 0 || index >= waveform_size) {
            return 0U;
        }
        return waveform_[static_cast<std::size_t>(waveform)][static_cast<std::size_t>(index)];
    }

    std::uint16_t scc::period(int channel) const noexcept {
        if (channel < 0 || channel >= channel_count) {
            return 0U;
        }
        return channels_[static_cast<std::size_t>(channel)].period;
    }

    std::uint8_t scc::volume(int channel) const noexcept {
        if (channel < 0 || channel >= channel_count) {
            return 0U;
        }
        return channels_[static_cast<std::size_t>(channel)].volume;
    }

    std::uint16_t scc::reload_period(const channel_state& channel) const noexcept {
        std::uint32_t reload = static_cast<std::uint32_t>(channel.period) + 1U;
        const std::uint8_t deform_rate = static_cast<std::uint8_t>(deformation_ & 0x03U);
        if (deform_rate == 1U) {
            reload = std::max<std::uint32_t>(1U, reload / 256U);
        } else if (deform_rate == 2U || deform_rate == 3U) {
            reload = std::max<std::uint32_t>(1U, reload / 16U);
        }
        return static_cast<std::uint16_t>(std::min<std::uint32_t>(reload, 0x1000U));
    }

    std::uint8_t scc::read(std::uint16_t address) noexcept {
        const std::uint8_t offset = static_cast<std::uint8_t>(address & 0x00FFU);
        if (offset < 0x80U) {
            const auto wave = static_cast<std::size_t>(offset >> 5U);
            const auto index = static_cast<std::size_t>(offset & 0x1FU);
            return waveform_[wave][index];
        }
        if (offset >= 0xE0U) {
            deformation_ |= 0x40U;
            return 0xFFU;
        }
        return 0xFFU;
    }

    void scc::write(std::uint16_t address, std::uint8_t value) noexcept {
        const std::uint8_t offset = static_cast<std::uint8_t>(address & 0x00FFU);
        note_write(static_cast<std::uint16_t>(0x9800U | offset), value);

        if (offset < 0x80U) {
            if ((deformation_ & 0x40U) == 0U) {
                const auto wave = static_cast<std::size_t>(offset >> 5U);
                const auto index = static_cast<std::size_t>(offset & 0x1FU);
                waveform_[wave][index] = value;
            }
            return;
        }

        if (offset >= 0x80U && offset < 0xA0U) {
            const std::uint8_t reg = static_cast<std::uint8_t>(offset & 0x0FU);
            if (reg < 0x0AU) {
                const auto ch = static_cast<std::size_t>(reg >> 1U);
                if ((reg & 1U) == 0U) {
                    channels_[ch].period =
                        static_cast<std::uint16_t>((channels_[ch].period & 0x0F00U) | value);
                } else {
                    channels_[ch].period = static_cast<std::uint16_t>(
                        (channels_[ch].period & 0x00FFU) |
                        (static_cast<std::uint16_t>(value & 0x0FU) << 8U));
                }
                if ((deformation_ & 0x20U) != 0U) {
                    channels_[ch].phase = 0U;
                    channels_[ch].counter = reload_period(channels_[ch]);
                }
                return;
            }
            if (reg >= 0x0AU && reg <= 0x0EU) {
                channels_[static_cast<std::size_t>(reg - 0x0AU)].volume =
                    static_cast<std::uint8_t>(value & 0x0FU);
                return;
            }
            enable_mask_ = static_cast<std::uint8_t>(value & 0x1FU);
            return;
        }

        if (offset >= 0xE0U) {
            deformation_ = value;
        }
    }

    void scc::step() noexcept {
        std::int32_t mix = 0;
        for (std::size_t i = 0; i < channels_.size(); ++i) {
            channel_state& ch = channels_[i];
            if ((enable_mask_ & (1U << i)) != 0U && ch.volume != 0U) {
                const auto wave = waveform_for_channel(static_cast<int>(i));
                const auto sample =
                    static_cast<std::int8_t>(waveform_[wave][static_cast<std::size_t>(ch.phase)]);
                mix += static_cast<std::int32_t>(sample) * static_cast<std::int32_t>(ch.volume) * 2;
            }

            if (ch.counter == 0U) {
                ch.counter = reload_period(ch);
                ch.phase = static_cast<std::uint8_t>((ch.phase + 1U) & 0x1FU);
            } else {
                --ch.counter;
            }
        }

        const std::int16_t sample = clamp16(mix);
        last_left_ = sample;
        last_right_ = sample;
    }

    void scc::tick(std::uint64_t cycles) {
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

    std::size_t scc::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void scc::reset(reset_kind /*kind*/) {
        for (auto& wave : waveform_) {
            wave.fill(0U);
        }
        channels_ = {};
        enable_mask_ = 0U;
        deformation_ = 0U;
        last_left_ = 0;
        last_right_ = 0;
        prescaler_ = 0;
        sample_queue_.clear();
    }

    void scc::save_state(state_writer& writer) const {
        writer.u32(k_state_version);
        for (const auto& wave : waveform_) {
            writer.bytes(wave);
        }
        for (const auto& ch : channels_) {
            writer.u16(ch.period);
            writer.u8(ch.volume);
            writer.u16(ch.counter);
            writer.u8(ch.phase);
        }
        writer.u8(enable_mask_);
        writer.u8(deformation_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
        writer.u16(static_cast<std::uint16_t>(last_left_));
        writer.u16(static_cast<std::uint16_t>(last_right_));
    }

    void scc::load_state(state_reader& reader) {
        if (reader.u32() != k_state_version) {
            reader.fail();
            return;
        }
        for (auto& wave : waveform_) {
            reader.bytes(wave);
        }
        for (auto& ch : channels_) {
            ch.period = reader.u16();
            ch.volume = reader.u8();
            ch.counter = reader.u16();
            ch.phase = reader.u8();
        }
        enable_mask_ = reader.u8();
        deformation_ = reader.u8();
        clock_divider_ = static_cast<int>(reader.u32());
        prescaler_ = static_cast<int>(reader.u32());
        last_left_ = static_cast<std::int16_t>(reader.u16());
        last_right_ = static_cast<std::int16_t>(reader.u16());
    }

    std::span<const register_descriptor> scc::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"FREQ_A", channels_[0].period, 12U, fmt::unsigned_integer};
        register_view_[1] = {"FREQ_B", channels_[1].period, 12U, fmt::unsigned_integer};
        register_view_[2] = {"FREQ_C", channels_[2].period, 12U, fmt::unsigned_integer};
        register_view_[3] = {"FREQ_D", channels_[3].period, 12U, fmt::unsigned_integer};
        register_view_[4] = {"FREQ_E", channels_[4].period, 12U, fmt::unsigned_integer};
        register_view_[5] = {"VOL_A", channels_[0].volume, 4U, fmt::unsigned_integer};
        register_view_[6] = {"VOL_B", channels_[1].volume, 4U, fmt::unsigned_integer};
        register_view_[7] = {"VOL_C", channels_[2].volume, 4U, fmt::unsigned_integer};
        register_view_[8] = {"VOL_D", channels_[3].volume, 4U, fmt::unsigned_integer};
        register_view_[9] = {"VOL_E", channels_[4].volume, 4U, fmt::unsigned_integer};
        register_view_[10] = {"ENABLE", enable_mask_, 5U, fmt::flags};
        register_view_[11] = {"DEFORM", deformation_, 8U, fmt::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto scc_registration =
            register_factory("konami.scc", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<scc>(); });
    } // namespace

} // namespace mnemos::chips::audio
