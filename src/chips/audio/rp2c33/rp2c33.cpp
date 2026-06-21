#include "rp2c33.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {

    namespace {
        [[nodiscard]] std::int16_t clip16(int v) noexcept {
            if (v > 32767) {
                return 32767;
            }
            if (v < -32768) {
                return -32768;
            }
            return static_cast<std::int16_t>(v);
        }

        // Master attenuator: $4089 bits 1-0 select full, 2/3, 2/4, 2/5 (fixed-point /64).
        constexpr std::array<int, 4> k_master_mul = {64, 42, 32, 26};

        // The mod table's 3-bit entry adjusts the signed mod counter (the RP2C33's
        // documented increment map): 0->0, 1->+1, 2->+2, 3->+4, 4->reset, then the
        // negatives 5->-4, 6->-2, 7->-1.
        constexpr std::array<int, 8> k_mod_adjust = {0, 1, 2, 4, 0, -4, -2, -1};
    } // namespace

    chip_metadata rp2c33::metadata() const noexcept {
        return {
            .manufacturer = "Ricoh",
            .part_number = "RP2C33",
            .family = "FDS",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void rp2c33::write_reg(std::uint16_t addr, std::uint8_t value) noexcept {
        if (addr >= reg_wave_base && addr < reg_wave_base + wave_size) {
            // Wave RAM is writable only while $4089 bit 7 holds the channel.
            if (wave_write_enable_) {
                wave_ram_[addr - reg_wave_base] = static_cast<std::uint8_t>(value & 0x3FU);
            }
            return;
        }
        switch (addr) {
        case reg_vol_env: // $4080
            vol_direct_ = (value & 0x80U) != 0U;
            vol_increase_ = (value & 0x40U) != 0U;
            vol_speed_ = static_cast<std::uint8_t>(value & 0x3FU);
            vol_timer_ =
                8 * (static_cast<int>(vol_speed_) + 1) * (static_cast<int>(env_speed_) + 1);
            if (vol_direct_) {
                vol_gain_ = static_cast<std::uint8_t>(value & 0x3FU); // direct gain
            }
            break;
        case reg_freq_lo: // $4082
            wave_freq_ = static_cast<std::uint16_t>((wave_freq_ & 0x0F00U) | value);
            break;
        case reg_freq_hi: // $4083
            wave_freq_ = static_cast<std::uint16_t>(
                (wave_freq_ & 0x00FFU) | (static_cast<std::uint16_t>(value & 0x0FU) << 8U));
            env_disabled_ = (value & 0x40U) != 0U;
            wave_halt_ = (value & 0x80U) != 0U;
            if (wave_halt_) {
                wave_acc_ = 0U; // halting resets the wave accumulator + position
            }
            break;
        case reg_mod_env: // $4084
            mod_direct_ = (value & 0x80U) != 0U;
            mod_increase_ = (value & 0x40U) != 0U;
            mod_speed_ = static_cast<std::uint8_t>(value & 0x3FU);
            mod_timer_ =
                8 * (static_cast<int>(mod_speed_) + 1) * (static_cast<int>(env_speed_) + 1);
            if (mod_direct_) {
                mod_gain_ = static_cast<std::uint8_t>(value & 0x3FU);
            }
            break;
        case reg_mod_counter: { // $4085 (7-bit signed bias)
            const auto v7 = static_cast<std::uint8_t>(value & 0x7FU);
            mod_counter_ = static_cast<std::int8_t>((v7 & 0x40U) != 0U ? (v7 | 0x80U) : v7);
            break;
        }
        case reg_mod_freq_lo: // $4086
            mod_freq_ = static_cast<std::uint16_t>((mod_freq_ & 0x0F00U) | value);
            break;
        case reg_mod_freq_hi: // $4087
            mod_freq_ = static_cast<std::uint16_t>(
                (mod_freq_ & 0x00FFU) | (static_cast<std::uint16_t>(value & 0x0FU) << 8U));
            mod_halt_ = (value & 0x80U) != 0U;
            if (mod_halt_) {
                mod_acc_ = 0U; // halt also resets the accumulator + enables table writes
            }
            break;
        case reg_mod_table: // $4088 (table write, only while halted)
            if (mod_halt_) {
                mod_table_[mod_table_pos_ & 0x1FU] = static_cast<std::uint8_t>(value & 0x07U);
                mod_table_pos_ = static_cast<std::uint8_t>((mod_table_pos_ + 1U) & 0x1FU);
            }
            break;
        case reg_master: // $4089
            master_volume_ = static_cast<std::uint8_t>(value & 0x03U);
            wave_write_enable_ = (value & 0x80U) != 0U;
            break;
        case reg_env_speed: // $408A
            env_speed_ = value;
            break;
        default:
            break;
        }
    }

    std::uint8_t rp2c33::read_reg(std::uint16_t addr) const noexcept {
        switch (addr) {
        case 0x4090U:
            return static_cast<std::uint8_t>(0x40U | (vol_gain_ & 0x3FU)); // current volume gain
        case 0x4092U:
            return static_cast<std::uint8_t>(0x40U | (mod_gain_ & 0x3FU)); // current mod gain
        default:
            return 0x00U;
        }
    }

    std::uint32_t rp2c33::modulated_pitch() const noexcept {
        // The signed mod counter scaled by the mod-envelope gain bends the carrier
        // pitch around its programmed value; a zero counter/gain leaves it unchanged
        // (keeping the base note in tune). Depth is approximate pending ear A/B.
        const int bias = static_cast<int>(mod_counter_) * static_cast<int>(mod_gain_);
        const std::int64_t base = static_cast<std::int64_t>(wave_freq_);
        const std::int64_t eff = base + ((base * bias) >> 12);
        if (eff <= 0) {
            return 0U;
        }
        return static_cast<std::uint32_t>(eff) & 0xFFFFFU;
    }

    void rp2c33::clock_internal() noexcept {
        if (mod_gain_ != 0U && !mod_halt_ && mod_freq_ != 0U) {
            // Advance the modulator; each wrap past bit 16 steps the table once and
            // applies its 3-bit entry to the signed mod counter.
            mod_acc_ += mod_freq_;
            if (mod_acc_ >= 0x10000U) {
                mod_acc_ -= 0x10000U;
                const int adj = k_mod_adjust[mod_table_[mod_table_pos_ & 0x1FU]];
                if (mod_table_[mod_table_pos_ & 0x1FU] == 4U) {
                    mod_counter_ = 0;
                } else {
                    mod_counter_ = static_cast<std::int8_t>(
                        std::clamp(static_cast<int>(mod_counter_) + adj, -64, 63));
                }
                mod_table_pos_ = static_cast<std::uint8_t>((mod_table_pos_ + 1U) & 0x1FU);
            }
        }
        if (!wave_halt_) {
            wave_acc_ = (wave_acc_ + modulated_pitch()) & 0x3FFFFU; // 18-bit phase
        }
    }

    void rp2c33::clock_envelopes() noexcept {
        if (env_disabled_) {
            return;
        }
        if (!vol_direct_) {
            if (--vol_timer_ <= 0) {
                vol_timer_ =
                    8 * (static_cast<int>(vol_speed_) + 1) * (static_cast<int>(env_speed_) + 1);
                if (vol_increase_ && vol_gain_ < 63U) {
                    ++vol_gain_;
                } else if (!vol_increase_ && vol_gain_ > 0U) {
                    --vol_gain_;
                }
            }
        }
        if (!mod_direct_) {
            if (--mod_timer_ <= 0) {
                mod_timer_ =
                    8 * (static_cast<int>(mod_speed_) + 1) * (static_cast<int>(env_speed_) + 1);
                if (mod_increase_ && mod_gain_ < 63U) {
                    ++mod_gain_;
                } else if (!mod_increase_ && mod_gain_ > 0U) {
                    --mod_gain_;
                }
            }
        }
    }

    std::int16_t rp2c33::output_sample() const noexcept {
        const int sample = static_cast<int>(wave_ram_[(wave_acc_ >> 12U) & 0x3FU]); // 0..63
        const int vol = std::min<int>(vol_gain_, 32);                               // output clamp
        // Centre the unsigned 6-bit wave (the chip AC-couples its output) and apply
        // the volume + master attenuator, then scale into the int16 mixing range.
        int level = (sample - 32) * vol; // -1024..992
        level = level * k_master_mul[master_volume_ & 0x03U] / 64;
        return clip16(level * 14);
    }

    void rp2c33::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            clock_envelopes();
            if (++internal_prescaler_ >= internal_divider) {
                internal_prescaler_ = 0;
                clock_internal();
            }
            if (++sample_prescaler_ >= clock_divider_) {
                sample_prescaler_ = 0;
                const std::int16_t s = output_sample();
                last_left_ = s;
                last_right_ = s;
                if (audio_capture_) {
                    sample_queue_.push_back(s);
                    sample_queue_.push_back(s);
                }
            }
        }
    }

    std::size_t rp2c33::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    void rp2c33::reset(reset_kind /*kind*/) {
        wave_ram_ = {};
        mod_table_ = {};
        wave_freq_ = 0U;
        wave_acc_ = 0U;
        wave_halt_ = false;
        env_disabled_ = false;
        wave_write_enable_ = false;
        master_volume_ = 0U;
        vol_direct_ = false;
        vol_increase_ = false;
        vol_speed_ = 0U;
        vol_gain_ = 0U;
        vol_timer_ = 8;
        mod_direct_ = false;
        mod_increase_ = false;
        mod_speed_ = 0U;
        mod_gain_ = 0U;
        mod_timer_ = 8;
        env_speed_ = 0U;
        mod_freq_ = 0U;
        mod_acc_ = 0U;
        mod_counter_ = 0;
        mod_table_pos_ = 0U;
        mod_halt_ = false;
        last_left_ = 0;
        last_right_ = 0;
        sample_prescaler_ = 0;
        internal_prescaler_ = 0;
        sample_queue_.clear();
    }

    void rp2c33::save_state(state_writer& writer) const {
        writer.bytes(wave_ram_);
        writer.bytes(mod_table_);
        writer.u16(wave_freq_);
        writer.u32(wave_acc_);
        writer.boolean(wave_halt_);
        writer.boolean(env_disabled_);
        writer.boolean(wave_write_enable_);
        writer.u8(master_volume_);
        writer.boolean(vol_direct_);
        writer.boolean(vol_increase_);
        writer.u8(vol_speed_);
        writer.u8(vol_gain_);
        writer.u32(static_cast<std::uint32_t>(vol_timer_));
        writer.boolean(mod_direct_);
        writer.boolean(mod_increase_);
        writer.u8(mod_speed_);
        writer.u8(mod_gain_);
        writer.u32(static_cast<std::uint32_t>(mod_timer_));
        writer.u8(env_speed_);
        writer.u16(mod_freq_);
        writer.u32(mod_acc_);
        writer.u8(static_cast<std::uint8_t>(mod_counter_));
        writer.u8(mod_table_pos_);
        writer.boolean(mod_halt_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(sample_prescaler_));
        writer.u32(static_cast<std::uint32_t>(internal_prescaler_));
    }

    void rp2c33::load_state(state_reader& reader) {
        reader.bytes(wave_ram_);
        reader.bytes(mod_table_);
        wave_freq_ = reader.u16();
        wave_acc_ = reader.u32();
        wave_halt_ = reader.boolean();
        env_disabled_ = reader.boolean();
        wave_write_enable_ = reader.boolean();
        master_volume_ = reader.u8();
        vol_direct_ = reader.boolean();
        vol_increase_ = reader.boolean();
        vol_speed_ = reader.u8();
        vol_gain_ = reader.u8();
        vol_timer_ = static_cast<int>(reader.u32());
        mod_direct_ = reader.boolean();
        mod_increase_ = reader.boolean();
        mod_speed_ = reader.u8();
        mod_gain_ = reader.u8();
        mod_timer_ = static_cast<int>(reader.u32());
        env_speed_ = reader.u8();
        mod_freq_ = reader.u16();
        mod_acc_ = reader.u32();
        mod_counter_ = static_cast<std::int8_t>(reader.u8());
        mod_table_pos_ = reader.u8();
        mod_halt_ = reader.boolean();
        clock_divider_ = static_cast<int>(reader.u32());
        sample_prescaler_ = static_cast<int>(reader.u32());
        internal_prescaler_ = static_cast<int>(reader.u32());
    }

    instrumentation::ichip_introspection& rp2c33::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> rp2c33::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"WAVE_FREQ", wave_freq_, 12U, fmt::unsigned_integer};
        register_view_[1] = {"VOL_GAIN", vol_gain_, 6U, fmt::unsigned_integer};
        register_view_[2] = {"MOD_FREQ", mod_freq_, 12U, fmt::unsigned_integer};
        register_view_[3] = {"MOD_GAIN", mod_gain_, 6U, fmt::unsigned_integer};
        register_view_[4] = {"MOD_CTR", static_cast<std::uint8_t>(mod_counter_), 8U,
                             fmt::signed_integer};
        register_view_[5] = {"MASTER", master_volume_, 2U, fmt::unsigned_integer};
        register_view_[6] = {"ENV_SPD", env_speed_, 8U, fmt::unsigned_integer};
        register_view_[7] = {"WAVE_POS", (wave_acc_ >> 12U) & 0x3FU, 6U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto rp2c33_registration =
            register_factory("ricoh.rp2c33", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<rp2c33>(); });
    } // namespace

} // namespace mnemos::chips::audio
