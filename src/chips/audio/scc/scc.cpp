#include "scc.hpp"

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

    chip_metadata scc::metadata() const noexcept {
        return {
            .manufacturer = "Konami",
            .part_number = "051649",
            .family = "Sound Creative Chip",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    std::uint8_t scc::canonical_register(std::uint8_t address) noexcept {
        // The $90-$9F range mirrors the $80-$8F frequency/volume/enable block on
        // common cartridge decodes; canonicalise it so writes through either window
        // update the same oscillator state.
        if (address >= 0x90U && address <= 0x9FU) {
            return static_cast<std::uint8_t>(address - 0x10U);
        }
        return address;
    }

    std::size_t scc::waveform_for_channel(int channel) noexcept {
        if (channel <= 0) {
            return 0U;
        }
        if (channel >= channel_count - 1) {
            return waveform_count - 1U;
        }
        return static_cast<std::size_t>(channel);
    }

    void scc::decode_register(std::uint8_t reg) noexcept {
        if (reg >= 0x80U && reg <= 0x89U) {
            const std::size_t ch = static_cast<std::size_t>((reg - 0x80U) / 2U);
            const std::uint16_t lo = regs_[0x80U + ch * 2U];
            const std::uint16_t hi = static_cast<std::uint16_t>(regs_[0x81U + ch * 2U] & 0x0FU);
            frequency_[ch] = static_cast<std::uint16_t>(lo | (hi << 8U));
            return;
        }
        if (reg >= 0x8AU && reg <= 0x8EU) {
            const std::size_t ch = static_cast<std::size_t>(reg - 0x8AU);
            volume_[ch] = static_cast<std::uint8_t>(regs_[reg] & 0x0FU);
            return;
        }
        if (reg == 0x8FU) {
            enable_mask_ = static_cast<std::uint8_t>(regs_[reg] & 0x1FU);
        }
    }

    std::uint8_t scc::read(std::uint16_t address) const noexcept {
        return regs_[canonical_register(static_cast<std::uint8_t>(address & 0x00FFU))];
    }

    void scc::write(std::uint16_t address, std::uint8_t value) noexcept {
        const std::uint8_t reg = canonical_register(static_cast<std::uint8_t>(address & 0x00FFU));
        regs_[reg] = value;
        decode_register(reg);
    }

    std::uint16_t scc::frequency(int channel) const noexcept {
        if (channel < 0 || channel >= channel_count) {
            return 0U;
        }
        return frequency_[static_cast<std::size_t>(channel)];
    }

    std::uint8_t scc::volume(int channel) const noexcept {
        if (channel < 0 || channel >= channel_count) {
            return 0U;
        }
        return volume_[static_cast<std::size_t>(channel)];
    }

    bool scc::channel_enabled(int channel) const noexcept {
        if (channel < 0 || channel >= channel_count) {
            return false;
        }
        return (enable_mask_ & (1U << static_cast<unsigned>(channel))) != 0U;
    }

    std::uint8_t scc::wave_sample(int channel, int offset) const noexcept {
        if (channel < 0 || channel >= channel_count || offset < 0 || offset >= waveform_size) {
            return 0U;
        }
        const std::size_t wave = waveform_for_channel(channel);
        const std::size_t index = wave * waveform_size + static_cast<std::size_t>(offset);
        return regs_[index];
    }

    std::int32_t scc::channel_output(int channel) const noexcept {
        if (channel < 0 || channel >= channel_count) {
            return 0;
        }
        return channel_output_[static_cast<std::size_t>(channel)];
    }

    void scc::advance_oscillators() noexcept {
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            if ((enable_mask_ & (1U << ch)) == 0U || volume_[ch] == 0U || frequency_[ch] == 0U) {
                channel_output_[ch] = 0;
                continue;
            }

            ++phase_counter_[ch];
            if (phase_counter_[ch] >= frequency_[ch]) {
                phase_counter_[ch] = 0U;
                wave_index_[ch] =
                    static_cast<std::uint8_t>((wave_index_[ch] + 1U) & (waveform_size - 1U));
            }

            const std::uint8_t sample = wave_sample(static_cast<int>(ch), wave_index_[ch]);
            const auto signed_sample = static_cast<std::int8_t>(sample);
            channel_output_[ch] = static_cast<int>(signed_sample) * static_cast<int>(volume_[ch]);
        }
    }

    std::int16_t scc::mix_output() noexcept {
        int sum = 0;
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            sum += channel_output_[ch];
        }
        return clip16(sum * k_output_gain);
    }

    void scc::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            advance_oscillators();
            if (++sample_prescaler_ >= clock_divider_) {
                sample_prescaler_ = 0;
                const std::int16_t sample = mix_output();
                last_left_ = sample;
                last_right_ = sample;
                if (audio_capture_) {
                    sample_queue_.push_back(sample);
                    sample_queue_.push_back(sample);
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
        regs_.fill(0U);
        frequency_.fill(0U);
        volume_.fill(0U);
        enable_mask_ = 0U;
        phase_counter_.fill(0U);
        wave_index_.fill(0U);
        channel_output_.fill(0);
        last_left_ = 0;
        last_right_ = 0;
        sample_prescaler_ = 0;
        sample_queue_.clear();
    }

    void scc::save_state(state_writer& writer) const {
        writer.bytes(regs_);
        for (const std::uint16_t f : frequency_) {
            writer.u16(f);
        }
        writer.bytes(volume_);
        writer.u8(enable_mask_);
        for (const std::uint16_t p : phase_counter_) {
            writer.u16(p);
        }
        writer.bytes(wave_index_);
        for (const std::int32_t o : channel_output_) {
            writer.u32(static_cast<std::uint32_t>(o));
        }
        writer.u16(static_cast<std::uint16_t>(last_left_));
        writer.u16(static_cast<std::uint16_t>(last_right_));
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(sample_prescaler_));
    }

    void scc::load_state(state_reader& reader) {
        reader.bytes(regs_);
        for (std::uint16_t& f : frequency_) {
            f = static_cast<std::uint16_t>(reader.u16() & 0x0FFFU);
        }
        reader.bytes(volume_);
        for (std::uint8_t& v : volume_) {
            v = static_cast<std::uint8_t>(v & 0x0FU);
        }
        enable_mask_ = static_cast<std::uint8_t>(reader.u8() & 0x1FU);
        for (std::uint16_t& p : phase_counter_) {
            p = reader.u16();
        }
        reader.bytes(wave_index_);
        for (std::uint8_t& i : wave_index_) {
            i = static_cast<std::uint8_t>(i & (waveform_size - 1U));
        }
        for (std::int32_t& o : channel_output_) {
            o = static_cast<std::int32_t>(reader.u32());
        }
        last_left_ = static_cast<std::int16_t>(reader.u16());
        last_right_ = static_cast<std::int16_t>(reader.u16());
        clock_divider_ = static_cast<int>(reader.u32());
        if (clock_divider_ <= 0) {
            clock_divider_ = default_clock_divider;
        }
        sample_prescaler_ = static_cast<int>(reader.u32());
        sample_queue_.clear();
    }

    instrumentation::ichip_introspection& scc::introspection() noexcept { return introspection_; }

    std::span<const register_descriptor> scc::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"ENABLE", enable_mask_, 5U, fmt::flags};
        for (std::size_t ch = 0; ch < channel_count; ++ch) {
            const std::size_t base = 1U + ch * 2U;
            register_view_[base] = {"FREQ", frequency_[ch], 12U, fmt::unsigned_integer};
            register_view_[base + 1U] = {"VOL", volume_[ch], 4U, fmt::unsigned_integer};
        }
        register_view_[11] = {"LAST", static_cast<std::uint16_t>(last_left_), 16U,
                              fmt::signed_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto scc_registration =
            register_factory("konami.051649", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<scc>(); });
    } // namespace

} // namespace mnemos::chips::audio
