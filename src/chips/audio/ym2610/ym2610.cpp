#include "ym2610.hpp"

#include "chip_registry.hpp"
#include "fm_tables.hpp"
#include "state.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>

namespace mnemos::chips::audio {

    namespace {
        // OPN/OPM output-pipeline tables shared with the YM2612 / YM2151.
        using fm::exp_table;
        using fm::sin_table;

        [[nodiscard]] std::int32_t clamp16(std::int32_t v) noexcept {
            if (v > 32767) {
                return 32767;
            }
            if (v < -32768) {
                return -32768;
            }
            return v;
        }

        // DT (3-bit) fine-detune deltas, indexed [keycode/4][dt & 3]; bit 2 of DT
        // selects the sign. OPN-family application-manual table; first-order reuse
        // pending the conformance pass.
        constexpr std::uint8_t dt_table[32][4] = {
            {0, 0, 1, 2},   {0, 0, 1, 2},   {0, 0, 1, 2},   {0, 0, 1, 2},   {0, 1, 2, 2},
            {0, 1, 2, 3},   {0, 1, 2, 3},   {0, 1, 2, 3},   {0, 1, 2, 4},   {0, 1, 3, 4},
            {0, 1, 3, 4},   {0, 1, 3, 5},   {0, 2, 4, 5},   {0, 2, 4, 6},   {0, 2, 4, 6},
            {0, 2, 5, 7},   {0, 2, 5, 8},   {0, 3, 6, 8},   {0, 3, 6, 9},   {0, 3, 7, 10},
            {0, 4, 8, 11},  {0, 4, 8, 12},  {0, 4, 9, 13},  {0, 5, 10, 14}, {0, 5, 11, 16},
            {0, 6, 12, 17}, {0, 6, 13, 19}, {0, 7, 14, 20}, {0, 8, 16, 22}, {0, 8, 16, 22},
            {0, 8, 16, 22}, {0, 8, 16, 22}};

        // Envelope increment patterns + the rate -> pattern selector (the
        // OPN/OPM EG stepping hardware; same tables as the in-tree YM2612/YM2151).
        constexpr std::uint8_t eg_pattern[19][8] = {
            {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1},
            {0, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 2, 1, 1, 1, 2},
            {1, 2, 1, 2, 1, 2, 1, 2}, {1, 2, 2, 2, 1, 2, 2, 2}, {2, 2, 2, 2, 2, 2, 2, 2},
            {2, 2, 2, 4, 2, 2, 2, 4}, {2, 4, 2, 4, 2, 4, 2, 4}, {2, 4, 4, 4, 2, 4, 4, 4},
            {4, 4, 4, 4, 4, 4, 4, 4}, {4, 4, 4, 8, 4, 4, 4, 8}, {4, 8, 4, 8, 4, 8, 4, 8},
            {4, 8, 8, 8, 4, 8, 8, 8}, {8, 8, 8, 8, 8, 8, 8, 8}, {8, 8, 8, 8, 8, 8, 8, 8},
            {0, 0, 0, 0, 0, 0, 0, 0}};

        constexpr std::array<std::uint8_t, 64> eg_rate_select = [] {
            std::array<std::uint8_t, 64> t{};
            for (std::size_t rate = 0; rate < 64; ++rate) {
                if (rate < 48) {
                    t[rate] = static_cast<std::uint8_t>(rate & 3U);
                } else if (rate < 60) {
                    t[rate] = static_cast<std::uint8_t>(4U + (rate - 48U));
                } else {
                    t[rate] = 16U;
                }
            }
            return t;
        }();

        // How often a rate's pattern advances: rates below 48 are slowed by a
        // power-of-two shift of the EG counter.
        [[nodiscard]] std::uint8_t eg_inc_for(std::uint8_t rate, std::uint32_t eg_cnt) noexcept {
            if (rate < 48U) {
                const unsigned shift = 11U - (rate >> 2U); // rate 0-3 -> /2048 ... 44-47 -> /2
                if ((eg_cnt & ((1U << shift) - 1U)) != 0U) {
                    return 0U;
                }
                return eg_pattern[eg_rate_select[rate]][(eg_cnt >> shift) & 7U];
            }
            return eg_pattern[eg_rate_select[rate]][eg_cnt & 7U];
        }

        // rate = 2*R + key scaling; R == 0 never moves.
        [[nodiscard]] std::uint8_t calc_rate(std::uint8_t r, std::uint8_t kc,
                                             std::uint8_t ks) noexcept {
            if (r == 0U) {
                return 0U;
            }
            const unsigned rate = 2U * r + (static_cast<unsigned>(kc) >> (3U - ks));
            return static_cast<std::uint8_t>(rate > 63U ? 63U : rate);
        }
    } // namespace

    chip_metadata ym2610::metadata() const noexcept {
        return {
            .manufacturer = "Yamaha",
            .part_number = "ym2610",
            .family = "opnb",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void ym2610::reset(reset_kind kind) {
        ssg_.reset(kind);
        adpcm_a_.reset(kind);
        adpcm_b_.reset(kind);

        channels_ = {};
        eg_counter_ = 0U;
        fm_regs_.fill(0U);
        bank_a_addr_ = 0U;
        bank_b_addr_ = 0U;

        timer_a_counter_ = 0U;
        timer_b_counter_ = 0U;
        timer_a_running_ = false;
        timer_b_running_ = false;
        timer_a_flag_ = false;
        timer_b_flag_ = false;
        irq_enable_a_ = false;
        irq_enable_b_ = false;
        timer_prescale_ = 0U;
        timer_b_sub_ = 0U;
        busy_remaining_ = 0U;

        last_left_ = 0;
        last_right_ = 0;
        prescaler_ = 0;
        sample_queue_.clear();
    }

    // ---- bus dispatch -------------------------------------------------------

    void ym2610::write_data_a(std::uint8_t value) noexcept {
        const std::uint8_t reg = bank_a_addr_;
        busy_remaining_ = 32U;

        if (reg < 0x10U) {
            // SSG $00..$0F.
            ssg_.write_reg(reg, value);
            return;
        }
        if (reg >= 0x10U && reg < 0x1CU) {
            // ADPCM-B at $10..$1B on the bus -> sub-block local $00..$0B.
            adpcm_b_.write_reg(static_cast<std::uint8_t>(reg - 0x10U), value);
            return;
        }
        // FM common + FM channels 1/2 (bank A).
        write_fm(0, reg, value);
    }

    void ym2610::write_data_b(std::uint8_t value) noexcept {
        const std::uint8_t reg = bank_b_addr_;
        busy_remaining_ = 32U;

        if (reg >= 0x10U && reg < 0x30U) {
            // ADPCM-A registers occupy bank-B $10..$3D on silicon; forward the
            // $00..$2D ADPCM-A range by subtracting 0x10 from the bus address.
            adpcm_a_.write_reg(static_cast<std::uint8_t>(reg - 0x10U), value);
            return;
        }
        // FM channels 3/4 (bank B).
        write_fm(1, reg, value);
    }

    void ym2610::write_fm(int bank, std::uint8_t reg, std::uint8_t value) noexcept {
        const std::uint16_t image = static_cast<std::uint16_t>((bank << 8U) | reg);
        fm_regs_[image] = value;

        // FM-common registers (timers, LFO, key-on) live only in bank A.
        if (bank == 0) {
            if (reg == 0x27U) {
                write_timer_control(value);
                return;
            }
            if (reg == 0x28U) {
                fm_key_on_off(value);
                return;
            }
            if (reg < 0x30U) {
                // $22 LFO, $24/$25 CLKA, $26 CLKB stored only; no decode here.
                return;
            }
        }

        if (reg < 0x30U) {
            return; // bank B has no common registers
        }

        // Per-channel register pair: bank 0 -> channels 0/1, bank 1 -> channels
        // 2/3. The low 2 bits select the channel within the pair (value 3 is
        // unused on this part).
        const std::uint8_t sel = reg & 0x03U;
        if (sel == 0x03U) {
            return;
        }
        const std::size_t ch_index = static_cast<std::size_t>(bank * 2 + sel);
        channel_state& ch = channels_[ch_index];

        if (reg < 0xA0U) {
            // Per-operator parameter blocks $30..$9F. The OPN operator order in
            // the register map is S1, S3, S2, S4 (interleaved); bits 3:2 of the
            // address pick the slot.
            static constexpr std::array<std::uint8_t, 4> slot_map = {0U, 2U, 1U, 3U};
            operator_state& op = ch.op[slot_map[(reg >> 2U) & 3U]];
            switch (reg & 0xF0U) {
            case 0x30U:
                op.dt = (value >> 4U) & 7U;
                op.mul = value & 0x0FU;
                break;
            case 0x40U:
                op.tl = value & 0x7FU;
                break;
            case 0x50U:
                op.ks = value >> 6U;
                op.ar = value & 0x1FU;
                break;
            case 0x60U:
                op.d1r = value & 0x1FU; // bit7 AM-enable stored only until the LFO
                break;
            case 0x70U:
                op.d2r = value & 0x1FU;
                break;
            case 0x80U:
                op.d1l = value >> 4U;
                op.rr = value & 0x0FU;
                break;
            default: // 0x90 SSG-EG envelope stored only until the conformance pass
                break;
            }
            return;
        }

        // Frequency + algorithm blocks $A0..$B6.
        switch (reg & 0xFCU) {
        case 0xA0U:
            // F-number low byte; the high 3 bits + block come from the $A4 latch.
            ch.fnum = static_cast<std::uint16_t>((ch.fnum & 0x0700U) | value);
            break;
        case 0xA4U:
            ch.fnum = static_cast<std::uint16_t>((ch.fnum & 0x00FFU) | ((value & 0x07U) << 8U));
            ch.block = (value >> 3U) & 0x07U;
            break;
        case 0xB0U:
            ch.feedback = (value >> 3U) & 0x07U;
            ch.algorithm = value & 0x07U;
            break;
        case 0xB4U:
            // bit7 = left enable, bit6 = right enable (PMS/AMS stored only).
            ch.pan = static_cast<std::uint8_t>(((value >> 7U) & 1U) | (((value >> 6U) & 1U) << 1U));
            break;
        default:
            break;
        }
    }

    void ym2610::fm_key_on_off(std::uint8_t value) noexcept {
        // $28: bits 1:0 select the FM channel (0/1 = bank-A ch 1/2, 2/3 =
        // bank-B ch 3/4; bit2 distinguishes the bank pair), bits 7:4 are the
        // per-slot key flags (S1, S2, S3, S4).
        const std::uint8_t sel = value & 0x03U;
        if (sel == 0x03U) {
            return;
        }
        const std::size_t ch_index =
            static_cast<std::size_t>(((value & 0x04U) != 0U ? 2 : 0) + sel);
        channel_state& ch = channels_[ch_index];
        for (int slot = 0; slot < operator_count; ++slot) {
            operator_state& op = ch.op[static_cast<std::size_t>(slot)];
            const bool on = (value & (0x10U << static_cast<unsigned>(slot))) != 0U;
            if (on && !op.key_on) {
                op.phase = 0U;
                op.state = eg_state::attack;
            } else if (!on && op.key_on) {
                op.state = eg_state::release;
            }
            op.key_on = on;
        }
    }

    void ym2610::write_timer_control(std::uint8_t value) noexcept {
        // $27: bit0/1 load+run Timer A/B (reload on the 0->1 edge), bit2/3 IRQ
        // enable A/B, bit4/5 write-one-to-clear flag A/B (bits 6:7 CSM/mode are
        // stored only).
        const bool run_a = (value & 0x01U) != 0U;
        const bool run_b = (value & 0x02U) != 0U;
        if (run_a && !timer_a_running_) {
            timer_a_counter_ = timer_a_load();
        }
        if (run_b && !timer_b_running_) {
            timer_b_counter_ = fm_regs_[0x26];
        }
        timer_a_running_ = run_a;
        timer_b_running_ = run_b;
        irq_enable_a_ = (value & 0x04U) != 0U;
        irq_enable_b_ = (value & 0x08U) != 0U;
        if ((value & 0x10U) != 0U) {
            timer_a_flag_ = false;
        }
        if ((value & 0x20U) != 0U) {
            timer_b_flag_ = false;
        }
    }

    std::uint16_t ym2610::timer_a_load() const noexcept {
        // CLKA: 10 bits across $24 (high 8) and $25 (low 2).
        return static_cast<std::uint16_t>((fm_regs_[0x24] << 2U) | (fm_regs_[0x25] & 0x03U));
    }

    std::uint8_t ym2610::read_status() const noexcept {
        std::uint8_t status = 0U;
        if (timer_a_flag_) {
            status |= status_timer_a;
        }
        if (timer_b_flag_) {
            status |= status_timer_b;
        }
        if (busy_remaining_ != 0U) {
            status |= status_busy;
        }
        return status;
    }

    void ym2610::update_irq() noexcept {
        // No external IRQ line is wired on this part's Mnemos surface; the status
        // flags are read-pollable. Kept as a hook for the conformance pass.
    }

    void ym2610::step_timer_a() noexcept {
        if (!timer_a_running_) {
            return;
        }
        if (++timer_a_counter_ >= 1024U) {
            timer_a_counter_ = timer_a_load();
            timer_a_flag_ = true;
            update_irq();
        }
    }

    void ym2610::step_timer_b() noexcept {
        if (!timer_b_running_) {
            return;
        }
        if (++timer_b_counter_ >= 256U) {
            timer_b_counter_ = fm_regs_[0x26];
            timer_b_flag_ = true;
            update_irq();
        }
    }

    // ---- FM synthesis -------------------------------------------------------

    std::uint8_t ym2610::key_code(const channel_state& ch) noexcept {
        // Key code = block in bits 4:2, plus a 2-bit note code derived from the
        // top F-number bits (the standard OPN F11/F10/(F9|F8) rule).
        const unsigned f11 = (ch.fnum >> 10U) & 1U;
        const unsigned f10 = (ch.fnum >> 9U) & 1U;
        const unsigned f9 = (ch.fnum >> 8U) & 1U;
        const unsigned f8 = (ch.fnum >> 7U) & 1U;
        const unsigned n4 = f11;
        const unsigned n3 = (f11 & (f10 | f9 | f8)) | ((~f11 & 1U) & f10 & f9 & f8);
        const unsigned note = (n4 << 1U) | n3;
        return static_cast<std::uint8_t>((static_cast<unsigned>(ch.block) << 2U) | note);
    }

    std::uint32_t ym2610::phase_increment(const channel_state& ch,
                                          const operator_state& op) const noexcept {
        // Base increment: (fnum << block) gives the per-sample phase step in
        // 1/(2^20) cycle units, scaled so block 0 reproduces the OPN octave.
        std::uint32_t inc = (static_cast<std::uint32_t>(ch.fnum) << ch.block) >> 1U;

        const std::uint8_t dt = op.dt & 3U;
        if (dt != 0U) {
            const auto delta = static_cast<std::int32_t>(dt_table[key_code(ch) & 31U][dt]);
            const auto signed_inc = static_cast<std::int32_t>(inc);
            inc = static_cast<std::uint32_t>((op.dt & 4U) != 0U ? signed_inc - delta
                                                                : signed_inc + delta);
        }
        if (op.mul == 0U) {
            inc >>= 1U; // multiply field 0 means x0.5
        } else {
            inc *= op.mul;
        }
        return inc & 0xFFFFFU;
    }

    void ym2610::eg_step(operator_state& op, std::uint8_t kc) noexcept {
        switch (op.state) {
        case eg_state::attack: {
            const std::uint8_t rate = calc_rate(op.ar, kc, op.ks);
            if (rate >= 62U) {
                op.eg_level = 0U;
                op.state = eg_state::decay;
                return;
            }
            const std::uint8_t inc = eg_inc_for(rate, eg_counter_);
            if (inc != 0U && op.eg_level > 0U) {
                const auto level = static_cast<std::int32_t>(op.eg_level);
                op.eg_level = static_cast<std::uint16_t>(
                    level + (((~level) * static_cast<std::int32_t>(inc)) >> 4));
                if (static_cast<std::int16_t>(op.eg_level) <= 0) {
                    op.eg_level = 0U;
                    op.state = eg_state::decay;
                }
            }
            break;
        }
        case eg_state::decay: {
            const std::uint16_t threshold =
                op.d1l == 15U ? 1023U : static_cast<std::uint16_t>(op.d1l * 32U);
            op.eg_level = static_cast<std::uint16_t>(
                op.eg_level + eg_inc_for(calc_rate(op.d1r, kc, op.ks), eg_counter_));
            if (op.eg_level >= threshold) {
                op.eg_level = threshold;
                op.state = eg_state::sustain;
            }
            break;
        }
        case eg_state::sustain:
            op.eg_level = static_cast<std::uint16_t>(
                op.eg_level + eg_inc_for(calc_rate(op.d2r, kc, op.ks), eg_counter_));
            if (op.eg_level > 1023U) {
                op.eg_level = 1023U;
            }
            break;
        case eg_state::release:
            // RR is 4-bit; the effective 5-bit rate is RR*2+1.
            op.eg_level = static_cast<std::uint16_t>(
                op.eg_level +
                eg_inc_for(calc_rate(static_cast<std::uint8_t>((op.rr << 1U) | 1U), kc, op.ks),
                           eg_counter_));
            if (op.eg_level >= 1023U) {
                op.eg_level = 1023U;
                op.state = eg_state::off;
            }
            break;
        case eg_state::off:
            break;
        }
    }

    std::int32_t ym2610::op_calc(const operator_state& op, std::uint32_t phase,
                                 std::int32_t modulation) noexcept {
        const std::uint32_t atten = (static_cast<std::uint32_t>(op.tl) << 3U) + op.eg_level;
        if (atten >= 1024U) {
            return 0;
        }
        std::uint32_t index = (phase >> 10U) & 0x3FFU;
        index = (index + (static_cast<std::uint32_t>(modulation >> 1) & 0x3FFU)) & 0x3FFU;
        const std::uint16_t log_sin = sin_table[index];
        const std::uint32_t total_log = log_sin + (atten << 2U);
        if (total_log >= 4096U) {
            return 0;
        }
        std::int32_t linear = exp_table[total_log & 0x3FFU] >> ((total_log >> 10U) << 2U);
        if (index >= 512U) {
            linear = -linear; // second half of the sine is negative
        }
        return linear;
    }

    std::int32_t ym2610::channel_calc(channel_state& ch) noexcept {
        auto& op = ch.op;
        const std::uint8_t kc = key_code(ch);

        // Advance every operator's phase, then step its envelope.
        for (int i = 0; i < operator_count; ++i) {
            auto& o = op[static_cast<std::size_t>(i)];
            o.phase = (o.phase + phase_increment(ch, o)) & 0xFFFFFU;
            eg_step(o, kc);
        }

        // S1 feedback: op_calc applies modulation>>1, so pre-shift left 1 for a
        // net (prev + cur) >> (10 - FB).
        std::int32_t fb = 0;
        if (ch.feedback > 0U) {
            fb = ((op[0].prev_output + op[0].output) >> (10U - ch.feedback)) << 1U;
        }
        const std::int32_t s1 = op_calc(op[0], op[0].phase, fb);
        op[0].prev_output = op[0].output;
        op[0].output = s1;

        // The 8 OPN connection algorithms (slot order S1, S2, S3, S4).
        std::int32_t s2 = 0;
        std::int32_t s3 = 0;
        std::int32_t out = 0;
        switch (ch.algorithm) {
        case 0: // S1 -> S2 -> S3 -> S4
            s2 = op_calc(op[1], op[1].phase, s1);
            s3 = op_calc(op[2], op[2].phase, s2);
            out = op_calc(op[3], op[3].phase, s3);
            break;
        case 1: // (S1 + S2) -> S3 -> S4
            s2 = op_calc(op[1], op[1].phase, 0);
            s3 = op_calc(op[2], op[2].phase, s1 + s2);
            out = op_calc(op[3], op[3].phase, s3);
            break;
        case 2: // S1 + (S2 -> S3) -> S4
            s2 = op_calc(op[1], op[1].phase, 0);
            s3 = op_calc(op[2], op[2].phase, s2);
            out = op_calc(op[3], op[3].phase, s1 + s3);
            break;
        case 3: // (S1 -> S2) + S3 -> S4
            s2 = op_calc(op[1], op[1].phase, s1);
            s3 = op_calc(op[2], op[2].phase, 0);
            out = op_calc(op[3], op[3].phase, s2 + s3);
            break;
        case 4: // (S1 -> S2) + (S3 -> S4)
            s2 = op_calc(op[1], op[1].phase, s1);
            s3 = op_calc(op[2], op[2].phase, 0);
            out = s2 + op_calc(op[3], op[3].phase, s3);
            break;
        case 5: // S1 modulates S2, S3, S4
            out = op_calc(op[1], op[1].phase, s1) + op_calc(op[2], op[2].phase, s1) +
                  op_calc(op[3], op[3].phase, s1);
            break;
        case 6: // (S1 -> S2) + S3 + S4
            out = op_calc(op[1], op[1].phase, s1) + op_calc(op[2], op[2].phase, 0) +
                  op_calc(op[3], op[3].phase, 0);
            break;
        default: // 7: four parallel carriers
            out = s1 + op_calc(op[1], op[1].phase, 0) + op_calc(op[2], op[2].phase, 0) +
                  op_calc(op[3], op[3].phase, 0);
            break;
        }
        return out;
    }

    // ---- top-level mix ------------------------------------------------------

    void ym2610::step() noexcept {
        ++eg_counter_;

        std::int32_t left = 0;
        std::int32_t right = 0;

        // FM core: four 4-operator channels with per-channel L/R routing.
        for (auto& ch : channels_) {
            const std::int32_t out = channel_calc(ch);
            if ((ch.pan & 1U) != 0U) {
                left += out;
            }
            if ((ch.pan & 2U) != 0U) {
                right += out;
            }
        }

        // SSG / ADPCM-A / ADPCM-B: each renders one native sample and contributes
        // its stereo pair. The sub-chips already mix to int16 (a quarter-scale
        // headroom keeps the four-way sum inside int16).
        ssg_.step();
        adpcm_a_.step();
        adpcm_b_.step();
        left += ssg_.last_left() + adpcm_a_.last_left() + adpcm_b_.last_left();
        right += ssg_.last_right() + adpcm_a_.last_right() + adpcm_b_.last_right();

        last_left_ = static_cast<std::int16_t>(clamp16(left));
        last_right_ = static_cast<std::int16_t>(clamp16(right));
    }

    void ym2610::generate(std::span<std::int16_t> buf_lr) noexcept {
        const std::size_t pairs = buf_lr.size() / 2U;
        for (std::size_t i = 0; i < pairs; ++i) {
            step();
            buf_lr[i * 2U] = last_left_;
            buf_lr[i * 2U + 1U] = last_right_;
        }
    }

    void ym2610::tick(std::uint64_t cycles) {
        for (std::uint64_t c = 0; c < cycles; ++c) {
            if (busy_remaining_ != 0U) {
                --busy_remaining_;
            }
            // Timer cadence: Timer A counts once per 16 prescaled steps, Timer B
            // once per 16*16 (a coarse OPN-family cadence; exact divider lands in
            // the conformance pass).
            if (++timer_prescale_ >= 16U) {
                timer_prescale_ = 0U;
                step_timer_a();
                if (++timer_b_sub_ >= 16U) {
                    timer_b_sub_ = 0U;
                    step_timer_b();
                }
            }
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

    std::size_t ym2610::drain_samples(std::int16_t* out, std::size_t max_pairs) noexcept {
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

    // ---- persistence --------------------------------------------------------

    void ym2610::save_state(state_writer& writer) const {
        // FM core.
        writer.bytes(fm_regs_);
        writer.u8(bank_a_addr_);
        writer.u8(bank_b_addr_);
        writer.u32(eg_counter_);
        for (const auto& ch : channels_) {
            writer.u16(ch.fnum);
            writer.u8(ch.block);
            writer.u8(ch.feedback);
            writer.u8(ch.algorithm);
            writer.u8(ch.pan);
            for (const auto& op : ch.op) {
                writer.u32(op.phase);
                writer.u32(static_cast<std::uint32_t>(op.output));
                writer.u32(static_cast<std::uint32_t>(op.prev_output));
                writer.u16(op.eg_level);
                writer.u8(static_cast<std::uint8_t>(op.state));
                writer.boolean(op.key_on);
                writer.u8(op.dt);
                writer.u8(op.mul);
                writer.u8(op.tl);
                writer.u8(op.ks);
                writer.u8(op.ar);
                writer.u8(op.d1r);
                writer.u8(op.d2r);
                writer.u8(op.d1l);
                writer.u8(op.rr);
            }
        }
        writer.u16(timer_a_counter_);
        writer.u16(timer_b_counter_);
        writer.boolean(timer_a_running_);
        writer.boolean(timer_b_running_);
        writer.boolean(timer_a_flag_);
        writer.boolean(timer_b_flag_);
        writer.boolean(irq_enable_a_);
        writer.boolean(irq_enable_b_);
        writer.u32(timer_prescale_);
        writer.u32(timer_b_sub_);
        writer.u32(busy_remaining_);
        writer.u32(static_cast<std::uint32_t>(clock_divider_));
        writer.u32(static_cast<std::uint32_t>(prescaler_));
        writer.u16(static_cast<std::uint16_t>(last_left_));
        writer.u16(static_cast<std::uint16_t>(last_right_));

        // Sub-chip state (length-prefixed so each block stays self-describing).
        std::vector<std::uint8_t> sub;
        state_writer sub_writer(sub);
        ssg_.save_state(sub_writer);
        writer.blob(sub);
        sub.clear();
        state_writer sub_writer_a(sub);
        adpcm_a_.save_state(sub_writer_a);
        writer.blob(sub);
        sub.clear();
        state_writer sub_writer_b(sub);
        adpcm_b_.save_state(sub_writer_b);
        writer.blob(sub);
    }

    void ym2610::load_state(state_reader& reader) {
        reader.bytes(fm_regs_);
        bank_a_addr_ = reader.u8();
        bank_b_addr_ = reader.u8();
        eg_counter_ = reader.u32();
        for (auto& ch : channels_) {
            ch.fnum = reader.u16();
            ch.block = reader.u8();
            ch.feedback = reader.u8();
            ch.algorithm = reader.u8();
            ch.pan = reader.u8();
            for (auto& op : ch.op) {
                op.phase = reader.u32();
                op.output = static_cast<std::int32_t>(reader.u32());
                op.prev_output = static_cast<std::int32_t>(reader.u32());
                op.eg_level = reader.u16();
                op.state = static_cast<eg_state>(reader.u8());
                op.key_on = reader.boolean();
                op.dt = reader.u8();
                op.mul = reader.u8();
                op.tl = reader.u8();
                op.ks = reader.u8();
                op.ar = reader.u8();
                op.d1r = reader.u8();
                op.d2r = reader.u8();
                op.d1l = reader.u8();
                op.rr = reader.u8();
            }
        }
        timer_a_counter_ = reader.u16();
        timer_b_counter_ = reader.u16();
        timer_a_running_ = reader.boolean();
        timer_b_running_ = reader.boolean();
        timer_a_flag_ = reader.boolean();
        timer_b_flag_ = reader.boolean();
        irq_enable_a_ = reader.boolean();
        irq_enable_b_ = reader.boolean();
        timer_prescale_ = reader.u32();
        timer_b_sub_ = reader.u32();
        busy_remaining_ = reader.u32();
        clock_divider_ = static_cast<int>(reader.u32());
        prescaler_ = static_cast<int>(reader.u32());
        last_left_ = static_cast<std::int16_t>(reader.u16());
        last_right_ = static_cast<std::int16_t>(reader.u16());

        const std::vector<std::uint8_t> ssg_blob = reader.blob();
        state_reader ssg_reader(ssg_blob);
        ssg_.load_state(ssg_reader);
        if (!ssg_reader.ok()) {
            reader.fail();
        }
        const std::vector<std::uint8_t> a_blob = reader.blob();
        state_reader a_reader(a_blob);
        adpcm_a_.load_state(a_reader);
        if (!a_reader.ok()) {
            reader.fail();
        }
        const std::vector<std::uint8_t> b_blob = reader.blob();
        state_reader b_reader(b_blob);
        adpcm_b_.load_state(b_reader);
        if (!b_reader.ok()) {
            reader.fail();
        }
    }

    instrumentation::ichip_introspection& ym2610::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> ym2610::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"BANK_A_ADDR", bank_a_addr_, 8U, fmt::unsigned_integer};
        register_view_[1] = {"BANK_B_ADDR", bank_b_addr_, 8U, fmt::unsigned_integer};
        register_view_[2] = {"STATUS", read_status(), 8U, fmt::flags};
        register_view_[3] = {"CLKA", timer_a_load(), 10U, fmt::unsigned_integer};
        register_view_[4] = {"CLKB", fm_regs_[0x26], 8U, fmt::unsigned_integer};
        register_view_[5] = {"EG_CNT", eg_counter_, 32U, fmt::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto ym2610_registration =
            register_factory("yamaha.ym2610", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<ym2610>(); });
    } // namespace

} // namespace mnemos::chips::audio
