#include "qsound.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::audio {
    namespace {
        constexpr std::uint32_t qsound_state_magic = 0x32445351U; // "QSD2", little-endian
        constexpr std::uint32_t qsound_state_version = 2U;
        constexpr std::array<std::int16_t, 16> adpcm_step_scale = {
            154, 154, 128, 102, 77, 58, 58, 58,
            58,  58,  58,  58, 77, 102, 128, 154,
        };

        [[nodiscard]] std::int16_t clamp_i16(std::int32_t v) noexcept {
            if (v > 32767) {
                return 32767;
            }
            if (v < -32768) {
                return -32768;
            }
            return static_cast<std::int16_t>(v);
        }

        [[nodiscard]] std::int32_t clamp_i32(std::int32_t v, std::int32_t min_value,
                                             std::int32_t max_value) noexcept {
            if (v > max_value) {
                return max_value;
            }
            if (v < min_value) {
                return min_value;
            }
            return v;
        }

        [[nodiscard]] std::int16_t clamp_i64_to_i16(std::int64_t v) noexcept {
            if (v > 32767) {
                return 32767;
            }
            if (v < -32768) {
                return -32768;
            }
            return static_cast<std::int16_t>(v);
        }

        [[nodiscard]] int sign_extend_adpcm_nibble(std::uint8_t nibble) noexcept {
            const int value = static_cast<int>(nibble & 0x0FU);
            return value >= 8 ? value - 16 : value;
        }

        void pcm_pan_gains(std::uint16_t pan_reg, std::int32_t& left_gain,
                           std::int32_t& right_gain) noexcept {
            std::int32_t pan = static_cast<std::int32_t>(pan_reg & 0x003FU) - 0x10;
            if (pan < 0) {
                pan = 0;
            } else if (pan > 0x20) {
                pan = 0x20;
            }
            left_gain = 0x20 - pan;
            right_gain = pan;
        }
    } // namespace

    chip_metadata qsound::metadata() const noexcept {
        return {
            .manufacturer = "Capcom",
            .part_number = "DL-1425",
            .family = "QSound",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    std::uint8_t qsound::read_sample_u8(std::uint32_t rom_addr) const noexcept {
        if (rom_.empty() || rom_addr >= rom_.size()) {
            return 0U;
        }
        return rom_[rom_addr];
    }

    std::int16_t qsound::read_sample(std::uint16_t bank, std::uint16_t addr) const noexcept {
        const std::uint32_t rom_addr = ((static_cast<std::uint32_t>(bank) & 0x7FFFU) << 16U) |
                                       static_cast<std::uint32_t>(addr);
        // 8-bit PCM promoted to the high byte of a signed 16-bit sample.
        return static_cast<std::int16_t>(static_cast<std::uint16_t>(read_sample_u8(rom_addr))
                                         << 8U);
    }

    std::uint8_t qsound::read_adpcm_nibble(const adpcm_voice& voice,
                                           std::uint32_t nibble) const noexcept {
        const std::uint32_t rom_addr =
            ((static_cast<std::uint32_t>(voice.bank) & 0x7FFFU) << 16U) |
            static_cast<std::uint32_t>(voice.cur_addr);
        const std::uint8_t value = read_sample_u8(rom_addr);
        return (nibble & 1U) == 0U ? static_cast<std::uint8_t>((value >> 4U) & 0x0FU)
                                   : static_cast<std::uint8_t>(value & 0x0FU);
    }

    std::int16_t qsound::step_adpcm(adpcm_voice& voice,
                                    std::uint32_t nibble_phase) noexcept {
        if (voice.flag == 0U && voice.play_volume == 0U) {
            voice.last_sample = 0;
            return 0;
        }

        if ((nibble_phase & 1U) == 0U) {
            if (voice.cur_addr == voice.end_addr) {
                voice.play_volume = 0U;
                voice.cur_vol = 0;
                voice.last_sample = 0;
            }

            if (voice.flag != 0U && voice.volume != 0U) {
                voice.flag = 0U;
                voice.play_volume = voice.volume;
                voice.cur_addr = voice.start_addr;
                voice.step_size = 10;
                voice.cur_vol = 0;
                voice.last_sample = 0;
            }
        }

        if (voice.play_volume == 0U) {
            voice.last_sample = 0;
            return 0;
        }

        if (voice.step_size < adpcm_min_step_size) {
            voice.step_size = adpcm_min_step_size;
        } else if (voice.step_size > adpcm_max_step_size) {
            voice.step_size = adpcm_max_step_size;
        }

        const int step = sign_extend_adpcm_nibble(read_adpcm_nibble(voice, nibble_phase));
        std::int32_t delta =
            (1 + (step < 0 ? -step : step) * 2) * static_cast<std::int32_t>(voice.step_size);
        delta >>= 1;
        if (step <= 0) {
            delta = -delta;
        }

        const std::int32_t predictor =
            clamp_i32(static_cast<std::int32_t>(voice.cur_vol) + delta, -32768, 32767);
        voice.cur_vol = static_cast<std::int16_t>(predictor);

        voice.step_size = static_cast<std::int16_t>(
            (static_cast<std::int32_t>(adpcm_step_scale[static_cast<std::size_t>(8 + step)]) *
             static_cast<std::int32_t>(voice.step_size)) >>
            6);
        if (voice.step_size < adpcm_min_step_size) {
            voice.step_size = adpcm_min_step_size;
        } else if (voice.step_size > adpcm_max_step_size) {
            voice.step_size = adpcm_max_step_size;
        }

        if ((nibble_phase & 1U) != 0U) {
            ++voice.cur_addr;
        }

        const std::int32_t sample =
            (predictor * static_cast<std::int32_t>(voice.play_volume)) >> 16;
        voice.last_sample = clamp_i16(sample);
        return voice.last_sample;
    }

    void qsound::write_register(std::uint8_t reg, std::uint16_t data) noexcept {
        if (reg < 0x80U) {
            const std::uint32_t voice_index = static_cast<std::uint32_t>(reg) >> 3U;
            const std::uint8_t voice_reg = static_cast<std::uint8_t>(reg & 0x07U);
            voice& v = voices_[voice_index];
            switch (voice_reg) {
            case 0:
                // The bank register programs the NEXT voice's bank (hardware quirk).
                voices_[(voice_index + 1U) & 0x0FU].bank = data;
                break;
            case 1:
                v.addr = data;
                break;
            case 2:
                v.rate = data;
                break;
            case 3:
                v.phase = data;
                break;
            case 4:
                v.loop_len = data;
                break;
            case 5:
                v.end_addr = data;
                break;
            case 6:
                v.volume = data;
                break;
            default:
                break;
            }
            return;
        }
        if (reg >= 0x80U && reg < 0x80U + voice_count) {
            voices_[reg - 0x80U].pan = data;
            return;
        }
        if (reg == 0x93U) {
            echo_feedback_ = static_cast<std::int16_t>(data);
            return;
        }
        if (reg >= 0xCAU && reg < 0xCAU + adpcm_voice_count * 4U) {
            const std::uint32_t voice_index = static_cast<std::uint32_t>((reg - 0xCAU) >> 2U);
            const std::uint8_t voice_reg = static_cast<std::uint8_t>((reg - 0xCAU) & 0x03U);
            adpcm_voice& v = adpcm_[voice_index];
            switch (voice_reg) {
            case 0:
                v.start_addr = data;
                break;
            case 1:
                v.end_addr = data;
                break;
            case 2:
                v.bank = data;
                break;
            case 3:
                v.volume = data;
                break;
            default:
                break;
            }
            return;
        }
        if (reg >= 0xD6U && reg < 0xD6U + adpcm_voice_count) {
            adpcm_[reg - 0xD6U].flag = data;
            return;
        }
        if (reg == 0xD9U) {
            echo_end_pos_ = data;
            if (echo_delay_pos_ >= echo_delay_length()) {
                echo_delay_pos_ = 0U;
            }
            return;
        }
        if (reg >= 0xBAU && reg < 0xBAU + voice_count) {
            voices_[reg - 0xBAU].echo = data;
        }
    }

    void qsound::write_port(std::uint8_t offset, std::uint8_t value) noexcept {
        switch (offset & 0x03U) {
        case 0:
            data_latch_ = static_cast<std::uint16_t>((data_latch_ & 0x00FFU) |
                                                     (static_cast<std::uint16_t>(value) << 8U));
            break;
        case 1:
            data_latch_ = static_cast<std::uint16_t>((data_latch_ & 0xFF00U) | value);
            break;
        case 2:
            write_register(value, data_latch_);
            ready_ = ready_flag;
            break;
        default:
            break;
        }
    }

    void qsound::step() noexcept {
        std::int32_t left = 0;
        std::int32_t right = 0;
        std::int64_t echo_input = 0;
        for (voice& v : voices_) {
            if (v.volume == 0U) {
                continue;
            }
            const std::int32_t sample = (static_cast<std::int32_t>(v.volume) *
                                         static_cast<std::int32_t>(read_sample(v.bank, v.addr))) >>
                                        14;
            echo_input += static_cast<std::int64_t>(sample) *
                          static_cast<std::int16_t>(v.echo) * 4;
            std::int32_t left_gain = 0;
            std::int32_t right_gain = 0;
            pcm_pan_gains(v.pan, left_gain, right_gain);
            left += (sample * left_gain) >> 5;
            right += (sample * right_gain) >> 5;

            std::int32_t phase = (static_cast<std::int32_t>(v.addr) << 12) |
                                 (static_cast<std::int32_t>(v.phase) >> 4);
            phase += static_cast<std::int32_t>(v.rate);
            if ((phase >> 12) >= static_cast<std::int32_t>(v.end_addr)) {
                phase -= static_cast<std::int32_t>(v.loop_len) << 12;
            }
            phase = clamp_i32(phase, -0x08000000, 0x07FFFFFF);
            v.addr = static_cast<std::uint16_t>(static_cast<std::uint32_t>(phase) >> 12U);
            v.phase = static_cast<std::uint16_t>((phase << 4) & 0xFFFF);
        }

        const std::uint32_t adpcm_period = adpcm_voice_count * 2U;
        const std::uint32_t phase = adpcm_phase_ % adpcm_period;
        const std::uint32_t voice_index = phase % adpcm_voice_count;
        const std::uint32_t nibble_phase = phase / adpcm_voice_count;
        (void)step_adpcm(adpcm_[voice_index], nibble_phase);
        ++adpcm_phase_;

        for (const adpcm_voice& v : adpcm_) {
            left += v.last_sample;
            right += v.last_sample;
        }
        const std::int16_t echo_sample = apply_echo(echo_input);
        left += echo_sample;
        right += echo_sample;
        last_l_ = clamp_i16(left >> mix_shift);
        last_r_ = clamp_i16(right >> mix_shift);
        ready_ = ready_flag;
    }

    void qsound::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            step();
            buf_lr[i * 2U] = last_l_;
            buf_lr[i * 2U + 1U] = last_r_;
        }
    }

    void qsound::tick(std::uint64_t /*cycles*/) {
        // QSound emits at a fixed ~24 kHz output rate independent of the CPU clock;
        // the board drains it via generate()/step() rather than per-cycle ticking.
    }

    void qsound::reset(reset_kind /*kind*/) {
        // The sample ROM is host-owned (never cleared here); every reset kind
        // clears the same voice + latch state back to the power-on defaults (each
        // voice's bank = 0x8000 and pan = default_pan via its member initialisers).
        for (voice& v : voices_) {
            v = voice{};
        }
        for (adpcm_voice& v : adpcm_) {
            v = adpcm_voice{};
        }
        data_latch_ = 0U;
        ready_ = ready_flag;
        adpcm_phase_ = 0U;
        reset_echo_state();
        last_l_ = 0;
        last_r_ = 0;
    }

    void qsound::reset_echo_state() noexcept {
        echo_delay_.fill(0);
        echo_end_pos_ = echo_delay_base + 6U;
        echo_feedback_ = 0;
        echo_delay_pos_ = 0U;
        echo_last_sample_ = 0;
    }

    std::uint16_t qsound::echo_delay_length() const noexcept {
        if (echo_end_pos_ <= echo_delay_base) {
            return 0U;
        }
        const std::uint32_t length =
            static_cast<std::uint32_t>(echo_end_pos_) - echo_delay_base;
        return static_cast<std::uint16_t>(
            length > echo_delay_capacity ? echo_delay_capacity : length);
    }

    std::int16_t qsound::apply_echo(std::int64_t input) noexcept {
        const std::uint16_t length = echo_delay_length();
        if (length == 0U) {
            echo_delay_pos_ = 0U;
            echo_last_sample_ = 0;
            return 0;
        }
        if (echo_delay_pos_ >= length) {
            echo_delay_pos_ = 0U;
        }

        const std::int32_t delayed = echo_delay_[echo_delay_pos_];
        const std::int32_t previous = echo_last_sample_;
        echo_last_sample_ = static_cast<std::int16_t>(delayed);
        const std::int32_t averaged = (delayed + previous) / 2;

        const std::int64_t feedback =
            static_cast<std::int64_t>(averaged) * echo_feedback_ * 4;
        echo_delay_[echo_delay_pos_] = clamp_i64_to_i16((input + feedback) / 65536);
        ++echo_delay_pos_;
        if (echo_delay_pos_ >= length) {
            echo_delay_pos_ = 0U;
        }
        return static_cast<std::int16_t>(averaged);
    }

    void qsound::save_state(state_writer& writer) const {
        writer.u32(qsound_state_magic);
        writer.u32(qsound_state_version);
        for (const voice& v : voices_) {
            writer.u16(v.bank);
            writer.u16(v.addr);
            writer.u16(v.rate);
            writer.u16(v.phase);
            writer.u16(v.loop_len);
            writer.u16(v.end_addr);
            writer.u16(v.volume);
            writer.u16(v.pan);
            writer.u16(v.echo);
        }
        for (const adpcm_voice& v : adpcm_) {
            writer.u16(v.start_addr);
            writer.u16(v.end_addr);
            writer.u16(v.bank);
            writer.u16(v.volume);
            writer.u16(v.play_volume);
            writer.u16(v.flag);
            writer.u16(v.cur_addr);
            writer.u16(static_cast<std::uint16_t>(v.step_size));
            writer.u16(static_cast<std::uint16_t>(v.cur_vol));
            writer.u16(static_cast<std::uint16_t>(v.last_sample));
        }
        writer.u16(data_latch_);
        writer.u8(ready_);
        writer.u32(adpcm_phase_);
        writer.u16(echo_end_pos_);
        writer.u16(static_cast<std::uint16_t>(echo_feedback_));
        writer.u16(echo_delay_pos_);
        writer.u16(static_cast<std::uint16_t>(echo_last_sample_));
        for (const std::int16_t sample : echo_delay_) {
            writer.u16(static_cast<std::uint16_t>(sample));
        }
        writer.u16(static_cast<std::uint16_t>(last_l_));
        writer.u16(static_cast<std::uint16_t>(last_r_));
    }

    void qsound::load_state(state_reader& reader) {
        const std::uint32_t marker = reader.u32();
        const bool legacy = marker != qsound_state_magic;
        std::uint32_t version = 0U;
        if (!legacy) {
            version = reader.u32();
            if (version == 0U || version > qsound_state_version) {
                reader.fail();
                return;
            }
        }
        for (std::size_t i = 0; i < voices_.size(); ++i) {
            voice& v = voices_[i];
            if (legacy && i == 0U) {
                v.bank = static_cast<std::uint16_t>(marker & 0xFFFFU);
                v.addr = static_cast<std::uint16_t>(marker >> 16U);
            } else {
                v.bank = reader.u16();
                v.addr = reader.u16();
            }
            v.rate = reader.u16();
            v.phase = reader.u16();
            v.loop_len = reader.u16();
            v.end_addr = reader.u16();
            v.volume = reader.u16();
            v.pan = reader.u16();
            v.echo = reader.u16();
        }
        if (legacy) {
            for (adpcm_voice& v : adpcm_) {
                v = adpcm_voice{};
            }
        } else {
            for (adpcm_voice& v : adpcm_) {
                v.start_addr = reader.u16();
                v.end_addr = reader.u16();
                v.bank = reader.u16();
                v.volume = reader.u16();
                v.play_volume = reader.u16();
                v.flag = reader.u16();
                v.cur_addr = reader.u16();
                v.step_size = static_cast<std::int16_t>(reader.u16());
                v.cur_vol = static_cast<std::int16_t>(reader.u16());
                v.last_sample = static_cast<std::int16_t>(reader.u16());
            }
        }
        data_latch_ = reader.u16();
        ready_ = reader.u8();
        adpcm_phase_ = legacy ? 0U : reader.u32();
        if (!legacy && version >= 2U) {
            echo_end_pos_ = reader.u16();
            echo_feedback_ = static_cast<std::int16_t>(reader.u16());
            echo_delay_pos_ = reader.u16();
            echo_last_sample_ = static_cast<std::int16_t>(reader.u16());
            for (std::int16_t& sample : echo_delay_) {
                sample = static_cast<std::int16_t>(reader.u16());
            }
            if (echo_delay_pos_ >= echo_delay_length()) {
                echo_delay_pos_ = 0U;
            }
        } else {
            reset_echo_state();
        }
        last_l_ = static_cast<std::int16_t>(reader.u16());
        last_r_ = static_cast<std::int16_t>(reader.u16());
    }

    instrumentation::ichip_introspection& qsound::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> qsound::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"READY", ready_, 8U, fmt::flags};
        register_view_[1] = {"V0BANK", voices_[0].bank, 16U, fmt::unsigned_integer};
        register_view_[2] = {"V0ADDR", voices_[0].addr, 16U, fmt::unsigned_integer};
        register_view_[3] = {"V0VOL", voices_[0].volume, 16U, fmt::unsigned_integer};
        register_view_[4] = {"ECHOFB", static_cast<std::uint16_t>(echo_feedback_), 16U,
                             fmt::signed_integer};
        register_view_[5] = {"ECHOLEN", echo_delay_length(), 16U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto qsound_registration =
            register_factory("capcom.qsound", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<qsound>(); });
    } // namespace

} // namespace mnemos::chips::audio
