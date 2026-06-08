#include "rf5c68.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
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
    } // namespace

    chip_metadata rf5c68::metadata() const noexcept {
        return {
            .manufacturer = "Ricoh",
            .part_number = "RF5C164",
            .family = "PCM",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    std::uint8_t rf5c68::read_reg(std::uint8_t index) const noexcept {
        index &= 0x0FU;
        if (index > 8U) {
            return 0xFFU; // reserved -- open bus
        }
        return regs_[index];
    }

    void rf5c68::apply_voice_write(std::uint8_t index, std::uint8_t value) noexcept {
        voice& v = voices_[selected_voice_];
        switch (index) {
        case reg_env:
            v.envelope = value;
            break;
        case reg_pan:
            v.pan = value;
            break;
        case reg_fdl:
            v.freq_divider = static_cast<std::uint16_t>((v.freq_divider & 0xFF00U) | value);
            break;
        case reg_fdh:
            v.freq_divider = static_cast<std::uint16_t>((v.freq_divider & 0x00FFU) | (value << 8U));
            break;
        case reg_lsl:
            v.loop_start = static_cast<std::uint16_t>((v.loop_start & 0xFF00U) | value);
            break;
        case reg_lsh:
            v.loop_start = static_cast<std::uint16_t>((v.loop_start & 0x00FFU) | (value << 8U));
            break;
        case reg_st:
            v.start_high = value;
            // RF5C164: an ST write moves the live play address only while the
            // voice is stopped (channel OFF); a sounding voice keeps its address.
            if (v.muted) {
                v.sample_pos = static_cast<std::uint16_t>(value << 8U);
                v.sample_frac = 0U;
            }
            break;
        default:
            break;
        }
    }

    void rf5c68::write_reg(std::uint8_t index, std::uint8_t value) noexcept {
        index &= 0x0FU;
        if (index > 8U) {
            return;
        }
        regs_[index] = value;
        switch (index) {
        case reg_ctrl:
            enabled_ = (value & ctrl_enable) != 0U;
            bank_index_ = static_cast<std::uint8_t>(value & ctrl_bank_mask);
            selected_voice_ = static_cast<std::uint8_t>((value >> 4U) & 0x07U);
            break;
        case reg_chan:
            channel_mute_ = value;
            // $08 ON/OFF (bit set = channel OFF). Turning a channel ON keys the
            // voice so it sounds from its current play address; turning it OFF
            // reloads that address to the start, so the next ON restarts it.
            // This is the only key-on path real Sega CD software uses.
            for (int i = 0; i < voice_count; ++i) {
                voice& v = voices_[static_cast<std::size_t>(i)];
                const bool muted = (value & (1U << i)) != 0U;
                v.muted = muted;
                if (muted) {
                    v.sample_pos = static_cast<std::uint16_t>(v.start_high << 8U);
                    v.sample_frac = 0U;
                } else {
                    v.active = true;
                }
            }
            break;
        default:
            apply_voice_write(index, value);
            break;
        }
    }

    std::uint8_t rf5c68::read_waveram(std::uint16_t window_offset) const noexcept {
        const std::uint32_t addr =
            ((static_cast<std::uint32_t>(bank_index_) << 12U) | (window_offset & 0x0FFFU)) &
            (waveram_size - 1U);
        return waveram_[addr];
    }

    void rf5c68::write_waveram(std::uint16_t window_offset, std::uint8_t value) noexcept {
        const std::uint32_t addr =
            ((static_cast<std::uint32_t>(bank_index_) << 12U) | (window_offset & 0x0FFFU)) &
            (waveram_size - 1U);
        waveram_[addr] = value;
    }

    void rf5c68::key_on(std::uint8_t voice_index) noexcept {
        if (voice_index >= voice_count) {
            return;
        }
        voice& v = voices_[voice_index];
        v.sample_pos = static_cast<std::uint16_t>(v.start_high << 8U);
        v.sample_frac = 0U;
        v.active = true;
    }

    void rf5c68::step() noexcept {
        if (!enabled_) {
            last_left_ = 0;
            last_right_ = 0;
            return;
        }
        std::int32_t left = 0;
        std::int32_t right = 0;
        for (voice& v : voices_) {
            if (v.muted || !v.active) {
                continue;
            }
            // RF5C68 samples are sign-magnitude (bit 7 = sign); 0xFF is the
            // loop/stop sentinel, distinct from -127 PCM data.
            std::uint8_t b = waveram_[v.sample_pos];
            if (b == 0xFFU) {
                v.sample_pos = v.loop_start;
                b = waveram_[v.sample_pos];
                if (b == 0xFFU) {
                    v.active = false;
                    continue;
                }
            }
            const auto mag = static_cast<std::int16_t>(b & 0x7FU);
            const auto sample = static_cast<std::int16_t>((b & 0x80U) ? -mag : mag);

            // Envelope (linear 0..255) then pan-split: high nibble = right gain,
            // low nibble = left gain.
            const std::int32_t scaled = (static_cast<std::int32_t>(sample) * v.envelope) >> 2;
            const auto pan_l = static_cast<std::int32_t>(v.pan & 0x0FU);
            const auto pan_r = static_cast<std::int32_t>((v.pan >> 4U) & 0x0FU);
            left += (scaled * pan_l) >> 4;
            right += (scaled * pan_r) >> 4;

            // Advance the address accumulator: the 16-bit freq divider feeds an
            // 11-bit fractional counter whose carry rolls into the integer
            // sample position.
            const std::uint32_t acc = static_cast<std::uint32_t>(v.sample_frac) + v.freq_divider;
            v.sample_pos = static_cast<std::uint16_t>((v.sample_pos + (acc >> 11U)) & 0xFFFFU);
            v.sample_frac = static_cast<std::uint16_t>(acc & 0x07FFU);
        }
        last_left_ = static_cast<std::int16_t>(clamp16(left));
        last_right_ = static_cast<std::int16_t>(clamp16(right));
    }

    void rf5c68::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            step();
            buf_lr[i * 2U] = last_left_;
            buf_lr[i * 2U + 1U] = last_right_;
        }
    }

    void rf5c68::tick(std::uint64_t cycles) {
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

    std::size_t rf5c68::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void rf5c68::reset(reset_kind kind) {
        regs_ = {};
        voices_ = {}; // each voice's `muted` default-initialises to true
        selected_voice_ = 0U;
        enabled_ = false;
        bank_index_ = 0U;
        channel_mute_ = 0xFFU;
        last_left_ = 0;
        last_right_ = 0;
        prescaler_ = 0;
        // Wave RAM survives a hard/soft reset on real hardware; only a cold
        // power-on clears it.
        if (kind == reset_kind::power_on) {
            waveram_.fill(0U);
        }
        sample_queue_.clear();
    }

    void rf5c68::save_state(state_writer& writer) const {
        writer.bytes(regs_);
        for (const auto& v : voices_) {
            writer.u8(v.envelope);
            writer.u8(v.pan);
            writer.u16(v.freq_divider);
            writer.u16(v.loop_start);
            writer.u8(v.start_high);
            writer.boolean(v.muted);
            writer.u16(v.sample_pos);
            writer.u16(v.sample_frac);
            writer.boolean(v.active);
        }
        writer.u8(selected_voice_);
        writer.boolean(enabled_);
        writer.u8(bank_index_);
        writer.u8(channel_mute_);
        writer.bytes(waveram_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
        writer.u16(static_cast<std::uint16_t>(last_left_));
        writer.u16(static_cast<std::uint16_t>(last_right_));
    }

    void rf5c68::load_state(state_reader& reader) {
        reader.bytes(regs_);
        for (auto& v : voices_) {
            v.envelope = reader.u8();
            v.pan = reader.u8();
            v.freq_divider = reader.u16();
            v.loop_start = reader.u16();
            v.start_high = reader.u8();
            v.muted = reader.boolean();
            v.sample_pos = reader.u16();
            v.sample_frac = reader.u16();
            v.active = reader.boolean();
        }
        selected_voice_ = reader.u8();
        enabled_ = reader.boolean();
        bank_index_ = reader.u8();
        channel_mute_ = reader.u8();
        reader.bytes(waveram_);
        clock_divider_ = static_cast<int>(reader.u32());
        prescaler_ = static_cast<int>(reader.u32());
        last_left_ = static_cast<std::int16_t>(reader.u16());
        last_right_ = static_cast<std::int16_t>(reader.u16());
    }

    instrumentation::ichip_introspection& rf5c68::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> rf5c68::register_snapshot() noexcept {
        using fmt = register_value_format;
        const voice& v = voices_[selected_voice_];
        register_view_[0] = {"CTRL", regs_[reg_ctrl], 8U, fmt::flags};
        register_view_[1] = {"CHAN", channel_mute_, 8U, fmt::flags};
        register_view_[2] = {"VOICE", selected_voice_, 3U, fmt::unsigned_integer};
        register_view_[3] = {"ENV", v.envelope, 8U, fmt::unsigned_integer};
        register_view_[4] = {"PAN", v.pan, 8U, fmt::flags};
        register_view_[5] = {"FREQ", v.freq_divider, 16U, fmt::unsigned_integer};
        register_view_[6] = {"LOOP", v.loop_start, 16U, fmt::unsigned_integer};
        register_view_[7] = {"START", static_cast<std::uint64_t>(v.start_high) << 8U, 16U,
                             fmt::unsigned_integer};
        register_view_[8] = {"POS", v.sample_pos, 16U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto rf5c68_registration =
            register_factory("ricoh.rf5c164", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<rf5c68>(); });
    } // namespace

} // namespace mnemos::chips::audio
