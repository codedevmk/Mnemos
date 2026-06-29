#include "adpcm_b.hpp"

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

        // ADPCM-B step-size adaptation. After each nibble the running step is
        // multiplied by a magnitude-keyed factor (encoded as x/64): small
        // magnitudes shrink the step (57/64 ~ 0.89), large magnitudes grow it
        // (up to 153/64 ~ 2.4). These are the datasheet's fixed adaptation
        // factors for the 49-step delta machine, indexed by the nibble's 3-bit
        // magnitude.
        constexpr std::array<std::int32_t, 8> step_scale = {57, 57, 57, 57, 77, 102, 128, 153};

        // The running step is bounded to [127, 24576] per the datasheet.
        constexpr std::int32_t step_min = 127;
        constexpr std::int32_t step_max = 24576;
    } // namespace

    chip_metadata adpcm_b::metadata() const noexcept {
        return {
            .manufacturer = "Yamaha",
            .part_number = "YM2610",
            .family = "ADPCM-B",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    std::uint8_t adpcm_b::read_reg(std::uint8_t index) const noexcept {
        index &= 0x0FU;
        if (index == reg_status) {
            return status_;
        }
        if (index > reg_limit_hi) {
            return 0xFFU; // $0E/$0F reserved -- open bus
        }
        return regs_[index];
    }

    void adpcm_b::decode_ctrl(std::uint8_t value) noexcept {
        if ((value & ctrl_reset) != 0U) {
            active_ = false;
        }
        sp_off_ = (value & ctrl_sp_off) != 0U;
        repeat_ = (value & ctrl_repeat) != 0U;
        memory_mode_ = (value & ctrl_mem) != 0U;
        // START rising re-keys the voice: rewind the stream to START, clear the
        // decoder, drop the EOS flag, and begin playing.
        if ((value & ctrl_start) != 0U) {
            key_on();
        }
    }

    void adpcm_b::write_reg(std::uint8_t index, std::uint8_t value) noexcept {
        index &= 0x0FU;
        if (index > reg_limit_hi || index == reg_status) {
            return; // status is read-only; $0E/$0F are reserved
        }
        regs_[index] = value;
        switch (index) {
        case reg_ctrl:
            decode_ctrl(value);
            break;
        case reg_pan_tl:
            pan_l_ = (value & pan_left) != 0U;
            pan_r_ = (value & pan_right) != 0U;
            tl_ = static_cast<std::uint8_t>(value & tl_mask);
            break;
        case reg_start_lo:
        case reg_start_hi:
            start_addr_ = static_cast<std::uint16_t>(
                regs_[reg_start_lo] | (static_cast<std::uint16_t>(regs_[reg_start_hi]) << 8U));
            break;
        case reg_end_lo:
        case reg_end_hi:
            end_addr_ = static_cast<std::uint16_t>(
                regs_[reg_end_lo] | (static_cast<std::uint16_t>(regs_[reg_end_hi]) << 8U));
            break;
        case reg_limit_lo:
        case reg_limit_hi:
            limit_addr_ = static_cast<std::uint16_t>(
                regs_[reg_limit_lo] | (static_cast<std::uint16_t>(regs_[reg_limit_hi]) << 8U));
            break;
        case reg_delta_lo:
        case reg_delta_hi:
            delta_n_ = static_cast<std::uint16_t>(
                regs_[reg_delta_lo] | (static_cast<std::uint16_t>(regs_[reg_delta_hi]) << 8U));
            break;
        default:
            break;
        }
    }

    void adpcm_b::key_on() noexcept {
        nibble_cursor_ = (static_cast<std::uint32_t>(start_addr_) << 8U) * 2U;
        accumulator_ = 0;
        step_ = step_init;
        phase_ = 0U;
        end_events_ = 0U;
        loop_events_ = 0U;
        rom_underruns_ = 0U;
        status_ = status_busy;
        active_ = true;
    }

    std::uint8_t adpcm_b::next_nibble() noexcept {
        const std::uint32_t byte_addr = nibble_cursor_ >> 1U;
        if (byte_addr >= rom_.size()) {
            active_ = false;
            ++rom_underruns_;
            status_ = static_cast<std::uint8_t>((status_ | status_eos) &
                                                static_cast<std::uint8_t>(~status_busy));
            return 0U;
        }
        const std::uint8_t b = rom_[byte_addr];
        // High nibble is consumed first within each byte (even cursor = high).
        const std::uint8_t n = ((nibble_cursor_ & 1U) == 0U)
                                   ? static_cast<std::uint8_t>((b >> 4U) & 0x0FU)
                                   : static_cast<std::uint8_t>(b & 0x0FU);
        ++nibble_cursor_;
        return n;
    }

    void adpcm_b::advance_nibble() noexcept {
        // Stop / loop at END: the address compared is byte-granular (START..END
        // inclusive of the END byte's two nibbles). The registers hold the high
        // 16 bits of a 24-bit byte address, so END includes its full 256-byte
        // block.
        const std::uint32_t end_byte =
            (static_cast<std::uint32_t>(end_addr_) << 8U) | 0xFFU;
        const std::uint32_t end_nibble = (end_byte + 1U) * 2U;
        if (nibble_cursor_ >= end_nibble) {
            ++end_events_;
            status_ |= status_eos;
            if (repeat_) {
                ++loop_events_;
                nibble_cursor_ = (static_cast<std::uint32_t>(start_addr_) << 8U) * 2U;
                accumulator_ = 0;
                step_ = step_init;
            } else {
                active_ = false;
                status_ &= static_cast<std::uint8_t>(~status_busy);
                return;
            }
        }

        const std::uint8_t nibble = next_nibble();
        const std::int32_t magnitude = nibble & 0x07U;

        // delta = (2*magnitude + 1) * step / 8  -- the datasheet's quantiser.
        std::int32_t delta = ((2 * magnitude + 1) * step_) >> 3;
        if ((nibble & 0x08U) != 0U) {
            delta = -delta;
        }
        accumulator_ = clamp16(accumulator_ + delta);

        // Adapt the running step by the magnitude-keyed factor (x/64), bounded.
        step_ = (step_ * step_scale[static_cast<std::size_t>(magnitude)]) >> 6;
        step_ = std::clamp(step_, step_min, step_max);
    }

    void adpcm_b::step() noexcept {
        if (!active_ || sp_off_) {
            last_left_ = 0;
            last_right_ = 0;
            return;
        }

        // DELTA-N sets the resample rate. The phase accumulator rolls over at
        // 0x10000 (one decoded nibble per carry); DELTA-N is the high word of a
        // 16.8 increment, so DELTA-N = 0x0100 == unity (one nibble per step),
        // smaller values play slower, larger values faster (and may decode
        // several nibbles per step). A zero DELTA-N never advances; treat it as
        // unity so a mis-programmed voice still streams.
        const std::uint32_t delta = delta_n_ != 0U ? static_cast<std::uint32_t>(delta_n_) : 0x0100U;
        phase_ += delta << 8U;
        while (phase_ >= 0x10000U && active_) {
            phase_ -= 0x10000U;
            advance_nibble();
        }

        // The accumulator is the mono 16-bit PCM sample. Apply the 6-bit TL
        // attenuator (0 = full volume, larger = quieter) then pan to L/R.
        const std::int32_t mono = accumulator_;
        const std::int32_t attenuated = (mono * (static_cast<std::int32_t>(tl_mask) - tl_)) >> 6;
        const std::int32_t l = pan_l_ ? attenuated : 0;
        const std::int32_t r = pan_r_ ? attenuated : 0;
        last_left_ = static_cast<std::int16_t>(clamp16(l));
        last_right_ = static_cast<std::int16_t>(clamp16(r));
    }

    void adpcm_b::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            step();
            buf_lr[i * 2U] = last_left_;
            buf_lr[i * 2U + 1U] = last_right_;
        }
    }

    void adpcm_b::tick(std::uint64_t cycles) {
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

    std::size_t adpcm_b::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void adpcm_b::reset(reset_kind kind) {
        regs_ = {};
        active_ = false;
        repeat_ = false;
        sp_off_ = false;
        memory_mode_ = false;
        pan_l_ = false;
        pan_r_ = false;
        tl_ = 0U;
        start_addr_ = 0U;
        end_addr_ = 0U;
        limit_addr_ = 0U;
        delta_n_ = 0U;
        status_ = 0U;
        nibble_cursor_ = 0U;
        accumulator_ = 0;
        step_ = step_init;
        phase_ = 0U;
        end_events_ = 0U;
        loop_events_ = 0U;
        rom_underruns_ = 0U;
        last_left_ = 0;
        last_right_ = 0;
        prescaler_ = 0;
        (void)kind;
        sample_queue_.clear();
    }

    void adpcm_b::save_state(state_writer& writer) const {
        writer.bytes(regs_);
        writer.boolean(active_);
        writer.boolean(repeat_);
        writer.boolean(sp_off_);
        writer.boolean(memory_mode_);
        writer.boolean(pan_l_);
        writer.boolean(pan_r_);
        writer.u8(tl_);
        writer.u16(start_addr_);
        writer.u16(end_addr_);
        writer.u16(limit_addr_);
        writer.u16(delta_n_);
        writer.u8(status_);
        writer.u32(nibble_cursor_);
        writer.u32(static_cast<std::uint32_t>(accumulator_));
        writer.u32(static_cast<std::uint32_t>(step_));
        writer.u32(phase_);
        writer.u32(end_events_);
        writer.u32(loop_events_);
        writer.u32(rom_underruns_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
        writer.u16(static_cast<std::uint16_t>(last_left_));
        writer.u16(static_cast<std::uint16_t>(last_right_));
    }

    void adpcm_b::load_state(state_reader& reader) {
        reader.bytes(regs_);
        active_ = reader.boolean();
        repeat_ = reader.boolean();
        sp_off_ = reader.boolean();
        memory_mode_ = reader.boolean();
        pan_l_ = reader.boolean();
        pan_r_ = reader.boolean();
        tl_ = reader.u8();
        start_addr_ = reader.u16();
        end_addr_ = reader.u16();
        limit_addr_ = reader.u16();
        delta_n_ = reader.u16();
        status_ = reader.u8();
        nibble_cursor_ = reader.u32();
        accumulator_ = static_cast<std::int32_t>(reader.u32());
        step_ = static_cast<std::int32_t>(reader.u32());
        phase_ = reader.u32();
        end_events_ = reader.u32();
        loop_events_ = reader.u32();
        rom_underruns_ = reader.u32();
        clock_divider_ = static_cast<int>(reader.u32());
        prescaler_ = static_cast<int>(reader.u32());
        last_left_ = static_cast<std::int16_t>(reader.u16());
        last_right_ = static_cast<std::int16_t>(reader.u16());
    }

    instrumentation::ichip_introspection& adpcm_b::introspection() noexcept {
        return introspection_;
    }

    // ---- audio sample extraction ----

    std::span<const instrumentation::sample_view> adpcm_b::audio_source_impl::samples() const {
        // The chip's native step rate at unity DELTA-N. The stored waveform's
        // natural playback rate; DELTA-N is a playback parameter, not a property
        // of the sample. ~18.5 kHz is the typical OPNB ADPCM-B base rate.
        constexpr std::uint32_t native_rate = 18500U;

        pcm_.clear();
        names_.clear();
        samples_.clear();

        // Decode the active START..END region with a private decoder snapshot, so
        // extraction never disturbs the live playback cursor.
        const std::uint16_t start = owner_->start_addr_;
        const std::uint16_t end = owner_->end_addr_;
        if (end < start) {
            return samples_;
        }

        const auto rom = owner_->rom_;
        std::int32_t acc = 0;
        std::int32_t step = step_min;
        std::uint32_t cursor = (static_cast<std::uint32_t>(start) << 8U) * 2U;
        const std::uint32_t end_byte = (static_cast<std::uint32_t>(end) << 8U) | 0xFFU;
        const std::uint32_t end_nibble = (end_byte + 1U) * 2U;
        while (cursor < end_nibble) {
            const std::uint32_t byte_addr = cursor >> 1U;
            if (byte_addr >= rom.size()) {
                break;
            }
            const std::uint8_t b = rom[byte_addr];
            const std::uint8_t nibble = ((cursor & 1U) == 0U)
                                            ? static_cast<std::uint8_t>((b >> 4U) & 0x0FU)
                                            : static_cast<std::uint8_t>(b & 0x0FU);
            ++cursor;

            const std::int32_t magnitude = nibble & 0x07U;
            std::int32_t delta = ((2 * magnitude + 1) * step) >> 3;
            if ((nibble & 0x08U) != 0U) {
                delta = -delta;
            }
            acc = clamp16(acc + delta);
            step = std::clamp((step * step_scale[static_cast<std::size_t>(magnitude)]) >> 6,
                              step_min, step_max);
            pcm_.push_back(static_cast<std::int16_t>(acc));
        }
        if (pcm_.empty()) {
            return samples_;
        }

        names_.reserve(1U);
        samples_.reserve(1U);
        std::array<char, 16> buf{};
        std::snprintf(buf.data(), buf.size(), "sample_%04x", start);
        names_.emplace_back(buf.data());
        const int loop = owner_->repeat_ ? 0 : -1;
        samples_.push_back(
            instrumentation::sample_view{.name = names_[0],
                                         .frames = std::span<const std::int16_t>(pcm_),
                                         .sample_rate = native_rate,
                                         .channels = 1,
                                         .loop_start = loop,
                                         .source_addr = static_cast<std::uint32_t>(start) << 8U});
        return samples_;
    }

    std::span<const register_descriptor> adpcm_b::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"CTRL", regs_[reg_ctrl], 8U, fmt::flags};
        register_view_[1] = {"STATUS", status_, 8U, fmt::flags};
        register_view_[2] = {"TL", tl_, 6U, fmt::unsigned_integer};
        register_view_[3] = {"START", start_addr_, 16U, fmt::unsigned_integer};
        register_view_[4] = {"END", end_addr_, 16U, fmt::unsigned_integer};
        register_view_[5] = {"LIMIT", limit_addr_, 16U, fmt::unsigned_integer};
        register_view_[6] = {"DELTA_N", delta_n_, 16U, fmt::unsigned_integer};
        register_view_[7] = {"CURSOR", nibble_cursor_, 32U, fmt::unsigned_integer};
        register_view_[8] = {"ACC",
                             static_cast<std::uint64_t>(static_cast<std::uint16_t>(accumulator_)),
                             16U, fmt::signed_integer};
        register_view_[9] = {"ACTIVE", active_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[10] = {"REPEAT", repeat_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[11] = {"PHASE", phase_, 32U, fmt::unsigned_integer};
        register_view_[12] = {"END_EVENTS", end_events_, 32U, fmt::unsigned_integer};
        register_view_[13] = {"LOOP_EVENTS", loop_events_, 32U, fmt::unsigned_integer};
        register_view_[14] = {"ROM_UNDERRUNS", rom_underruns_, 32U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto adpcm_b_registration = register_factory(
            "yamaha.adpcm_b", chip_class::audio_synth,
            []() -> std::unique_ptr<ichip> { return std::make_unique<adpcm_b>(); });
    } // namespace

} // namespace mnemos::chips::audio
