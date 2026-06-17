#include "ssg.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {
    namespace {
        // 16-step volume table (4-bit level -> linear-ish amplitude). The datasheet
        // specifies a ~-3 dB-per-step logarithmic taper; index 0 is silence, index
        // 15 is full scale. Three channels summed must stay inside int16, so peak is
        // chosen so 3 * table[15] does not clip (3 * 9100 = 27300 < 32767).
        constexpr std::array<std::int16_t, 16> vol_table = {
            0, 72, 105, 155, 227, 331, 481, 698, 1014, 1473, 2141, 3111, 4520, 6569, 7910, 9100};

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

    chip_metadata ssg::metadata() const noexcept {
        return {
            .manufacturer = "Yamaha",
            .part_number = "YM2149",
            .family = "SSG",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    // ---- register decode (ported from the Emu reference) ----

    void ssg::decode_channel(int idx) noexcept {
        channel& c = channels_[static_cast<std::size_t>(idx)];
        const auto base = static_cast<std::uint8_t>(idx * 2);
        const auto hi = static_cast<std::uint8_t>(regs_[base + 1U] & 0x0FU);
        c.tone_period =
            static_cast<std::uint16_t>(regs_[base] | (static_cast<std::uint16_t>(hi) << 8U));
        c.level = regs_[reg_a_level + idx];
        c.envelope_mode = (c.level & level_env_mode) != 0U;
        c.volume = static_cast<std::uint8_t>(c.level & level_vol_mask);
        // Mixer bits are active-low: 0 enables the channel path.
        c.tone_enabled = (mixer_ & (mixer_tone_a << idx)) == 0U;
        c.noise_enabled = (mixer_ & (mixer_noise_a << idx)) == 0U;
    }

    void ssg::decode_all() noexcept {
        noise_period_ = static_cast<std::uint8_t>(regs_[reg_noise] & 0x1FU);
        mixer_ = regs_[reg_mixer];
        envelope_period_ = static_cast<std::uint16_t>(
            regs_[reg_env_lo] | (static_cast<std::uint16_t>(regs_[reg_env_hi]) << 8U));
        envelope_shape_ = static_cast<std::uint8_t>(regs_[reg_env_shape] & 0x0FU);
        decode_channel(0);
        decode_channel(1);
        decode_channel(2);
    }

    std::uint8_t ssg::read_reg(std::uint8_t index) const noexcept { return regs_[index & 0x0FU]; }

    void ssg::write_reg(std::uint8_t index, std::uint8_t value) noexcept {
        index &= 0x0FU;
        note_write(index, value);
        // Per chip spec the FREQ_HI nibble, NOISE 5-bit, and ENV_SHAPE nibble carry
        // only their documented bits; the raw byte is stored and masked on decode so
        // a reader sees what was written.
        regs_[index] = value;
        // An envelope-shape write always restarts the envelope generator from the
        // top of its ramp (datasheet: $0D is the only register with this side
        // effect), so it must be detected before decode_all() refreshes the shape.
        const bool shape_write = (index == reg_env_shape);
        decode_all();
        if (shape_write) {
            restart_envelope();
        }
    }

    // ---- envelope shape machine (clean-room per the datasheet) ----

    void ssg::restart_envelope() noexcept {
        // The shape's CONT/ATT/ALT/HOLD bits define a 32-phase ramp the generator
        // walks at the envelope clock. We model it as a 0..15 step plus an `attack`
        // XOR mask that flips the level for descending ramps; ALT/HOLD decide what
        // happens at the end of each 16-step half.
        envelope_counter_ = 0U;
        envelope_step_ = 0U;
        envelope_holding_ = false;
        // ATT bit selects the initial ramp direction: attack (up) XORs nothing,
        // decay (down) XORs 0x0F so step 0 reads as level 15.
        envelope_attack_ = (envelope_shape_ & env_att) != 0U ? 0x00U : 0x0FU;
        envelope_volume_ = static_cast<std::uint8_t>(envelope_step_ ^ envelope_attack_);
    }

    void ssg::step() noexcept {
        // Tone dividers: each channel toggles its square output whenever its
        // countdown reaches zero, reloading the 12-bit period. A period of 0 or 1
        // produces the maximum frequency (output toggles every step) -- the
        // datasheet treats both as a 1-cycle divider.
        for (auto& c : channels_) {
            if (c.tone_counter == 0U) {
                c.tone_counter = c.tone_period;
                c.tone_output ^= 1U;
            } else {
                --c.tone_counter;
            }
        }

        // Noise divider drives the 17-bit LFSR. The noise period is 5-bit; a value
        // of 0 acts as 1. The LFSR taps bit 0 and bit 3 (x^17 + x^14 + 1), feeding
        // the XOR back into the top bit. The output bit is the LFSR's bit 0.
        if (noise_counter_ == 0U) {
            noise_counter_ = noise_period_ != 0U ? noise_period_ : 1U;
            const std::uint32_t feedback = ((noise_lfsr_ ^ (noise_lfsr_ >> 3U)) & 1U);
            noise_lfsr_ = (noise_lfsr_ >> 1U) | (feedback << 16U);
            noise_output_ = static_cast<std::uint8_t>(noise_lfsr_ & 1U);
        } else {
            --noise_counter_;
        }

        // Envelope divider: each expiry advances the 16-step ramp. The generator
        // runs at half the rate of the tone/noise dividers (the datasheet's
        // envelope clock is the master / 256 vs tone master / 16), so the period is
        // scaled by an extra /16 here.
        if (!envelope_holding_) {
            if (envelope_counter_ == 0U) {
                envelope_counter_ = static_cast<std::uint32_t>(
                    (envelope_period_ != 0U ? envelope_period_ : 1U) * 16U);
                if (envelope_step_ >= 15U) {
                    // End of a 16-step half: decide what the shape does next.
                    const bool cont = (envelope_shape_ & env_cont) != 0U;
                    const bool alt = (envelope_shape_ & env_alt) != 0U;
                    const bool hold = (envelope_shape_ & env_hold) != 0U;
                    if (!cont) {
                        // CONT=0: one ramp then silence (the shape collapses to the
                        // 0..15 down-ramp followed by a held zero, independent of the
                        // other bits).
                        envelope_attack_ = 0x0FU;
                        envelope_volume_ = 0U;
                        envelope_holding_ = true;
                    } else if (hold) {
                        // CONT=1, HOLD=1: stop at the terminal level. ALT decides
                        // whether that level is the start or end of the ramp.
                        if (alt) {
                            envelope_attack_ ^= 0x0FU;
                        }
                        envelope_volume_ = static_cast<std::uint8_t>(15U ^ envelope_attack_);
                        envelope_holding_ = true;
                    } else {
                        // CONT=1, HOLD=0: continuous ramp. ALT flips direction each
                        // half (triangle); otherwise it sawtooths back to the start.
                        envelope_step_ = 0U;
                        if (alt) {
                            envelope_attack_ ^= 0x0FU;
                        }
                        envelope_volume_ =
                            static_cast<std::uint8_t>(envelope_step_ ^ envelope_attack_);
                    }
                } else {
                    ++envelope_step_;
                    envelope_volume_ = static_cast<std::uint8_t>(envelope_step_ ^ envelope_attack_);
                }
            } else {
                --envelope_counter_;
            }
        }

        // Mix the three channels. A channel is audible when, for at least one of its
        // enabled paths, the relevant generator output is high. Tone and noise are
        // ANDed against their square/LFSR levels and ORed together (datasheet mixer):
        // a disabled path is treated as "always high" so it never gates the other.
        std::int32_t mix = 0;
        for (const auto& c : channels_) {
            const std::uint8_t tone_high = c.tone_enabled ? c.tone_output : 1U;
            const std::uint8_t noise_high = c.noise_enabled ? noise_output_ : 1U;
            if ((tone_high & noise_high) == 0U) {
                continue; // the gated output holds the channel low this step
            }
            const std::uint8_t level = c.envelope_mode ? envelope_volume_ : c.volume;
            mix += vol_table[level & 0x0FU];
        }

        const auto sample = static_cast<std::int16_t>(clamp16(mix));
        // Mono chip: duplicate the mix onto both stereo lanes (mirrors rf5c68).
        last_left_ = sample;
        last_right_ = sample;
    }

    void ssg::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            step();
            buf_lr[i * 2U] = last_left_;
            buf_lr[i * 2U + 1U] = last_right_;
        }
    }

    void ssg::tick(std::uint64_t cycles) {
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

    std::size_t ssg::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void ssg::reset(reset_kind /*kind*/) {
        regs_ = {};
        // Power-on / reset: the mixer comes up with $FF (all tone + noise inhibited,
        // ports as inputs) per real hardware.
        regs_[reg_mixer] = 0xFFU;
        selected_reg_ = 0U;
        channels_ = {};
        noise_period_ = 0U;
        envelope_period_ = 0U;
        envelope_shape_ = 0U;
        mixer_ = 0xFFU;
        noise_counter_ = 0U;
        noise_output_ = 0U;
        noise_lfsr_ = 1U;
        envelope_counter_ = 0U;
        envelope_step_ = 0U;
        envelope_volume_ = 0U;
        envelope_holding_ = false;
        envelope_attack_ = 0x0FU;
        last_left_ = 0;
        last_right_ = 0;
        prescaler_ = 0;
        sample_queue_.clear();
        decode_all();
    }

    void ssg::save_state(state_writer& writer) const {
        writer.bytes(regs_);
        writer.u8(selected_reg_);
        for (const auto& c : channels_) {
            writer.u16(c.tone_period);
            writer.u8(c.level);
            writer.u8(c.volume);
            writer.boolean(c.envelope_mode);
            writer.boolean(c.tone_enabled);
            writer.boolean(c.noise_enabled);
            writer.u16(c.tone_counter);
            writer.u8(c.tone_output);
        }
        writer.u8(noise_period_);
        writer.u16(envelope_period_);
        writer.u8(envelope_shape_);
        writer.u8(mixer_);
        writer.u16(noise_counter_);
        writer.u8(noise_output_);
        writer.u32(noise_lfsr_);
        writer.u32(envelope_counter_);
        writer.u8(envelope_step_);
        writer.u8(envelope_volume_);
        writer.boolean(envelope_holding_);
        writer.u8(envelope_attack_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
        writer.u16(static_cast<std::uint16_t>(last_left_));
        writer.u16(static_cast<std::uint16_t>(last_right_));
    }

    void ssg::load_state(state_reader& reader) {
        reader.bytes(regs_);
        selected_reg_ = reader.u8();
        for (auto& c : channels_) {
            c.tone_period = reader.u16();
            c.level = reader.u8();
            c.volume = reader.u8();
            c.envelope_mode = reader.boolean();
            c.tone_enabled = reader.boolean();
            c.noise_enabled = reader.boolean();
            c.tone_counter = reader.u16();
            c.tone_output = reader.u8();
        }
        noise_period_ = reader.u8();
        envelope_period_ = reader.u16();
        envelope_shape_ = reader.u8();
        mixer_ = reader.u8();
        noise_counter_ = reader.u16();
        noise_output_ = reader.u8();
        noise_lfsr_ = reader.u32();
        envelope_counter_ = reader.u32();
        envelope_step_ = reader.u8();
        envelope_volume_ = reader.u8();
        envelope_holding_ = reader.boolean();
        envelope_attack_ = reader.u8();
        clock_divider_ = static_cast<int>(reader.u32());
        prescaler_ = static_cast<int>(reader.u32());
        last_left_ = static_cast<std::int16_t>(reader.u16());
        last_right_ = static_cast<std::int16_t>(reader.u16());
    }

    instrumentation::ichip_introspection& ssg::introspection() noexcept { return introspection_; }

    std::span<const register_descriptor> ssg::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"TONE_A", channels_[0].tone_period, 12U, fmt::unsigned_integer};
        register_view_[1] = {"TONE_B", channels_[1].tone_period, 12U, fmt::unsigned_integer};
        register_view_[2] = {"TONE_C", channels_[2].tone_period, 12U, fmt::unsigned_integer};
        register_view_[3] = {"LVL_A", channels_[0].level, 8U, fmt::flags};
        register_view_[4] = {"LVL_B", channels_[1].level, 8U, fmt::flags};
        register_view_[5] = {"LVL_C", channels_[2].level, 8U, fmt::flags};
        register_view_[6] = {"NOISE", noise_period_, 5U, fmt::unsigned_integer};
        register_view_[7] = {"MIXER", mixer_, 8U, fmt::flags};
        register_view_[8] = {"ENV_PER", envelope_period_, 16U, fmt::unsigned_integer};
        register_view_[9] = {"ENV_SHP", envelope_shape_, 4U, fmt::flags};
        register_view_[10] = {"ENV_VOL", envelope_volume_, 4U, fmt::unsigned_integer};
        register_view_[11] = {"NOISE_LFSR", noise_lfsr_, 17U, fmt::unsigned_integer};
        register_view_[12] = {"SEL_REG", selected_reg_, 4U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto ssg_registration =
            register_factory("yamaha.ssg", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<ssg>(); });
    } // namespace

} // namespace mnemos::chips::audio
