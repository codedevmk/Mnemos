#include "ym2612.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>

namespace mnemos::chips::audio {
    namespace {
        // The YM2612 numbers its operator registers in "slot" order S1,S2,S3,S4 but
        // wires them internally as S1,S3,S2,S4 -- every per-operator ($30-$9F) and
        // key-on ($28) access is remapped through this table. Matches Nuked-OPN2.
        constexpr std::array<int, 4> op_map = {0, 2, 1, 3};

        constexpr double pi = 3.14159265358979323846;
    } // namespace

    chip_metadata ym2612::metadata() const noexcept {
        return {
            .manufacturer = "Yamaha",
            .part_number = "YM2612",
            .family = "OPN2",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void ym2612::eg_key_on(operator_state& op) noexcept {
        if (!op.key_on) {
            op.key_on = true;
            op.phase = eg_phase::attack;
            op.eg_level = 0x3FFU; // start silent; the attack ramps up (phase 2)
            op.pg_phase = 0U;     // restart the phase generator on key-on
        }
    }

    void ym2612::eg_key_off(operator_state& op) noexcept {
        if (op.key_on) {
            op.key_on = false;
            if (op.phase != eg_phase::off) {
                op.phase = eg_phase::release;
            }
        }
    }

    void ym2612::write(int port, bool addr_or_data, std::uint8_t value) noexcept {
        port &= 1;
        if (!addr_or_data) {
            addr_latch_[static_cast<std::size_t>(port)] = value; // latch register address
        } else {
            write_register(port, addr_latch_[static_cast<std::size_t>(port)], value);
        }
    }

    void ym2612::write_register(int port, std::uint8_t reg, std::uint8_t value) noexcept {
        const int ch_base = port * 3; // port 0 -> ch 0-2, port 1 -> ch 3-5

        // ---- Global registers (port 0 only, $20-$2F) ----
        if (reg < 0x30U) {
            switch (reg) {
            case 0x22: // LFO enable + frequency
                lfo_enable_ = ((value >> 3U) & 1U) != 0U;
                lfo_freq_ = static_cast<std::uint8_t>(value & 7U);
                break;
            case 0x24: // Timer A MSB (high 8 of 10 bits)
                timer_a_load_ = static_cast<std::uint16_t>(
                    (timer_a_load_ & 3U) | (static_cast<std::uint16_t>(value) << 2U));
                break;
            case 0x25: // Timer A LSB (low 2 bits)
                timer_a_load_ = static_cast<std::uint16_t>((timer_a_load_ & 0x3FCU) | (value & 3U));
                break;
            case 0x26: // Timer B
                timer_b_load_ = value;
                break;
            case 0x27: // Ch3 mode / timer control
                ch3_mode_ = static_cast<std::uint8_t>(value & 0xC0U);
                timer_a_run_ = (value & 0x01U) != 0U;
                timer_b_run_ = (value & 0x02U) != 0U;
                timer_a_irq_en_ = ((value >> 2U) & 1U) != 0U;
                timer_b_irq_en_ = ((value >> 3U) & 1U) != 0U;
                if ((value & 0x10U) != 0U) { // reset Timer A overflow
                    timer_a_ovf_ = false;
                    status_ = static_cast<std::uint8_t>(status_ & ~0x01U);
                }
                if ((value & 0x20U) != 0U) { // reset Timer B overflow
                    timer_b_ovf_ = false;
                    status_ = static_cast<std::uint8_t>(status_ & ~0x02U);
                }
                break;
            case 0x28: { // Key on/off
                int ch_idx = value & 3;
                if (ch_idx == 3) {
                    break; // invalid channel selector
                }
                if ((value & 4U) != 0U) {
                    ch_idx += 3; // channels 4-6 live on the bit-2 page
                }
                channel_state& ch = ch_[static_cast<std::size_t>(ch_idx)];
                for (int i = 0; i < operator_count; ++i) {
                    operator_state& op =
                        ch.op[static_cast<std::size_t>(op_map[static_cast<std::size_t>(i)])];
                    if ((value & (0x10U << static_cast<unsigned>(i))) != 0U) {
                        eg_key_on(op);
                    } else {
                        eg_key_off(op);
                    }
                }
                break;
            }
            case 0x2A: // DAC data (channel 6 PCM)
                dac_data_ = value;
                break;
            case 0x2B: // DAC enable
                dac_enable_ = ((value >> 7U) & 1U) != 0U;
                break;
            default:
                break;
            }
            return;
        }

        // ---- Per-operator registers ($30-$9F) ----
        if (reg < 0xA0U) {
            int ch_idx = reg & 3;
            if (ch_idx == 3) {
                return; // invalid
            }
            ch_idx += ch_base;
            const int op_idx = op_map[static_cast<std::size_t>((reg >> 2U) & 3U)];
            operator_state& op =
                ch_[static_cast<std::size_t>(ch_idx)].op[static_cast<std::size_t>(op_idx)];

            switch (reg & 0xF0U) {
            case 0x30: // DT1 / MUL
                op.detune = static_cast<std::uint8_t>((value >> 4U) & 7U);
                op.multiply = static_cast<std::uint8_t>(value & 0x0FU);
                break;
            case 0x40: // TL (total level)
                op.total_level = static_cast<std::uint8_t>(value & 0x7FU);
                break;
            case 0x50: // RS / AR
                op.key_scale = static_cast<std::uint8_t>((value >> 6U) & 3U);
                op.attack_rate = static_cast<std::uint8_t>(value & 0x1FU);
                break;
            case 0x60: // AM / D1R
                op.am_enable = ((value >> 7U) & 1U) != 0U;
                op.decay_rate = static_cast<std::uint8_t>(value & 0x1FU);
                break;
            case 0x70: // D2R (sustain rate)
                op.sustain_rate = static_cast<std::uint8_t>(value & 0x1FU);
                break;
            case 0x80: // SL / RR
                op.sustain_level = static_cast<std::uint8_t>((value >> 4U) & 0x0FU);
                op.release_rate = static_cast<std::uint8_t>(value & 0x0FU);
                break;
            case 0x90: // SSG-EG
                op.ssg_eg = static_cast<std::uint8_t>(value & 0x0FU);
                break;
            default:
                break;
            }
            return;
        }

        // ---- Per-channel registers ($A0-$BF) ----
        {
            int ch_idx = reg & 3;
            if (ch_idx == 3) {
                return;
            }
            ch_idx += ch_base;
            channel_state& ch = ch_[static_cast<std::size_t>(ch_idx)];

            switch (reg & 0xFCU) {
            case 0xA0: // Frequency LSB -- commits the standing A4 latch atomically
                ch.fnum = static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(ch.fnum_hi_latch & 0x07U) << 8U) | value);
                ch.block = ch.block_latch;
                break;
            case 0xA4: // Frequency MSB + block -- latch only, applied on the next $A0
                ch.fnum_hi_latch = static_cast<std::uint8_t>(value & 0x07U);
                ch.block_latch = static_cast<std::uint8_t>((value >> 3U) & 0x07U);
                break;
            case 0xA8: // Ch3 special-mode freq LSB -- commits the $AC latch
                if (port == 0 && ch_idx < 3) {
                    const std::size_t slot = static_cast<std::size_t>(reg & 3);
                    ch3_fnum_[slot] = static_cast<std::uint16_t>(
                        (static_cast<std::uint16_t>(ch3_fnum_hi_latch_[slot] & 0x07U) << 8U) |
                        value);
                    ch3_block_[slot] = ch3_block_latch_[slot];
                }
                break;
            case 0xAC: // Ch3 special-mode freq MSB -- latch only, applied on $A8
                if (port == 0 && ch_idx < 3) {
                    const std::size_t slot = static_cast<std::size_t>(reg & 3);
                    ch3_fnum_hi_latch_[slot] = static_cast<std::uint8_t>(value & 0x07U);
                    ch3_block_latch_[slot] = static_cast<std::uint8_t>((value >> 3U) & 0x07U);
                }
                break;
            case 0xB0: // Feedback / algorithm
                ch.algorithm = static_cast<std::uint8_t>(value & 7U);
                ch.feedback = static_cast<std::uint8_t>((value >> 3U) & 7U);
                break;
            case 0xB4: // Stereo / LFO sensitivity
                ch.left = ((value >> 7U) & 1U) != 0U;
                ch.right = ((value >> 6U) & 1U) != 0U;
                ch.ams = static_cast<std::uint8_t>((value >> 4U) & 3U);
                ch.pms = static_cast<std::uint8_t>(value & 7U);
                break;
            default:
                break;
            }
        }
    }

    bool ym2612::timer_a_tick() noexcept {
        if (!timer_a_run_) {
            return false;
        }
        ++timer_a_cnt_;
        if (timer_a_cnt_ < (1024U - timer_a_load_)) {
            return false;
        }
        timer_a_cnt_ = 0U;
        timer_a_ovf_ = true;
        // CSM mode: a Timer A overflow force-keys all of channel 3's operators.
        if ((ch3_mode_ & 0x80U) != 0U) {
            channel_state& ch3 = ch_[2];
            for (auto& op : ch3.op) {
                eg_key_on(op);
            }
            csm_key_pending_ = true;
        }
        if (!timer_a_irq_en_) {
            return false;
        }
        status_ = static_cast<std::uint8_t>(status_ | 0x01U);
        return true;
    }

    bool ym2612::timer_b_tick() noexcept {
        if (!timer_b_run_) {
            return false;
        }
        ++timer_b_cnt_;
        if (timer_b_cnt_ < (256U - timer_b_load_)) {
            return false;
        }
        timer_b_cnt_ = 0U;
        timer_b_ovf_ = true;
        if (!timer_b_irq_en_) {
            return false;
        }
        status_ = static_cast<std::uint8_t>(status_ | 0x02U);
        return true;
    }

    bool ym2612::tick_timers_master(std::uint32_t master_clocks) noexcept {
        bool irq = false;

        timer_a_accum_ += master_clocks;
        while (timer_a_accum_ >= timer_a_master_period) {
            timer_a_accum_ -= timer_a_master_period;
            if (timer_a_tick()) {
                irq = true;
            }
        }

        timer_b_accum_ += master_clocks;
        while (timer_b_accum_ >= timer_b_master_period) {
            timer_b_accum_ -= timer_b_master_period;
            if (timer_b_tick()) {
                irq = true;
            }
        }

        return irq;
    }

    void ym2612::tick(std::uint64_t cycles) {
        // Drive the timer prescalers in master clocks. Chunked so a 64-bit cycle
        // count can't overflow the 32-bit accumulator entry point.
        while (cycles > 0U) {
            const auto chunk = static_cast<std::uint32_t>(std::min<std::uint64_t>(cycles, 0xFFFFU));
            (void)tick_timers_master(chunk);
            cycles -= chunk;
        }
    }

    ym2612::stereo_sample ym2612::step() noexcept {
        // Phase 1: no FM synthesis yet -- the chip is silent. The output still runs
        // through the analog low-pass so the filter state stays well-defined.
        std::int32_t left = 0;
        std::int32_t right = 0;
        if (lp_alpha_q15_ != 0) {
            lp_state_l_ += (lp_alpha_q15_ * (left - lp_state_l_)) >> 15;
            lp_state_r_ += (lp_alpha_q15_ * (right - lp_state_r_)) >> 15;
            left = lp_state_l_;
            right = lp_state_r_;
        }
        last_left_ = static_cast<std::int16_t>(left);
        last_right_ = static_cast<std::int16_t>(right);
        return {last_left_, last_right_};
    }

    void ym2612::set_lowpass_cutoff_hz(int sample_rate_hz, int cutoff_hz) noexcept {
        if (sample_rate_hz <= 0 || cutoff_hz <= 0) {
            lp_alpha_q15_ = 0; // pass-through
            lp_state_l_ = 0;
            lp_state_r_ = 0;
            return;
        }
        const double fs = static_cast<double>(sample_rate_hz);
        const double fc = static_cast<double>(cutoff_hz);
        const double rc = 1.0 / (2.0 * pi * fc);
        const double dt = 1.0 / fs;
        double alpha = dt / (dt + rc);
        if (alpha < 0.0) {
            alpha = 0.0;
        }
        if (alpha > 1.0) {
            alpha = 1.0;
        }
        auto q15 = static_cast<std::int32_t>(alpha * 32767.0 + 0.5);
        if (q15 == 0) {
            q15 = 1;
        }
        lp_alpha_q15_ = q15;
        lp_state_l_ = 0;
        lp_state_r_ = 0;
    }

    void ym2612::reset(reset_kind /*kind*/) {
        // Clear the whole digital chip state but keep the analog output-stage filter
        // config (the RC network doesn't see the RESET pin); the filter memory still
        // drains to zero while the chip is held in reset.
        const std::int32_t saved_alpha = lp_alpha_q15_;

        ch_ = {};
        ch3_fnum_ = {};
        ch3_block_ = {};
        ch3_fnum_hi_latch_ = {};
        ch3_block_latch_ = {};
        ch3_mode_ = 0U;
        dac_enable_ = false;
        dac_data_ = 0U;
        lfo_enable_ = false;
        lfo_freq_ = 0U;
        timer_a_load_ = 0U;
        timer_a_cnt_ = 0U;
        timer_b_load_ = 0U;
        timer_b_cnt_ = 0U;
        timer_b_div_ = 0U;
        timer_a_run_ = false;
        timer_b_run_ = false;
        timer_a_ovf_ = false;
        timer_b_ovf_ = false;
        timer_a_irq_en_ = false;
        timer_b_irq_en_ = false;
        timer_a_accum_ = 0U;
        timer_b_accum_ = 0U;
        addr_latch_ = {};
        status_ = 0U;
        csm_key_pending_ = false;

        lp_alpha_q15_ = saved_alpha;
        lp_state_l_ = 0;
        lp_state_r_ = 0;
        last_left_ = 0;
        last_right_ = 0;
    }

    void ym2612::save_state(state_writer& writer) const {
        for (const auto& ch : ch_) {
            for (const auto& op : ch.op) {
                writer.u8(op.detune);
                writer.u8(op.multiply);
                writer.u8(op.total_level);
                writer.u8(op.key_scale);
                writer.u8(op.attack_rate);
                writer.u8(op.decay_rate);
                writer.u8(op.sustain_rate);
                writer.u8(op.release_rate);
                writer.u8(op.sustain_level);
                writer.boolean(op.am_enable);
                writer.u8(op.ssg_eg);
                writer.boolean(op.key_on);
                writer.u8(static_cast<std::uint8_t>(op.phase));
                writer.u32(op.pg_phase);
                writer.u32(op.pg_increment);
                writer.u16(op.eg_level);
            }
            writer.u16(ch.fnum);
            writer.u8(ch.block);
            writer.u8(ch.algorithm);
            writer.u8(ch.feedback);
            writer.boolean(ch.left);
            writer.boolean(ch.right);
            writer.u8(ch.ams);
            writer.u8(ch.pms);
            writer.u8(ch.fnum_hi_latch);
            writer.u8(ch.block_latch);
        }
        for (std::size_t i = 0; i < ch3_fnum_.size(); ++i) {
            writer.u16(ch3_fnum_[i]);
            writer.u8(ch3_block_[i]);
            writer.u8(ch3_fnum_hi_latch_[i]);
            writer.u8(ch3_block_latch_[i]);
        }
        writer.u8(ch3_mode_);
        writer.boolean(dac_enable_);
        writer.u8(dac_data_);
        writer.boolean(lfo_enable_);
        writer.u8(lfo_freq_);
        writer.u16(timer_a_load_);
        writer.u16(timer_a_cnt_);
        writer.u8(timer_b_load_);
        writer.u16(timer_b_cnt_);
        writer.u8(timer_b_div_);
        writer.boolean(timer_a_run_);
        writer.boolean(timer_b_run_);
        writer.boolean(timer_a_ovf_);
        writer.boolean(timer_b_ovf_);
        writer.boolean(timer_a_irq_en_);
        writer.boolean(timer_b_irq_en_);
        writer.u32(timer_a_accum_);
        writer.u32(timer_b_accum_);
        writer.u8(addr_latch_[0]);
        writer.u8(addr_latch_[1]);
        writer.u8(status_);
        writer.boolean(csm_key_pending_);
        writer.u32(static_cast<std::uint32_t>(lp_alpha_q15_));
        writer.u32(static_cast<std::uint32_t>(lp_state_l_));
        writer.u32(static_cast<std::uint32_t>(lp_state_r_));
        writer.u16(static_cast<std::uint16_t>(last_left_));
        writer.u16(static_cast<std::uint16_t>(last_right_));
    }

    void ym2612::load_state(state_reader& reader) {
        for (auto& ch : ch_) {
            for (auto& op : ch.op) {
                op.detune = reader.u8();
                op.multiply = reader.u8();
                op.total_level = reader.u8();
                op.key_scale = reader.u8();
                op.attack_rate = reader.u8();
                op.decay_rate = reader.u8();
                op.sustain_rate = reader.u8();
                op.release_rate = reader.u8();
                op.sustain_level = reader.u8();
                op.am_enable = reader.boolean();
                op.ssg_eg = reader.u8();
                op.key_on = reader.boolean();
                op.phase = static_cast<eg_phase>(reader.u8());
                op.pg_phase = reader.u32();
                op.pg_increment = reader.u32();
                op.eg_level = reader.u16();
            }
            ch.fnum = reader.u16();
            ch.block = reader.u8();
            ch.algorithm = reader.u8();
            ch.feedback = reader.u8();
            ch.left = reader.boolean();
            ch.right = reader.boolean();
            ch.ams = reader.u8();
            ch.pms = reader.u8();
            ch.fnum_hi_latch = reader.u8();
            ch.block_latch = reader.u8();
        }
        for (std::size_t i = 0; i < ch3_fnum_.size(); ++i) {
            ch3_fnum_[i] = reader.u16();
            ch3_block_[i] = reader.u8();
            ch3_fnum_hi_latch_[i] = reader.u8();
            ch3_block_latch_[i] = reader.u8();
        }
        ch3_mode_ = reader.u8();
        dac_enable_ = reader.boolean();
        dac_data_ = reader.u8();
        lfo_enable_ = reader.boolean();
        lfo_freq_ = reader.u8();
        timer_a_load_ = reader.u16();
        timer_a_cnt_ = reader.u16();
        timer_b_load_ = reader.u8();
        timer_b_cnt_ = reader.u16();
        timer_b_div_ = reader.u8();
        timer_a_run_ = reader.boolean();
        timer_b_run_ = reader.boolean();
        timer_a_ovf_ = reader.boolean();
        timer_b_ovf_ = reader.boolean();
        timer_a_irq_en_ = reader.boolean();
        timer_b_irq_en_ = reader.boolean();
        timer_a_accum_ = reader.u32();
        timer_b_accum_ = reader.u32();
        addr_latch_[0] = reader.u8();
        addr_latch_[1] = reader.u8();
        status_ = reader.u8();
        csm_key_pending_ = reader.boolean();
        lp_alpha_q15_ = static_cast<std::int32_t>(reader.u32());
        lp_state_l_ = static_cast<std::int32_t>(reader.u32());
        lp_state_r_ = static_cast<std::int32_t>(reader.u32());
        last_left_ = static_cast<std::int16_t>(reader.u16());
        last_right_ = static_cast<std::int16_t>(reader.u16());
    }

    instrumentation::ichip_introspection& ym2612::introspection() noexcept {
        return introspection_;
    }

    std::uint16_t ym2612::channel_fnum(int ch) const noexcept {
        return (ch >= 0 && ch < channel_count) ? ch_[static_cast<std::size_t>(ch)].fnum : 0U;
    }

    std::uint8_t ym2612::channel_block(int ch) const noexcept {
        return (ch >= 0 && ch < channel_count) ? ch_[static_cast<std::size_t>(ch)].block : 0U;
    }

    std::uint8_t ym2612::channel_algorithm(int ch) const noexcept {
        return (ch >= 0 && ch < channel_count) ? ch_[static_cast<std::size_t>(ch)].algorithm : 0U;
    }

    std::uint8_t ym2612::operator_total_level(int ch, int op) const noexcept {
        if (ch < 0 || ch >= channel_count || op < 0 || op >= operator_count) {
            return 0U;
        }
        return ch_[static_cast<std::size_t>(ch)].op[static_cast<std::size_t>(op)].total_level;
    }

    bool ym2612::operator_key_on(int ch, int op) const noexcept {
        if (ch < 0 || ch >= channel_count || op < 0 || op >= operator_count) {
            return false;
        }
        return ch_[static_cast<std::size_t>(ch)].op[static_cast<std::size_t>(op)].key_on;
    }

    std::span<const register_descriptor> ym2612::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"TIMER_A", timer_a_load_, 10U, fmt::unsigned_integer};
        register_view_[1] = {"TIMER_B", timer_b_load_, 8U, fmt::unsigned_integer};
        register_view_[2] = {"STATUS", status_, 8U, fmt::flags};
        register_view_[3] = {"CH3_MODE", ch3_mode_, 8U, fmt::flags};
        register_view_[4] = {"LFO_EN", lfo_enable_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[5] = {"LFO_FREQ", lfo_freq_, 3U, fmt::unsigned_integer};
        register_view_[6] = {"DAC_EN", dac_enable_ ? 1U : 0U, 1U, fmt::flags};
        register_view_[7] = {"DAC_DATA", dac_data_, 8U, fmt::unsigned_integer};
        register_view_[8] = {"CH1_FNUM", ch_[0].fnum, 11U, fmt::unsigned_integer};
        register_view_[9] = {"CH1_BLOCK", ch_[0].block, 3U, fmt::unsigned_integer};
        register_view_[10] = {"CH1_ALG", ch_[0].algorithm, 3U, fmt::unsigned_integer};
        register_view_[11] = {"CH1_FB", ch_[0].feedback, 3U, fmt::unsigned_integer};
        register_view_[12] = {"CH6_FNUM", ch_[5].fnum, 11U, fmt::unsigned_integer};
        register_view_[13] = {"CH6_ALG", ch_[5].algorithm, 3U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto ym2612_registration =
            register_factory("yamaha.ym2612", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<ym2612>(); });
    } // namespace

} // namespace mnemos::chips::audio
