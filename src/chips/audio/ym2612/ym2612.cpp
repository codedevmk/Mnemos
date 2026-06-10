#include "ym2612.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <memory>

namespace mnemos::chips::audio {
    namespace {
        // The YM2612 numbers its operator registers in "slot" order S1,S2,S3,S4 but
        // wires them internally as M1,C1,M2,C2 = S1,S3,S2,S4 -- every per-operator
        // ($30-$9F), key-on ($28), and channel-3 special-mode ($A8) access is remapped
        // through this table. Matches published hardware studies (see THIRD-PARTY.md).
        constexpr std::array<int, 4> op_map = {0, 2, 1, 3};

        constexpr double pi = 3.14159265358979323846;

        // DT1 detune table (YM2608 Application Manual), indexed [kcode & 31][detune & 3].
        // Hardware-exact values (see THIRD-PARTY.md).
        constexpr std::uint8_t dt1_table[32][4] = {
            {0, 0, 1, 2},   {0, 0, 1, 2},   {0, 0, 1, 2},   {0, 0, 1, 2},   {0, 1, 2, 2},
            {0, 1, 2, 3},   {0, 1, 2, 3},   {0, 1, 2, 3},   {0, 1, 2, 4},   {0, 1, 3, 4},
            {0, 1, 3, 4},   {0, 1, 3, 5},   {0, 2, 4, 5},   {0, 2, 4, 6},   {0, 2, 4, 6},
            {0, 2, 5, 7},   {0, 2, 5, 8},   {0, 3, 6, 8},   {0, 3, 6, 9},   {0, 3, 7, 10},
            {0, 4, 8, 11},  {0, 4, 8, 12},  {0, 4, 9, 13},  {0, 5, 10, 14}, {0, 5, 11, 16},
            {0, 6, 12, 17}, {0, 6, 13, 19}, {0, 7, 14, 20}, {0, 8, 16, 22}, {0, 8, 16, 22},
            {0, 8, 16, 22}, {0, 8, 16, 22},
        };

        // LFO: samples between 7-bit counter ticks, indexed by the LFO frequency field.
        constexpr std::array<std::uint8_t, 8> lfo_divider_table = {108, 77, 71, 67, 62, 44, 8, 5};

        // Vibrato depth, indexed by PMS and the folded 3-bit LFO FM phase.
        constexpr std::uint8_t lfo_pm_table[8][8] = {
            {0, 0, 0, 0, 0, 0, 0, 0},       {0, 0, 0, 0, 4, 4, 4, 4},
            {0, 0, 0, 4, 4, 4, 8, 8},       {0, 0, 4, 4, 8, 8, 12, 12},
            {0, 0, 4, 8, 8, 8, 12, 16},     {0, 0, 8, 12, 16, 16, 20, 24},
            {0, 0, 16, 24, 32, 32, 40, 48}, {0, 0, 32, 48, 64, 64, 80, 96},
        };

        // Envelope-rate increment pattern (community hardware tests; see THIRD-PARTY.md),
        // indexed eg_pattern[eg_rate_select[rate]][(eg_cnt >> shift) & 7].
        constexpr std::uint8_t eg_pattern[19][8] = {
            {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1},
            {0, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 2, 1, 1, 1, 2},
            {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2}, {2, 2, 2, 2, 2, 2, 2, 2},
            {2, 2, 2, 4, 2, 2, 2, 4}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
            {4, 4, 4, 4, 4, 4, 4, 4}, {4, 4, 4, 8, 4, 4, 4, 8}, {4, 8, 4, 8, 4, 8, 4, 8},
            {4, 8, 8, 8, 4, 8, 8, 8}, {8, 8, 8, 8, 8, 8, 8, 8}, {16, 16, 16, 16, 16, 16, 16, 16},
            {0, 0, 0, 0, 0, 0, 0, 0},
        };

        constexpr std::array<std::uint8_t, 64> eg_rate_select = {
            18, 18, 2, 3, 0, 1, 2, 3, 0, 1, 2,  3,  0,  1,  2,  3,  0,  1,  2,  3,  0, 1,
            2,  3,  0, 1, 2, 3, 0, 1, 2, 3, 0,  1,  2,  3,  0,  1,  2,  3,  0,  1,  2, 3,
            0,  1,  2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 16, 16, 16,
        };

        // Log-sine (first quadrant folded across all four) and exp tables, built once
        // at static init. sin_table: 10-bit phase -> 12-bit log-sin. exp_table:
        // 2^(13 - i/256) -- converts the summed log attenuation back to linear.
        const std::array<std::uint16_t, 1024> sin_table = [] {
            std::array<std::uint16_t, 1024> t{};
            for (std::size_t i = 0; i < 256; ++i) {
                const double phase = (static_cast<double>(i) + 0.5) / 256.0 * (pi / 2.0);
                double s = std::sin(phase);
                if (s < 0.000001) {
                    s = 0.000001;
                }
                auto v = static_cast<std::uint16_t>(-std::log2(s) * 256.0 + 0.5);
                if (v > 0x0FFFU) {
                    v = 0x0FFFU;
                }
                t[i] = v;
            }
            for (std::size_t i = 0; i < 256; ++i) {
                t[256 + i] = t[255 - i]; // Q2 mirror
                t[512 + i] = t[i];       // Q3 (sign handled separately)
                t[768 + i] = t[255 - i]; // Q4 mirror
            }
            return t;
        }();

        const std::array<std::uint16_t, 1024> exp_table = [] {
            std::array<std::uint16_t, 1024> t{};
            for (std::size_t i = 0; i < 1024; ++i) {
                const double e = std::pow(2.0, 13.0 - static_cast<double>(i) / 256.0);
                t[i] = static_cast<std::uint16_t>(e + 0.5);
            }
            return t;
        }();

        // Hardware key-code: a full 5-bit KCODE from BLOCK + the top FNUM bits.
        [[nodiscard]] std::uint8_t ym_kcode(std::uint8_t block, std::uint16_t fnum) noexcept {
            const auto f11 = static_cast<unsigned>((fnum >> 10) & 1U);
            const auto f10 = static_cast<unsigned>((fnum >> 9) & 1U);
            const auto f9 = static_cast<unsigned>((fnum >> 8) & 1U);
            const auto f8 = static_cast<unsigned>((fnum >> 7) & 1U);
            const unsigned n3 = (f11 & (f10 | f9 | f8)) | ((f11 ^ 1U) & f10 & f9 & f8);
            const unsigned note = (f11 << 1U) | n3;
            return static_cast<std::uint8_t>(((static_cast<unsigned>(block) & 7U) << 2U) | note);
        }

        [[nodiscard]] std::uint8_t eg_step_for(std::uint8_t rate, std::uint32_t eg_cnt) noexcept {
            return eg_pattern[eg_rate_select[rate & 0x3FU]][eg_cnt & 7U];
        }

        // EG counter right-shift for an effective rate: shift = 11 - (rate>>2) for
        // rates 0-47, 0 for 48-63 (the pattern then runs at full eg-clock rate).
        [[nodiscard]] std::uint8_t eg_rate_shift(std::uint8_t rate) noexcept {
            return (rate < 48U) ? static_cast<std::uint8_t>(11U - (rate >> 2U)) : std::uint8_t{0};
        }

        // One EG increment for a rate at the current counter (0 when this clock is
        // gated out by the rate's shift).
        [[nodiscard]] std::uint8_t eg_get_inc(std::uint8_t rate, std::uint32_t eg_cnt) noexcept {
            const std::uint8_t shift = eg_rate_shift(rate);
            if (shift > 0U && (eg_cnt & ((1U << shift) - 1U)) != 0U) {
                return 0U;
            }
            return eg_step_for(rate, eg_cnt >> shift);
        }

        // Effective rate = base*2 + key-scaled key-code, clamped to 63.
        [[nodiscard]] int calc_rate(std::uint8_t base_rate, std::uint8_t kc,
                                    std::uint8_t key_scale) noexcept {
            if (base_rate == 0U) {
                return 0;
            }
            int rate = base_rate * 2 + (kc >> (3U - key_scale));
            if (rate > 63) {
                rate = 63;
            }
            return rate;
        }
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
            // Hardware-verified OPN2 behaviour: key-on enters attack FROM THE
            // CURRENT attenuation (only rate >= 62 snaps to 0, in eg_step).
            // Forcing 0x3FF here made every re-key of a still-sounding
            // operator drop to silence first, audibly changing fast retriggers.
            op.phase = eg_phase::attack;
            op.pg_phase = 0U; // restart the phase generator on key-on
            op.ssg_inv = false;
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
                if (!lfo_enable_) {
                    lfo_counter_ = 0U;
                    lfo_phase_ = 0U;
                    lfo_divider_ = 0U;
                }
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
                update_channel_freq(ch_idx);
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
                update_channel_freq(ch_idx);
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
                    if (ch3_mode_ != 0U) {
                        update_channel_freq(2);
                    }
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
        // Drive the timer prescalers. The scheduler passes CHIP cycles (one
        // chip cycle = 7 master clocks on Genesis, set via the chip's divider
        // in the scheduled_chip entry), but tick_timers_master() expects
        // master clocks -- so scale up by the Genesis YM divider here. This
        // is the same /7 that makes the chip clock master/7 in the first
        // place. Without it the Timer A/B periods (1008 / 16128 master clocks)
        // are reached 7x too slowly: music driver IRQs fire ~7x rarer, music
        // tempo crawls, timer-driven game logic drifts.
        const std::uint64_t total = cycles;
        const std::uint64_t master = cycles * 7U;
        std::uint64_t remaining = master;
        while (remaining > 0U) {
            const auto chunk =
                static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, 0xFFFFU));
            (void)tick_timers_master(chunk);
            remaining -= chunk;
        }
        // Audio capture: emit a stereo sample every 1008 master clocks. We do
        // this after the timer tick so the audio sees the same end-of-chunk
        // state the timers do. The runtime calls tick() with small `cycles`
        // each scheduler step (the YM divider is /7, so cycles is usually 7
        // or a small multiple), so on average each tick() produces 0-1
        // samples; bursts queue and drain pulls them.
        if (audio_capture_) {
            sample_accum_ += static_cast<std::uint32_t>(total);
            while (sample_accum_ >= chip_cycles_per_sample) {
                sample_accum_ -= chip_cycles_per_sample;
                const auto s = step();
                sample_queue_.push_back(s.left);
                sample_queue_.push_back(s.right);
            }
        }
    }

    std::size_t ym2612::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    // ------------------------------------------------------------------------
    //  FM synthesis core
    // ------------------------------------------------------------------------

    std::uint32_t ym2612::calc_phase_inc_value(const operator_state& op, std::uint16_t fnum,
                                               std::uint8_t block, std::uint8_t kc,
                                               bool extra_precision) noexcept {
        std::uint32_t inc = extra_precision ? ((static_cast<std::uint32_t>(fnum) << block) >> 2U)
                                            : ((static_cast<std::uint32_t>(fnum) << block) >> 1U);
        if (op.multiply == 0U) {
            inc >>= 1U; // multiply field 0 means x0.5
        } else {
            inc *= static_cast<std::uint32_t>(op.multiply);
        }
        const std::uint8_t dt = op.detune & 3U;
        if (dt != 0U) {
            const auto delta = static_cast<std::int32_t>(dt1_table[kc & 31U][dt]);
            const auto signed_inc = static_cast<std::int32_t>(inc);
            inc = static_cast<std::uint32_t>((op.detune & 4U) != 0U ? signed_inc - delta
                                                                    : signed_inc + delta);
        }
        return inc & 0xFFFFFU;
    }

    std::uint32_t ym2612::calc_lfo_phase_inc(const operator_state& op, std::uint16_t fnum,
                                             std::uint8_t block, std::uint8_t kc, std::uint8_t pms,
                                             std::uint8_t lfo_counter) noexcept {
        if (pms == 0U) {
            return op.pg_increment;
        }
        const std::uint8_t lfo_high = static_cast<std::uint8_t>(lfo_counter >> 2U);
        const std::uint8_t lfo_idx = (lfo_high & 0x08U) == 0U
                                         ? static_cast<std::uint8_t>(lfo_high & 0x07U)
                                         : static_cast<std::uint8_t>(7U - (lfo_high & 0x07U));
        const auto multiplier = static_cast<std::uint16_t>(lfo_pm_table[pms][lfo_idx]);
        std::uint16_t delta = 0U;
        for (unsigned bit = 4; bit <= 10; ++bit) {
            if ((fnum & (1U << bit)) != 0U) {
                delta = static_cast<std::uint16_t>(delta + (multiplier >> (10U - bit)));
            }
        }
        auto fm_fnum = static_cast<std::uint16_t>(fnum << 1U);
        if ((lfo_high & 0x10U) != 0U) {
            fm_fnum = static_cast<std::uint16_t>((fm_fnum - delta) & 0x0FFFU);
        } else {
            fm_fnum = static_cast<std::uint16_t>((fm_fnum + delta) & 0x0FFFU);
        }
        return calc_phase_inc_value(op, fm_fnum, block, kc, true);
    }

    bool ym2612::ssg_output_inverted(const operator_state& op) noexcept {
        if ((op.ssg_eg & 0x08U) == 0U || !op.key_on) {
            return false;
        }
        return op.ssg_inv != ((op.ssg_eg & 0x04U) != 0U);
    }

    std::uint16_t ym2612::ssg_eg_inc(const operator_state& op, std::uint8_t inc) noexcept {
        if ((op.ssg_eg & 0x08U) == 0U) {
            return inc;
        }
        if (op.eg_level >= 0x200U) {
            return 0U;
        }
        return static_cast<std::uint16_t>(inc << 2U);
    }

    void ym2612::ssg_boundary_step(operator_state& op) noexcept {
        if ((op.ssg_eg & 0x08U) == 0U || op.eg_level < 0x200U) {
            return;
        }
        const bool hold = (op.ssg_eg & 0x01U) != 0U;
        const bool alternate = (op.ssg_eg & 0x02U) != 0U;
        if (alternate) {
            op.ssg_inv = hold ? true : !op.ssg_inv;
        }
        if (!alternate && !hold) {
            op.pg_phase = 0U;
        }
        if (!op.key_on) {
            op.eg_level = 0x3FFU;
            op.phase = eg_phase::off;
            return;
        }
        if (!hold) {
            op.phase = eg_phase::attack;
            op.eg_level = 1023U;
            return;
        }
        if (op.phase != eg_phase::attack && !ssg_output_inverted(op)) {
            op.eg_level = 0x3FFU;
        }
    }

    void ym2612::eg_step(operator_state& op, std::uint8_t kc, std::uint32_t eg_cnt) noexcept {
        ssg_boundary_step(op);

        switch (op.phase) {
        case eg_phase::attack: {
            const int rate = calc_rate(op.attack_rate, kc, op.key_scale);
            if (rate >= 62) {
                op.eg_level = 0U; // instant attack
                op.phase = eg_phase::decay;
                return;
            }
            if (rate > 0 && op.eg_level > 0U) {
                const std::uint8_t inc = eg_get_inc(static_cast<std::uint8_t>(rate), eg_cnt);
                if (inc != 0U) {
                    // Hardware exponential convergence: level += (~level * inc) >> 4.
                    const auto level = static_cast<std::int32_t>(op.eg_level);
                    op.eg_level = static_cast<std::uint16_t>(
                        level + (((~level) * static_cast<std::int32_t>(inc)) >> 4));
                    if (static_cast<std::int16_t>(op.eg_level) <= 0) {
                        op.eg_level = 0U;
                        op.phase = eg_phase::decay;
                    }
                }
            }
            break;
        }
        case eg_phase::decay: {
            const int rate = calc_rate(op.decay_rate, kc, op.key_scale);
            const std::uint16_t sl_threshold =
                (op.sustain_level == 15U) ? 1023U
                                          : static_cast<std::uint16_t>(op.sustain_level * 32U);
            if (rate > 0) {
                const std::uint16_t inc = ssg_eg_inc(
                    op, eg_get_inc(static_cast<std::uint8_t>(rate < 63 ? rate : 63), eg_cnt));
                op.eg_level = static_cast<std::uint16_t>(op.eg_level + inc);
            }
            if (op.eg_level >= sl_threshold) {
                op.eg_level = sl_threshold;
                op.phase = eg_phase::sustain;
            }
            if (op.eg_level > 1023U) {
                op.eg_level = 1023U;
            }
            break;
        }
        case eg_phase::sustain: {
            const int rate = calc_rate(op.sustain_rate, kc, op.key_scale);
            if (rate > 0) {
                const std::uint16_t inc = ssg_eg_inc(
                    op, eg_get_inc(static_cast<std::uint8_t>(rate < 63 ? rate : 63), eg_cnt));
                op.eg_level = static_cast<std::uint16_t>(op.eg_level + inc);
            }
            if (op.eg_level > 1023U) {
                op.eg_level = 1023U;
            }
            break;
        }
        case eg_phase::release: {
            const int rate =
                calc_rate(static_cast<std::uint8_t>(op.release_rate * 2U + 1U), kc, op.key_scale);
            if (rate > 0) {
                const std::uint16_t inc = ssg_eg_inc(
                    op, eg_get_inc(static_cast<std::uint8_t>(rate < 63 ? rate : 63), eg_cnt));
                op.eg_level = static_cast<std::uint16_t>(op.eg_level + inc);
            }
            if (op.eg_level >= 1023U) {
                op.eg_level = 1023U;
                op.phase = eg_phase::off;
            }
            break;
        }
        case eg_phase::off:
            op.eg_level = 1023U;
            break;
        }
    }

    std::int32_t ym2612::op_calc(operator_state& op, std::int32_t modulation,
                                 std::uint32_t am_offset) noexcept {
        std::uint16_t eg = op.eg_level;
        if (ssg_output_inverted(op)) {
            eg = static_cast<std::uint16_t>((0x200U - eg) & 0x3FFU);
        }
        std::uint32_t atten = (static_cast<std::uint32_t>(op.total_level) << 3U) + eg;
        if (op.am_enable) {
            atten += am_offset;
        }
        if (atten >= 1024U) {
            return 0; // fully silent
        }
        std::uint32_t phase = (op.pg_phase >> 10U) & 0x3FFU;
        phase = (phase + (static_cast<std::uint32_t>((modulation >> 1) & 0x3FF))) & 0x3FFU;
        const std::uint16_t log_sin = sin_table[phase & 0x3FFU];
        const std::uint32_t total_log = log_sin + (atten << 2U);
        if (total_log >= 4096U) {
            return 0;
        }
        // exp_table spans 4 octaves per 1024-block, so the octave shift is x4.
        std::int32_t linear = exp_table[total_log & 0x3FFU] >> ((total_log >> 10U) << 2U);
        if (phase >= 512U) {
            linear = -linear; // second half of the sine is negative
        }
        return linear;
    }

    void ym2612::update_channel_freq(int ch_idx) noexcept {
        channel_state& ch = ch_[static_cast<std::size_t>(ch_idx)];
        const std::uint8_t kc = ym_kcode(ch.block, ch.fnum);
        for (int i = 0; i < operator_count; ++i) {
            std::uint16_t fn = ch.fnum;
            std::uint8_t bl = ch.block;
            std::uint8_t op_kc = kc;
            if (ch_idx == 2 && ch3_mode_ != 0U && i < 3) {
                const auto slot = static_cast<std::size_t>(op_map[static_cast<std::size_t>(i)]);
                fn = ch3_fnum_[slot];
                bl = ch3_block_[slot];
                op_kc = ym_kcode(bl, fn);
            }
            auto& op = ch.op[static_cast<std::size_t>(i)];
            op.pg_increment = calc_phase_inc_value(op, fn, bl, op_kc, false);
        }
    }

    void ym2612::lfo_tick() noexcept {
        if (!lfo_enable_) {
            lfo_counter_ = 0U;
            lfo_phase_ = 0U;
            lfo_divider_ = 0U;
            return;
        }
        ++lfo_divider_;
        if (lfo_divider_ >= lfo_divider_table[lfo_freq_]) {
            lfo_divider_ = 0U;
            lfo_counter_ = static_cast<std::uint8_t>((lfo_counter_ + 1U) & 0x7FU);
        }
        lfo_phase_ = lfo_counter_;
    }

    std::uint32_t ym2612::lfo_am_offset(std::uint8_t ams) const noexcept {
        if (ams == 0U) {
            return 0U;
        }
        std::uint32_t lfo_am = (lfo_counter_ & 0x40U) == 0U
                                   ? (0x3FU - (static_cast<std::uint32_t>(lfo_counter_) & 0x3FU))
                                   : (static_cast<std::uint32_t>(lfo_counter_) & 0x3FU);
        lfo_am <<= 1U;
        switch (ams) {
        case 1:
            return lfo_am >> 3U;
        case 2:
            return lfo_am >> 1U;
        default:
            return lfo_am;
        }
    }

    std::int32_t ym2612::channel_calc(int ch_idx, std::uint32_t am_offset) noexcept {
        channel_state& ch = ch_[static_cast<std::size_t>(ch_idx)];
        auto& op = ch.op;
        const std::uint8_t kc = ym_kcode(ch.block, ch.fnum);

        // Feedback on operator 0. op_calc applies modulation>>1 to all inputs, so the
        // feedback is pre-shifted left 1 to give a net (prev+cur) >> (10 - FB).
        std::int32_t fb = 0;
        if (ch.feedback > 0U) {
            fb = ((op[0].prev_output + op[0].output) >> (10 - ch.feedback)) << 1;
        }

        // Advance every operator's phase (with PM vibrato).
        for (int i = 0; i < operator_count; ++i) {
            std::uint16_t fn = ch.fnum;
            std::uint8_t bl = ch.block;
            std::uint8_t op_kc = kc;
            if (ch_idx == 2 && ch3_mode_ != 0U && i < 3) {
                const auto slot = static_cast<std::size_t>(op_map[static_cast<std::size_t>(i)]);
                fn = ch3_fnum_[slot];
                bl = ch3_block_[slot];
                op_kc = ym_kcode(bl, fn);
            }
            auto& o = op[static_cast<std::size_t>(i)];
            const std::uint32_t inc = calc_lfo_phase_inc(o, fn, bl, op_kc, ch.pms, lfo_counter_);
            o.pg_phase = (o.pg_phase + inc) & 0xFFFFFU;
        }

        const std::int32_t m1 = op_calc(op[0], fb, am_offset);
        op[0].prev_output = op[0].output;
        op[0].output = m1;

        std::int32_t c1 = 0;
        std::int32_t m2 = 0;
        std::int32_t c2 = 0;
        std::int32_t out = 0;
        switch (ch.algorithm) {
        case 0: // M1->C1->M2->C2 | C2
            c1 = op_calc(op[1], m1, am_offset);
            m2 = op_calc(op[2], c1, am_offset);
            c2 = op_calc(op[3], m2, am_offset);
            out = c2;
            break;
        case 1: // (M1+C1)->M2->C2 | C2
            c1 = op_calc(op[1], 0, am_offset);
            m2 = op_calc(op[2], m1 + c1, am_offset);
            c2 = op_calc(op[3], m2, am_offset);
            out = c2;
            break;
        case 2: // (C1 + M1->M2)->C2 | C2
            c1 = op_calc(op[1], 0, am_offset);
            m2 = op_calc(op[2], m1, am_offset);
            c2 = op_calc(op[3], c1 + m2, am_offset);
            out = c2;
            break;
        case 3: // (M1->C1 + M2)->C2 | C2
            c1 = op_calc(op[1], m1, am_offset);
            m2 = op_calc(op[2], 0, am_offset);
            c2 = op_calc(op[3], c1 + m2, am_offset);
            out = c2;
            break;
        case 4: // M1->C1, M2->C2 | C1+C2
            c1 = op_calc(op[1], m1, am_offset);
            m2 = op_calc(op[2], 0, am_offset);
            c2 = op_calc(op[3], m2, am_offset);
            out = c1 + c2;
            break;
        case 5: // M1->(C1+M2+C2) | C1+M2+C2
            c1 = op_calc(op[1], m1, am_offset);
            m2 = op_calc(op[2], m1, am_offset);
            c2 = op_calc(op[3], m1, am_offset);
            out = c1 + m2 + c2;
            break;
        case 6: // M1->C1, M2, C2 | C1+M2+C2
            c1 = op_calc(op[1], m1, am_offset);
            m2 = op_calc(op[2], 0, am_offset);
            c2 = op_calc(op[3], 0, am_offset);
            out = c1 + m2 + c2;
            break;
        case 7: // M1, C1, M2, C2 | M1+C1+M2+C2
            c1 = op_calc(op[1], 0, am_offset);
            m2 = op_calc(op[2], 0, am_offset);
            c2 = op_calc(op[3], 0, am_offset);
            out = m1 + c1 + m2 + c2;
            break;
        default:
            out = 0;
            break;
        }
        return out;
    }

    namespace {
        // Analog-amp-style hyperbolic soft clip: identity below |T|, asymptotic to the
        // int16 limit above it (avoids hard-clip tearing on hot mixes).
        [[nodiscard]] std::int32_t soft_clip(std::int32_t v) noexcept {
            constexpr std::int32_t t = 25000;
            constexpr std::int32_t r = 32767 - t;
            if (v > t) {
                const std::int32_t e = v - t;
                return t + static_cast<std::int32_t>((static_cast<std::int64_t>(r) * e) / (e + r));
            }
            if (v < -t) {
                const std::int32_t e = -t - v;
                return -t - static_cast<std::int32_t>((static_cast<std::int64_t>(r) * e) / (e + r));
            }
            return v;
        }
    } // namespace

    ym2612::stereo_sample ym2612::step() noexcept {
        std::int32_t left_acc = 0;
        std::int32_t right_acc = 0;

        // CSM: a Timer A overflow keyed CH3 on last sample; key it off now.
        if (csm_key_pending_) {
            for (auto& op : ch_[2].op) {
                eg_key_off(op);
            }
            csm_key_pending_ = false;
        }

        lfo_tick();

        // The envelope generator advances once per 3 FM samples (hardware-verified).
        // The 12-bit EG clock skips the all-zero value, so it wraps to 1.
        bool eg_advance = false;
        if (++eg_timer_ >= 3U) {
            eg_timer_ = 0U;
            if (++eg_clock_ >= 4096U) {
                eg_clock_ = 1U;
            }
            eg_advance = true;
        }

        for (int i = 0; i < channel_count; ++i) {
            channel_state& ch = ch_[static_cast<std::size_t>(i)];
            if (eg_advance) {
                const std::uint8_t kc = ym_kcode(ch.block, ch.fnum);
                for (int j = 0; j < operator_count; ++j) {
                    std::uint8_t op_kc = kc;
                    if (i == 2 && ch3_mode_ != 0U && j < 3) {
                        const auto slot =
                            static_cast<std::size_t>(op_map[static_cast<std::size_t>(j)]);
                        op_kc = ym_kcode(ch3_block_[slot], ch3_fnum_[slot]);
                    }
                    eg_step(ch.op[static_cast<std::size_t>(j)], op_kc, eg_clock_);
                }
            }

            // Always run channel_calc so phases advance every sample; ch6 overrides
            // with the DAC value when enabled (centred on 128, scaled to the FM range).
            const std::int32_t fm_out = channel_calc(i, lfo_am_offset(ch.ams));
            const std::int32_t out = (i == 5 && dac_enable_)
                                         ? ((static_cast<std::int32_t>(dac_data_) - 128) << 6)
                                         : fm_out;
            if (ch.left) {
                left_acc += out;
            }
            if (ch.right) {
                right_acc += out;
            }
        }

        left_acc = soft_clip(left_acc);
        right_acc = soft_clip(right_acc);
        left_acc = std::clamp(left_acc, -32768, 32767);
        right_acc = std::clamp(right_acc, -32768, 32767);

        if (lp_alpha_q15_ != 0) {
            lp_state_l_ += (lp_alpha_q15_ * (left_acc - lp_state_l_)) >> 15;
            lp_state_r_ += (lp_alpha_q15_ * (right_acc - lp_state_r_)) >> 15;
            left_acc = lp_state_l_;
            right_acc = lp_state_r_;
        }

        last_left_ = static_cast<std::int16_t>(left_acc);
        last_right_ = static_cast<std::int16_t>(right_acc);
        return {last_left_, last_right_};
    }

    void ym2612::update(std::span<std::int16_t> out) noexcept {
        const std::size_t frames = out.size() / 2U;
        for (std::size_t s = 0; s < frames; ++s) {
            const stereo_sample sample = step();
            out[s * 2U + 0U] = sample.left;
            out[s * 2U + 1U] = sample.right;
        }
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
        // Power-on default: every channel routes to both speakers (operators stay at
        // eg_level 1023 / EG_OFF via the operator_state member initializers, so the
        // chip is silent until a voice is programmed and keyed on).
        for (auto& ch : ch_) {
            ch.left = true;
            ch.right = true;
        }
        ch3_fnum_ = {};
        ch3_block_ = {};
        ch3_fnum_hi_latch_ = {};
        ch3_block_latch_ = {};
        ch3_mode_ = 0U;
        dac_enable_ = false;
        dac_data_ = 0U;
        lfo_enable_ = false;
        lfo_freq_ = 0U;
        lfo_counter_ = 0U;
        lfo_phase_ = 0U;
        lfo_divider_ = 0U;
        eg_timer_ = 0U;
        eg_clock_ = 0U;
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
                writer.u32(static_cast<std::uint32_t>(op.output));
                writer.u32(static_cast<std::uint32_t>(op.prev_output));
                writer.boolean(op.ssg_inv);
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
        writer.u8(lfo_counter_);
        writer.u8(lfo_phase_);
        writer.u8(lfo_divider_);
        writer.u8(eg_timer_);
        writer.u32(eg_clock_);
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
                op.output = static_cast<std::int32_t>(reader.u32());
                op.prev_output = static_cast<std::int32_t>(reader.u32());
                op.ssg_inv = reader.boolean();
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
        lfo_counter_ = reader.u8();
        lfo_phase_ = reader.u8();
        lfo_divider_ = reader.u8();
        eg_timer_ = reader.u8();
        eg_clock_ = reader.u32();
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
