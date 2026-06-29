#include "ymz280b.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string_view>

namespace mnemos::chips::audio {

    namespace {
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

    ymz280b::ymz280b() {
        introspection_.with_registers([this] { return register_snapshot(); });
        reset(reset_kind::power_on);
    }

    chip_metadata ymz280b::metadata() const noexcept {
        return {
            .manufacturer = "Yamaha",
            .part_number = "YMZ280B",
            .family = "YMZ280B",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    std::uint32_t ymz280b::channel_address(std::size_t channel,
                                           std::uint8_t lo_reg) const noexcept {
        const std::size_t base = channel * channel_register_count;
        const std::uint32_t lo = regs_[base + lo_reg];
        const std::uint32_t mid = regs_[base + static_cast<std::size_t>(lo_reg + 1U)];
        const std::uint32_t hi = regs_[base + static_cast<std::size_t>(lo_reg + 2U)];
        return lo | (mid << 8U) | (hi << 16U);
    }

    void ymz280b::key_channel_from_registers(std::size_t channel_index) noexcept {
        const std::size_t base = channel_index * channel_register_count;
        const std::uint32_t start = channel_address(channel_index, reg_start_low);
        const std::uint32_t end = channel_address(channel_index, reg_end_low);
        const std::uint8_t volume = regs_[base + reg_volume];
        const bool loop = (regs_[base + reg_control] & control_loop) != 0U;
        key_channel(channel_index, start, end, volume, loop);
    }

    void ymz280b::key_channel(std::size_t channel_index, std::uint32_t start, std::uint32_t end,
                              std::uint8_t volume, bool loop) noexcept {
        if (channel_index >= channels_.size()) {
            return;
        }
        channel& ch = channels_[channel_index];
        const std::size_t base = channel_index * channel_register_count;
        ch.start = start;
        ch.pos = start;
        ch.end = end;
        ch.accumulator = 0U;
        ch.rate = std::max<std::uint8_t>(1U, regs_[base + reg_rate]);
        ch.volume = volume;
        ch.loop = loop;
        ch.active = !rom_.empty() && start < end && start < rom_.size();
    }

    bool ymz280b::channel_active(std::size_t index) const noexcept {
        return index < channels_.size() && channels_[index].active;
    }

    std::uint8_t ymz280b::active_mask() const noexcept {
        std::uint8_t mask = 0U;
        for (std::size_t i = 0; i < channels_.size(); ++i) {
            if (channels_[i].active) {
                mask |= static_cast<std::uint8_t>(1U << i);
            }
        }
        return mask;
    }

    std::uint8_t ymz280b::read_register(std::uint8_t offset) const noexcept {
        if (offset == global_status_register) {
            return active_mask();
        }
        const std::size_t channel_index = offset / channel_register_count;
        const std::size_t reg_index = offset % channel_register_count;
        if (channel_index < channels_.size() && reg_index == reg_status) {
            return channels_[channel_index].active ? status_active : 0x00U;
        }
        return regs_[offset];
    }

    void ymz280b::write_register(std::uint8_t offset, std::uint8_t value) noexcept {
        regs_[offset] = value;
        const std::size_t channel_index = offset / channel_register_count;
        const std::size_t reg_index = offset % channel_register_count;
        if (channel_index >= channels_.size()) {
            return;
        }
        channel& ch = channels_[channel_index];
        switch (reg_index) {
        case reg_rate:
            ch.rate = std::max<std::uint8_t>(1U, value);
            break;
        case reg_volume:
            ch.volume = value;
            break;
        case reg_control:
            ch.loop = (value & control_loop) != 0U;
            if ((value & control_key_on) != 0U) {
                key_channel_from_registers(channel_index);
            } else {
                ch.active = false;
            }
            break;
        default:
            break;
        }
    }

    std::int16_t ymz280b::step() noexcept {
        std::int32_t mixed = 0;
        for (channel& ch : channels_) {
            if (!ch.active) {
                continue;
            }
            if (ch.pos >= ch.end || ch.pos >= rom_.size()) {
                if (ch.loop && ch.start < ch.end && ch.start < rom_.size()) {
                    ch.pos = ch.start;
                } else {
                    ch.active = false;
                    continue;
                }
            }

            const std::int32_t centered =
                static_cast<std::int32_t>(rom_[ch.pos]) - static_cast<std::int32_t>(0x80);
            mixed += centered * static_cast<std::int32_t>(ch.volume);

            ch.accumulator += std::max<std::uint8_t>(1U, ch.rate);
            const std::uint32_t advance = ch.accumulator >> 8U;
            ch.accumulator &= 0xFFU;
            ch.pos += std::max<std::uint32_t>(1U, advance);
        }

        last_sample_ = clamp16(mixed);
        return last_sample_;
    }

    void ymz280b::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            const std::int16_t sample = step();
            buf_lr[i * 2U] = sample;
            buf_lr[i * 2U + 1U] = sample;
        }
    }

    void ymz280b::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            ++sample_clock_;
            if (sample_clock_ < clocks_per_sample) {
                continue;
            }
            sample_clock_ = 0U;
            const std::int16_t sample = step();
            if (audio_capture_) {
                ++capture_counter_;
                if (capture_counter_ >= capture_divider_) {
                    capture_counter_ = 0U;
                    sample_queue_.push_back(sample);
                    sample_queue_.push_back(sample);
                }
            }
        }
    }

    std::size_t ymz280b::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void ymz280b::reset(reset_kind /*kind*/) {
        regs_.fill(0U);
        channels_ = {};
        sample_clock_ = 0U;
        capture_counter_ = 0U;
        last_sample_ = 0;
        sample_queue_.clear();
    }

    void ymz280b::save_state(state_writer& writer) const {
        writer.bytes(regs_);
        for (const channel& ch : channels_) {
            writer.u32(ch.start);
            writer.u32(ch.pos);
            writer.u32(ch.end);
            writer.u32(ch.accumulator);
            writer.u8(ch.rate);
            writer.u8(ch.volume);
            writer.boolean(ch.loop);
            writer.boolean(ch.active);
        }
        writer.u32(input_clock_hz_);
        writer.u32(sample_clock_);
        writer.u32(capture_divider_);
        writer.u32(capture_counter_);
        writer.u16(static_cast<std::uint16_t>(last_sample_));
    }

    void ymz280b::load_state(state_reader& reader) {
        reader.bytes(regs_);
        for (channel& ch : channels_) {
            ch.start = reader.u32();
            ch.pos = reader.u32();
            ch.end = reader.u32();
            ch.accumulator = reader.u32() & 0xFFU;
            ch.rate = std::max<std::uint8_t>(1U, reader.u8());
            ch.volume = reader.u8();
            ch.loop = reader.boolean();
            ch.active = reader.boolean();
        }
        input_clock_hz_ = reader.u32();
        sample_clock_ = reader.u32() % clocks_per_sample;
        capture_divider_ = reader.u32();
        if (capture_divider_ == 0U) {
            capture_divider_ = 1U;
        }
        capture_counter_ = reader.u32() % capture_divider_;
        last_sample_ = static_cast<std::int16_t>(reader.u16());
        sample_queue_.clear();
    }

    instrumentation::ichip_introspection& ymz280b::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> ymz280b::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"STATUS", active_mask(), 8U, fmt::flags};
        static constexpr std::array<std::string_view, channel_count> names = {
            "CH0POS", "CH1POS", "CH2POS", "CH3POS",
            "CH4POS", "CH5POS", "CH6POS", "CH7POS"};
        for (std::size_t i = 0; i < channels_.size(); ++i) {
            register_view_[i + 1U] = {names[i], channels_[i].pos, 24U, fmt::unsigned_integer};
        }
        register_view_[9] = {"CH0VOL", channels_[0].volume, 8U, fmt::unsigned_integer};
        register_view_[10] = {"CH0RATE", channels_[0].rate, 8U, fmt::unsigned_integer};
        register_view_[11] = {"LAST", static_cast<std::uint16_t>(last_sample_), 16U,
                              fmt::signed_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto ymz280b_registration =
            register_factory("yamaha.ymz280b", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> {
                                 return std::make_unique<ymz280b>();
                             });
    } // namespace

} // namespace mnemos::chips::audio
