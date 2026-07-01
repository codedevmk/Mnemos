#include "paula.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {
    namespace {
        [[nodiscard]] std::int16_t clamp16(std::int32_t v) noexcept {
            if (v > 32767) {
                return 32767;
            }
            if (v < -32768) {
                return -32768;
            }
            return static_cast<std::int16_t>(v);
        }

        [[nodiscard]] bool channel_valid(int channel) noexcept {
            return channel >= 0 && channel < paula::channel_count;
        }

        // 20-bit word-aligned pointer assembly: top 5 bits live in the high word,
        // low 16 bits in the low word, with the LSB forced off (word-aligned).
        [[nodiscard]] std::uint32_t merge_pointer(std::uint16_t high, std::uint16_t low) noexcept {
            return ((static_cast<std::uint32_t>(high) & 0x001FU) << 16U) |
                   (static_cast<std::uint32_t>(low) & 0xFFFEU);
        }

        [[nodiscard]] std::uint16_t clamp_volume_register(std::uint16_t value) noexcept {
            std::uint16_t clamped = value & 0x007FU;
            if (clamped > paula::volume_max) {
                clamped = paula::volume_max;
            }
            return clamped;
        }

        // Interrupt source bit for each audio channel (AUD0..AUD3).
        constexpr std::array<std::uint8_t, paula::channel_count> audio_int_source = {0x01U, 0x02U,
                                                                                     0x04U, 0x08U};
    } // namespace

    chip_metadata paula::metadata() const noexcept {
        return {
            .manufacturer = "Commodore",
            .part_number = "8364",
            .family = "PCM",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    std::uint16_t paula::read_reg(int channel, std::uint8_t index) const noexcept {
        if (!channel_valid(channel)) {
            return 0U;
        }
        const voice& ch = channels_[static_cast<std::size_t>(channel)];
        switch (index) {
        case reg_lch:
            return static_cast<std::uint16_t>((ch.lc >> 16U) & 0x001FU);
        case reg_lcl:
            return static_cast<std::uint16_t>(ch.lc & 0xFFFEU);
        case reg_len:
            return ch.len;
        case reg_per:
            return ch.period;
        case reg_vol:
            return ch.volume;
        case reg_dat:
            return ch.dat;
        default:
            return 0U;
        }
    }

    void paula::write_reg(int channel, std::uint8_t index, std::uint16_t value) noexcept {
        if (!channel_valid(channel)) {
            return;
        }
        voice& ch = channels_[static_cast<std::size_t>(channel)];
        switch (index) {
        case reg_lch:
            ch.lc = merge_pointer(value, static_cast<std::uint16_t>(ch.lc));
            break;
        case reg_lcl:
            ch.lc = merge_pointer(static_cast<std::uint16_t>(ch.lc >> 16U), value);
            break;
        case reg_len:
            ch.len = value;
            break;
        case reg_per:
            ch.period = value;
            break;
        case reg_vol: {
            // Paula clamps volume to 0..64 on the DAC path; the low 7 bits hold
            // the written value, then the comparison value is capped at 64.
            ch.volume = clamp_volume_register(value);
            break;
        }
        case reg_dat:
            // Manual-mode data write (used by software that does not drive DMA).
            ch.dat = value;
            if (!dma_master_ || !ch.dma_enable) {
                ch.current_word = value;
                ch.words_remaining = 0U;
                ch.live_pointer = 0U;
                ch.period_counter = 0;
                ch.state = channel_state::manual_ready;
            }
            break;
        default:
            break;
        }
    }

    void paula::set_dma(bool master_enable, std::uint8_t channel_enable_mask) noexcept {
        const bool master_was_on = dma_master_;
        dma_master_ = master_enable;

        for (int i = 0; i < channel_count; ++i) {
            voice& ch = channels_[static_cast<std::size_t>(i)];
            const bool bit = (channel_enable_mask & (1U << i)) != 0U;
            const bool was_running = master_was_on && ch.dma_enable;
            const bool is_running = master_enable && bit;

            ch.dma_enable = bit;

            if (!was_running && is_running) {
                // OFF->ON: reload live fetch state from the latched registers and
                // park in READY so the next step consumes the first fetch.
                ch.live_pointer = ch.lc;
                ch.words_remaining = ch.len;
                ch.state = channel_state::ready;
                ch.modulation_volume_next = true;
            } else if (was_running && !is_running) {
                // ON->OFF: park, preserving latched lc/len/period/volume for a
                // clean re-enable.
                ch.state = channel_state::idle;
                ch.live_pointer = 0U;
                ch.words_remaining = 0U;
            }
        }
    }

    void paula::set_audio_attachment(std::uint8_t volume_mask,
                                     std::uint8_t period_mask) noexcept {
        volume_attach_mask_ = static_cast<std::uint8_t>(volume_mask & 0x0FU);
        period_attach_mask_ = static_cast<std::uint8_t>(period_mask & 0x0FU);
        for (int i = 0; i < channel_count; ++i) {
            if (channel_attached(i)) {
                channels_[static_cast<std::size_t>(i)].last_sample = 0;
            }
        }
    }

    bool paula::channel_active(int channel) const noexcept {
        if (!channel_valid(channel)) {
            return false;
        }
        return channels_[static_cast<std::size_t>(channel)].state != channel_state::idle;
    }

    namespace {
        [[nodiscard]] std::size_t mirrored_chipram_address(std::size_t size,
                                                           std::uint32_t addr) noexcept {
            if (size == 0U) {
                return 0U;
            }
            if ((size & (size - 1U)) == 0U) {
                return static_cast<std::size_t>(addr) & (size - 1U);
            }
            return static_cast<std::size_t>(addr) % size;
        }
    } // namespace

    std::uint16_t paula::fetch_word(std::uint32_t addr) const noexcept {
        if (chipram_.size() < 2U) {
            return 0U;
        }
        const std::size_t a = mirrored_chipram_address(chipram_.size(), addr);
        // Big-endian word: high byte first, then low byte.
        const std::uint16_t hi = chipram_[a];
        const std::uint16_t lo = chipram_[mirrored_chipram_address(chipram_.size(), addr + 1U)];
        return static_cast<std::uint16_t>((hi << 8U) | lo);
    }

    void paula::resize_chipram(std::size_t size) {
        chipram_.assign(size == 0U ? chipram_size : size, 0U);
    }

    void paula::latch_sample(voice& ch, std::int8_t sample) noexcept {
        // Hardware volume 64 -> unity gain on the signed 8-bit sample.
        ch.last_sample = static_cast<std::int16_t>(static_cast<std::int32_t>(sample) *
                                                   static_cast<std::int32_t>(ch.volume));
    }

    bool paula::channel_attached(int channel_index) const noexcept {
        if (!channel_valid(channel_index)) {
            return false;
        }
        const auto bit = static_cast<std::uint8_t>(1U << channel_index);
        return (volume_attach_mask_ & bit) != 0U || (period_attach_mask_ & bit) != 0U;
    }

    void paula::apply_modulation_word(int channel_index, std::uint16_t value) noexcept {
        if (!channel_attached(channel_index) || channel_index >= channel_count - 1) {
            return;
        }
        voice& source = channels_[static_cast<std::size_t>(channel_index)];
        voice& target = channels_[static_cast<std::size_t>(channel_index + 1)];
        const auto bit = static_cast<std::uint8_t>(1U << channel_index);
        const bool volume_attached = (volume_attach_mask_ & bit) != 0U;
        const bool period_attached = (period_attach_mask_ & bit) != 0U;
        if (volume_attached && period_attached) {
            if (source.modulation_volume_next) {
                target.volume = clamp_volume_register(value);
            } else {
                target.period = value;
            }
            source.modulation_volume_next = !source.modulation_volume_next;
        } else if (volume_attached) {
            target.volume = clamp_volume_register(value);
        } else if (period_attached) {
            target.period = value;
        }
    }

    bool paula::advance_channel(int channel_index, voice& ch) noexcept {
        switch (ch.state) {
        case channel_state::idle:
            return false;

        case channel_state::ready:
            // First fetch after DMA came up.
            ch.current_word = fetch_word(ch.live_pointer);
            ch.live_pointer = (ch.live_pointer + 2U) & pointer_mask;
            if (channel_attached(channel_index)) {
                apply_modulation_word(channel_index, ch.current_word);
                ch.last_sample = 0;
                ch.state = channel_state::play_high;
                return true;
            }
            latch_sample(ch, static_cast<std::int8_t>(ch.current_word >> 8U));
            ch.state = channel_state::play_high;
            return true;

        case channel_state::play_high:
            if (channel_attached(channel_index)) {
                ch.last_sample = 0;
                ch.state = channel_state::play_low;
                return true;
            }
            latch_sample(ch, static_cast<std::int8_t>(ch.current_word & 0xFFU));
            ch.state = channel_state::play_low;
            return true;

        case channel_state::play_low:
            // Both samples of the current word are done -- account for it and
            // maybe wrap the buffer.
            if (ch.words_remaining > 0U) {
                --ch.words_remaining;
            }
            if (ch.words_remaining == 0U) {
                // Buffer wrap: reload live state from latched registers and latch
                // the matching AUDxINT request.
                ch.live_pointer = ch.lc;
                ch.words_remaining = ch.len;
                const std::uint8_t source =
                    audio_int_source[static_cast<std::size_t>(channel_index)];
                audio_int_ |= source;
                if (interrupt_callback_) {
                    interrupt_callback_(source);
                }
            }
            ch.current_word = fetch_word(ch.live_pointer);
            ch.live_pointer = (ch.live_pointer + 2U) & pointer_mask;
            if (channel_attached(channel_index)) {
                apply_modulation_word(channel_index, ch.current_word);
                ch.last_sample = 0;
                ch.state = channel_state::play_high;
                return true;
            }
            latch_sample(ch, static_cast<std::int8_t>(ch.current_word >> 8U));
            ch.state = channel_state::play_high;
            return true;

        case channel_state::manual_ready:
            if (channel_attached(channel_index)) {
                apply_modulation_word(channel_index, ch.current_word);
                ch.last_sample = 0;
                ch.state = channel_state::manual_low;
                return true;
            }
            latch_sample(ch, static_cast<std::int8_t>(ch.current_word >> 8U));
            ch.state = channel_state::manual_high;
            return true;

        case channel_state::manual_high:
            if (channel_attached(channel_index)) {
                ch.last_sample = 0;
                ch.state = channel_state::manual_low;
                return true;
            }
            latch_sample(ch, static_cast<std::int8_t>(ch.current_word & 0xFFU));
            ch.state = channel_state::manual_low;
            return true;

        case channel_state::manual_low: {
            const std::uint8_t source = audio_int_source[static_cast<std::size_t>(channel_index)];
            audio_int_ |= source;
            if (interrupt_callback_) {
                interrupt_callback_(source);
            }
            ch.state = channel_state::idle;
            return false;
        }
        }
        return false;
    }

    std::int16_t paula::channel_output(int channel) const noexcept {
        if (!channel_valid(channel)) {
            return 0;
        }
        const voice& ch = channels_[static_cast<std::size_t>(channel)];
        if (channel_attached(channel)) {
            return 0;
        }
        if (ch.state == channel_state::idle || ch.state == channel_state::ready ||
            ch.state == channel_state::manual_ready) {
            return 0;
        }
        return ch.last_sample;
    }

    void paula::step() noexcept {
        // One native sample step == one color clock fed to every active channel.
        for (int i = 0; i < channel_count; ++i) {
            voice& ch = channels_[static_cast<std::size_t>(i)];
            if (ch.state == channel_state::idle) {
                continue;
            }
            if (ch.state == channel_state::ready || ch.state == channel_state::manual_ready) {
                // Zero-cost initial fetch to prime the state machine, then load
                // the first sample's period (floor period 0 at 1 to avoid a stall).
                if (!advance_channel(i, ch)) {
                    continue;
                }
                ch.period_counter = ch.period > 0U ? static_cast<std::int32_t>(ch.period) : 1;
                continue;
            }
            if (ch.period_counter <= 0) {
                ch.period_counter = ch.period > 0U ? static_cast<std::int32_t>(ch.period) : 1;
            }
            --ch.period_counter;
            if (ch.period_counter == 0) {
                if (!advance_channel(i, ch)) {
                    continue;
                }
                ch.period_counter = ch.period > 0U ? static_cast<std::int32_t>(ch.period) : 1;
            }
        }
        // Fixed hardware panning: left = ch0 + ch3, right = ch1 + ch2.
        const std::int32_t left = static_cast<std::int32_t>(channel_output(0)) +
                                  static_cast<std::int32_t>(channel_output(3));
        const std::int32_t right = static_cast<std::int32_t>(channel_output(1)) +
                                   static_cast<std::int32_t>(channel_output(2));
        last_left_ = clamp16(left);
        last_right_ = clamp16(right);
    }

    void paula::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            step();
            buf_lr[i * 2U] = last_left_;
            buf_lr[i * 2U + 1U] = last_right_;
        }
    }

    void paula::tick(std::uint64_t cycles) {
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

    std::size_t paula::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
        // The queue holds interleaved (L,R) int16; counts are in stereo pairs.
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

    void paula::reset(reset_kind kind) {
        // Drop DMA enables and park every channel's live fetch state; the latched
        // register values survive a hard/soft reset on real hardware (they are R-S
        // latches, not cleared by /RESET). Only a cold power-on clears them.
        dma_master_ = false;
        for (voice& ch : channels_) {
            ch.state = channel_state::idle;
            ch.dma_enable = false;
            ch.live_pointer = 0U;
            ch.words_remaining = 0U;
            ch.current_word = 0U;
            ch.period_counter = 0;
            ch.last_sample = 0;
            ch.modulation_volume_next = true;
            if (kind == reset_kind::power_on) {
                ch.lc = 0U;
                ch.len = 0U;
                ch.period = 0U;
                ch.volume = 0U;
                ch.dat = 0U;
            }
        }
        volume_attach_mask_ = 0U;
        period_attach_mask_ = 0U;
        audio_int_ = 0U;
        last_left_ = 0;
        last_right_ = 0;
        prescaler_ = 0;
        // Chip RAM is host-owned external memory; only a cold power-on clears it.
        if (kind == reset_kind::power_on) {
            std::fill(chipram_.begin(), chipram_.end(), std::uint8_t{0U});
        }
        sample_queue_.clear();
    }

    void paula::save_state(state_writer& writer) const {
        for (const auto& ch : channels_) {
            writer.u32(ch.lc);
            writer.u16(ch.len);
            writer.u16(ch.period);
            writer.u16(ch.volume);
            writer.u16(ch.dat);
            writer.u32(ch.live_pointer);
            writer.u16(ch.words_remaining);
            writer.u8(static_cast<std::uint8_t>(ch.state));
            writer.boolean(ch.dma_enable);
            writer.u16(ch.current_word);
            writer.u32(static_cast<std::uint32_t>(ch.period_counter));
            writer.u16(static_cast<std::uint16_t>(ch.last_sample));
            writer.boolean(ch.modulation_volume_next);
        }
        writer.boolean(dma_master_);
        writer.u8(volume_attach_mask_);
        writer.u8(period_attach_mask_);
        writer.u8(audio_int_);
        writer.bytes(chipram_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
        writer.u16(static_cast<std::uint16_t>(last_left_));
        writer.u16(static_cast<std::uint16_t>(last_right_));
    }

    void paula::load_state(state_reader& reader) {
        for (auto& ch : channels_) {
            ch.lc = reader.u32();
            ch.len = reader.u16();
            ch.period = reader.u16();
            ch.volume = reader.u16();
            ch.dat = reader.u16();
            ch.live_pointer = reader.u32();
            ch.words_remaining = reader.u16();
            ch.state = static_cast<channel_state>(reader.u8());
            ch.dma_enable = reader.boolean();
            ch.current_word = reader.u16();
            ch.period_counter = static_cast<std::int32_t>(reader.u32());
            ch.last_sample = static_cast<std::int16_t>(reader.u16());
            ch.modulation_volume_next = reader.boolean();
        }
        dma_master_ = reader.boolean();
        volume_attach_mask_ = static_cast<std::uint8_t>(reader.u8() & 0x0FU);
        period_attach_mask_ = static_cast<std::uint8_t>(reader.u8() & 0x0FU);
        audio_int_ = reader.u8();
        reader.bytes(chipram_);
        clock_divider_ = static_cast<int>(reader.u32());
        prescaler_ = static_cast<int>(reader.u32());
        last_left_ = static_cast<std::int16_t>(reader.u16());
        last_right_ = static_cast<std::int16_t>(reader.u16());
    }

    instrumentation::ichip_introspection& paula::introspection() noexcept { return introspection_; }

    // ---- audio sample extraction ----

    std::span<const instrumentation::sample_view> paula::audio_source_impl::samples() const {
        // The chip's native sample rate scales with AUDxPER; at the nominal NTSC
        // color-clock rate (~3.546 MHz / period). A sample's natural rate is a
        // playback parameter (period), so the stored waveform is exported at the
        // common ~16.7 kHz unity-ish reference; consumers resample as needed.
        constexpr std::uint32_t native_rate = 16726U;

        const auto& ram = owner_->chipram_;
        if (ram.empty()) {
            return samples_;
        }

        struct meta final {
            std::uint32_t start;
            std::size_t off;
            std::size_t len;
        };
        std::vector<meta> metas;

        pcm_.clear();
        // A sample is a chip-RAM region a channel's LC points at; several channels
        // may share one. Decode the distinct regions (deduped by start address)
        // the 4 channels reference rather than near-duplicates.
        for (int ci = 0; ci < paula::channel_count; ++ci) {
            const voice& ch = owner_->channels_[static_cast<std::size_t>(ci)];
            const auto start =
                static_cast<std::uint32_t>(mirrored_chipram_address(ram.size(), ch.lc));
            const std::uint16_t words = ch.len;
            if (words == 0U) {
                continue;
            }
            const bool seen = std::any_of(metas.begin(), metas.end(),
                                          [start](const meta& m) { return m.start == start; });
            if (seen) {
                continue;
            }
            // LEN counts 16-bit words; each carries two signed 8-bit samples.
            const std::size_t byte_count = static_cast<std::size_t>(words) * 2U;
            const std::size_t off = pcm_.size();
            std::size_t decoded = 0U;
            for (std::size_t b = 0; b < byte_count; ++b) {
                const std::size_t a =
                    mirrored_chipram_address(ram.size(), static_cast<std::uint32_t>(start + b));
                const auto s = static_cast<std::int8_t>(ram[a]);
                // Signed 8-bit -> s16 (x256: +-127 -> +-32512).
                pcm_.push_back(static_cast<std::int16_t>(static_cast<std::int32_t>(s) * 256));
                ++decoded;
            }
            metas.push_back({.start = start, .off = off, .len = decoded});
        }

        // names_ is reserved up front so the string_views the samples hold into it
        // never dangle on reallocation; pcm_ is complete, so its spans are stable.
        names_.clear();
        names_.reserve(metas.size());
        samples_.clear();
        samples_.reserve(metas.size());
        for (std::size_t i = 0; i < metas.size(); ++i) {
            std::array<char, 16> buf{};
            std::snprintf(buf.data(), buf.size(), "sample_%05x", metas[i].start);
            names_.emplace_back(buf.data());
            samples_.push_back(instrumentation::sample_view{
                .name = names_[i],
                .frames = std::span<const std::int16_t>(pcm_).subspan(metas[i].off, metas[i].len),
                .sample_rate = native_rate,
                .channels = 1,
                .loop_start = 0, // Paula DMA loops the whole buffer continuously.
                .source_addr = metas[i].start});
        }
        return samples_;
    }

    std::span<const register_descriptor> paula::register_snapshot() noexcept {
        using fmt = register_value_format;
        // Surface channel 0 plus the global DMA/IRQ state; the four channels are
        // symmetric, so a single representative channel keeps the view compact.
        const voice& ch = channels_[0];
        register_view_[0] = {
            "DMA",
            static_cast<std::uint64_t>(dma_master_ ? 1U : 0U) |
                (static_cast<std::uint64_t>(channels_[0].dma_enable ? 1U : 0U) << 1U) |
                (static_cast<std::uint64_t>(channels_[1].dma_enable ? 1U : 0U) << 2U) |
                (static_cast<std::uint64_t>(channels_[2].dma_enable ? 1U : 0U) << 3U) |
                (static_cast<std::uint64_t>(channels_[3].dma_enable ? 1U : 0U) << 4U),
            5U, fmt::flags};
        register_view_[1] = {"AUDINT", audio_int_, 4U, fmt::flags};
        register_view_[2] = {"A0LC", ch.lc, 20U, fmt::unsigned_integer};
        register_view_[3] = {"A0LEN", ch.len, 16U, fmt::unsigned_integer};
        register_view_[4] = {"A0PER", ch.period, 16U, fmt::unsigned_integer};
        register_view_[5] = {"A0VOL", ch.volume, 7U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto paula_registration =
            register_factory("commodore.paula", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<paula>(); });
    } // namespace

} // namespace mnemos::chips::audio
