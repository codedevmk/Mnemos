#include "qsound.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::audio {
    namespace {
        [[nodiscard]] std::int16_t clamp_i16(std::int32_t v) noexcept {
            if (v > 32767) {
                return 32767;
            }
            if (v < -32768) {
                return -32768;
            }
            return static_cast<std::int16_t>(v);
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

    std::int16_t qsound::read_sample(std::uint16_t bank, std::uint16_t addr) const noexcept {
        const std::uint32_t rom_addr = ((static_cast<std::uint32_t>(bank) & 0x7FFFU) << 16U) |
                                       static_cast<std::uint32_t>(addr);
        if (rom_.empty() || rom_addr >= rom_.size()) {
            return 0;
        }
        // 8-bit PCM promoted to the high byte of a signed 16-bit sample.
        return static_cast<std::int16_t>(static_cast<std::uint16_t>(rom_[rom_addr]) << 8U);
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
        for (voice& v : voices_) {
            if (v.volume == 0U || v.rate == 0U) {
                continue;
            }
            const std::int32_t sample = (static_cast<std::int32_t>(v.volume) *
                                         static_cast<std::int32_t>(read_sample(v.bank, v.addr))) >>
                                        14;
            std::int32_t pan = static_cast<std::int32_t>(v.pan & 0x003FU);
            if (pan > 0x20) {
                pan = 0x10;
            }
            left += (sample * (0x20 - pan)) >> 5;
            right += (sample * pan) >> 5;

            std::int32_t phase = (static_cast<std::int32_t>(v.addr) << 12) |
                                 (static_cast<std::int32_t>(v.phase) >> 4);
            phase += static_cast<std::int32_t>(v.rate);
            std::int32_t position = phase >> 12;
            if (position >= static_cast<std::int32_t>(v.end_addr)) {
                if (v.loop_len != 0U) {
                    phase -= static_cast<std::int32_t>(v.loop_len) << 12;
                    position = phase >> 12;
                } else {
                    v.volume = 0U;
                    continue;
                }
            }
            if (phase < 0) {
                phase = 0;
                position = 0;
            }
            v.addr = static_cast<std::uint16_t>(position);
            v.phase = static_cast<std::uint16_t>((phase << 4) & 0xFFFF);
        }
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
        data_latch_ = 0U;
        ready_ = ready_flag;
        last_l_ = 0;
        last_r_ = 0;
    }

    void qsound::save_state(state_writer& writer) const {
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
        writer.u16(data_latch_);
        writer.u8(ready_);
        writer.u16(static_cast<std::uint16_t>(last_l_));
        writer.u16(static_cast<std::uint16_t>(last_r_));
    }

    void qsound::load_state(state_reader& reader) {
        for (voice& v : voices_) {
            v.bank = reader.u16();
            v.addr = reader.u16();
            v.rate = reader.u16();
            v.phase = reader.u16();
            v.loop_len = reader.u16();
            v.end_addr = reader.u16();
            v.volume = reader.u16();
            v.pan = reader.u16();
            v.echo = reader.u16();
        }
        data_latch_ = reader.u16();
        ready_ = reader.u8();
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
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto qsound_registration =
            register_factory("capcom.qsound", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<qsound>(); });
    } // namespace

} // namespace mnemos::chips::audio
