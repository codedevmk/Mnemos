#include "adpcm_a.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {
    namespace {
        [[nodiscard]] std::int32_t clamp16(std::int32_t v) noexcept {
            if (v > 32767) {
                return 32767;
            }
            if (v < -32768) {
                return -32768;
            }
            return v;
        }

        // ADPCM step-index adjustment per decoded nibble magnitude (bits 2:0).
        // Negative magnitudes (the lower nibbles) shrink the step; larger ones
        // grow it. The standard ADPCM-A table.
        constexpr std::array<std::int32_t, 8> kIndexAdjust = {-1, -1, -1, -1, 2, 5, 7, 9};

        // ADPCM step-size table (49 entries). Each successive step is ~1.1x the
        // previous; the step index walks this table clamped to [0, 48]. Standard
        // ADPCM-A / IMA-derived step ladder.
        constexpr std::array<std::int32_t, 49> kStepSize = {
            16,  17,  19,  21,  23,  25,  28,  31,  34,  37,  41,   45,   50,   55,   60,  66,  73,
            80,  88,  97,  107, 118, 130, 143, 157, 173, 190, 209,  230,  253,  279,  307, 337, 371,
            408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552};

        constexpr std::int32_t step_index_max = 48;

        // Decode one 4-bit ADPCM nibble against a running accumulator + step
        // index. `accumulator` is the 12-bit signed prediction; `step_index`
        // walks kStepSize. Returns the new 12-bit signed PCM value and mutates
        // both in place. Bit 3 of the nibble is the sign; bits 2:0 the magnitude.
        [[nodiscard]] std::int32_t adpcm_decode_step(std::int32_t& accumulator,
                                                     std::int32_t& step_index,
                                                     std::uint8_t nibble) noexcept {
            const std::int32_t step = kStepSize[static_cast<std::size_t>(step_index)];
            const std::uint8_t mag = nibble & 0x07U;

            // delta = step * (mag + 0.5) / 4, accumulated with the sign bit.
            // Integer form: (step/8) + (step/4)*bit0 + (step/2)*bit1 + step*bit2.
            std::int32_t delta = step >> 3;
            if (mag & 0x04U) {
                delta += step;
            }
            if (mag & 0x02U) {
                delta += step >> 1;
            }
            if (mag & 0x01U) {
                delta += step >> 2;
            }
            if (nibble & 0x08U) {
                accumulator -= delta;
            } else {
                accumulator += delta;
            }

            // The accumulator is a 12-bit signed value: clamp to [-2048, 2047].
            accumulator = std::clamp(accumulator, -2048, 2047);

            // Walk the step index by the magnitude-keyed adjustment, clamped.
            step_index += kIndexAdjust[mag];
            step_index = std::clamp(step_index, 0, step_index_max);

            return accumulator;
        }
    } // namespace

    chip_metadata adpcm_a::metadata() const noexcept {
        return {
            .manufacturer = "Yamaha",
            .part_number = "YM2610-ADPCMA",
            .family = "ADPCM",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    std::uint8_t adpcm_a::read_reg(std::uint8_t index) const noexcept {
        index &= 0x3FU;
        if (index >= reg_count) {
            return 0xFFU; // reserved -- open bus
        }
        return regs_[index];
    }

    void adpcm_a::decode_channel(std::uint8_t channel_index) noexcept {
        if (channel_index >= channel_count) {
            return;
        }
        channel& c = channels_[channel_index];
        const std::uint8_t pan_level = regs_[reg_ch_pan_level + channel_index];
        c.pan_l = (pan_level & 0x80U) != 0U;
        c.pan_r = (pan_level & 0x40U) != 0U;
        c.level = static_cast<std::uint8_t>(pan_level & 0x1FU);

        const auto sl = static_cast<std::uint16_t>(regs_[reg_ch_start_lo + channel_index]);
        const auto sh = static_cast<std::uint16_t>(regs_[reg_ch_start_hi + channel_index]);
        c.start_addr = static_cast<std::uint16_t>(sl | (sh << 8U));

        const auto el = static_cast<std::uint16_t>(regs_[reg_ch_end_lo + channel_index]);
        const auto eh = static_cast<std::uint16_t>(regs_[reg_ch_end_hi + channel_index]);
        c.end_addr = static_cast<std::uint16_t>(el | (eh << 8U));
    }

    void adpcm_a::key_on(std::uint8_t channel_index) noexcept {
        if (channel_index >= channel_count) {
            return;
        }
        channel& c = channels_[channel_index];
        // The 16-bit register address is the high word of a 24-bit ROM address
        // (the low 8 bits are implied zero), so playback starts at start_addr<<8.
        c.cur_addr = static_cast<std::uint32_t>(c.start_addr) << 8U;
        c.high_nibble = false;
        c.accumulator = 0;
        c.step_index = 0;
        c.active = true;
    }

    void adpcm_a::key_off(std::uint8_t channel_index) noexcept {
        if (channel_index >= channel_count) {
            return;
        }
        channels_[channel_index].active = false;
    }

    void adpcm_a::write_reg(std::uint8_t index, std::uint8_t value) noexcept {
        index &= 0x3FU;
        if (index >= reg_count) {
            return;
        }
        regs_[index] = value;

        if (index == reg_key) {
            key_mask_ = value;
            // bit7 set => OFF (DUMP) mode: low 6 bits silence those channels;
            // otherwise ON mode: low 6 bits key those channels on.
            const bool dump = (value & key_dump_mask) != 0U;
            for (std::uint8_t i = 0; i < channel_count; ++i) {
                if ((value & (1U << i)) == 0U) {
                    continue;
                }
                if (dump) {
                    key_off(i);
                } else {
                    key_on(i);
                }
            }
            return;
        }
        if (index == reg_tl) {
            tl_ = static_cast<std::uint8_t>(value & 0x3FU); // master L/R, lower 6 bits
            return;
        }

        // Per-channel register blocks: each spans `channel_count` consecutive
        // offsets from its base. A write re-decodes that channel's fields.
        const std::array<std::uint8_t, 5> bases = {reg_ch_pan_level, reg_ch_start_lo,
                                                   reg_ch_start_hi, reg_ch_end_lo, reg_ch_end_hi};
        for (const std::uint8_t base : bases) {
            if (index >= base && index < base + channel_count) {
                decode_channel(static_cast<std::uint8_t>(index - base));
                return;
            }
        }
    }

    std::uint8_t adpcm_a::next_nibble(channel& c) noexcept {
        if (c.cur_addr >= rom_.size()) {
            c.active = false;
            return 0U;
        }
        const std::uint8_t byte = rom_[c.cur_addr];
        std::uint8_t nibble{};
        if (!c.high_nibble) {
            // High nibble of the byte plays first.
            nibble = static_cast<std::uint8_t>((byte >> 4U) & 0x0FU);
            c.high_nibble = true;
        } else {
            nibble = static_cast<std::uint8_t>(byte & 0x0FU);
            c.high_nibble = false;
            ++c.cur_addr; // both nibbles consumed -> next byte
        }
        return nibble;
    }

    std::int32_t adpcm_a::decode_nibble(channel& c, std::uint8_t nibble) noexcept {
        return adpcm_decode_step(c.accumulator, c.step_index, nibble);
    }

    void adpcm_a::step() noexcept {
        std::int32_t left = 0;
        std::int32_t right = 0;
        // Master Total Level: 6-bit attenuation, 0 = full volume. Model as a
        // linear gain of (63 - tl)/63 to keep the math integer and monotonic.
        const std::int32_t master_gain = 63 - static_cast<std::int32_t>(tl_);

        for (channel& c : channels_) {
            if (!c.active) {
                continue;
            }
            // The end address is inclusive of its 256-byte block; playback stops
            // once we pass (end_addr<<8 | 0xFF).
            const std::uint32_t end_byte = (static_cast<std::uint32_t>(c.end_addr) << 8U) | 0xFFU;
            if (c.cur_addr > end_byte) {
                c.active = false;
                continue;
            }

            const std::uint8_t nibble = next_nibble(c);
            if (!c.active) {
                continue; // ran off the end of the ROM span
            }
            const std::int32_t pcm = decode_nibble(c, nibble); // 12-bit signed

            // Per-channel 5-bit individual level (IL): higher = louder, 0x1F =
            // full volume (0 = silent), then the master TL attenuation.
            const std::int32_t ch_gain = static_cast<std::int32_t>(c.level);
            // 12-bit PCM -> ~16-bit: x16 puts +-2048 near full scale; the
            // per-channel level and master TL are applied as /31 and /63
            // fixed-point divides.
            const std::int32_t scaled = (((pcm * 16) * ch_gain) / 31) * master_gain / 63;
            if (c.pan_l) {
                left += scaled;
            }
            if (c.pan_r) {
                right += scaled;
            }
        }
        last_left_ = static_cast<std::int16_t>(clamp16(left));
        last_right_ = static_cast<std::int16_t>(clamp16(right));
    }

    void adpcm_a::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            step();
            buf_lr[i * 2U] = last_left_;
            buf_lr[i * 2U + 1U] = last_right_;
        }
    }

    void adpcm_a::tick(std::uint64_t cycles) {
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

    std::size_t adpcm_a::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void adpcm_a::reset(reset_kind /*kind*/) {
        regs_ = {};
        channels_ = {};
        tl_ = 0U;
        key_mask_ = 0U;
        last_left_ = 0;
        last_right_ = 0;
        prescaler_ = 0;
        // The external sample ROM is host-owned and survives a chip reset; only
        // the host clears/replaces it via set_sample_rom().
        sample_queue_.clear();
    }

    void adpcm_a::save_state(state_writer& writer) const {
        writer.bytes(regs_);
        for (const auto& c : channels_) {
            writer.boolean(c.pan_l);
            writer.boolean(c.pan_r);
            writer.u8(c.level);
            writer.u16(c.start_addr);
            writer.u16(c.end_addr);
            writer.boolean(c.active);
            writer.u32(c.cur_addr);
            writer.boolean(c.high_nibble);
            writer.u32(static_cast<std::uint32_t>(c.accumulator));
            writer.u32(static_cast<std::uint32_t>(c.step_index));
        }
        writer.u8(tl_);
        writer.u8(key_mask_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
        writer.u16(static_cast<std::uint16_t>(last_left_));
        writer.u16(static_cast<std::uint16_t>(last_right_));
    }

    void adpcm_a::load_state(state_reader& reader) {
        reader.bytes(regs_);
        for (auto& c : channels_) {
            c.pan_l = reader.boolean();
            c.pan_r = reader.boolean();
            c.level = reader.u8();
            c.start_addr = reader.u16();
            c.end_addr = reader.u16();
            c.active = reader.boolean();
            c.cur_addr = reader.u32();
            c.high_nibble = reader.boolean();
            c.accumulator = static_cast<std::int32_t>(reader.u32());
            c.step_index = static_cast<std::int32_t>(reader.u32());
        }
        tl_ = reader.u8();
        key_mask_ = reader.u8();
        clock_divider_ = static_cast<int>(reader.u32());
        prescaler_ = static_cast<int>(reader.u32());
        last_left_ = static_cast<std::int16_t>(reader.u16());
        last_right_ = static_cast<std::int16_t>(reader.u16());
    }

    instrumentation::ichip_introspection& adpcm_a::introspection() noexcept {
        return introspection_;
    }

    // ---- audio sample extraction ----

    std::span<const instrumentation::sample_view> adpcm_a::audio_source_impl::samples() const {
        // The chip's single fixed playback rate (~18.5 kHz, per the datasheet).
        constexpr std::uint32_t native_rate = 18500U;

        const auto rom = owner_->rom_;

        struct meta final {
            std::uint32_t start;
            std::size_t off;
            std::size_t len;
        };
        std::vector<meta> metas;

        pcm_.clear();
        // A sample is a ROM region a channel references (start..end), deduped by
        // start address: several channels may point at the same region.
        for (int ci = 0; ci < adpcm_a::channel_count; ++ci) {
            const channel& c = owner_->channels_[static_cast<std::size_t>(ci)];
            // An untouched channel (both address words zero) is not a sample.
            if (c.start_addr == 0U && c.end_addr == 0U) {
                continue;
            }
            const auto start = static_cast<std::uint32_t>(c.start_addr) << 8U;
            const std::uint32_t end_byte = (static_cast<std::uint32_t>(c.end_addr) << 8U) | 0xFFU;
            if (end_byte <= start || start >= rom.size()) {
                continue;
            }
            const bool seen = std::any_of(metas.begin(), metas.end(),
                                          [start](const meta& m) { return m.start == start; });
            if (seen) {
                continue;
            }
            const std::uint32_t end =
                std::min<std::uint32_t>(end_byte + 1U, static_cast<std::uint32_t>(rom.size()));

            // Decode the region's nibble stream with a fresh predictor (a key-on
            // resets the accumulator + step index on real hardware).
            std::int32_t accumulator = 0;
            std::int32_t step_index = 0;
            const std::size_t off = pcm_.size();
            for (std::uint32_t a = start; a < end; ++a) {
                const std::uint8_t byte = rom[a];
                const std::uint8_t hi = static_cast<std::uint8_t>((byte >> 4U) & 0x0FU);
                const std::uint8_t lo = static_cast<std::uint8_t>(byte & 0x0FU);
                pcm_.push_back(static_cast<std::int16_t>(
                    clamp16(adpcm_decode_step(accumulator, step_index, hi) * 16)));
                pcm_.push_back(static_cast<std::int16_t>(
                    clamp16(adpcm_decode_step(accumulator, step_index, lo) * 16)));
            }
            metas.push_back({.start = start, .off = off, .len = pcm_.size() - off});
        }

        // names_ is reserved up front so the string_views the samples hold into it
        // never dangle on reallocation; pcm_ is complete, so its spans are stable.
        names_.clear();
        names_.reserve(metas.size());
        samples_.clear();
        samples_.reserve(metas.size());
        for (std::size_t i = 0; i < metas.size(); ++i) {
            std::array<char, 16> buf{};
            std::snprintf(buf.data(), buf.size(), "sample_%06x", metas[i].start);
            names_.emplace_back(buf.data());
            samples_.push_back(instrumentation::sample_view{
                .name = names_[i],
                .frames = std::span<const std::int16_t>(pcm_).subspan(metas[i].off, metas[i].len),
                .sample_rate = native_rate,
                .channels = 1,
                .loop_start = -1,
                .source_addr = metas[i].start});
        }
        return samples_;
    }

    std::span<const register_descriptor> adpcm_a::register_snapshot() noexcept {
        using fmt = register_value_format;
        const channel& c = channels_[0];
        register_view_[0] = {"KEY", key_mask_, 8U, fmt::flags};
        register_view_[1] = {"TL", tl_, 6U, fmt::unsigned_integer};
        register_view_[2] = {"CH0_PAN", regs_[reg_ch_pan_level], 8U, fmt::flags};
        register_view_[3] = {"CH0_LEVEL", c.level, 5U, fmt::unsigned_integer};
        register_view_[4] = {"CH0_START", static_cast<std::uint64_t>(c.start_addr) << 8U, 24U,
                             fmt::unsigned_integer};
        register_view_[5] = {"CH0_END", static_cast<std::uint64_t>(c.end_addr) << 8U, 24U,
                             fmt::unsigned_integer};
        register_view_[6] = {"CH0_ADDR", c.cur_addr, 24U, fmt::unsigned_integer};
        register_view_[7] = {"CH0_ACC", static_cast<std::uint64_t>(c.accumulator & 0xFFFFU), 16U,
                             fmt::signed_integer};
        register_view_[8] = {"CH0_STEP", static_cast<std::uint64_t>(c.step_index), 8U,
                             fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto adpcm_a_registration = register_factory(
            "yamaha.adpcm_a", chip_class::audio_synth,
            []() -> std::unique_ptr<ichip> { return std::make_unique<adpcm_a>(); });
    } // namespace

} // namespace mnemos::chips::audio
