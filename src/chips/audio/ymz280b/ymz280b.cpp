#include "ymz280b.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
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

        constexpr std::array<std::int32_t, 8> adpcm_step_scale = {
            57, 57, 57, 57, 77, 102, 128, 153};
        constexpr std::int32_t adpcm_step_min = 127;
        constexpr std::int32_t adpcm_step_max = 24576;

        [[nodiscard]] std::int16_t decode_adpcm4(std::int32_t& accumulator,
                                                 std::int32_t& step,
                                                 std::uint8_t nibble) noexcept {
            const std::int32_t magnitude = nibble & 0x07U;
            std::int32_t delta = ((2 * magnitude + 1) * step) >> 3;
            if ((nibble & 0x08U) != 0U) {
                delta = -delta;
            }
            accumulator = clamp16(accumulator + delta);
            step = std::clamp(
                (step * adpcm_step_scale[static_cast<std::size_t>(magnitude)]) >> 6,
                adpcm_step_min, adpcm_step_max);
            return static_cast<std::int16_t>(accumulator);
        }

        [[nodiscard]] constexpr std::uint8_t address_base_high() noexcept {
            return ymz280b::reg_start_high;
        }

        [[nodiscard]] constexpr std::uint8_t address_base_mid() noexcept {
            return ymz280b::reg_start_mid;
        }

        [[nodiscard]] constexpr std::uint8_t address_base_low() noexcept {
            return ymz280b::reg_start_low;
        }

        [[nodiscard]] constexpr std::uint8_t channel_address_offset(
            std::size_t channel,
            ymz280b::address_kind kind,
            std::uint8_t segment_base) noexcept {
            return static_cast<std::uint8_t>(
                segment_base + (channel * ymz280b::channel_register_count) +
                static_cast<std::uint8_t>(kind));
        }

        [[nodiscard]] constexpr bool is_address_register(std::uint8_t offset) noexcept {
            return (offset >= 0x20U && offset < 0x80U);
        }

        [[nodiscard]] constexpr std::size_t address_register_channel(
            std::uint8_t offset) noexcept {
            return ((offset & 0x1FU) / ymz280b::channel_register_count);
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
            .revision = 2U,
        };
    }

    std::uint32_t ymz280b::channel_address(std::size_t channel,
                                           address_kind kind) const noexcept {
        if (channel >= channels_.size()) {
            return 0U;
        }
        const std::uint32_t hi =
            regs_[channel_address_offset(channel, kind, address_base_high())];
        const std::uint32_t mid =
            regs_[channel_address_offset(channel, kind, address_base_mid())];
        const std::uint32_t lo =
            regs_[channel_address_offset(channel, kind, address_base_low())];
        return (hi << 16U) | (mid << 8U) | lo;
    }

    ymz280b::sample_mode ymz280b::mode_from_control(std::uint8_t control) const noexcept {
        switch (control & control_mode_mask) {
        case control_mode_adpcm4:
            return sample_mode::adpcm4;
        case control_mode_pcm8:
            return sample_mode::pcm8;
        case control_mode_pcm16:
            return sample_mode::pcm16;
        default:
            return sample_mode::disabled;
        }
    }

    void ymz280b::refresh_channel_from_registers(std::size_t channel_index) noexcept {
        if (channel_index >= channels_.size()) {
            return;
        }
        channel& ch = channels_[channel_index];
        const std::size_t base = channel_index * channel_register_count;
        const std::uint8_t control = regs_[base + reg_control];
        const auto pitch_low = static_cast<std::uint16_t>(regs_[base + reg_pitch]);
        const auto pitch_high =
            static_cast<std::uint16_t>((control & control_fn8) != 0U ? 0x0100U : 0U);
        ch.pitch = std::max<std::uint16_t>(1U, static_cast<std::uint16_t>(pitch_high | pitch_low));
        ch.total_level = regs_[base + reg_total_level];
        ch.pan = static_cast<std::uint8_t>(regs_[base + reg_pan] & 0x0FU);
        ch.mode = mode_from_control(control);
        ch.loop = (control & control_loop) != 0U;
        ch.start = channel_address(channel_index, address_kind::start);
        ch.loop_start = channel_address(channel_index, address_kind::loop_start);
        ch.loop_end = channel_address(channel_index, address_kind::loop_end);
        ch.end = channel_address(channel_index, address_kind::end);
        if (ch.active && ch.mode == sample_mode::disabled) {
            ch.active = false;
        }
    }

    void ymz280b::key_channel_from_registers(std::size_t channel_index) noexcept {
        if (channel_index >= channels_.size()) {
            return;
        }
        refresh_channel_from_registers(channel_index);
        channel& ch = channels_[channel_index];
        ch.pos = ch.start;
        ch.accumulator = 0U;
        ch.high_nibble = true;
        ch.adpcm_accumulator = 0;
        ch.adpcm_step = adpcm_step_min;
        const std::uint32_t playback_end =
            (ch.loop && ch.loop_end > ch.loop_start) ? ch.loop_end : ch.end;
        ch.active = !rom_.empty() && ch.mode != sample_mode::disabled &&
                    ch.start < playback_end && ch.start < rom_.size();
    }

    void ymz280b::key_channel(std::size_t channel_index, std::uint32_t start, std::uint32_t end,
                              std::uint8_t volume, bool loop) noexcept {
        if (channel_index >= channels_.size()) {
            return;
        }

        const auto write_address = [this, channel_index](address_kind kind,
                                                         std::uint32_t address) noexcept {
            regs_[channel_address_offset(channel_index, kind, address_base_high())] =
                static_cast<std::uint8_t>((address >> 16U) & 0xFFU);
            regs_[channel_address_offset(channel_index, kind, address_base_mid())] =
                static_cast<std::uint8_t>((address >> 8U) & 0xFFU);
            regs_[channel_address_offset(channel_index, kind, address_base_low())] =
                static_cast<std::uint8_t>(address & 0xFFU);
        };

        const std::size_t base = channel_index * channel_register_count;
        regs_[utility_control_register] |=
            static_cast<std::uint8_t>(utility_control_key_enable |
                                      utility_control_memory_enable);
        regs_[base + reg_pitch] = 0x00U;
        regs_[base + reg_control] = static_cast<std::uint8_t>(
            control_key_on | control_mode_pcm8 | control_fn8 |
            (loop ? control_loop : 0U));
        regs_[base + reg_total_level] = volume;
        regs_[base + reg_pan] = 0x08U;
        write_address(address_kind::start, start);
        write_address(address_kind::loop_start, start);
        write_address(address_kind::loop_end, end);
        write_address(address_kind::end, end);
        key_channel_from_registers(channel_index);
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

    std::uint8_t ymz280b::read_register(std::uint8_t offset) noexcept {
        if (offset == global_status_register) {
            const std::uint8_t flags = status_flags_ != 0U ? status_flags_ : active_mask();
            status_flags_ = 0U;
            return flags;
        }
        return regs_[offset];
    }

    void ymz280b::write_register(std::uint8_t offset, std::uint8_t value) noexcept {
        regs_[offset] = value;

        if (offset == utility_control_register) {
            if ((value & utility_control_key_enable) == 0U) {
                for (channel& ch : channels_) {
                    ch.active = false;
                }
            }
            return;
        }

        if (is_channel_function_register(offset)) {
            const std::size_t channel_index = offset / channel_register_count;
            const std::size_t reg_index = offset % channel_register_count;
            refresh_channel_from_registers(channel_index);
            if (reg_index == reg_control) {
                channel& ch = channels_[channel_index];
                if ((value & control_key_on) != 0U && ch.mode != sample_mode::disabled) {
                    key_channel_from_registers(channel_index);
                } else {
                    ch.active = false;
                }
            }
            return;
        }

        if (is_address_register(offset)) {
            const std::size_t channel_index = address_register_channel(offset);
            if (channel_index < channels_.size()) {
                refresh_channel_from_registers(channel_index);
            }
        }
    }

    std::int16_t ymz280b::decode_current_sample(channel& ch) noexcept {
        switch (ch.mode) {
        case sample_mode::pcm8:
            return static_cast<std::int16_t>(
                static_cast<std::int32_t>(static_cast<std::int8_t>(rom_[ch.pos])) << 8U);
        case sample_mode::pcm16: {
            const auto word = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(rom_[ch.pos]) << 8U) |
                static_cast<std::uint16_t>(rom_[ch.pos + 1U]));
            return static_cast<std::int16_t>(word);
        }
        case sample_mode::adpcm4: {
            const std::uint8_t byte = rom_[ch.pos];
            const std::uint8_t nibble =
                ch.high_nibble ? static_cast<std::uint8_t>((byte >> 4U) & 0x0FU)
                               : static_cast<std::uint8_t>(byte & 0x0FU);
            return decode_adpcm4(ch.adpcm_accumulator, ch.adpcm_step, nibble);
        }
        case sample_mode::disabled:
            break;
        }
        return 0;
    }

    void ymz280b::advance_channel(channel& ch) noexcept {
        ch.accumulator += ch.pitch;
        std::uint32_t steps = ch.accumulator >> 8U;
        ch.accumulator &= 0xFFU;
        steps = std::max<std::uint32_t>(1U, steps);

        for (std::uint32_t i = 0; i < steps; ++i) {
            switch (ch.mode) {
            case sample_mode::pcm16:
                ch.pos += 2U;
                break;
            case sample_mode::adpcm4:
                if (ch.high_nibble) {
                    ch.high_nibble = false;
                } else {
                    ch.high_nibble = true;
                    ++ch.pos;
                }
                break;
            case sample_mode::pcm8:
            case sample_mode::disabled:
                ++ch.pos;
                break;
            }
        }
    }

    void ymz280b::mark_channel_end(std::size_t channel_index) noexcept {
        const std::uint8_t mask = static_cast<std::uint8_t>(1U << channel_index);
        if ((regs_[irq_enable_register] & mask) != 0U) {
            status_flags_ |= mask;
        }
    }

    std::int16_t ymz280b::step() noexcept {
        std::int32_t mixed = 0;
        std::int32_t mixed_left = 0;
        std::int32_t mixed_right = 0;

        const auto playback_limit = [](const channel& ch) noexcept -> std::uint32_t {
            if (ch.loop && ch.loop_end > ch.loop_start) {
                return ch.loop_end;
            }
            return ch.end;
        };

        const auto at_limit = [this, playback_limit](const channel& ch) noexcept -> bool {
            const std::uint32_t limit = playback_limit(ch);
            if (limit == 0U || ch.pos >= limit || ch.pos >= rom_.size()) {
                return true;
            }
            if (ch.mode == sample_mode::pcm16 &&
                (ch.pos + 1U >= limit || ch.pos + 1U >= rom_.size())) {
                return true;
            }
            return false;
        };

        const auto rewind_or_stop = [this, playback_limit, at_limit](std::size_t index,
                                                                     channel& ch) noexcept {
            if (!at_limit(ch)) {
                return;
            }
            const std::uint32_t limit = playback_limit(ch);
            if (ch.loop && ch.loop_start < limit && ch.loop_start < rom_.size()) {
                ch.pos = ch.loop_start;
                ch.high_nibble = true;
                return;
            }
            ch.active = false;
            mark_channel_end(index);
        };

        for (std::size_t i = 0; i < channels_.size(); ++i) {
            channel& ch = channels_[i];
            if (!ch.active) {
                continue;
            }

            rewind_or_stop(i, ch);
            if (!ch.active) {
                continue;
            }

            const std::int32_t sample = decode_current_sample(ch);
            const std::int32_t scaled =
                (sample * static_cast<std::int32_t>(ch.total_level)) / 0xFF;
            mixed += scaled;

            const std::int32_t pan = std::clamp<std::int32_t>(ch.pan, 0, 15);
            const std::int32_t left_gain = 15 - pan;
            const std::int32_t right_gain = pan;
            mixed_left += (scaled * left_gain) / 15;
            mixed_right += (scaled * right_gain) / 15;

            advance_channel(ch);
            rewind_or_stop(i, ch);
        }

        last_sample_ = clamp16(mixed);
        last_left_ = clamp16(mixed_left);
        last_right_ = clamp16(mixed_right);
        return last_sample_;
    }

    void ymz280b::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            (void)step();
            buf_lr[i * 2U] = last_left_;
            buf_lr[i * 2U + 1U] = last_right_;
        }
    }

    void ymz280b::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            ++sample_clock_;
            if (sample_clock_ < clocks_per_sample) {
                continue;
            }
            sample_clock_ = 0U;
            (void)step();
            if (audio_capture_) {
                ++capture_counter_;
                if (capture_counter_ >= capture_divider_) {
                    capture_counter_ = 0U;
                    sample_queue_.push_back(last_left_);
                    sample_queue_.push_back(last_right_);
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
        status_flags_ = 0U;
        sample_clock_ = 0U;
        capture_counter_ = 0U;
        last_sample_ = 0;
        last_left_ = 0;
        last_right_ = 0;
        sample_queue_.clear();
    }

    void ymz280b::save_state(state_writer& writer) const {
        writer.bytes(regs_);
        for (const channel& ch : channels_) {
            writer.u32(ch.start);
            writer.u32(ch.pos);
            writer.u32(ch.loop_start);
            writer.u32(ch.loop_end);
            writer.u32(ch.end);
            writer.u32(ch.accumulator);
            writer.u16(ch.pitch);
            writer.u8(ch.total_level);
            writer.u8(ch.pan);
            writer.u8(static_cast<std::uint8_t>(ch.mode));
            writer.u32(static_cast<std::uint32_t>(ch.adpcm_accumulator));
            writer.u32(static_cast<std::uint32_t>(ch.adpcm_step));
            writer.boolean(ch.high_nibble);
            writer.boolean(ch.loop);
            writer.boolean(ch.active);
        }
        writer.u8(status_flags_);
        writer.u32(input_clock_hz_);
        writer.u32(sample_clock_);
        writer.u32(capture_divider_);
        writer.u32(capture_counter_);
        writer.u16(static_cast<std::uint16_t>(last_sample_));
        writer.u16(static_cast<std::uint16_t>(last_left_));
        writer.u16(static_cast<std::uint16_t>(last_right_));
    }

    void ymz280b::load_state(state_reader& reader) {
        reader.bytes(regs_);
        for (channel& ch : channels_) {
            ch.start = reader.u32();
            ch.pos = reader.u32();
            ch.loop_start = reader.u32();
            ch.loop_end = reader.u32();
            ch.end = reader.u32();
            ch.accumulator = reader.u32() & 0xFFU;
            ch.pitch = std::max<std::uint16_t>(1U, reader.u16());
            ch.total_level = reader.u8();
            ch.pan = static_cast<std::uint8_t>(reader.u8() & 0x0FU);
            const std::uint8_t mode = reader.u8();
            ch.mode = mode <= static_cast<std::uint8_t>(sample_mode::pcm16)
                          ? static_cast<sample_mode>(mode)
                          : sample_mode::disabled;
            ch.adpcm_accumulator =
                std::clamp(static_cast<std::int32_t>(reader.u32()), -32768, 32767);
            ch.adpcm_step =
                std::clamp(static_cast<std::int32_t>(reader.u32()), adpcm_step_min,
                           adpcm_step_max);
            ch.high_nibble = reader.boolean();
            ch.loop = reader.boolean();
            ch.active = reader.boolean();
        }
        status_flags_ = reader.u8();
        input_clock_hz_ = reader.u32();
        sample_clock_ = reader.u32() % clocks_per_sample;
        capture_divider_ = reader.u32();
        if (capture_divider_ == 0U) {
            capture_divider_ = 1U;
        }
        capture_counter_ = reader.u32() % capture_divider_;
        last_sample_ = static_cast<std::int16_t>(reader.u16());
        last_left_ = static_cast<std::int16_t>(reader.u16());
        last_right_ = static_cast<std::int16_t>(reader.u16());
        sample_queue_.clear();
    }

    instrumentation::ichip_introspection& ymz280b::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> ymz280b::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"STATUS", status_flags_ != 0U ? status_flags_ : active_mask(), 8U,
                             fmt::flags};
        static constexpr std::array<std::string_view, channel_count> names = {
            "CH0POS", "CH1POS", "CH2POS", "CH3POS",
            "CH4POS", "CH5POS", "CH6POS", "CH7POS"};
        for (std::size_t i = 0; i < channels_.size(); ++i) {
            register_view_[i + 1U] = {names[i], channels_[i].pos, 24U, fmt::unsigned_integer};
        }
        register_view_[9] = {"CH0TL", channels_[0].total_level, 8U, fmt::unsigned_integer};
        register_view_[10] = {"CH0PITCH", channels_[0].pitch, 9U, fmt::unsigned_integer};
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
