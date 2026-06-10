#include "v30.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <memory>
#include <utility>

namespace mnemos::chips::cpu {

    namespace {
        // Parity flag: set when the low byte of a result has an even number
        // of set bits.
        [[nodiscard]] constexpr bool parity_even(std::uint8_t value) noexcept {
            unsigned bits = value;
            bits ^= bits >> 4U;
            bits ^= bits >> 2U;
            bits ^= bits >> 1U;
            return (bits & 1U) == 0U;
        }

        // ALU operation indices as encoded in opcode bits 5-3.
        constexpr int alu_add = 0;
        constexpr int alu_or = 1;
        constexpr int alu_adc = 2;
        constexpr int alu_sbb = 3;
        constexpr int alu_and = 4;
        constexpr int alu_sub = 5;
        constexpr int alu_xor = 6;
        constexpr int alu_cmp = 7;
    } // namespace

    chip_metadata v30::metadata() const noexcept {
        return {
            .manufacturer = "NEC",
            .part_number = "v30",
            .family = "v-series",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    void v30::reset(reset_kind /*kind*/) {
        ax_ = bx_ = cx_ = dx_ = 0U;
        si_ = di_ = bp_ = sp_ = 0U;
        ds_ = es_ = ss_ = 0U;
        cs_ = 0xFFFFU; // first fetch at physical 0xFFFF0
        ip_ = 0U;
        flags_ = 0U;
        halted_ = false;
        irq_line_ = false;
        nmi_pending_ = false;
        interrupt_inhibit_ = false;
        seg_override_ = false;
        seg_override_value_ = 0U;
        rep_prefix_ = 0;
        step_cycles_ = 0;
        cycle_debt_ = 0;
        elapsed_ = 0U;
    }

    void v30::tick(std::uint64_t cycles) {
        cycle_debt_ += static_cast<std::int64_t>(cycles);
        while (cycle_debt_ > 0) {
            cycle_debt_ -= step_instruction();
        }
    }

    void v30::set_nmi_line(bool asserted) noexcept {
        // Edge-triggered: latch on assertion only.
        if (asserted) {
            nmi_pending_ = true;
        }
    }

    // ---- flags ------------------------------------------------------------

    void v30::set_szp8(std::uint8_t result) noexcept {
        assign_flag(flag_s, (result & 0x80U) != 0U);
        assign_flag(flag_z, result == 0U);
        assign_flag(flag_p, parity_even(result));
    }

    void v30::set_szp16(std::uint16_t result) noexcept {
        assign_flag(flag_s, (result & 0x8000U) != 0U);
        assign_flag(flag_z, result == 0U);
        assign_flag(flag_p, parity_even(static_cast<std::uint8_t>(result)));
    }

    // ---- memory + I/O -----------------------------------------------------

    std::uint8_t v30::rb(std::uint16_t segment, std::uint16_t offset) noexcept {
        return bus_ != nullptr ? bus_->read8(phys(segment, offset)) : 0xFFU;
    }

    void v30::wb(std::uint16_t segment, std::uint16_t offset, std::uint8_t value) noexcept {
        if (bus_ != nullptr) {
            bus_->write8(phys(segment, offset), value);
        }
    }

    std::uint16_t v30::rw(std::uint16_t segment, std::uint16_t offset) noexcept {
        // Little-endian. The wide bus accessor serves the common case; a word
        // at offset 0xFFFF wraps within the segment like the silicon, which a
        // physically-contiguous access cannot express, so it composes bytes.
        if (bus_ != nullptr && offset != 0xFFFFU) {
            return bus_->read16_le(phys(segment, offset));
        }
        const std::uint8_t lo = rb(segment, offset);
        const std::uint8_t hi = rb(segment, static_cast<std::uint16_t>(offset + 1U));
        return static_cast<std::uint16_t>(lo | (hi << 8U));
    }

    void v30::ww(std::uint16_t segment, std::uint16_t offset, std::uint16_t value) noexcept {
        if (bus_ != nullptr && offset != 0xFFFFU) {
            bus_->write16_le(phys(segment, offset), value);
            return;
        }
        wb(segment, offset, static_cast<std::uint8_t>(value));
        wb(segment, static_cast<std::uint16_t>(offset + 1U),
           static_cast<std::uint8_t>(value >> 8U));
    }

    std::uint8_t v30::fetch8() noexcept {
        const std::uint8_t value = rb(cs_, ip_);
        ip_ = static_cast<std::uint16_t>(ip_ + 1U);
        return value;
    }

    std::uint16_t v30::fetch16() noexcept {
        const std::uint8_t lo = fetch8();
        const std::uint8_t hi = fetch8();
        return static_cast<std::uint16_t>(lo | (hi << 8U));
    }

    void v30::push16(std::uint16_t value) noexcept {
        sp_ = static_cast<std::uint16_t>(sp_ - 2U);
        ww(ss_, sp_, value);
    }

    std::uint16_t v30::pop16() noexcept {
        const std::uint16_t value = rw(ss_, sp_);
        sp_ = static_cast<std::uint16_t>(sp_ + 2U);
        return value;
    }

    std::uint8_t v30::port_in8(std::uint16_t port) { return port_in_ ? port_in_(port) : 0xFFU; }

    void v30::port_out8(std::uint16_t port, std::uint8_t value) {
        if (port_out_) {
            port_out_(port, value);
        }
    }

    std::uint16_t v30::port_in16(std::uint16_t port) {
        const std::uint8_t lo = port_in8(port);
        const std::uint8_t hi = port_in8(static_cast<std::uint16_t>(port + 1U));
        return static_cast<std::uint16_t>(lo | (hi << 8U));
    }

    void v30::port_out16(std::uint16_t port, std::uint16_t value) {
        port_out8(port, static_cast<std::uint8_t>(value));
        port_out8(static_cast<std::uint16_t>(port + 1U), static_cast<std::uint8_t>(value >> 8U));
    }

    // ---- modrm / effective address -----------------------------------------

    std::uint16_t v30::data_segment(std::uint16_t default_segment) const noexcept {
        return seg_override_ ? seg_override_value_ : default_segment;
    }

    void v30::fetch_modrm() noexcept {
        const std::uint8_t modrm = fetch8();
        const int mod = modrm >> 6U;
        modrm_reg_ = (modrm >> 3U) & 7;
        modrm_rm_ = modrm & 7;
        rm_is_reg_ = mod == 3;
        rm_offset_ = 0U;
        rm_segment_ = 0U;
        if (rm_is_reg_) {
            return;
        }

        std::uint16_t offset = 0U;
        bool bp_based = false;
        switch (modrm_rm_) {
        case 0:
            offset = static_cast<std::uint16_t>(bx_ + si_);
            break;
        case 1:
            offset = static_cast<std::uint16_t>(bx_ + di_);
            break;
        case 2:
            offset = static_cast<std::uint16_t>(bp_ + si_);
            bp_based = true;
            break;
        case 3:
            offset = static_cast<std::uint16_t>(bp_ + di_);
            bp_based = true;
            break;
        case 4:
            offset = si_;
            break;
        case 5:
            offset = di_;
            break;
        case 6:
            if (mod == 0) {
                offset = fetch16(); // direct address, DS-relative
            } else {
                offset = bp_;
                bp_based = true;
            }
            break;
        default: // 7
            offset = bx_;
            break;
        }

        if (mod == 1) {
            const auto disp = static_cast<std::int8_t>(fetch8());
            offset = static_cast<std::uint16_t>(offset + static_cast<std::uint16_t>(disp));
        } else if (mod == 2) {
            offset = static_cast<std::uint16_t>(offset + fetch16());
        }

        rm_segment_ = data_segment(bp_based ? ss_ : ds_);
        rm_offset_ = offset;
    }

    std::uint8_t v30::read_rm8() noexcept {
        return rm_is_reg_ ? get_reg8(modrm_rm_) : rb(rm_segment_, rm_offset_);
    }

    void v30::write_rm8(std::uint8_t value) noexcept {
        if (rm_is_reg_) {
            set_reg8(modrm_rm_, value);
        } else {
            wb(rm_segment_, rm_offset_, value);
        }
    }

    std::uint16_t v30::read_rm16() noexcept {
        return rm_is_reg_ ? get_reg16(modrm_rm_) : rw(rm_segment_, rm_offset_);
    }

    void v30::write_rm16(std::uint16_t value) noexcept {
        if (rm_is_reg_) {
            set_reg16(modrm_rm_, value);
        } else {
            ww(rm_segment_, rm_offset_, value);
        }
    }

    std::uint8_t v30::get_reg8(int reg) const noexcept {
        // AL CL DL BL AH CH DH BH
        switch (reg & 7) {
        case 0:
            return static_cast<std::uint8_t>(ax_);
        case 1:
            return static_cast<std::uint8_t>(cx_);
        case 2:
            return static_cast<std::uint8_t>(dx_);
        case 3:
            return static_cast<std::uint8_t>(bx_);
        case 4:
            return static_cast<std::uint8_t>(ax_ >> 8U);
        case 5:
            return static_cast<std::uint8_t>(cx_ >> 8U);
        case 6:
            return static_cast<std::uint8_t>(dx_ >> 8U);
        default:
            return static_cast<std::uint8_t>(bx_ >> 8U);
        }
    }

    void v30::set_reg8(int reg, std::uint8_t value) noexcept {
        const auto set_lo = [value](std::uint16_t& pair) {
            pair = static_cast<std::uint16_t>((pair & 0xFF00U) | value);
        };
        const auto set_hi = [value](std::uint16_t& pair) {
            pair = static_cast<std::uint16_t>((pair & 0x00FFU) | (value << 8U));
        };
        switch (reg & 7) {
        case 0:
            set_lo(ax_);
            break;
        case 1:
            set_lo(cx_);
            break;
        case 2:
            set_lo(dx_);
            break;
        case 3:
            set_lo(bx_);
            break;
        case 4:
            set_hi(ax_);
            break;
        case 5:
            set_hi(cx_);
            break;
        case 6:
            set_hi(dx_);
            break;
        default:
            set_hi(bx_);
            break;
        }
    }

    std::uint16_t v30::get_reg16(int reg) const noexcept {
        // AX CX DX BX SP BP SI DI
        switch (reg & 7) {
        case 0:
            return ax_;
        case 1:
            return cx_;
        case 2:
            return dx_;
        case 3:
            return bx_;
        case 4:
            return sp_;
        case 5:
            return bp_;
        case 6:
            return si_;
        default:
            return di_;
        }
    }

    void v30::set_reg16(int reg, std::uint16_t value) noexcept {
        switch (reg & 7) {
        case 0:
            ax_ = value;
            break;
        case 1:
            cx_ = value;
            break;
        case 2:
            dx_ = value;
            break;
        case 3:
            bx_ = value;
            break;
        case 4:
            sp_ = value;
            break;
        case 5:
            bp_ = value;
            break;
        case 6:
            si_ = value;
            break;
        default:
            di_ = value;
            break;
        }
    }

    std::uint16_t v30::get_sreg(int reg) const noexcept {
        // ES CS SS DS
        switch (reg & 3) {
        case 0:
            return es_;
        case 1:
            return cs_;
        case 2:
            return ss_;
        default:
            return ds_;
        }
    }

    void v30::set_sreg(int reg, std::uint16_t value) noexcept {
        switch (reg & 3) {
        case 0:
            es_ = value;
            break;
        case 1:
            cs_ = value;
            break;
        case 2:
            ss_ = value;
            // Loading SS shadows interrupts for one instruction so SS:SP
            // updates are atomic against IRQs.
            interrupt_inhibit_ = true;
            break;
        default:
            ds_ = value;
            break;
        }
    }

    // ---- ALU ----------------------------------------------------------------

    std::uint8_t v30::alu8(int op, std::uint8_t lhs, std::uint8_t rhs) noexcept {
        switch (op) {
        case alu_add:
        case alu_adc: {
            const unsigned carry = (op == alu_adc && flag(flag_c)) ? 1U : 0U;
            const unsigned wide = static_cast<unsigned>(lhs) + rhs + carry;
            const auto result = static_cast<std::uint8_t>(wide);
            assign_flag(flag_c, wide > 0xFFU);
            assign_flag(flag_a, ((lhs ^ rhs ^ wide) & 0x10U) != 0U);
            assign_flag(flag_o, ((lhs ^ result) & (rhs ^ result) & 0x80U) != 0U);
            set_szp8(result);
            return result;
        }
        case alu_sub:
        case alu_sbb:
        case alu_cmp: {
            const unsigned borrow = (op == alu_sbb && flag(flag_c)) ? 1U : 0U;
            const unsigned wide = static_cast<unsigned>(lhs) - rhs - borrow;
            const auto result = static_cast<std::uint8_t>(wide);
            assign_flag(flag_c, static_cast<unsigned>(rhs) + borrow > lhs);
            assign_flag(flag_a, ((lhs ^ rhs ^ wide) & 0x10U) != 0U);
            assign_flag(flag_o, ((lhs ^ rhs) & (lhs ^ result) & 0x80U) != 0U);
            set_szp8(result);
            return op == alu_cmp ? lhs : result;
        }
        default: { // OR / AND / XOR
            std::uint8_t result{};
            if (op == alu_or) {
                result = static_cast<std::uint8_t>(lhs | rhs);
            } else if (op == alu_and) {
                result = static_cast<std::uint8_t>(lhs & rhs);
            } else {
                result = static_cast<std::uint8_t>(lhs ^ rhs);
            }
            assign_flag(flag_c, false);
            assign_flag(flag_o, false);
            assign_flag(flag_a, false);
            set_szp8(result);
            return result;
        }
        }
    }

    std::uint16_t v30::alu16(int op, std::uint16_t lhs, std::uint16_t rhs) noexcept {
        switch (op) {
        case alu_add:
        case alu_adc: {
            const unsigned carry = (op == alu_adc && flag(flag_c)) ? 1U : 0U;
            const unsigned wide = static_cast<unsigned>(lhs) + rhs + carry;
            const auto result = static_cast<std::uint16_t>(wide);
            assign_flag(flag_c, wide > 0xFFFFU);
            assign_flag(flag_a, ((lhs ^ rhs ^ wide) & 0x10U) != 0U);
            assign_flag(flag_o, ((lhs ^ result) & (rhs ^ result) & 0x8000U) != 0U);
            set_szp16(result);
            return result;
        }
        case alu_sub:
        case alu_sbb:
        case alu_cmp: {
            const unsigned borrow = (op == alu_sbb && flag(flag_c)) ? 1U : 0U;
            const unsigned wide = static_cast<unsigned>(lhs) - rhs - borrow;
            const auto result = static_cast<std::uint16_t>(wide);
            assign_flag(flag_c, static_cast<unsigned>(rhs) + borrow > lhs);
            assign_flag(flag_a, ((lhs ^ rhs ^ wide) & 0x10U) != 0U);
            assign_flag(flag_o, ((lhs ^ rhs) & (lhs ^ result) & 0x8000U) != 0U);
            set_szp16(result);
            return op == alu_cmp ? lhs : result;
        }
        default: { // OR / AND / XOR
            std::uint16_t result{};
            if (op == alu_or) {
                result = static_cast<std::uint16_t>(lhs | rhs);
            } else if (op == alu_and) {
                result = static_cast<std::uint16_t>(lhs & rhs);
            } else {
                result = static_cast<std::uint16_t>(lhs ^ rhs);
            }
            assign_flag(flag_c, false);
            assign_flag(flag_o, false);
            assign_flag(flag_a, false);
            set_szp16(result);
            return result;
        }
        }
    }

    std::uint8_t v30::inc8(std::uint8_t value) noexcept {
        const bool carry = flag(flag_c); // INC/DEC preserve CF
        const std::uint8_t result = alu8(alu_add, value, 1U);
        assign_flag(flag_c, carry);
        return result;
    }

    std::uint8_t v30::dec8(std::uint8_t value) noexcept {
        const bool carry = flag(flag_c);
        const std::uint8_t result = alu8(alu_sub, value, 1U);
        assign_flag(flag_c, carry);
        return result;
    }

    std::uint16_t v30::inc16(std::uint16_t value) noexcept {
        const bool carry = flag(flag_c);
        const std::uint16_t result = alu16(alu_add, value, 1U);
        assign_flag(flag_c, carry);
        return result;
    }

    std::uint16_t v30::dec16(std::uint16_t value) noexcept {
        const bool carry = flag(flag_c);
        const std::uint16_t result = alu16(alu_sub, value, 1U);
        assign_flag(flag_c, carry);
        return result;
    }

    std::uint8_t v30::shift_rotate8(int op, std::uint8_t value, unsigned count) noexcept {
        count &= 0x1FU; // the V-series masks shift counts to 5 bits
        if (count == 0U) {
            return value;
        }
        std::uint8_t v = value;
        bool carry = flag(flag_c);
        for (unsigned i = 0; i < count; ++i) {
            switch (op) {
            case 0: // ROL
                carry = (v & 0x80U) != 0U;
                v = static_cast<std::uint8_t>((v << 1U) | (carry ? 1U : 0U));
                break;
            case 1: // ROR
                carry = (v & 0x01U) != 0U;
                v = static_cast<std::uint8_t>((v >> 1U) | (carry ? 0x80U : 0U));
                break;
            case 2: { // RCL
                const bool new_carry = (v & 0x80U) != 0U;
                v = static_cast<std::uint8_t>((v << 1U) | (carry ? 1U : 0U));
                carry = new_carry;
                break;
            }
            case 3: { // RCR
                const bool new_carry = (v & 0x01U) != 0U;
                v = static_cast<std::uint8_t>((v >> 1U) | (carry ? 0x80U : 0U));
                carry = new_carry;
                break;
            }
            case 4:
            case 6: // SHL (6 is the undocumented SAL alias)
                carry = (v & 0x80U) != 0U;
                v = static_cast<std::uint8_t>(v << 1U);
                break;
            case 5: // SHR
                carry = (v & 0x01U) != 0U;
                v = static_cast<std::uint8_t>(v >> 1U);
                break;
            default: // 7: SAR
                carry = (v & 0x01U) != 0U;
                v = static_cast<std::uint8_t>((v >> 1U) | (v & 0x80U));
                break;
            }
        }
        assign_flag(flag_c, carry);
        // OF reflects the documented single-shift definition (undefined for
        // multi-bit counts; we leave the single-shift value in place).
        if (op == 1 || op == 3) { // right rotates: top two result bits differ
            assign_flag(flag_o, (((v >> 7U) ^ (v >> 6U)) & 1U) != 0U);
        } else if (op == 5) { // SHR: MSB of the original value
            assign_flag(flag_o, (value & 0x80U) != 0U);
        } else if (op == 7) { // SAR
            assign_flag(flag_o, false);
        } else { // left shifts/rotates: MSB(result) xor CF
            assign_flag(flag_o, (((v & 0x80U) != 0U) != carry));
        }
        if (op >= 4) { // shifts update SZP; rotates do not
            set_szp8(v);
        }
        return v;
    }

    std::uint16_t v30::shift_rotate16(int op, std::uint16_t value, unsigned count) noexcept {
        count &= 0x1FU;
        if (count == 0U) {
            return value;
        }
        std::uint16_t v = value;
        bool carry = flag(flag_c);
        for (unsigned i = 0; i < count; ++i) {
            switch (op) {
            case 0: // ROL
                carry = (v & 0x8000U) != 0U;
                v = static_cast<std::uint16_t>((v << 1U) | (carry ? 1U : 0U));
                break;
            case 1: // ROR
                carry = (v & 0x0001U) != 0U;
                v = static_cast<std::uint16_t>((v >> 1U) | (carry ? 0x8000U : 0U));
                break;
            case 2: { // RCL
                const bool new_carry = (v & 0x8000U) != 0U;
                v = static_cast<std::uint16_t>((v << 1U) | (carry ? 1U : 0U));
                carry = new_carry;
                break;
            }
            case 3: { // RCR
                const bool new_carry = (v & 0x0001U) != 0U;
                v = static_cast<std::uint16_t>((v >> 1U) | (carry ? 0x8000U : 0U));
                carry = new_carry;
                break;
            }
            case 4:
            case 6: // SHL
                carry = (v & 0x8000U) != 0U;
                v = static_cast<std::uint16_t>(v << 1U);
                break;
            case 5: // SHR
                carry = (v & 0x0001U) != 0U;
                v = static_cast<std::uint16_t>(v >> 1U);
                break;
            default: // 7: SAR
                carry = (v & 0x0001U) != 0U;
                v = static_cast<std::uint16_t>((v >> 1U) | (v & 0x8000U));
                break;
            }
        }
        assign_flag(flag_c, carry);
        if (op == 1 || op == 3) {
            assign_flag(flag_o, (((v >> 15U) ^ (v >> 14U)) & 1U) != 0U);
        } else if (op == 5) {
            assign_flag(flag_o, (value & 0x8000U) != 0U);
        } else if (op == 7) {
            assign_flag(flag_o, false);
        } else {
            assign_flag(flag_o, (((v & 0x8000U) != 0U) != carry));
        }
        if (op >= 4) {
            set_szp16(v);
        }
        return v;
    }

    bool v30::test_cc(int cc) const noexcept {
        bool result{};
        switch ((cc >> 1) & 7) {
        case 0:
            result = flag(flag_o);
            break;
        case 1:
            result = flag(flag_c);
            break;
        case 2:
            result = flag(flag_z);
            break;
        case 3:
            result = flag(flag_c) || flag(flag_z);
            break;
        case 4:
            result = flag(flag_s);
            break;
        case 5:
            result = flag(flag_p);
            break;
        case 6:
            result = flag(flag_s) != flag(flag_o);
            break;
        default:
            result = flag(flag_z) || (flag(flag_s) != flag(flag_o));
            break;
        }
        return ((cc & 1) != 0) ? !result : result;
    }

    // ---- interrupts -----------------------------------------------------------

    void v30::interrupt(std::uint8_t vector) noexcept {
        push16(flags_word());
        assign_flag(flag_i, false);
        assign_flag(flag_t, false);
        push16(cs_);
        push16(ip_);
        const auto table = static_cast<std::uint16_t>(vector * 4U);
        ip_ = rw(0U, table);
        cs_ = rw(0U, static_cast<std::uint16_t>(table + 2U));
        take_cycles(32);
    }

    // ---- execution --------------------------------------------------------------

    int v30::step_instruction() {
        step_cycles_ = 0;

        if (nmi_pending_) {
            nmi_pending_ = false;
            halted_ = false;
            interrupt(2U);
            elapsed_ += static_cast<std::uint64_t>(step_cycles_);
            return step_cycles_;
        }
        if (irq_line_ && flag(flag_i) && !interrupt_inhibit_) {
            halted_ = false;
            interrupt(irq_ack_ ? irq_ack_() : 0U);
            elapsed_ += static_cast<std::uint64_t>(step_cycles_);
            return step_cycles_;
        }
        interrupt_inhibit_ = false;

        if (halted_) {
            take_cycles(1);
            elapsed_ += static_cast<std::uint64_t>(step_cycles_);
            return step_cycles_;
        }

        if (trace_callback_) {
            trace_callback_(phys(cs_, ip_));
        }

        // Prefix loop: collect segment-override / REP / LOCK prefixes, then
        // execute the instruction they decorate.
        seg_override_ = false;
        seg_override_value_ = 0U;
        rep_prefix_ = 0;
        for (;;) {
            const std::uint8_t opcode = fetch8();
            bool is_prefix = true;
            switch (opcode) {
            case 0x26U:
                seg_override_ = true;
                seg_override_value_ = es_;
                break;
            case 0x2EU:
                seg_override_ = true;
                seg_override_value_ = cs_;
                break;
            case 0x36U:
                seg_override_ = true;
                seg_override_value_ = ss_;
                break;
            case 0x3EU:
                seg_override_ = true;
                seg_override_value_ = ds_;
                break;
            case 0x64U: // REPNC (V-series)
                rep_prefix_ = 4;
                break;
            case 0x65U: // REPC (V-series)
                rep_prefix_ = 3;
                break;
            case 0xF0U: // LOCK: bus arbitration only; no architectural effect here
                break;
            case 0xF2U: // REPNE
                rep_prefix_ = 2;
                break;
            case 0xF3U: // REP / REPE
                rep_prefix_ = 1;
                break;
            default:
                is_prefix = false;
                break;
            }
            if (!is_prefix) {
                exec_one(opcode);
                break;
            }
            take_cycles(2);
        }

        elapsed_ += static_cast<std::uint64_t>(step_cycles_);
        return step_cycles_;
    }

    void v30::exec_one(std::uint8_t opcode) {
        // ALU block 0x00-0x3F: the 8 operations x 6 addressing forms, with
        // the segment/BCD opcodes interleaved (prefixes already consumed).
        if (opcode < 0x40U && (opcode & 7U) < 6U) {
            const int op = (opcode >> 3U) & 7;
            switch (opcode & 7U) {
            case 0: { // op r/m8, r8
                fetch_modrm();
                const std::uint8_t result = alu8(op, read_rm8(), get_reg8(modrm_reg_));
                if (op != alu_cmp) {
                    write_rm8(result);
                }
                take_cycles(3 + ea_cycles());
                return;
            }
            case 1: { // op r/m16, r16
                fetch_modrm();
                const std::uint16_t result = alu16(op, read_rm16(), get_reg16(modrm_reg_));
                if (op != alu_cmp) {
                    write_rm16(result);
                }
                take_cycles(3 + ea_cycles());
                return;
            }
            case 2: { // op r8, r/m8
                fetch_modrm();
                const std::uint8_t result = alu8(op, get_reg8(modrm_reg_), read_rm8());
                if (op != alu_cmp) {
                    set_reg8(modrm_reg_, result);
                }
                take_cycles(3 + ea_cycles());
                return;
            }
            case 3: { // op r16, r/m16
                fetch_modrm();
                const std::uint16_t result = alu16(op, get_reg16(modrm_reg_), read_rm16());
                if (op != alu_cmp) {
                    set_reg16(modrm_reg_, result);
                }
                take_cycles(3 + ea_cycles());
                return;
            }
            case 4: { // op AL, imm8
                const std::uint8_t result = alu8(op, get_reg8(0), fetch8());
                if (op != alu_cmp) {
                    set_reg8(0, result);
                }
                take_cycles(4);
                return;
            }
            default: { // op AX, imm16
                const std::uint16_t result = alu16(op, ax_, fetch16());
                if (op != alu_cmp) {
                    ax_ = result;
                }
                take_cycles(4);
                return;
            }
            }
        }

        switch (opcode) {
        // -- segment register push/pop + BCD adjust (the 0x07/0x0F column) --
        case 0x06U:
        case 0x0EU:
        case 0x16U:
        case 0x1EU: // PUSH ES/CS/SS/DS
            push16(get_sreg((opcode >> 3U) & 3));
            take_cycles(10);
            break;
        case 0x07U:
        case 0x17U:
        case 0x1FU: // POP ES/SS/DS (POP CS is the 0F escape below)
            set_sreg((opcode >> 3U) & 3, pop16());
            take_cycles(8);
            break;
        case 0x0FU:
            // V30 extension group (TEST1/SET1/CLR1/NOT1, BCD strings, ...).
            // Deferred increment A2: consume the extension byte, no-op.
            static_cast<void>(fetch8());
            take_cycles(2);
            break;
        case 0x27U: { // DAA
            const std::uint8_t old_al = get_reg8(0);
            const bool old_cf = flag(flag_c);
            std::uint8_t al = old_al;
            if ((al & 0x0FU) > 9U || flag(flag_a)) {
                al = static_cast<std::uint8_t>(al + 6U);
                assign_flag(flag_a, true);
            }
            if (old_al > 0x99U || old_cf) {
                al = static_cast<std::uint8_t>(al + 0x60U);
                assign_flag(flag_c, true);
            } else {
                assign_flag(flag_c, false);
            }
            set_reg8(0, al);
            set_szp8(al);
            take_cycles(4);
            break;
        }
        case 0x2FU: { // DAS
            const std::uint8_t old_al = get_reg8(0);
            const bool old_cf = flag(flag_c);
            std::uint8_t al = old_al;
            if ((al & 0x0FU) > 9U || flag(flag_a)) {
                al = static_cast<std::uint8_t>(al - 6U);
                assign_flag(flag_a, true);
            }
            if (old_al > 0x99U || old_cf) {
                al = static_cast<std::uint8_t>(al - 0x60U);
                assign_flag(flag_c, true);
            } else {
                assign_flag(flag_c, false);
            }
            set_reg8(0, al);
            set_szp8(al);
            take_cycles(4);
            break;
        }
        case 0x37U: { // AAA
            if ((get_reg8(0) & 0x0FU) > 9U || flag(flag_a)) {
                set_reg8(0, static_cast<std::uint8_t>(get_reg8(0) + 6U));
                set_reg8(4, static_cast<std::uint8_t>(get_reg8(4) + 1U));
                assign_flag(flag_a, true);
                assign_flag(flag_c, true);
            } else {
                assign_flag(flag_a, false);
                assign_flag(flag_c, false);
            }
            set_reg8(0, static_cast<std::uint8_t>(get_reg8(0) & 0x0FU));
            take_cycles(4);
            break;
        }
        case 0x3FU: { // AAS
            if ((get_reg8(0) & 0x0FU) > 9U || flag(flag_a)) {
                set_reg8(0, static_cast<std::uint8_t>(get_reg8(0) - 6U));
                set_reg8(4, static_cast<std::uint8_t>(get_reg8(4) - 1U));
                assign_flag(flag_a, true);
                assign_flag(flag_c, true);
            } else {
                assign_flag(flag_a, false);
                assign_flag(flag_c, false);
            }
            set_reg8(0, static_cast<std::uint8_t>(get_reg8(0) & 0x0FU));
            take_cycles(4);
            break;
        }

        // -- INC/DEC r16, PUSH/POP r16 --
        case 0x40U:
        case 0x41U:
        case 0x42U:
        case 0x43U:
        case 0x44U:
        case 0x45U:
        case 0x46U:
        case 0x47U:
            set_reg16(opcode & 7, inc16(get_reg16(opcode & 7)));
            take_cycles(2);
            break;
        case 0x48U:
        case 0x49U:
        case 0x4AU:
        case 0x4BU:
        case 0x4CU:
        case 0x4DU:
        case 0x4EU:
        case 0x4FU:
            set_reg16(opcode & 7, dec16(get_reg16(opcode & 7)));
            take_cycles(2);
            break;
        case 0x50U:
        case 0x51U:
        case 0x52U:
        case 0x53U:
        case 0x55U:
        case 0x56U:
        case 0x57U:
            push16(get_reg16(opcode & 7));
            take_cycles(10);
            break;
        case 0x54U: // PUSH SP pushes the decremented value (8086 semantics)
            sp_ = static_cast<std::uint16_t>(sp_ - 2U);
            ww(ss_, sp_, sp_);
            take_cycles(10);
            break;
        case 0x58U:
        case 0x59U:
        case 0x5AU:
        case 0x5BU:
        case 0x5CU:
        case 0x5DU:
        case 0x5EU:
        case 0x5FU:
            set_reg16(opcode & 7, pop16());
            take_cycles(8);
            break;

        // -- 186/V-series block 0x60-0x6F --
        case 0x60U: { // PUSHA
            const std::uint16_t original_sp = sp_;
            push16(ax_);
            push16(cx_);
            push16(dx_);
            push16(bx_);
            push16(original_sp);
            push16(bp_);
            push16(si_);
            push16(di_);
            take_cycles(36);
            break;
        }
        case 0x61U: // POPA
            di_ = pop16();
            si_ = pop16();
            bp_ = pop16();
            static_cast<void>(pop16()); // SP slot discarded
            bx_ = pop16();
            dx_ = pop16();
            cx_ = pop16();
            ax_ = pop16();
            take_cycles(34);
            break;
        case 0x62U: { // BOUND (NEC: CHKIND)
            fetch_modrm();
            const auto lower = static_cast<std::int16_t>(rw(rm_segment_, rm_offset_));
            const auto upper = static_cast<std::int16_t>(
                rw(rm_segment_, static_cast<std::uint16_t>(rm_offset_ + 2U)));
            const auto index = static_cast<std::int16_t>(get_reg16(modrm_reg_));
            take_cycles(13 + ea_cycles());
            if (index < lower || index > upper) {
                interrupt(5U);
            }
            break;
        }
        case 0x68U: // PUSH imm16
            push16(fetch16());
            take_cycles(10);
            break;
        case 0x6AU: { // PUSH imm8 (sign-extended)
            const auto imm = static_cast<std::int8_t>(fetch8());
            push16(static_cast<std::uint16_t>(imm));
            take_cycles(10);
            break;
        }
        case 0x69U: { // IMUL r16, r/m16, imm16
            fetch_modrm();
            const auto lhs = static_cast<std::int16_t>(read_rm16());
            const auto rhs = static_cast<std::int16_t>(fetch16());
            const std::int32_t product = static_cast<std::int32_t>(lhs) * rhs;
            set_reg16(modrm_reg_, static_cast<std::uint16_t>(product));
            const bool overflow = product != static_cast<std::int16_t>(product);
            assign_flag(flag_c, overflow);
            assign_flag(flag_o, overflow);
            take_cycles(30 + ea_cycles());
            break;
        }
        case 0x6BU: { // IMUL r16, r/m16, imm8
            fetch_modrm();
            const auto lhs = static_cast<std::int16_t>(read_rm16());
            const auto rhs = static_cast<std::int16_t>(static_cast<std::int8_t>(fetch8()));
            const std::int32_t product = static_cast<std::int32_t>(lhs) * rhs;
            set_reg16(modrm_reg_, static_cast<std::uint16_t>(product));
            const bool overflow = product != static_cast<std::int16_t>(product);
            assign_flag(flag_c, overflow);
            assign_flag(flag_o, overflow);
            take_cycles(22 + ea_cycles());
            break;
        }
        case 0x63U: // undefined on the V30: no-op (bring-up convention)
        case 0x66U: // FPO2 (no coprocessor fitted)
        case 0x67U:
            take_cycles(2);
            break;
        case 0x6CU: // INS/OUTS are REP-able string instructions
        case 0x6DU:
        case 0x6EU:
        case 0x6FU:
            exec_string(opcode);
            break;

        // -- Jcc rel8 --
        case 0x70U:
        case 0x71U:
        case 0x72U:
        case 0x73U:
        case 0x74U:
        case 0x75U:
        case 0x76U:
        case 0x77U:
        case 0x78U:
        case 0x79U:
        case 0x7AU:
        case 0x7BU:
        case 0x7CU:
        case 0x7DU:
        case 0x7EU:
        case 0x7FU: {
            const auto disp = static_cast<std::int8_t>(fetch8());
            if (test_cc(opcode & 0x0F)) {
                ip_ = static_cast<std::uint16_t>(ip_ + static_cast<std::uint16_t>(disp));
                take_cycles(16);
            } else {
                take_cycles(4);
            }
            break;
        }

        // -- immediate-group / test / xchg / mov block --
        case 0x80U:
        case 0x81U:
        case 0x82U:
        case 0x83U:
            exec_group_80_83(opcode);
            break;
        case 0x84U: { // TEST r/m8, r8
            fetch_modrm();
            static_cast<void>(alu8(alu_and, read_rm8(), get_reg8(modrm_reg_)));
            take_cycles(3 + ea_cycles());
            break;
        }
        case 0x85U: { // TEST r/m16, r16
            fetch_modrm();
            static_cast<void>(alu16(alu_and, read_rm16(), get_reg16(modrm_reg_)));
            take_cycles(3 + ea_cycles());
            break;
        }
        case 0x86U: { // XCHG r/m8, r8
            fetch_modrm();
            const std::uint8_t temp = read_rm8();
            write_rm8(get_reg8(modrm_reg_));
            set_reg8(modrm_reg_, temp);
            take_cycles(4 + ea_cycles());
            break;
        }
        case 0x87U: { // XCHG r/m16, r16
            fetch_modrm();
            const std::uint16_t temp = read_rm16();
            write_rm16(get_reg16(modrm_reg_));
            set_reg16(modrm_reg_, temp);
            take_cycles(4 + ea_cycles());
            break;
        }
        case 0x88U: // MOV r/m8, r8
            fetch_modrm();
            write_rm8(get_reg8(modrm_reg_));
            take_cycles(2 + ea_cycles());
            break;
        case 0x89U: // MOV r/m16, r16
            fetch_modrm();
            write_rm16(get_reg16(modrm_reg_));
            take_cycles(2 + ea_cycles());
            break;
        case 0x8AU: // MOV r8, r/m8
            fetch_modrm();
            set_reg8(modrm_reg_, read_rm8());
            take_cycles(2 + ea_cycles());
            break;
        case 0x8BU: // MOV r16, r/m16
            fetch_modrm();
            set_reg16(modrm_reg_, read_rm16());
            take_cycles(2 + ea_cycles());
            break;
        case 0x8CU: // MOV r/m16, sreg
            fetch_modrm();
            write_rm16(get_sreg(modrm_reg_));
            take_cycles(2 + ea_cycles());
            break;
        case 0x8DU: // LEA r16, m
            fetch_modrm();
            set_reg16(modrm_reg_, rm_offset_);
            take_cycles(2 + ea_cycles());
            break;
        case 0x8EU: // MOV sreg, r/m16
            fetch_modrm();
            set_sreg(modrm_reg_, read_rm16());
            take_cycles(2 + ea_cycles());
            break;
        case 0x8FU: // POP r/m16
            fetch_modrm();
            write_rm16(pop16());
            take_cycles(8 + ea_cycles());
            break;

        // -- XCHG AX / conversions / far call / flags transfers --
        case 0x90U: // NOP (XCHG AX, AX)
            take_cycles(2);
            break;
        case 0x91U:
        case 0x92U:
        case 0x93U:
        case 0x94U:
        case 0x95U:
        case 0x96U:
        case 0x97U: {
            const std::uint16_t temp = get_reg16(opcode & 7);
            set_reg16(opcode & 7, ax_);
            ax_ = temp;
            take_cycles(3);
            break;
        }
        case 0x98U: // CBW
            ax_ = static_cast<std::uint16_t>(
                static_cast<std::int16_t>(static_cast<std::int8_t>(get_reg8(0))));
            take_cycles(2);
            break;
        case 0x99U: // CWD
            dx_ = (ax_ & 0x8000U) != 0U ? 0xFFFFU : 0x0000U;
            take_cycles(4);
            break;
        case 0x9AU: { // CALL far imm
            const std::uint16_t offset = fetch16();
            const std::uint16_t segment = fetch16();
            push16(cs_);
            push16(ip_);
            cs_ = segment;
            ip_ = offset;
            take_cycles(28);
            break;
        }
        case 0x9BU: // WAIT: no coprocessor, completes immediately
            take_cycles(2);
            break;
        case 0x9CU: // PUSHF
            push16(flags_word());
            take_cycles(10);
            break;
        case 0x9DU: // POPF
            set_flags_word(pop16());
            take_cycles(8);
            break;
        case 0x9EU: // SAHF
            flags_ = static_cast<std::uint16_t>((flags_ & 0xFF00U) | (get_reg8(4) & 0xD5U));
            take_cycles(4);
            break;
        case 0x9FU: // LAHF
            set_reg8(4, static_cast<std::uint8_t>((flags_ & 0xD5U) | 0x02U));
            take_cycles(2);
            break;

        // -- accumulator <-> direct address, string ops, TEST imm --
        case 0xA0U: { // MOV AL, moffs
            const std::uint16_t offset = fetch16();
            set_reg8(0, rb(data_segment(ds_), offset));
            take_cycles(8);
            break;
        }
        case 0xA1U: { // MOV AX, moffs
            const std::uint16_t offset = fetch16();
            ax_ = rw(data_segment(ds_), offset);
            take_cycles(8);
            break;
        }
        case 0xA2U: { // MOV moffs, AL
            const std::uint16_t offset = fetch16();
            wb(data_segment(ds_), offset, get_reg8(0));
            take_cycles(8);
            break;
        }
        case 0xA3U: { // MOV moffs, AX
            const std::uint16_t offset = fetch16();
            ww(data_segment(ds_), offset, ax_);
            take_cycles(8);
            break;
        }
        case 0xA4U:
        case 0xA5U:
        case 0xA6U:
        case 0xA7U:
        case 0xAAU:
        case 0xABU:
        case 0xACU:
        case 0xADU:
        case 0xAEU:
        case 0xAFU:
            exec_string(opcode);
            break;
        case 0xA8U: // TEST AL, imm8
            static_cast<void>(alu8(alu_and, get_reg8(0), fetch8()));
            take_cycles(4);
            break;
        case 0xA9U: // TEST AX, imm16
            static_cast<void>(alu16(alu_and, ax_, fetch16()));
            take_cycles(4);
            break;

        // -- MOV r, imm --
        case 0xB0U:
        case 0xB1U:
        case 0xB2U:
        case 0xB3U:
        case 0xB4U:
        case 0xB5U:
        case 0xB6U:
        case 0xB7U:
            set_reg8(opcode & 7, fetch8());
            take_cycles(4);
            break;
        case 0xB8U:
        case 0xB9U:
        case 0xBAU:
        case 0xBBU:
        case 0xBCU:
        case 0xBDU:
        case 0xBEU:
        case 0xBFU:
            set_reg16(opcode & 7, fetch16());
            take_cycles(4);
            break;

        // -- shift-by-imm, returns, LES/LDS, MOV imm, ENTER/LEAVE, INT --
        case 0xC0U: { // shift/rotate r/m8, imm8 (186/V-series)
            fetch_modrm();
            const std::uint8_t value = read_rm8();
            const std::uint8_t count = fetch8();
            write_rm8(shift_rotate8(modrm_reg_, value, count));
            take_cycles(7 + ea_cycles());
            break;
        }
        case 0xC1U: { // shift/rotate r/m16, imm8
            fetch_modrm();
            const std::uint16_t value = read_rm16();
            const std::uint8_t count = fetch8();
            write_rm16(shift_rotate16(modrm_reg_, value, count));
            take_cycles(7 + ea_cycles());
            break;
        }
        case 0xC2U: { // RET imm16
            const std::uint16_t adjust = fetch16();
            ip_ = pop16();
            sp_ = static_cast<std::uint16_t>(sp_ + adjust);
            take_cycles(16);
            break;
        }
        case 0xC3U: // RET
            ip_ = pop16();
            take_cycles(16);
            break;
        case 0xC4U: { // LES r16, m
            fetch_modrm();
            set_reg16(modrm_reg_, rw(rm_segment_, rm_offset_));
            es_ = rw(rm_segment_, static_cast<std::uint16_t>(rm_offset_ + 2U));
            take_cycles(12 + ea_cycles());
            break;
        }
        case 0xC5U: { // LDS r16, m
            fetch_modrm();
            set_reg16(modrm_reg_, rw(rm_segment_, rm_offset_));
            ds_ = rw(rm_segment_, static_cast<std::uint16_t>(rm_offset_ + 2U));
            take_cycles(12 + ea_cycles());
            break;
        }
        case 0xC6U: // MOV r/m8, imm8
            fetch_modrm();
            write_rm8(fetch8());
            take_cycles(4 + ea_cycles());
            break;
        case 0xC7U: // MOV r/m16, imm16
            fetch_modrm();
            write_rm16(fetch16());
            take_cycles(4 + ea_cycles());
            break;
        case 0xC8U: { // ENTER (NEC: PREPARE)
            const std::uint16_t locals = fetch16();
            const unsigned level = fetch8() & 0x1FU;
            push16(bp_);
            const std::uint16_t frame = sp_;
            if (level > 0U) {
                for (unsigned i = 1U; i < level; ++i) {
                    bp_ = static_cast<std::uint16_t>(bp_ - 2U);
                    push16(rw(ss_, bp_));
                }
                push16(frame);
            }
            bp_ = frame;
            sp_ = static_cast<std::uint16_t>(sp_ - locals);
            take_cycles(16 + static_cast<int>(level) * 4);
            break;
        }
        case 0xC9U: // LEAVE (NEC: DISPOSE)
            sp_ = bp_;
            bp_ = pop16();
            take_cycles(8);
            break;
        case 0xCAU: { // RETF imm16
            const std::uint16_t adjust = fetch16();
            ip_ = pop16();
            cs_ = pop16();
            sp_ = static_cast<std::uint16_t>(sp_ + adjust);
            take_cycles(24);
            break;
        }
        case 0xCBU: // RETF
            ip_ = pop16();
            cs_ = pop16();
            take_cycles(24);
            break;
        case 0xCCU: // INT3
            interrupt(3U);
            break;
        case 0xCDU: // INT imm8
            interrupt(fetch8());
            break;
        case 0xCEU: // INTO
            if (flag(flag_o)) {
                interrupt(4U);
            } else {
                take_cycles(4);
            }
            break;
        case 0xCFU: // IRET
            ip_ = pop16();
            cs_ = pop16();
            set_flags_word(pop16());
            take_cycles(28);
            break;

        // -- shift group, BCD multiply/divide, XLAT, ESC --
        case 0xD0U: // shift/rotate r/m8, 1
            fetch_modrm();
            write_rm8(shift_rotate8(modrm_reg_, read_rm8(), 1U));
            take_cycles(2 + ea_cycles());
            break;
        case 0xD1U: // shift/rotate r/m16, 1
            fetch_modrm();
            write_rm16(shift_rotate16(modrm_reg_, read_rm16(), 1U));
            take_cycles(2 + ea_cycles());
            break;
        case 0xD2U: // shift/rotate r/m8, CL
            fetch_modrm();
            write_rm8(shift_rotate8(modrm_reg_, read_rm8(), get_reg8(1)));
            take_cycles(7 + ea_cycles());
            break;
        case 0xD3U: // shift/rotate r/m16, CL
            fetch_modrm();
            write_rm16(shift_rotate16(modrm_reg_, read_rm16(), get_reg8(1)));
            take_cycles(7 + ea_cycles());
            break;
        case 0xD4U: { // AAM imm8 (NEC: CVTBD)
            const std::uint8_t divisor = fetch8();
            take_cycles(16);
            if (divisor == 0U) {
                interrupt(0U);
                break;
            }
            const std::uint8_t al = get_reg8(0);
            set_reg8(4, static_cast<std::uint8_t>(al / divisor));
            set_reg8(0, static_cast<std::uint8_t>(al % divisor));
            set_szp8(get_reg8(0));
            break;
        }
        case 0xD5U: { // AAD imm8 (NEC: CVTDB)
            const std::uint8_t multiplier = fetch8();
            const auto al = static_cast<std::uint8_t>(get_reg8(0) + get_reg8(4) * multiplier);
            set_reg8(0, al);
            set_reg8(4, 0U);
            set_szp8(al);
            take_cycles(6);
            break;
        }
        case 0xD6U: // undefined: no-op
            take_cycles(2);
            break;
        case 0xD7U: // XLAT (NEC: TRANS)
            set_reg8(0, rb(data_segment(ds_), static_cast<std::uint16_t>(bx_ + get_reg8(0))));
            take_cycles(4);
            break;
        case 0xD8U:
        case 0xD9U:
        case 0xDAU:
        case 0xDBU:
        case 0xDCU:
        case 0xDDU:
        case 0xDEU:
        case 0xDFU: // ESC: no coprocessor on the M72; consume the EA, no-op
            fetch_modrm();
            take_cycles(2 + ea_cycles());
            break;

        // -- loops, IN/OUT, jumps --
        case 0xE0U: { // LOOPNE/LOOPNZ
            const auto disp = static_cast<std::int8_t>(fetch8());
            cx_ = static_cast<std::uint16_t>(cx_ - 1U);
            if (cx_ != 0U && !flag(flag_z)) {
                ip_ = static_cast<std::uint16_t>(ip_ + static_cast<std::uint16_t>(disp));
                take_cycles(19);
            } else {
                take_cycles(5);
            }
            break;
        }
        case 0xE1U: { // LOOPE/LOOPZ
            const auto disp = static_cast<std::int8_t>(fetch8());
            cx_ = static_cast<std::uint16_t>(cx_ - 1U);
            if (cx_ != 0U && flag(flag_z)) {
                ip_ = static_cast<std::uint16_t>(ip_ + static_cast<std::uint16_t>(disp));
                take_cycles(18);
            } else {
                take_cycles(6);
            }
            break;
        }
        case 0xE2U: { // LOOP
            const auto disp = static_cast<std::int8_t>(fetch8());
            cx_ = static_cast<std::uint16_t>(cx_ - 1U);
            if (cx_ != 0U) {
                ip_ = static_cast<std::uint16_t>(ip_ + static_cast<std::uint16_t>(disp));
                take_cycles(17);
            } else {
                take_cycles(5);
            }
            break;
        }
        case 0xE3U: { // JCXZ
            const auto disp = static_cast<std::int8_t>(fetch8());
            if (cx_ == 0U) {
                ip_ = static_cast<std::uint16_t>(ip_ + static_cast<std::uint16_t>(disp));
                take_cycles(18);
            } else {
                take_cycles(6);
            }
            break;
        }
        case 0xE4U: // IN AL, imm8
            set_reg8(0, port_in8(fetch8()));
            take_cycles(10);
            break;
        case 0xE5U: // IN AX, imm8
            ax_ = port_in16(fetch8());
            take_cycles(10);
            break;
        case 0xE6U: // OUT imm8, AL
            port_out8(fetch8(), get_reg8(0));
            take_cycles(10);
            break;
        case 0xE7U: // OUT imm8, AX
            port_out16(fetch8(), ax_);
            take_cycles(10);
            break;
        case 0xE8U: { // CALL rel16
            const std::uint16_t disp = fetch16();
            push16(ip_);
            ip_ = static_cast<std::uint16_t>(ip_ + disp);
            take_cycles(19);
            break;
        }
        case 0xE9U: { // JMP rel16
            const std::uint16_t disp = fetch16();
            ip_ = static_cast<std::uint16_t>(ip_ + disp);
            take_cycles(15);
            break;
        }
        case 0xEAU: { // JMP far imm
            const std::uint16_t offset = fetch16();
            cs_ = fetch16();
            ip_ = offset;
            take_cycles(15);
            break;
        }
        case 0xEBU: { // JMP rel8
            const auto disp = static_cast<std::int8_t>(fetch8());
            ip_ = static_cast<std::uint16_t>(ip_ + static_cast<std::uint16_t>(disp));
            take_cycles(15);
            break;
        }
        case 0xECU: // IN AL, DX
            set_reg8(0, port_in8(dx_));
            take_cycles(8);
            break;
        case 0xEDU: // IN AX, DX
            ax_ = port_in16(dx_);
            take_cycles(8);
            break;
        case 0xEEU: // OUT DX, AL
            port_out8(dx_, get_reg8(0));
            take_cycles(8);
            break;
        case 0xEFU: // OUT DX, AX
            port_out16(dx_, ax_);
            take_cycles(8);
            break;

        // -- flag ops, HLT, groups --
        case 0xF1U: // undefined: no-op
            take_cycles(2);
            break;
        case 0xF4U: // HLT
            halted_ = true;
            take_cycles(2);
            break;
        case 0xF5U: // CMC
            assign_flag(flag_c, !flag(flag_c));
            take_cycles(2);
            break;
        case 0xF6U:
        case 0xF7U:
            exec_group_f6_f7(opcode);
            break;
        case 0xF8U: // CLC
            assign_flag(flag_c, false);
            take_cycles(2);
            break;
        case 0xF9U: // STC
            assign_flag(flag_c, true);
            take_cycles(2);
            break;
        case 0xFAU: // CLI
            assign_flag(flag_i, false);
            take_cycles(2);
            break;
        case 0xFBU: // STI: interrupts recognised after the NEXT instruction
            assign_flag(flag_i, true);
            interrupt_inhibit_ = true;
            take_cycles(2);
            break;
        case 0xFCU: // CLD
            assign_flag(flag_d, false);
            take_cycles(2);
            break;
        case 0xFDU: // STD
            assign_flag(flag_d, true);
            take_cycles(2);
            break;
        case 0xFEU:
            exec_group_fe();
            break;
        case 0xFFU:
            exec_group_ff();
            break;

        default:
            // Remaining undefined encodings execute as no-ops during bring-up
            // (the m68000 / SH-2 convention).
            take_cycles(2);
            break;
        }
    }

    void v30::exec_group_80_83(std::uint8_t opcode) {
        fetch_modrm();
        const int op = modrm_reg_;
        if ((opcode & 1U) == 0U) { // 0x80 / 0x82: r/m8, imm8
            const std::uint8_t lhs = read_rm8();
            const std::uint8_t rhs = fetch8();
            const std::uint8_t result = alu8(op, lhs, rhs);
            if (op != alu_cmp) {
                write_rm8(result);
            }
        } else {
            const std::uint16_t lhs = read_rm16();
            std::uint16_t rhs{};
            if (opcode == 0x83U) { // imm8 sign-extended
                rhs = static_cast<std::uint16_t>(
                    static_cast<std::int16_t>(static_cast<std::int8_t>(fetch8())));
            } else {
                rhs = fetch16();
            }
            const std::uint16_t result = alu16(op, lhs, rhs);
            if (op != alu_cmp) {
                write_rm16(result);
            }
        }
        take_cycles(4 + ea_cycles());
    }

    void v30::exec_group_f6_f7(std::uint8_t opcode) {
        fetch_modrm();
        const bool word = (opcode & 1U) != 0U;
        switch (modrm_reg_) {
        case 0:
        case 1: // TEST r/m, imm
            if (word) {
                static_cast<void>(alu16(alu_and, read_rm16(), fetch16()));
            } else {
                static_cast<void>(alu8(alu_and, read_rm8(), fetch8()));
            }
            take_cycles(4 + ea_cycles());
            break;
        case 2: // NOT
            if (word) {
                write_rm16(static_cast<std::uint16_t>(~read_rm16()));
            } else {
                write_rm8(static_cast<std::uint8_t>(~read_rm8()));
            }
            take_cycles(3 + ea_cycles());
            break;
        case 3: // NEG
            if (word) {
                write_rm16(alu16(alu_sub, 0U, read_rm16()));
            } else {
                write_rm8(alu8(alu_sub, 0U, read_rm8()));
            }
            take_cycles(3 + ea_cycles());
            break;
        case 4: // MUL
            if (word) {
                const std::uint32_t product = static_cast<std::uint32_t>(ax_) * read_rm16();
                ax_ = static_cast<std::uint16_t>(product);
                dx_ = static_cast<std::uint16_t>(product >> 16U);
                const bool high = dx_ != 0U;
                assign_flag(flag_c, high);
                assign_flag(flag_o, high);
                assign_flag(flag_z, product == 0U);
            } else {
                const auto product = static_cast<std::uint16_t>(
                    static_cast<std::uint16_t>(get_reg8(0)) * read_rm8());
                ax_ = product;
                const bool high = (product & 0xFF00U) != 0U;
                assign_flag(flag_c, high);
                assign_flag(flag_o, high);
                assign_flag(flag_z, product == 0U);
            }
            take_cycles(30 + ea_cycles());
            break;
        case 5: // IMUL
            if (word) {
                const std::int32_t product =
                    static_cast<std::int32_t>(static_cast<std::int16_t>(ax_)) *
                    static_cast<std::int16_t>(read_rm16());
                ax_ = static_cast<std::uint16_t>(product);
                dx_ = static_cast<std::uint16_t>(static_cast<std::uint32_t>(product) >> 16U);
                const bool overflow = product != static_cast<std::int16_t>(product);
                assign_flag(flag_c, overflow);
                assign_flag(flag_o, overflow);
                assign_flag(flag_z, product == 0);
            } else {
                const std::int16_t product =
                    static_cast<std::int16_t>(static_cast<std::int8_t>(get_reg8(0))) *
                    static_cast<std::int16_t>(static_cast<std::int8_t>(read_rm8()));
                ax_ = static_cast<std::uint16_t>(product);
                const bool overflow = product != static_cast<std::int8_t>(product);
                assign_flag(flag_c, overflow);
                assign_flag(flag_o, overflow);
                assign_flag(flag_z, product == 0);
            }
            take_cycles(34 + ea_cycles());
            break;
        case 6: // DIV
            take_cycles(38 + ea_cycles());
            if (word) {
                const std::uint16_t divisor = read_rm16();
                const std::uint32_t dividend = (static_cast<std::uint32_t>(dx_) << 16U) | ax_;
                if (divisor == 0U || dividend / divisor > 0xFFFFU) {
                    interrupt(0U);
                    break;
                }
                ax_ = static_cast<std::uint16_t>(dividend / divisor);
                dx_ = static_cast<std::uint16_t>(dividend % divisor);
            } else {
                const std::uint8_t divisor = read_rm8();
                const std::uint16_t dividend = ax_;
                if (divisor == 0U || dividend / divisor > 0xFFU) {
                    interrupt(0U);
                    break;
                }
                set_reg8(0, static_cast<std::uint8_t>(dividend / divisor));
                set_reg8(4, static_cast<std::uint8_t>(dividend % divisor));
            }
            break;
        default: // 7: IDIV
            take_cycles(43 + ea_cycles());
            if (word) {
                const auto divisor = static_cast<std::int16_t>(read_rm16());
                const auto dividend =
                    static_cast<std::int32_t>((static_cast<std::uint32_t>(dx_) << 16U) | ax_);
                if (divisor == 0) {
                    interrupt(0U);
                    break;
                }
                const std::int32_t quotient = dividend / divisor;
                if (quotient > 0x7FFF || quotient < -0x8000) {
                    interrupt(0U);
                    break;
                }
                ax_ = static_cast<std::uint16_t>(quotient);
                dx_ = static_cast<std::uint16_t>(dividend % divisor);
            } else {
                const auto divisor = static_cast<std::int8_t>(read_rm8());
                const auto dividend = static_cast<std::int16_t>(ax_);
                if (divisor == 0) {
                    interrupt(0U);
                    break;
                }
                const std::int16_t quotient = static_cast<std::int16_t>(dividend / divisor);
                if (quotient > 0x7F || quotient < -0x80) {
                    interrupt(0U);
                    break;
                }
                set_reg8(0, static_cast<std::uint8_t>(quotient));
                set_reg8(4, static_cast<std::uint8_t>(dividend % divisor));
            }
            break;
        }
    }

    void v30::exec_group_fe() {
        fetch_modrm();
        switch (modrm_reg_) {
        case 0: // INC r/m8
            write_rm8(inc8(read_rm8()));
            take_cycles(3 + ea_cycles());
            break;
        case 1: // DEC r/m8
            write_rm8(dec8(read_rm8()));
            take_cycles(3 + ea_cycles());
            break;
        default: // undefined: no-op
            take_cycles(2);
            break;
        }
    }

    void v30::exec_group_ff() {
        fetch_modrm();
        switch (modrm_reg_) {
        case 0: // INC r/m16
            write_rm16(inc16(read_rm16()));
            take_cycles(3 + ea_cycles());
            break;
        case 1: // DEC r/m16
            write_rm16(dec16(read_rm16()));
            take_cycles(3 + ea_cycles());
            break;
        case 2: { // CALL near r/m16
            const std::uint16_t target = read_rm16();
            push16(ip_);
            ip_ = target;
            take_cycles(16 + ea_cycles());
            break;
        }
        case 3: {             // CALL far m16:16
            if (rm_is_reg_) { // register form is undefined; no-op
                take_cycles(2);
                break;
            }
            const std::uint16_t offset = rw(rm_segment_, rm_offset_);
            const std::uint16_t segment =
                rw(rm_segment_, static_cast<std::uint16_t>(rm_offset_ + 2U));
            push16(cs_);
            push16(ip_);
            cs_ = segment;
            ip_ = offset;
            take_cycles(28 + ea_cycles());
            break;
        }
        case 4: // JMP near r/m16
            ip_ = read_rm16();
            take_cycles(11 + ea_cycles());
            break;
        case 5: { // JMP far m16:16
            if (rm_is_reg_) {
                take_cycles(2);
                break;
            }
            const std::uint16_t offset = rw(rm_segment_, rm_offset_);
            cs_ = rw(rm_segment_, static_cast<std::uint16_t>(rm_offset_ + 2U));
            ip_ = offset;
            take_cycles(15 + ea_cycles());
            break;
        }
        case 6: // PUSH r/m16
            push16(read_rm16());
            take_cycles(10 + ea_cycles());
            break;
        default: // 7: undefined: no-op
            take_cycles(2);
            break;
        }
    }

    void v30::exec_string(std::uint8_t opcode) {
        const bool word = (opcode & 1U) != 0U;
        const auto delta =
            static_cast<std::uint16_t>(flag(flag_d) ? (word ? -2 : -1) : (word ? 2 : 1));
        const std::uint16_t source_segment = data_segment(ds_);

        // One iteration of the string primitive; returns false when a REPE/
        // REPNE/REPC/REPNC condition says to stop after this iteration.
        const auto iterate = [&]() -> bool {
            switch (opcode) {
            case 0xA4U: // MOVSB
            case 0xA5U: // MOVSW
                if (word) {
                    ww(es_, di_, rw(source_segment, si_));
                } else {
                    wb(es_, di_, rb(source_segment, si_));
                }
                si_ = static_cast<std::uint16_t>(si_ + delta);
                di_ = static_cast<std::uint16_t>(di_ + delta);
                take_cycles(9);
                return true;
            case 0xA6U:   // CMPSB
            case 0xA7U: { // CMPSW
                if (word) {
                    static_cast<void>(alu16(alu_cmp, rw(source_segment, si_), rw(es_, di_)));
                } else {
                    static_cast<void>(alu8(alu_cmp, rb(source_segment, si_), rb(es_, di_)));
                }
                si_ = static_cast<std::uint16_t>(si_ + delta);
                di_ = static_cast<std::uint16_t>(di_ + delta);
                take_cycles(11);
                break;
            }
            case 0xAAU: // STOSB
            case 0xABU: // STOSW
                if (word) {
                    ww(es_, di_, ax_);
                } else {
                    wb(es_, di_, get_reg8(0));
                }
                di_ = static_cast<std::uint16_t>(di_ + delta);
                take_cycles(6);
                return true;
            case 0xACU: // LODSB
            case 0xADU: // LODSW
                if (word) {
                    ax_ = rw(source_segment, si_);
                } else {
                    set_reg8(0, rb(source_segment, si_));
                }
                si_ = static_cast<std::uint16_t>(si_ + delta);
                take_cycles(6);
                return true;
            case 0xAEU:   // SCASB
            case 0xAFU: { // SCASW
                if (word) {
                    static_cast<void>(alu16(alu_cmp, ax_, rw(es_, di_)));
                } else {
                    static_cast<void>(alu8(alu_cmp, get_reg8(0), rb(es_, di_)));
                }
                di_ = static_cast<std::uint16_t>(di_ + delta);
                take_cycles(8);
                break;
            }
            case 0x6CU: // INSB
            case 0x6DU: // INSW
                if (word) {
                    ww(es_, di_, port_in16(dx_));
                } else {
                    wb(es_, di_, port_in8(dx_));
                }
                di_ = static_cast<std::uint16_t>(di_ + delta);
                take_cycles(8);
                return true;
            default: // 0x6E/0x6F: OUTSB / OUTSW
                if (word) {
                    port_out16(dx_, rw(source_segment, si_));
                } else {
                    port_out8(dx_, rb(source_segment, si_));
                }
                si_ = static_cast<std::uint16_t>(si_ + delta);
                take_cycles(8);
                return true;
            }
            // Comparison primitives: evaluate the repeat-while condition.
            switch (rep_prefix_) {
            case 1: // REPE: continue while equal
                return flag(flag_z);
            case 2: // REPNE
                return !flag(flag_z);
            case 3: // REPC (V-series): continue while carry
                return flag(flag_c);
            case 4: // REPNC
                return !flag(flag_c);
            default:
                return true;
            }
        };

        if (rep_prefix_ == 0) {
            static_cast<void>(iterate());
            return;
        }
        // REP runs to completion within this step (interruptibility is a
        // deferred increment; see the header notes).
        while (cx_ != 0U) {
            const bool keep_going = iterate();
            cx_ = static_cast<std::uint16_t>(cx_ - 1U);
            if (!keep_going) {
                break;
            }
        }
    }

    // ---- snapshots / persistence ---------------------------------------------

    v30::registers v30::cpu_registers() const noexcept {
        return {
            .ax = ax_,
            .bx = bx_,
            .cx = cx_,
            .dx = dx_,
            .si = si_,
            .di = di_,
            .bp = bp_,
            .sp = sp_,
            .ip = ip_,
            .cs = cs_,
            .ds = ds_,
            .es = es_,
            .ss = ss_,
            .flags = flags_word(),
        };
    }

    void v30::set_registers(const registers& values) noexcept {
        ax_ = values.ax;
        bx_ = values.bx;
        cx_ = values.cx;
        dx_ = values.dx;
        si_ = values.si;
        di_ = values.di;
        bp_ = values.bp;
        sp_ = values.sp;
        ip_ = values.ip;
        cs_ = values.cs;
        ds_ = values.ds;
        es_ = values.es;
        ss_ = values.ss;
        set_flags_word(values.flags);
    }

    void v30::save_state(state_writer& writer) const {
        writer.u16(ax_);
        writer.u16(bx_);
        writer.u16(cx_);
        writer.u16(dx_);
        writer.u16(si_);
        writer.u16(di_);
        writer.u16(bp_);
        writer.u16(sp_);
        writer.u16(ip_);
        writer.u16(cs_);
        writer.u16(ds_);
        writer.u16(es_);
        writer.u16(ss_);
        writer.u16(flags_);
        writer.boolean(halted_);
        writer.boolean(irq_line_);
        writer.boolean(nmi_pending_);
        writer.boolean(interrupt_inhibit_);
        writer.u64(static_cast<std::uint64_t>(cycle_debt_));
        writer.u64(elapsed_);
    }

    void v30::load_state(state_reader& reader) {
        ax_ = reader.u16();
        bx_ = reader.u16();
        cx_ = reader.u16();
        dx_ = reader.u16();
        si_ = reader.u16();
        di_ = reader.u16();
        bp_ = reader.u16();
        sp_ = reader.u16();
        ip_ = reader.u16();
        cs_ = reader.u16();
        ds_ = reader.u16();
        es_ = reader.u16();
        ss_ = reader.u16();
        flags_ = reader.u16();
        halted_ = reader.boolean();
        irq_line_ = reader.boolean();
        nmi_pending_ = reader.boolean();
        interrupt_inhibit_ = reader.boolean();
        cycle_debt_ = static_cast<std::int64_t>(reader.u64());
        elapsed_ = reader.u64();
    }

    // ---- introspection ----------------------------------------------------------

    std::span<const register_descriptor> v30::register_snapshot() noexcept {
        using fmt = register_value_format;
        register_view_[0] = {"AX", ax_, 16U, fmt::unsigned_integer};
        register_view_[1] = {"BX", bx_, 16U, fmt::unsigned_integer};
        register_view_[2] = {"CX", cx_, 16U, fmt::unsigned_integer};
        register_view_[3] = {"DX", dx_, 16U, fmt::unsigned_integer};
        register_view_[4] = {"SI", si_, 16U, fmt::unsigned_integer};
        register_view_[5] = {"DI", di_, 16U, fmt::unsigned_integer};
        register_view_[6] = {"BP", bp_, 16U, fmt::unsigned_integer};
        register_view_[7] = {"SP", sp_, 16U, fmt::unsigned_integer};
        register_view_[8] = {"IP", ip_, 16U, fmt::unsigned_integer};
        register_view_[9] = {"CS", cs_, 16U, fmt::unsigned_integer};
        register_view_[10] = {"DS", ds_, 16U, fmt::unsigned_integer};
        register_view_[11] = {"ES", es_, 16U, fmt::unsigned_integer};
        register_view_[12] = {"SS", ss_, 16U, fmt::unsigned_integer};
        register_view_[13] = {"FLAGS", flags_word(), 16U, fmt::flags};
        return register_view_;
    }

    instrumentation::ichip_introspection& v30::introspection() noexcept { return introspection_; }

    v30::introspection_surface::introspection_surface(v30& owner) noexcept
        : trace_impl_(owner), registers_impl_(owner) {}

    void v30::introspection_surface::trace_impl::install(callback cb) {
        if (cb) {
            v30* cpu = owner_;
            owner_->trace_callback_ = [cpu, cb = std::move(cb)](std::uint32_t pc) {
                cb({.pc = pc, .cycles = cpu->elapsed_cycles()});
            };
        } else {
            owner_->trace_callback_ = {};
        }
    }

    std::span<const register_descriptor> v30::introspection_surface::registers_impl::registers() {
        return owner_->register_snapshot();
    }

    namespace {
        [[maybe_unused]] const auto v30_registration =
            register_factory("nec.v30", chip_class::cpu,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<v30>(); });
    } // namespace

} // namespace mnemos::chips::cpu
