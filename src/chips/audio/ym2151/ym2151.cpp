#include "ym2151.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <cmath>
#include <memory>

namespace mnemos::chips::audio {

    namespace {
        constexpr double pi = 3.14159265358979323846;

        // Log-sine (first quadrant folded across all four) and exp tables --
        // the output pipeline the OPM shares with the OPN family; built once
        // at static init, same construction as the in-tree ym2612.
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

        // KC note nibble -> semitones above the octave's C. The hardware skips
        // every fourth code (3/7/11/15 sound as the next note).
        constexpr std::array<std::uint8_t, 16> kc_semitone = {1U, 2U, 3U, 4U,  4U,  5U,  6U,  7U,
                                                              7U, 8U, 9U, 10U, 10U, 11U, 12U, 13U};

        // DT2 coarse detune in 1/64-semitone steps: +0 / +600 / +781 / +950
        // cents.
        constexpr std::array<std::uint16_t, 4> dt2_steps = {0U, 384U, 500U, 608U};

        // DT1 fine detune deltas (YM2608-family application-manual table),
        // indexed [keycode/4][dt1 & 3]; first-order reuse for the OPM pending
        // the conformance pass.
        constexpr std::uint8_t dt1_table[32][4] = {
            {0, 0, 1, 2},   {0, 0, 1, 2},   {0, 0, 1, 2},   {0, 0, 1, 2},   {0, 1, 2, 2},
            {0, 1, 2, 3},   {0, 1, 2, 3},   {0, 1, 2, 3},   {0, 1, 2, 4},   {0, 1, 3, 4},
            {0, 1, 3, 4},   {0, 1, 3, 5},   {0, 2, 4, 5},   {0, 2, 4, 6},   {0, 2, 4, 6},
            {0, 2, 5, 7},   {0, 2, 5, 8},   {0, 3, 6, 8},   {0, 3, 6, 9},   {0, 3, 7, 10},
            {0, 4, 8, 11},  {0, 4, 8, 12},  {0, 4, 9, 13},  {0, 5, 10, 14}, {0, 5, 11, 16},
            {0, 6, 12, 17}, {0, 6, 13, 19}, {0, 7, 14, 20}, {0, 8, 16, 22}, {0, 8, 16, 22},
            {0, 8, 16, 22}, {0, 8, 16, 22}};

        // Envelope increment patterns + the rate -> pattern selector (the
        // OPN/OPM EG stepping hardware; same tables as the in-tree ym2612).
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

        // Phase increments for one octave in 1/64-semitone steps (20-bit
        // phase counter, one sample per 64 chip clocks): octave 0 starts at
        // C0 = 16.35159783 Hz at the nominal 3.579545 MHz clock; the ratio is
        // clock-invariant because note frequency and sample rate both scale
        // with the crystal.
        const std::array<std::uint32_t, 768> kf_increment = [] {
            std::array<std::uint32_t, 768> t{};
            for (std::size_t i = 0; i < 768; ++i) {
                const double freq = 16.35159783 * std::pow(2.0, static_cast<double>(i) / 768.0);
                const double inc = freq * 1048576.0 * 64.0 / 3579545.0;
                t[i] = static_cast<std::uint32_t>(inc + 0.5);
            }
            return t;
        }();

        // rate = 2*R + key scaling; R == 0 never moves.
        [[nodiscard]] std::uint8_t calc_rate(std::uint8_t r, std::uint8_t kc,
                                             std::uint8_t ks) noexcept {
            if (r == 0U) {
                return 0U;
            }
            const unsigned rate = 2U * r + (static_cast<unsigned>(kc) >> (5U - ks));
            return static_cast<std::uint8_t>(rate > 63U ? 63U : rate);
        }
    } // namespace

    chip_metadata ym2151::metadata() const noexcept {
        return {
            .manufacturer = "Yamaha",
            .part_number = "ym2151",
            .family = "opm",
            .klass = chip_class::audio_synth,
            .revision = 1U,
        };
    }

    void ym2151::reset(reset_kind /*kind*/) {
        registers_.fill(0U);
        channels_ = {};
        address_ = 0U;
        eg_counter_ = 0U;
        timer_a_counter_ = 0U;
        timer_b_counter_ = 0U;
        timer_a_running_ = false;
        timer_b_running_ = false;
        timer_a_flag_ = false;
        timer_b_flag_ = false;
        irq_enable_a_ = false;
        irq_enable_b_ = false;
        prescale_64_ = 0U;
        timer_b_sub_ = 0U;
        busy_remaining_ = 0U;
        elapsed_ = 0U;
        const bool was_asserted = irq_line_;
        irq_line_ = false;
        if (was_asserted && irq_) {
            irq_(false);
        }
    }

    void ym2151::update_irq() noexcept {
        const bool asserted = (timer_a_flag_ && irq_enable_a_) || (timer_b_flag_ && irq_enable_b_);
        if (asserted != irq_line_) {
            irq_line_ = asserted;
            if (irq_) {
                irq_(asserted);
            }
        }
    }

    void ym2151::key_on_off(std::uint8_t value) noexcept {
        // First-cut slot mapping (validated in the conformance pass):
        // bit3 = M1, bit4 = M2, bit5 = C1, bit6 = C2.
        channel_state& ch = channels_[value & 7U];
        for (int slot = 0; slot < operator_count; ++slot) {
            operator_state& op = ch.op[static_cast<std::size_t>(slot)];
            const bool on = (value & (1U << (3U + static_cast<unsigned>(slot)))) != 0U;
            if (on && !op.key_on) {
                op.phase = 0U;
                op.state = eg_state::attack;
            } else if (!on && op.key_on) {
                op.state = eg_state::release;
            }
            op.key_on = on;
        }
    }

    void ym2151::write_data(std::uint8_t value) noexcept {
        registers_[address_] = value;
        busy_remaining_ = busy_clocks;

        if (address_ == 0x08U) {
            key_on_off(value);
            return;
        }
        if (address_ == 0x14U) {
            // Timer control: bit0/1 run A/B (reload on the 0->1 edge),
            // bit2/3 IRQ enable A/B, bit4/5 write-one-to-clear flag A/B.
            // (bit7 CSM is stored but not acted on in this increment.)
            const bool run_a = (value & 0x01U) != 0U;
            const bool run_b = (value & 0x02U) != 0U;
            if (run_a && !timer_a_running_) {
                timer_a_counter_ = timer_a_load();
            }
            if (run_b && !timer_b_running_) {
                timer_b_counter_ = registers_[0x12];
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
            update_irq();
            return;
        }
        if (address_ >= 0x20U && address_ <= 0x3FU) {
            channel_state& ch = channels_[address_ & 7U];
            switch (address_ & 0xF8U) {
            case 0x20U:
                // bit6 = left enable, bit7 = right enable (first-cut order).
                ch.rl =
                    static_cast<std::uint8_t>(((value >> 6U) & 1U) | (((value >> 7U) & 1U) << 1U));
                ch.feedback = (value >> 3U) & 7U;
                ch.connection = value & 7U;
                break;
            case 0x28U:
                ch.kc = value & 0x7FU;
                break;
            case 0x30U:
                ch.kf = value >> 2U;
                break;
            default:
                break; // $38 PMS/AMS stored only until the LFO lands
            }
            return;
        }
        if (address_ >= 0x40U) {
            // Per-operator parameter: slot order M1, M2, C1, C2 in bits 3-4.
            channel_state& ch = channels_[address_ & 7U];
            operator_state& op = ch.op[(address_ >> 3U) & 3U];
            switch (address_ & 0xE0U) {
            case 0x40U:
                op.dt1 = (value >> 4U) & 7U;
                op.mul = value & 0x0FU;
                break;
            case 0x60U:
                op.tl = value & 0x7FU;
                break;
            case 0x80U:
                op.ks = value >> 6U;
                op.ar = value & 0x1FU;
                break;
            case 0xA0U:
                op.d1r = value & 0x1FU; // bit7 AMS-EN stored only until the LFO
                break;
            case 0xC0U:
                op.dt2 = value >> 6U;
                op.d2r = value & 0x1FU;
                break;
            default: // 0xE0
                op.d1l = value >> 4U;
                op.rr = value & 0x0FU;
                break;
            }
        }
    }

    std::uint8_t ym2151::read_status() const noexcept {
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

    void ym2151::step_timer_a() noexcept {
        if (!timer_a_running_) {
            return;
        }
        if (++timer_a_counter_ >= 1024U) {
            timer_a_counter_ = timer_a_load();
            timer_a_flag_ = true;
            update_irq();
        }
    }

    void ym2151::step_timer_b() noexcept {
        if (!timer_b_running_) {
            return;
        }
        if (++timer_b_counter_ >= 256U) {
            timer_b_counter_ = registers_[0x12];
            timer_b_flag_ = true;
            update_irq();
        }
    }

    void ym2151::tick(std::uint64_t cycles) {
        elapsed_ += cycles;
        for (std::uint64_t c = 0; c < cycles; ++c) {
            if (busy_remaining_ != 0U) {
                --busy_remaining_;
            }
            if (++prescale_64_ == timer_a_step_clocks) {
                prescale_64_ = 0U;
                step_timer_a();
                if (++timer_b_sub_ == timer_b_step_clocks / timer_a_step_clocks) {
                    timer_b_sub_ = 0U;
                    step_timer_b();
                }
            }
        }
    }

    // ---- synthesis ----------------------------------------------------------

    std::uint32_t ym2151::phase_increment(const channel_state& ch,
                                          const operator_state& op) const noexcept {
        std::uint32_t pos =
            static_cast<std::uint32_t>(kc_semitone[ch.kc & 15U]) * 64U + ch.kf + dt2_steps[op.dt2];
        std::uint32_t octave = ch.kc >> 4U;
        while (pos >= 768U) {
            pos -= 768U;
            ++octave;
        }
        std::uint32_t inc = kf_increment[pos] << octave;

        const std::uint8_t dt = op.dt1 & 3U;
        if (dt != 0U) {
            const auto delta = static_cast<std::int32_t>(dt1_table[(ch.kc >> 2U) & 31U][dt]);
            const auto signed_inc = static_cast<std::int32_t>(inc);
            inc = static_cast<std::uint32_t>((op.dt1 & 4U) != 0U ? signed_inc - delta
                                                                 : signed_inc + delta);
        }
        if (op.mul == 0U) {
            inc >>= 1U; // multiply field 0 means x0.5
        } else {
            inc *= op.mul;
        }
        return inc & 0xFFFFFU;
    }

    void ym2151::eg_step(operator_state& op, std::uint8_t kc) noexcept {
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
                // Hardware exponential convergence: level += (~level * inc) >> 4.
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

    std::int32_t ym2151::op_calc(const operator_state& op, std::uint32_t phase,
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

    std::int32_t ym2151::channel_calc(channel_state& ch) noexcept {
        auto& op = ch.op;

        // Advance every operator's phase, then step its envelope.
        for (int i = 0; i < operator_count; ++i) {
            auto& o = op[static_cast<std::size_t>(i)];
            o.phase = (o.phase + phase_increment(ch, o)) & 0xFFFFFU;
            eg_step(o, ch.kc);
        }

        // M1 feedback: op_calc applies modulation>>1, so pre-shift left 1 for
        // a net (prev + cur) >> (10 - FB).
        std::int32_t fb = 0;
        if (ch.feedback > 0U) {
            fb = ((op[0].prev_output + op[0].output) >> (10U - ch.feedback)) << 1U;
        }
        const std::int32_t m1 = op_calc(op[0], op[0].phase, fb);
        op[0].prev_output = op[0].output;
        op[0].output = m1;

        // First-cut connection set in register slot order M1, M2, C1, C2
        // (exact OPM routing is a conformance-pass item).
        std::int32_t m2 = 0;
        std::int32_t c1 = 0;
        std::int32_t out = 0;
        switch (ch.connection) {
        case 0: // M1 -> M2 -> C1 -> C2
            m2 = op_calc(op[1], op[1].phase, m1);
            c1 = op_calc(op[2], op[2].phase, m2);
            out = op_calc(op[3], op[3].phase, c1);
            break;
        case 1: // (M1 + M2) -> C1 -> C2
            m2 = op_calc(op[1], op[1].phase, 0);
            c1 = op_calc(op[2], op[2].phase, m1 + m2);
            out = op_calc(op[3], op[3].phase, c1);
            break;
        case 2: // M1 + (M2 -> C1) -> C2
            m2 = op_calc(op[1], op[1].phase, 0);
            c1 = op_calc(op[2], op[2].phase, m2);
            out = op_calc(op[3], op[3].phase, m1 + c1);
            break;
        case 3: // (M1 -> M2) + C1 -> C2
            m2 = op_calc(op[1], op[1].phase, m1);
            c1 = op_calc(op[2], op[2].phase, 0);
            out = op_calc(op[3], op[3].phase, m2 + c1);
            break;
        case 4: // (M1 -> M2) + (C1 -> C2)
            m2 = op_calc(op[1], op[1].phase, m1);
            c1 = op_calc(op[2], op[2].phase, 0);
            out = m2 + op_calc(op[3], op[3].phase, c1);
            break;
        case 5: // M1 modulates M2, C1, C2
            out = op_calc(op[1], op[1].phase, m1) + op_calc(op[2], op[2].phase, m1) +
                  op_calc(op[3], op[3].phase, m1);
            break;
        case 6: // (M1 -> M2) + C1 + C2
            out = op_calc(op[1], op[1].phase, m1) + op_calc(op[2], op[2].phase, 0) +
                  op_calc(op[3], op[3].phase, 0);
            break;
        default: // 7: four parallel carriers
            out = m1 + op_calc(op[1], op[1].phase, 0) + op_calc(op[2], op[2].phase, 0) +
                  op_calc(op[3], op[3].phase, 0);
            break;
        }
        return out;
    }

    ym2151::stereo_sample ym2151::step() noexcept {
        ++eg_counter_;
        std::int32_t left = 0;
        std::int32_t right = 0;
        for (auto& ch : channels_) {
            const std::int32_t out = channel_calc(ch);
            if ((ch.rl & 1U) != 0U) {
                left += out;
            }
            if ((ch.rl & 2U) != 0U) {
                right += out;
            }
        }
        const auto clamp = [](std::int32_t v) -> std::int16_t {
            if (v > 32767) {
                return 32767;
            }
            if (v < -32768) {
                return -32768;
            }
            return static_cast<std::int16_t>(v);
        };
        return {clamp(left), clamp(right)};
    }

    void ym2151::update(std::span<std::int16_t> out) noexcept {
        for (std::size_t i = 0; i + 1U < out.size(); i += 2U) {
            const stereo_sample s = step();
            out[i] = s.left;
            out[i + 1U] = s.right;
        }
    }

    // ---- persistence ----------------------------------------------------------

    void ym2151::save_state(state_writer& writer) const {
        writer.bytes(registers_);
        writer.u8(address_);
        writer.u32(eg_counter_);
        for (const auto& ch : channels_) {
            for (const auto& op : ch.op) {
                writer.u32(op.phase);
                writer.u32(static_cast<std::uint32_t>(op.output));
                writer.u32(static_cast<std::uint32_t>(op.prev_output));
                writer.u16(op.eg_level);
                writer.u8(static_cast<std::uint8_t>(op.state));
                writer.boolean(op.key_on);
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
        writer.boolean(irq_line_);
        writer.u32(prescale_64_);
        writer.u32(timer_b_sub_);
        writer.u32(busy_remaining_);
        writer.u64(elapsed_);
    }

    void ym2151::load_state(state_reader& reader) {
        reader.bytes(registers_);
        address_ = reader.u8();
        eg_counter_ = reader.u32();
        // Re-decode the parameter registers into channel/operator state (the
        // dynamic fields then overwrite what the decode initialised). Key and
        // timer side effects are restored from the serialized fields below,
        // so $08/$10-$1F are not replayed.
        const std::uint8_t saved_address = address_;
        for (unsigned reg = 0x20U; reg <= 0xFFU; ++reg) {
            address_ = static_cast<std::uint8_t>(reg);
            const std::uint8_t value = registers_[reg];
            write_data(value);
            registers_[reg] = value; // write_data re-stores; keep the image
        }
        address_ = saved_address;
        for (auto& ch : channels_) {
            for (auto& op : ch.op) {
                op.phase = reader.u32();
                op.output = static_cast<std::int32_t>(reader.u32());
                op.prev_output = static_cast<std::int32_t>(reader.u32());
                op.eg_level = reader.u16();
                op.state = static_cast<eg_state>(reader.u8());
                op.key_on = reader.boolean();
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
        irq_line_ = reader.boolean();
        prescale_64_ = reader.u32();
        timer_b_sub_ = reader.u32();
        busy_remaining_ = reader.u32();
        elapsed_ = reader.u64();
    }

    instrumentation::ichip_introspection& ym2151::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor>
    ym2151::introspection_surface::registers_impl::registers() {
        using fmt = register_value_format;
        ym2151& chip = *owner_;
        chip.register_view_[0] = {"ADDR", chip.address_, 8U, fmt::unsigned_integer};
        chip.register_view_[1] = {"STATUS", chip.read_status(), 8U, fmt::flags};
        chip.register_view_[2] = {"CLKA", chip.timer_a_load(), 10U, fmt::unsigned_integer};
        chip.register_view_[3] = {"CLKB", chip.registers_[0x12], 8U, fmt::unsigned_integer};
        chip.register_view_[4] = {"TA", chip.timer_a_counter_, 10U, fmt::unsigned_integer};
        chip.register_view_[5] = {"TB", chip.timer_b_counter_, 8U, fmt::unsigned_integer};
        return chip.register_view_;
    }

    namespace {
        [[maybe_unused]] const auto ym2151_registration =
            register_factory("yamaha.ym2151", chip_class::audio_synth,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<ym2151>(); });
    } // namespace

} // namespace mnemos::chips::audio
