#include "irem_ga20.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

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

        [[nodiscard]] std::uint16_t ga20_volume(std::uint8_t value) noexcept {
            return value == 0U ? 0U
                               : static_cast<std::uint16_t>(
                                     (static_cast<std::uint32_t>(value) * 256U) /
                                     (static_cast<std::uint32_t>(value) + 10U));
        }
    } // namespace

    chip_metadata irem_ga20::metadata() const noexcept {
        return {
            .manufacturer = "Nanao/Irem",
            .part_number = "GA20",
            .family = "PCM",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    std::uint32_t irem_ga20::channel_address(std::size_t channel,
                                             std::uint8_t lo_reg) const noexcept {
        const std::size_t base = channel * channel_register_count;
        const std::uint32_t lo = regs_[base + lo_reg];
        const std::uint32_t hi = regs_[base + static_cast<std::size_t>(lo_reg + 1U)];
        return ((hi << 8U) | lo) << 4U;
    }

    void irem_ga20::key_channel(std::size_t channel_index) noexcept {
        channel& ch = channels_[channel_index];
        ch.pos = channel_address(channel_index, reg_start_low);
        ch.end = channel_address(channel_index, reg_end_low);
        ch.counter = 0x100;
        ch.rate = regs_[channel_index * channel_register_count + reg_rate];
        ch.volume = ga20_volume(regs_[channel_index * channel_register_count + reg_volume]);
        ch.active = ch.pos < ch.end && ch.pos < rom_.size();
    }

    std::uint8_t irem_ga20::read_register(std::uint8_t offset) const noexcept {
        offset = static_cast<std::uint8_t>(offset & 0x1FU);
        const std::size_t channel_index = offset >> 3U;
        if ((offset & 0x07U) == reg_status) {
            return channels_[channel_index].active ? status_active : 0x00U;
        }
        return 0x00U;
    }

    void irem_ga20::write_register(std::uint8_t offset, std::uint8_t value) noexcept {
        offset = static_cast<std::uint8_t>(offset & 0x1FU);
        regs_[offset] = value;

        const std::size_t channel_index = offset >> 3U;
        channel& ch = channels_[channel_index];
        switch (offset & 0x07U) {
        case reg_rate:
            ch.rate = value;
            break;
        case reg_volume:
            ch.volume = ga20_volume(value);
            break;
        case reg_control:
            if ((value & control_key_on) != 0U) {
                key_channel(channel_index);
            } else {
                ch.active = false;
            }
            break;
        default:
            break;
        }
    }

    bool irem_ga20::channel_active(std::size_t index) const noexcept {
        return index < channels_.size() && channels_[index].active;
    }

    std::int16_t irem_ga20::step() noexcept {
        std::int32_t mixed = 0;
        for (channel& ch : channels_) {
            if (!ch.active) {
                continue;
            }
            if (ch.pos >= ch.end || ch.pos >= rom_.size()) {
                ch.active = false;
                continue;
            }
            const std::uint8_t sample = rom_[ch.pos];
            if (sample == 0x00U) {
                ch.active = false;
                continue;
            }
            mixed += (static_cast<std::int32_t>(sample) - 0x80) *
                     static_cast<std::int32_t>(ch.volume);

            --ch.counter;
            if (ch.counter <= static_cast<std::int32_t>(ch.rate)) {
                ++ch.pos;
                ch.counter = 0x100;
            }
        }

        last_sample_ = clamp16(mixed);
        return last_sample_;
    }

    void irem_ga20::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            const std::int16_t sample = step();
            buf_lr[i * 2U] = sample;
            buf_lr[i * 2U + 1U] = sample;
        }
    }

    void irem_ga20::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            ++sample_clock_;
            if (sample_clock_ < clocks_per_sample) {
                continue;
            }
            sample_clock_ = 0U;
            const std::int16_t sample = step();
            if (audio_capture_) {
                sample_queue_.push_back(sample);
                sample_queue_.push_back(sample);
            }
        }
    }

    std::size_t irem_ga20::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void irem_ga20::reset(reset_kind /*kind*/) {
        regs_.fill(0U);
        channels_ = {};
        sample_clock_ = 0U;
        last_sample_ = 0;
        sample_queue_.clear();
    }

    void irem_ga20::save_state(state_writer& writer) const {
        writer.bytes(regs_);
        for (const channel& ch : channels_) {
            writer.u32(ch.pos);
            writer.u32(ch.end);
            writer.u32(static_cast<std::uint32_t>(ch.counter));
            writer.u8(ch.rate);
            writer.u16(ch.volume);
            writer.boolean(ch.active);
        }
        writer.u32(input_clock_hz_);
        writer.u32(sample_clock_);
        writer.u16(static_cast<std::uint16_t>(last_sample_));
    }

    void irem_ga20::load_state(state_reader& reader) {
        reader.bytes(regs_);
        for (channel& ch : channels_) {
            ch.pos = reader.u32();
            ch.end = reader.u32();
            ch.counter = static_cast<std::int32_t>(reader.u32());
            ch.rate = reader.u8();
            ch.volume = reader.u16();
            ch.active = reader.boolean();
        }
        input_clock_hz_ = reader.u32();
        sample_clock_ = reader.u32() % clocks_per_sample;
        last_sample_ = static_cast<std::int16_t>(reader.u16());
        sample_queue_.clear();
    }

    instrumentation::ichip_introspection& irem_ga20::introspection() noexcept {
        return introspection_;
    }

    std::span<const instrumentation::sample_view> irem_ga20::audio_source_impl::samples() const {
        const auto rom = owner_->rom_;

        struct meta final {
            std::uint32_t start;
            std::size_t off;
            std::size_t len;
        };
        std::vector<meta> metas;

        pcm_.clear();
        for (std::size_t channel_index = 0; channel_index < irem_ga20::channel_count;
             ++channel_index) {
            const std::uint32_t start = owner_->channel_address(channel_index, reg_start_low);
            const std::uint32_t end = owner_->channel_address(channel_index, reg_end_low);
            if (start >= end || start >= rom.size()) {
                continue;
            }
            const bool seen = std::any_of(metas.begin(), metas.end(),
                                          [start](const meta& m) { return m.start == start; });
            if (seen) {
                continue;
            }
            const std::size_t off = pcm_.size();
            std::size_t addr = start;
            while (addr < end && addr < rom.size() && rom[addr] != 0x00U) {
                const auto centered = static_cast<std::int16_t>(
                    (static_cast<std::int32_t>(rom[addr]) - 0x80) * 256);
                pcm_.push_back(centered);
                ++addr;
            }
            if (pcm_.size() == off) {
                continue;
            }
            metas.push_back({.start = start, .off = off, .len = pcm_.size() - off});
        }

        names_.clear();
        names_.reserve(metas.size());
        samples_.clear();
        samples_.reserve(metas.size());
        for (std::size_t i = 0; i < metas.size(); ++i) {
            std::array<char, 20> buf{};
            std::snprintf(buf.data(), buf.size(), "ga20_%05x",
                          static_cast<unsigned>(metas[i].start));
            names_.emplace_back(buf.data());
            samples_.push_back(instrumentation::sample_view{
                .name = names_[i],
                .frames = std::span<const std::int16_t>(pcm_).subspan(metas[i].off, metas[i].len),
                .sample_rate = owner_->native_sample_rate(),
                .channels = 1,
                .loop_start = -1,
                .source_addr = metas[i].start});
        }
        return samples_;
    }

    std::span<const register_descriptor> irem_ga20::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"CH0STATUS", read_register(0x07U), 1U, fmt::flags};
        register_view_[1] = {"CH1STATUS", read_register(0x0FU), 1U, fmt::flags};
        register_view_[2] = {"CH2STATUS", read_register(0x17U), 1U, fmt::flags};
        register_view_[3] = {"CH3STATUS", read_register(0x1FU), 1U, fmt::flags};
        register_view_[4] = {"CH0POS", channels_[0].pos, 24U, fmt::unsigned_integer};
        register_view_[5] = {"CH1POS", channels_[1].pos, 24U, fmt::unsigned_integer};
        register_view_[6] = {"CH2POS", channels_[2].pos, 24U, fmt::unsigned_integer};
        register_view_[7] = {"CH3POS", channels_[3].pos, 24U, fmt::unsigned_integer};
        register_view_[8] = {"RATE0", channels_[0].rate, 8U, fmt::unsigned_integer};
        register_view_[9] = {"VOL0", channels_[0].volume, 16U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto irem_ga20_registration =
            register_factory("irem.ga20", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> {
                                 return std::make_unique<irem_ga20>();
                             });
    } // namespace

} // namespace mnemos::chips::audio
