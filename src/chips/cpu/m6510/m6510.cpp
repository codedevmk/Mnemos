#include "m6510.hpp"

#include "decode_table.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::cpu {
    namespace {
        // The "magic" constant for the unstable ANE/LXA opcodes. On real NMOS silicon
        // the result depends on analog effects; emulators pick a fixed constant.
        constexpr std::uint8_t unstable_magic = 0xEEU;
    } // namespace

    chip_metadata m6510::metadata() const noexcept {
        return {
            .manufacturer = "MOS Technology",
            .part_number = "6510",
            .family = "6502",
            .klass = chip_class::cpu,
            .revision = 1U,
        };
    }

    void m6510::tick(std::uint64_t cycles) {
        for (std::uint64_t i = 0; i < cycles; ++i) {
            step_one_cycle();
            ++cycles_;
        }
    }

    void m6510::step_one_cycle() {
        if (tcu_ == 0U) {
            // Interrupts are polled at the instruction boundary (a v0.1
            // simplification of the real mid-instruction polling; the dedicated
            // interrupt-timing tests come with the conformance ROMs).
            const bool take_nmi = nmi_pending_;
            const bool take_irq = irq_line_ && !flag(status_flag::irq_disable);
            if (take_nmi || take_irq) {
                in_interrupt_ = true;
                interrupt_vector_ = take_nmi ? 0xFFFAU : 0xFFFEU;
                nmi_pending_ = false;
                static_cast<void>(read(registers_.pc)); // sequence cycle 1 (dummy)
                tcu_ = 1U;
                return;
            }
            // Trace fires HERE: one PC per executed instruction (not per
            // cycle), and only for regular instruction execution (IRQ/NMI
            // entries above don't trace, matching m68000/z80 behaviour).
            if (trace_callback_) {
                trace_callback_(registers_.pc);
            }
            ir_ = read(registers_.pc++); // opcode fetch is cycle 1
            tcu_ = 1U;
            return;
        }

        if (in_interrupt_) {
            step_interrupt();
            return;
        }

        const decoded& entry = decode_table()[ir_];
        switch (entry.kind) {
        case access_kind::implied:
            step_implied(entry);
            return;
        case access_kind::read:
            step_read(entry);
            return;
        case access_kind::write:
            step_write(entry);
            return;
        case access_kind::stack:
            step_stack(entry);
            return;
        case access_kind::read_modify_write:
            step_rmw(entry);
            return;
        case access_kind::relative:
            step_branch(entry);
            return;
        case access_kind::jump:
            step_jump(entry);
            return;
        default:
            // Unimplemented access kinds (illegal/jam slots for now) end here;
            // real micro-sequences are added in their own tasks.
            tcu_ = 0U;
            return;
        }
    }

    void m6510::step_implied(const decoded& entry) {
        static_cast<void>(read(registers_.pc)); // dummy read of next byte, no increment
        execute_implied(entry.op);
        tcu_ = 0U;
    }

    void m6510::step_read(const decoded& entry) {
        switch (entry.mode) {
        case addressing_mode::immediate:
            operand_ = read(registers_.pc++);
            execute_read(entry.op);
            tcu_ = 0U;
            return;

        case addressing_mode::zero_page:
            switch (tcu_) {
            case 1U:
                ea_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            default:
                operand_ = read(ea_);
                execute_read(entry.op);
                tcu_ = 0U;
                return;
            }

        case addressing_mode::zero_page_x:
        case addressing_mode::zero_page_y: {
            const std::uint8_t index =
                (entry.mode == addressing_mode::zero_page_x) ? registers_.x : registers_.y;
            switch (tcu_) {
            case 1U:
                ptr_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                static_cast<void>(read(ptr_)); // dummy read at the un-indexed address
                ea_ = static_cast<std::uint8_t>(ptr_ + index); // wraps within zero page
                tcu_ = 3U;
                return;
            default:
                operand_ = read(ea_);
                execute_read(entry.op);
                tcu_ = 0U;
                return;
            }
        }

        case addressing_mode::absolute:
            switch (tcu_) {
            case 1U:
                ea_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                ea_ = static_cast<std::uint16_t>(
                    ea_ | static_cast<std::uint16_t>(read(registers_.pc++) << 8U));
                tcu_ = 3U;
                return;
            default:
                operand_ = read(ea_);
                execute_read(entry.op);
                tcu_ = 0U;
                return;
            }

        case addressing_mode::absolute_x:
        case addressing_mode::absolute_y: {
            const std::uint8_t index =
                (entry.mode == addressing_mode::absolute_x) ? registers_.x : registers_.y;
            switch (tcu_) {
            case 1U:
                ea_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U: {
                const auto high = static_cast<std::uint16_t>(read(registers_.pc++));
                const auto base =
                    static_cast<std::uint16_t>(ea_ | static_cast<std::uint16_t>(high << 8U));
                const unsigned low_sum = (base & 0x00FFU) + index;
                page_cross_ = low_sum > 0xFFU;
                ea_ = static_cast<std::uint16_t>((base & 0xFF00U) | (low_sum & 0x00FFU));
                tcu_ = 3U;
                return;
            }
            case 3U:
                if (page_cross_) {
                    static_cast<void>(read(ea_)); // dummy read at the un-fixed address
                    ea_ = static_cast<std::uint16_t>(ea_ + 0x0100U);
                    tcu_ = 4U;
                    return;
                }
                operand_ = read(ea_);
                execute_read(entry.op);
                tcu_ = 0U;
                return;
            default:
                operand_ = read(ea_);
                execute_read(entry.op);
                tcu_ = 0U;
                return;
            }
        }

        case addressing_mode::indexed_indirect: // (zp,X)
            switch (tcu_) {
            case 1U:
                ptr_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                static_cast<void>(read(ptr_));
                ptr_ = static_cast<std::uint8_t>(ptr_ + registers_.x);
                tcu_ = 3U;
                return;
            case 3U:
                ea_ = read(ptr_);
                tcu_ = 4U;
                return;
            case 4U:
                ea_ = static_cast<std::uint16_t>(
                    ea_ |
                    static_cast<std::uint16_t>(read(static_cast<std::uint8_t>(ptr_ + 1U)) << 8U));
                tcu_ = 5U;
                return;
            default:
                operand_ = read(ea_);
                execute_read(entry.op);
                tcu_ = 0U;
                return;
            }

        case addressing_mode::indirect_indexed: // (zp),Y
            switch (tcu_) {
            case 1U:
                ptr_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                ea_ = read(ptr_);
                tcu_ = 3U;
                return;
            case 3U: {
                const auto high =
                    static_cast<std::uint16_t>(read(static_cast<std::uint8_t>(ptr_ + 1U)));
                const auto base =
                    static_cast<std::uint16_t>(ea_ | static_cast<std::uint16_t>(high << 8U));
                const unsigned low_sum = (base & 0x00FFU) + registers_.y;
                page_cross_ = low_sum > 0xFFU;
                ea_ = static_cast<std::uint16_t>((base & 0xFF00U) | (low_sum & 0x00FFU));
                tcu_ = 4U;
                return;
            }
            case 4U:
                if (page_cross_) {
                    static_cast<void>(read(ea_)); // dummy read at the un-fixed address
                    ea_ = static_cast<std::uint16_t>(ea_ + 0x0100U);
                    tcu_ = 5U;
                    return;
                }
                operand_ = read(ea_);
                execute_read(entry.op);
                tcu_ = 0U;
                return;
            default:
                operand_ = read(ea_);
                execute_read(entry.op);
                tcu_ = 0U;
                return;
            }

        default:
            tcu_ = 0U;
            return;
        }
    }

    void m6510::step_write(const decoded& entry) {
        switch (entry.mode) {
        case addressing_mode::zero_page:
            switch (tcu_) {
            case 1U:
                ea_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            default:
                write(ea_, store_value(entry.op));
                tcu_ = 0U;
                return;
            }

        case addressing_mode::zero_page_x:
        case addressing_mode::zero_page_y: {
            const std::uint8_t index =
                (entry.mode == addressing_mode::zero_page_x) ? registers_.x : registers_.y;
            switch (tcu_) {
            case 1U:
                ptr_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                static_cast<void>(read(ptr_));
                ea_ = static_cast<std::uint8_t>(ptr_ + index);
                tcu_ = 3U;
                return;
            default:
                write(ea_, store_value(entry.op));
                tcu_ = 0U;
                return;
            }
        }

        case addressing_mode::absolute:
            switch (tcu_) {
            case 1U:
                ea_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                ea_ = static_cast<std::uint16_t>(
                    ea_ | static_cast<std::uint16_t>(read(registers_.pc++) << 8U));
                tcu_ = 3U;
                return;
            default:
                write(ea_, store_value(entry.op));
                tcu_ = 0U;
                return;
            }

        case addressing_mode::absolute_x:
        case addressing_mode::absolute_y: {
            const std::uint8_t index =
                (entry.mode == addressing_mode::absolute_x) ? registers_.x : registers_.y;
            switch (tcu_) {
            case 1U:
                ea_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U: {
                const auto high = static_cast<std::uint16_t>(read(registers_.pc++));
                const auto base =
                    static_cast<std::uint16_t>(ea_ | static_cast<std::uint16_t>(high << 8U));
                const unsigned low_sum = (base & 0x00FFU) + index;
                page_cross_ = low_sum > 0xFFU;
                ea_ = static_cast<std::uint16_t>((base & 0xFF00U) | (low_sum & 0x00FFU));
                tcu_ = 3U;
                return;
            }
            case 3U:
                static_cast<void>(read(ea_)); // dummy read (un-fixed address on carry)
                if (page_cross_) {
                    ea_ = static_cast<std::uint16_t>(ea_ + 0x0100U);
                }
                tcu_ = 4U;
                return;
            default:
                if (entry.op == operation::tas) { // TAS sets SP = A & X before storing
                    registers_.sp = static_cast<std::uint8_t>(registers_.a & registers_.x);
                }
                write(ea_, store_value(entry.op));
                tcu_ = 0U;
                return;
            }
        }

        case addressing_mode::indexed_indirect: // (zp,X)
            switch (tcu_) {
            case 1U:
                ptr_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                static_cast<void>(read(ptr_));
                ptr_ = static_cast<std::uint8_t>(ptr_ + registers_.x);
                tcu_ = 3U;
                return;
            case 3U:
                ea_ = read(ptr_);
                tcu_ = 4U;
                return;
            case 4U:
                ea_ = static_cast<std::uint16_t>(
                    ea_ |
                    static_cast<std::uint16_t>(read(static_cast<std::uint8_t>(ptr_ + 1U)) << 8U));
                tcu_ = 5U;
                return;
            default:
                write(ea_, store_value(entry.op));
                tcu_ = 0U;
                return;
            }

        case addressing_mode::indirect_indexed: // (zp),Y
            switch (tcu_) {
            case 1U:
                ptr_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                ea_ = read(ptr_);
                tcu_ = 3U;
                return;
            case 3U: {
                const auto high =
                    static_cast<std::uint16_t>(read(static_cast<std::uint8_t>(ptr_ + 1U)));
                const auto base =
                    static_cast<std::uint16_t>(ea_ | static_cast<std::uint16_t>(high << 8U));
                const unsigned low_sum = (base & 0x00FFU) + registers_.y;
                page_cross_ = low_sum > 0xFFU;
                ea_ = static_cast<std::uint16_t>((base & 0xFF00U) | (low_sum & 0x00FFU));
                tcu_ = 4U;
                return;
            }
            case 4U:
                static_cast<void>(read(ea_)); // dummy read (un-fixed address on carry)
                if (page_cross_) {
                    ea_ = static_cast<std::uint16_t>(ea_ + 0x0100U);
                }
                tcu_ = 5U;
                return;
            default:
                write(ea_, store_value(entry.op));
                tcu_ = 0U;
                return;
            }

        default:
            tcu_ = 0U;
            return;
        }
    }

    std::uint8_t m6510::store_value(operation op) const noexcept {
        // The unstable SHA/SHX/SHY/TAS stores AND the source with (target high + 1).
        const auto high_plus_1 = static_cast<std::uint8_t>(((ea_ >> 8U) + 1U) & 0xFFU);
        switch (op) {
        case operation::stx:
            return registers_.x;
        case operation::sty:
            return registers_.y;
        case operation::sax: // store A & X (no flags)
            return static_cast<std::uint8_t>(registers_.a & registers_.x);
        case operation::sha:
            return static_cast<std::uint8_t>(registers_.a & registers_.x & high_plus_1);
        case operation::shx:
            return static_cast<std::uint8_t>(registers_.x & high_plus_1);
        case operation::shy:
            return static_cast<std::uint8_t>(registers_.y & high_plus_1);
        case operation::tas: // SP was set to A & X in step_write before the store
            return static_cast<std::uint8_t>(registers_.sp & high_plus_1);
        case operation::sta:
        default:
            return registers_.a;
        }
    }

    std::uint16_t m6510::stack_address() const noexcept {
        return static_cast<std::uint16_t>(0x0100U | registers_.sp);
    }

    void m6510::step_stack(const decoded& entry) {
        switch (entry.op) {
        case operation::pha:
            if (tcu_ == 1U) {
                static_cast<void>(read(registers_.pc)); // dummy read
                tcu_ = 2U;
                return;
            }
            write(stack_address(), registers_.a);
            registers_.sp = static_cast<std::uint8_t>(registers_.sp - 1U);
            tcu_ = 0U;
            return;

        case operation::php:
            if (tcu_ == 1U) {
                static_cast<void>(read(registers_.pc));
                tcu_ = 2U;
                return;
            }
            // Pushed status has B and the unused bit set.
            write(stack_address(), static_cast<std::uint8_t>(registers_.p | 0x30U));
            registers_.sp = static_cast<std::uint8_t>(registers_.sp - 1U);
            tcu_ = 0U;
            return;

        case operation::pla:
            switch (tcu_) {
            case 1U:
                static_cast<void>(read(registers_.pc));
                tcu_ = 2U;
                return;
            case 2U:
                static_cast<void>(read(stack_address())); // dummy read while incrementing SP
                registers_.sp = static_cast<std::uint8_t>(registers_.sp + 1U);
                tcu_ = 3U;
                return;
            default:
                registers_.a = read(stack_address());
                set_nz(registers_.a);
                tcu_ = 0U;
                return;
            }

        case operation::plp:
            switch (tcu_) {
            case 1U:
                static_cast<void>(read(registers_.pc));
                tcu_ = 2U;
                return;
            case 2U:
                static_cast<void>(read(stack_address()));
                registers_.sp = static_cast<std::uint8_t>(registers_.sp + 1U);
                tcu_ = 3U;
                return;
            default:
                // B is dropped and the unused bit forced set in the register copy.
                registers_.p = static_cast<std::uint8_t>((read(stack_address()) & 0xEFU) | 0x20U);
                tcu_ = 0U;
                return;
            }

        default:
            tcu_ = 0U;
            return;
        }
    }

    void m6510::execute_read(operation op) noexcept {
        switch (op) {
        case operation::lda:
            op_lda(operand_);
            return;
        case operation::ldx:
            op_ldx(operand_);
            return;
        case operation::ldy:
            op_ldy(operand_);
            return;
        case operation::and_:
            op_and(operand_);
            return;
        case operation::ora:
            op_ora(operand_);
            return;
        case operation::eor:
            op_eor(operand_);
            return;
        case operation::adc:
            op_adc(operand_);
            return;
        case operation::sbc:
            op_sbc(operand_);
            return;
        case operation::cmp:
            op_compare(registers_.a, operand_);
            return;
        case operation::cpx:
            op_compare(registers_.x, operand_);
            return;
        case operation::cpy:
            op_compare(registers_.y, operand_);
            return;
        case operation::bit:
            op_bit(operand_);
            return;
        case operation::lax: // LDA + LDX
            registers_.a = operand_;
            registers_.x = operand_;
            set_nz(operand_);
            return;
        case operation::anc: // AND #imm, then copy N into C
            registers_.a = static_cast<std::uint8_t>(registers_.a & operand_);
            set_nz(registers_.a);
            set_flag(status_flag::carry, (registers_.a & 0x80U) != 0U);
            return;
        case operation::alr: // AND #imm, then LSR A
            registers_.a = static_cast<std::uint8_t>(registers_.a & operand_);
            set_flag(status_flag::carry, (registers_.a & 0x01U) != 0U);
            registers_.a = static_cast<std::uint8_t>(registers_.a >> 1U);
            set_nz(registers_.a);
            return;
        case operation::arr: { // AND #imm, then a ROR with special C/V (and BCD fixup)
            const auto value = static_cast<std::uint8_t>(registers_.a & operand_);
            const auto carry_in = static_cast<std::uint8_t>(flag(status_flag::carry) ? 0x80U : 0U);
            auto result = static_cast<std::uint8_t>((value >> 1U) | carry_in);

            if (flag(status_flag::decimal)) {
                // NMOS decimal ARR: N/Z/V come from the pre-fixup result, then the
                // nibbles are BCD-adjusted and carry is decided on the high nibble.
                set_nz(result);
                set_flag(status_flag::overflow, ((value ^ result) & 0x40U) != 0U);
                if (((value & 0x0FU) + (value & 0x01U)) > 0x05U) {
                    result =
                        static_cast<std::uint8_t>((result & 0xF0U) | ((result + 0x06U) & 0x0FU));
                }
                if (((value & 0xF0U) + (value & 0x10U)) > 0x50U) {
                    result = static_cast<std::uint8_t>(result + 0x60U);
                    set_flag(status_flag::carry, true);
                } else {
                    set_flag(status_flag::carry, false);
                }
                registers_.a = result;
                return;
            }

            registers_.a = result;
            set_nz(result);
            set_flag(status_flag::carry, (result & 0x40U) != 0U);
            set_flag(status_flag::overflow, (((result >> 6U) ^ (result >> 5U)) & 0x01U) != 0U);
            return;
        }
        case operation::sbx: { // X = (A & X) - imm, sets C like CMP
            const auto t = static_cast<unsigned>(registers_.a & registers_.x);
            set_flag(status_flag::carry, t >= operand_);
            registers_.x = static_cast<std::uint8_t>(t - operand_);
            set_nz(registers_.x);
            return;
        }
        case operation::ane: // (A | magic) & X & imm
            registers_.a = static_cast<std::uint8_t>((registers_.a | unstable_magic) &
                                                     registers_.x & operand_);
            set_nz(registers_.a);
            return;
        case operation::lxa: // A = X = (A | magic) & imm
            registers_.a = static_cast<std::uint8_t>((registers_.a | unstable_magic) & operand_);
            registers_.x = registers_.a;
            set_nz(registers_.a);
            return;
        case operation::las: // A = X = SP = mem & SP
            registers_.sp = static_cast<std::uint8_t>(operand_ & registers_.sp);
            registers_.a = registers_.sp;
            registers_.x = registers_.sp;
            set_nz(registers_.sp);
            return;
        default:
            return;
        }
    }

    void m6510::execute_implied(operation op) noexcept {
        switch (op) {
        case operation::tax:
            registers_.x = registers_.a;
            set_nz(registers_.x);
            return;
        case operation::tay:
            registers_.y = registers_.a;
            set_nz(registers_.y);
            return;
        case operation::txa:
            registers_.a = registers_.x;
            set_nz(registers_.a);
            return;
        case operation::tya:
            registers_.a = registers_.y;
            set_nz(registers_.a);
            return;
        case operation::tsx:
            registers_.x = registers_.sp;
            set_nz(registers_.x);
            return;
        case operation::txs:
            registers_.sp = registers_.x; // TXS does not touch flags
            return;
        case operation::inx:
            registers_.x = static_cast<std::uint8_t>(registers_.x + 1U);
            set_nz(registers_.x);
            return;
        case operation::iny:
            registers_.y = static_cast<std::uint8_t>(registers_.y + 1U);
            set_nz(registers_.y);
            return;
        case operation::dex:
            registers_.x = static_cast<std::uint8_t>(registers_.x - 1U);
            set_nz(registers_.x);
            return;
        case operation::dey:
            registers_.y = static_cast<std::uint8_t>(registers_.y - 1U);
            set_nz(registers_.y);
            return;
        case operation::clc:
            set_flag(status_flag::carry, false);
            return;
        case operation::sec:
            set_flag(status_flag::carry, true);
            return;
        case operation::cli:
            set_flag(status_flag::irq_disable, false);
            return;
        case operation::sei:
            set_flag(status_flag::irq_disable, true);
            return;
        case operation::cld:
            set_flag(status_flag::decimal, false);
            return;
        case operation::sed:
            set_flag(status_flag::decimal, true);
            return;
        case operation::clv:
            set_flag(status_flag::overflow, false);
            return;
        case operation::nop:
        default:
            return;
        }
    }

    void m6510::op_lda(std::uint8_t value) noexcept {
        registers_.a = value;
        set_nz(value);
    }

    void m6510::op_ldx(std::uint8_t value) noexcept {
        registers_.x = value;
        set_nz(value);
    }

    void m6510::op_ldy(std::uint8_t value) noexcept {
        registers_.y = value;
        set_nz(value);
    }

    void m6510::op_and(std::uint8_t value) noexcept {
        registers_.a = static_cast<std::uint8_t>(registers_.a & value);
        set_nz(registers_.a);
    }

    void m6510::op_ora(std::uint8_t value) noexcept {
        registers_.a = static_cast<std::uint8_t>(registers_.a | value);
        set_nz(registers_.a);
    }

    void m6510::op_eor(std::uint8_t value) noexcept {
        registers_.a = static_cast<std::uint8_t>(registers_.a ^ value);
        set_nz(registers_.a);
    }

    void m6510::op_adc(std::uint8_t value) noexcept {
        if (flag(status_flag::decimal)) {
            adc_decimal(value);
            return;
        }
        adc_binary(value);
    }

    void m6510::op_sbc(std::uint8_t value) noexcept {
        if (flag(status_flag::decimal)) {
            sbc_decimal(value);
            return;
        }
        // Binary subtract is add-with-carry of the one's complement.
        adc_binary(static_cast<std::uint8_t>(~value));
    }

    void m6510::adc_binary(std::uint8_t value) noexcept {
        const unsigned carry = flag(status_flag::carry) ? 1U : 0U;
        const unsigned sum = static_cast<unsigned>(registers_.a) + value + carry;
        const auto result = static_cast<std::uint8_t>(sum);
        set_flag(status_flag::carry, sum > 0xFFU);
        set_flag(status_flag::overflow, ((registers_.a ^ result) & (value ^ result) & 0x80U) != 0U);
        registers_.a = result;
        set_nz(result);
    }

    void m6510::adc_decimal(std::uint8_t value) noexcept {
        // NMOS 6502 BCD add (Bruce Clark's algorithm). Z is the binary result;
        // N and V reflect the intermediate sum before the high-nibble adjust.
        const unsigned carry = flag(status_flag::carry) ? 1U : 0U;
        const unsigned a = registers_.a;

        set_flag(status_flag::zero, ((a + value + carry) & 0xFFU) == 0U);

        unsigned low = (a & 0x0FU) + (value & 0x0FU) + carry;
        if (low >= 0x0AU) {
            low = ((low + 0x06U) & 0x0FU) + 0x10U;
        }
        unsigned sum = (a & 0xF0U) + (value & 0xF0U) + low;

        set_flag(status_flag::negative, (sum & 0x80U) != 0U);
        set_flag(status_flag::overflow, ((~(a ^ value)) & (a ^ sum) & 0x80U) != 0U);

        if (sum >= 0xA0U) {
            sum += 0x60U;
        }
        set_flag(status_flag::carry, sum >= 0x100U);
        registers_.a = static_cast<std::uint8_t>(sum & 0xFFU);
    }

    void m6510::sbc_decimal(std::uint8_t value) noexcept {
        // NMOS 6502 BCD subtract. The N/V/Z/C flags match binary SBC exactly;
        // only the accumulator result is decimal-adjusted.
        const unsigned carry = flag(status_flag::carry) ? 1U : 0U;
        const unsigned a = registers_.a;

        const unsigned inv = static_cast<std::uint8_t>(~value);
        const unsigned bin = a + inv + carry;
        const auto bin_res = static_cast<std::uint8_t>(bin);
        set_flag(status_flag::carry, bin > 0xFFU);
        set_flag(status_flag::overflow, ((a ^ bin_res) & (inv ^ bin_res) & 0x80U) != 0U);
        set_flag(status_flag::zero, bin_res == 0U);
        set_flag(status_flag::negative, (bin_res & 0x80U) != 0U);

        int low = static_cast<int>(a & 0x0FU) - static_cast<int>(value & 0x0FU) +
                  static_cast<int>(carry) - 1;
        if (low < 0) {
            low = ((low - 0x06) & 0x0F) - 0x10;
        }
        int sum = static_cast<int>(a & 0xF0U) - static_cast<int>(value & 0xF0U) + low;
        if (sum < 0) {
            sum -= 0x60;
        }
        registers_.a = static_cast<std::uint8_t>(static_cast<unsigned>(sum) & 0xFFU);
    }

    void m6510::op_bit(std::uint8_t value) noexcept {
        set_flag(status_flag::zero, (registers_.a & value) == 0U);
        set_flag(status_flag::negative, (value & 0x80U) != 0U);
        set_flag(status_flag::overflow, (value & 0x40U) != 0U);
    }

    void m6510::op_compare(std::uint8_t reg, std::uint8_t value) noexcept {
        const auto diff = static_cast<std::uint8_t>(reg - value);
        set_flag(status_flag::carry, reg >= value);
        set_nz(diff);
    }

    std::uint8_t m6510::modify_rmw(operation op, std::uint8_t value) noexcept {
        std::uint8_t result = value;
        switch (op) {
        case operation::asl:
            set_flag(status_flag::carry, (value & 0x80U) != 0U);
            result = static_cast<std::uint8_t>(value << 1U);
            break;
        case operation::lsr:
            set_flag(status_flag::carry, (value & 0x01U) != 0U);
            result = static_cast<std::uint8_t>(value >> 1U);
            break;
        case operation::rol: {
            const auto old = static_cast<std::uint8_t>(flag(status_flag::carry) ? 1U : 0U);
            set_flag(status_flag::carry, (value & 0x80U) != 0U);
            result = static_cast<std::uint8_t>((value << 1U) | old);
            break;
        }
        case operation::ror: {
            const auto old = static_cast<std::uint8_t>(flag(status_flag::carry) ? 0x80U : 0U);
            set_flag(status_flag::carry, (value & 0x01U) != 0U);
            result = static_cast<std::uint8_t>((value >> 1U) | old);
            break;
        }
        case operation::inc:
            result = static_cast<std::uint8_t>(value + 1U);
            break;
        case operation::dec:
            result = static_cast<std::uint8_t>(value - 1U);
            break;
        // Combined undocumented RMW ops: do the shift/inc/dec, then an ALU op on A
        // that sets the N/Z (and, where applicable, C/V) flags.
        case operation::slo:
            set_flag(status_flag::carry, (value & 0x80U) != 0U);
            result = static_cast<std::uint8_t>(value << 1U);
            op_ora(result);
            return result;
        case operation::rla: {
            const auto old = static_cast<std::uint8_t>(flag(status_flag::carry) ? 1U : 0U);
            set_flag(status_flag::carry, (value & 0x80U) != 0U);
            result = static_cast<std::uint8_t>((value << 1U) | old);
            op_and(result);
            return result;
        }
        case operation::sre:
            set_flag(status_flag::carry, (value & 0x01U) != 0U);
            result = static_cast<std::uint8_t>(value >> 1U);
            op_eor(result);
            return result;
        case operation::rra: {
            const auto old = static_cast<std::uint8_t>(flag(status_flag::carry) ? 0x80U : 0U);
            set_flag(status_flag::carry, (value & 0x01U) != 0U);
            result = static_cast<std::uint8_t>((value >> 1U) | old);
            op_adc(result);
            return result;
        }
        case operation::dcp:
            result = static_cast<std::uint8_t>(value - 1U);
            op_compare(registers_.a, result);
            return result;
        case operation::isc:
            result = static_cast<std::uint8_t>(value + 1U);
            op_sbc(result);
            return result;
        default:
            break;
        }
        set_nz(result);
        return result;
    }

    void m6510::step_rmw(const decoded& entry) {
        if (entry.mode == addressing_mode::accumulator) {
            static_cast<void>(read(registers_.pc)); // dummy read
            registers_.a = modify_rmw(entry.op, registers_.a);
            tcu_ = 0U;
            return;
        }

        switch (entry.mode) {
        case addressing_mode::zero_page:
            switch (tcu_) {
            case 1U:
                ea_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                operand_ = read(ea_);
                tcu_ = 3U;
                return;
            case 3U:
                write(ea_, operand_); // dummy write of the original value
                operand_ = modify_rmw(entry.op, operand_);
                tcu_ = 4U;
                return;
            default:
                write(ea_, operand_);
                tcu_ = 0U;
                return;
            }

        case addressing_mode::zero_page_x:
            switch (tcu_) {
            case 1U:
                ptr_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                static_cast<void>(read(ptr_));
                ea_ = static_cast<std::uint8_t>(ptr_ + registers_.x);
                tcu_ = 3U;
                return;
            case 3U:
                operand_ = read(ea_);
                tcu_ = 4U;
                return;
            case 4U:
                write(ea_, operand_);
                operand_ = modify_rmw(entry.op, operand_);
                tcu_ = 5U;
                return;
            default:
                write(ea_, operand_);
                tcu_ = 0U;
                return;
            }

        case addressing_mode::absolute:
            switch (tcu_) {
            case 1U:
                ea_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                ea_ = static_cast<std::uint16_t>(
                    ea_ | static_cast<std::uint16_t>(read(registers_.pc++) << 8U));
                tcu_ = 3U;
                return;
            case 3U:
                operand_ = read(ea_);
                tcu_ = 4U;
                return;
            case 4U:
                write(ea_, operand_);
                operand_ = modify_rmw(entry.op, operand_);
                tcu_ = 5U;
                return;
            default:
                write(ea_, operand_);
                tcu_ = 0U;
                return;
            }

        case addressing_mode::absolute_x:
        case addressing_mode::absolute_y: {
            const std::uint8_t index =
                (entry.mode == addressing_mode::absolute_x) ? registers_.x : registers_.y;
            switch (tcu_) {
            case 1U:
                ea_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U: {
                const auto high = static_cast<std::uint16_t>(read(registers_.pc++));
                const auto base =
                    static_cast<std::uint16_t>(ea_ | static_cast<std::uint16_t>(high << 8U));
                const unsigned low_sum = (base & 0x00FFU) + index;
                page_cross_ = low_sum > 0xFFU;
                ea_ = static_cast<std::uint16_t>((base & 0xFF00U) | (low_sum & 0x00FFU));
                tcu_ = 3U;
                return;
            }
            case 3U:
                static_cast<void>(read(ea_)); // dummy read (un-fixed address on carry)
                if (page_cross_) {
                    ea_ = static_cast<std::uint16_t>(ea_ + 0x0100U);
                }
                tcu_ = 4U;
                return;
            case 4U:
                operand_ = read(ea_);
                tcu_ = 5U;
                return;
            case 5U:
                write(ea_, operand_);
                operand_ = modify_rmw(entry.op, operand_);
                tcu_ = 6U;
                return;
            default:
                write(ea_, operand_);
                tcu_ = 0U;
                return;
            }
        }

        case addressing_mode::indexed_indirect: // (zp,X) — used by illegal RMW ops
            switch (tcu_) {
            case 1U:
                ptr_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                static_cast<void>(read(ptr_));
                ptr_ = static_cast<std::uint8_t>(ptr_ + registers_.x);
                tcu_ = 3U;
                return;
            case 3U:
                ea_ = read(ptr_);
                tcu_ = 4U;
                return;
            case 4U:
                ea_ = static_cast<std::uint16_t>(
                    ea_ |
                    static_cast<std::uint16_t>(read(static_cast<std::uint8_t>(ptr_ + 1U)) << 8U));
                tcu_ = 5U;
                return;
            case 5U:
                operand_ = read(ea_);
                tcu_ = 6U;
                return;
            case 6U:
                write(ea_, operand_);
                operand_ = modify_rmw(entry.op, operand_);
                tcu_ = 7U;
                return;
            default:
                write(ea_, operand_);
                tcu_ = 0U;
                return;
            }

        case addressing_mode::indirect_indexed: // (zp),Y — used by illegal RMW ops
            switch (tcu_) {
            case 1U:
                ptr_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                ea_ = read(ptr_);
                tcu_ = 3U;
                return;
            case 3U: {
                const auto high =
                    static_cast<std::uint16_t>(read(static_cast<std::uint8_t>(ptr_ + 1U)));
                const auto base =
                    static_cast<std::uint16_t>(ea_ | static_cast<std::uint16_t>(high << 8U));
                const unsigned low_sum = (base & 0x00FFU) + registers_.y;
                page_cross_ = low_sum > 0xFFU;
                ea_ = static_cast<std::uint16_t>((base & 0xFF00U) | (low_sum & 0x00FFU));
                tcu_ = 4U;
                return;
            }
            case 4U:
                static_cast<void>(read(ea_)); // dummy read (un-fixed address on carry)
                if (page_cross_) {
                    ea_ = static_cast<std::uint16_t>(ea_ + 0x0100U);
                }
                tcu_ = 5U;
                return;
            case 5U:
                operand_ = read(ea_);
                tcu_ = 6U;
                return;
            case 6U:
                write(ea_, operand_);
                operand_ = modify_rmw(entry.op, operand_);
                tcu_ = 7U;
                return;
            default:
                write(ea_, operand_);
                tcu_ = 0U;
                return;
            }

        default:
            tcu_ = 0U;
            return;
        }
    }

    void m6510::push(std::uint8_t value) noexcept {
        write(stack_address(), value);
        registers_.sp = static_cast<std::uint8_t>(registers_.sp - 1U);
    }

    bool m6510::branch_taken(operation op) const noexcept {
        switch (op) {
        case operation::bpl:
            return !flag(status_flag::negative);
        case operation::bmi:
            return flag(status_flag::negative);
        case operation::bvc:
            return !flag(status_flag::overflow);
        case operation::bvs:
            return flag(status_flag::overflow);
        case operation::bcc:
            return !flag(status_flag::carry);
        case operation::bcs:
            return flag(status_flag::carry);
        case operation::bne:
            return !flag(status_flag::zero);
        case operation::beq:
            return flag(status_flag::zero);
        default:
            return false;
        }
    }

    void m6510::step_branch(const decoded& entry) {
        switch (tcu_) {
        case 1U:
            operand_ = read(registers_.pc++); // signed branch offset
            if (!branch_taken(entry.op)) {
                tcu_ = 0U; // not taken: 2 cycles
                return;
            }
            tcu_ = 2U;
            return;
        case 2U: {
            static_cast<void>(read(registers_.pc)); // dummy fetch of next opcode
            const auto offset = static_cast<std::int8_t>(operand_);
            const std::uint16_t old_pc = registers_.pc;
            const auto target = static_cast<std::uint16_t>(static_cast<int>(old_pc) + offset);
            if ((old_pc & 0xFF00U) == (target & 0xFF00U)) {
                registers_.pc = target;
                tcu_ = 0U; // same page: 3 cycles
                return;
            }
            // Page crossed: keep the wrong high byte for one more (dummy) cycle.
            registers_.pc = static_cast<std::uint16_t>((old_pc & 0xFF00U) | (target & 0x00FFU));
            ea_ = target;
            tcu_ = 3U;
            return;
        }
        default:
            static_cast<void>(read(registers_.pc)); // dummy read at the wrong page
            registers_.pc = ea_;
            tcu_ = 0U; // page cross: 4 cycles
            return;
        }
    }

    void m6510::step_jump(const decoded& entry) {
        switch (entry.op) {
        case operation::jmp:
            if (entry.mode == addressing_mode::absolute) {
                switch (tcu_) {
                case 1U:
                    operand_ = read(registers_.pc++);
                    tcu_ = 2U;
                    return;
                default:
                    registers_.pc = static_cast<std::uint16_t>(
                        operand_ | static_cast<std::uint16_t>(read(registers_.pc) << 8U));
                    tcu_ = 0U;
                    return;
                }
            }
            // JMP (indirect), with the page-boundary high-byte bug.
            switch (tcu_) {
            case 1U:
                ea_ = read(registers_.pc++);
                tcu_ = 2U;
                return;
            case 2U:
                ea_ = static_cast<std::uint16_t>(
                    ea_ | static_cast<std::uint16_t>(read(registers_.pc++) << 8U));
                tcu_ = 3U;
                return;
            case 3U:
                operand_ = read(ea_); // target low byte
                tcu_ = 4U;
                return;
            default: {
                const auto hi_addr =
                    static_cast<std::uint16_t>((ea_ & 0xFF00U) | ((ea_ + 1U) & 0x00FFU));
                const auto high = static_cast<std::uint16_t>(read(hi_addr));
                registers_.pc =
                    static_cast<std::uint16_t>(operand_ | static_cast<std::uint16_t>(high << 8U));
                tcu_ = 0U;
                return;
            }
            }

        case operation::jsr:
            switch (tcu_) {
            case 1U:
                operand_ = read(registers_.pc++); // target low byte
                tcu_ = 2U;
                return;
            case 2U:
                static_cast<void>(read(stack_address())); // internal cycle
                tcu_ = 3U;
                return;
            case 3U:
                push(static_cast<std::uint8_t>(registers_.pc >> 8U)); // PCH (return - 1)
                tcu_ = 4U;
                return;
            case 4U:
                push(static_cast<std::uint8_t>(registers_.pc & 0xFFU)); // PCL
                tcu_ = 5U;
                return;
            default: {
                const auto high = static_cast<std::uint16_t>(read(registers_.pc));
                registers_.pc =
                    static_cast<std::uint16_t>(operand_ | static_cast<std::uint16_t>(high << 8U));
                tcu_ = 0U;
                return;
            }
            }

        case operation::rts:
            switch (tcu_) {
            case 1U:
                static_cast<void>(read(registers_.pc));
                tcu_ = 2U;
                return;
            case 2U:
                static_cast<void>(read(stack_address()));
                registers_.sp = static_cast<std::uint8_t>(registers_.sp + 1U);
                tcu_ = 3U;
                return;
            case 3U:
                operand_ = read(stack_address()); // PCL
                registers_.sp = static_cast<std::uint8_t>(registers_.sp + 1U);
                tcu_ = 4U;
                return;
            case 4U:
                registers_.pc = static_cast<std::uint16_t>(
                    operand_ | static_cast<std::uint16_t>(read(stack_address()) << 8U)); // PCH
                tcu_ = 5U;
                return;
            default:
                static_cast<void>(read(registers_.pc));
                registers_.pc++;
                tcu_ = 0U;
                return;
            }

        case operation::rti:
            switch (tcu_) {
            case 1U:
                static_cast<void>(read(registers_.pc));
                tcu_ = 2U;
                return;
            case 2U:
                static_cast<void>(read(stack_address()));
                registers_.sp = static_cast<std::uint8_t>(registers_.sp + 1U);
                tcu_ = 3U;
                return;
            case 3U:
                registers_.p = static_cast<std::uint8_t>((read(stack_address()) & 0xEFU) | 0x20U);
                registers_.sp = static_cast<std::uint8_t>(registers_.sp + 1U);
                tcu_ = 4U;
                return;
            case 4U:
                operand_ = read(stack_address()); // PCL
                registers_.sp = static_cast<std::uint8_t>(registers_.sp + 1U);
                tcu_ = 5U;
                return;
            default:
                registers_.pc = static_cast<std::uint16_t>(
                    operand_ | static_cast<std::uint16_t>(read(stack_address()) << 8U)); // PCH
                tcu_ = 0U;
                return;
            }

        case operation::brk:
            switch (tcu_) {
            case 1U:
                static_cast<void>(read(registers_.pc)); // BRK is a 2-byte instruction
                registers_.pc++;
                tcu_ = 2U;
                return;
            case 2U:
                push(static_cast<std::uint8_t>(registers_.pc >> 8U)); // PCH
                tcu_ = 3U;
                return;
            case 3U:
                push(static_cast<std::uint8_t>(registers_.pc & 0xFFU)); // PCL
                tcu_ = 4U;
                return;
            case 4U:
                push(static_cast<std::uint8_t>(registers_.p | 0x30U)); // P with B set
                tcu_ = 5U;
                return;
            case 5U:
                operand_ = read(0xFFFEU); // IRQ/BRK vector low
                tcu_ = 6U;
                return;
            default:
                registers_.pc = static_cast<std::uint16_t>(
                    operand_ | static_cast<std::uint16_t>(read(0xFFFFU) << 8U));
                set_flag(status_flag::irq_disable, true);
                tcu_ = 0U;
                return;
            }

        default:
            tcu_ = 0U;
            return;
        }
    }

    void m6510::step_interrupt() {
        // Cycles 2-7 of the IRQ/NMI sequence (cycle 1 was the boundary dummy
        // read). Pushes PC and P (with B clear), then loads the vector and sets I.
        switch (tcu_) {
        case 1U:
            static_cast<void>(read(registers_.pc)); // second dummy read
            tcu_ = 2U;
            return;
        case 2U:
            push(static_cast<std::uint8_t>(registers_.pc >> 8U)); // PCH
            tcu_ = 3U;
            return;
        case 3U:
            push(static_cast<std::uint8_t>(registers_.pc & 0xFFU)); // PCL
            tcu_ = 4U;
            return;
        case 4U:
            push(static_cast<std::uint8_t>((registers_.p & 0xEFU) | 0x20U)); // P, B clear
            tcu_ = 5U;
            return;
        case 5U:
            operand_ = read(interrupt_vector_); // vector low
            tcu_ = 6U;
            return;
        default: {
            const auto high = static_cast<std::uint16_t>(
                read(static_cast<std::uint16_t>(interrupt_vector_ + 1U)));
            registers_.pc =
                static_cast<std::uint16_t>(operand_ | static_cast<std::uint16_t>(high << 8U));
            set_flag(status_flag::irq_disable, true);
            in_interrupt_ = false;
            tcu_ = 0U;
            return;
        }
        }
    }

    void m6510::set_nz(std::uint8_t value) noexcept {
        set_flag(status_flag::zero, value == 0U);
        set_flag(status_flag::negative, (value & 0x80U) != 0U);
    }

    void m6510::reset(reset_kind kind) {
        // Functional reset: power-on starts from a cleared register file, while a
        // hard/soft reset preserves A/X/Y as the NMOS part does. Cycle-accurate
        // RES sequencing is part of the interrupt-handling task.
        if (kind == reset_kind::power_on) {
            registers_ = registers{};
        }

        registers_.sp = 0xFDU;
        set_flag(status_flag::irq_disable, true);
        set_flag(status_flag::unused, true);
        cycles_ = 0U;
        tcu_ = 0U;
        in_interrupt_ = false;
        nmi_pending_ = false;
        port_fade_cycle_ = {};
        port_fade_value_ = {};

        if (bus_ != nullptr) {
            const auto low = static_cast<std::uint16_t>(bus_->read8(reset_vector));
            const auto high = static_cast<std::uint16_t>(bus_->read8(reset_vector + 1U));
            registers_.pc =
                static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
        }
    }

    void m6510::save_state(state_writer& writer) const {
        writer.u8(registers_.a);
        writer.u8(registers_.x);
        writer.u8(registers_.y);
        writer.u8(registers_.sp);
        writer.u8(registers_.p);
        writer.u16(registers_.pc);
        writer.u64(cycles_);
        writer.boolean(port_enabled_);
        writer.u8(port_ddr_);
        writer.u8(port_data_);
        writer.u8(port_input_);
        writer.u64(port_fade_cycle_[0]);
        writer.u64(port_fade_cycle_[1]);
        writer.boolean(port_fade_value_[0]);
        writer.boolean(port_fade_value_[1]);
        // In-progress instruction + interrupt sequencing.
        writer.u8(ir_);
        writer.u8(tcu_);
        writer.u16(ea_);
        writer.u8(operand_);
        writer.u8(ptr_);
        writer.boolean(page_cross_);
        writer.boolean(irq_line_);
        writer.boolean(nmi_line_);
        writer.boolean(nmi_pending_);
        writer.boolean(in_interrupt_);
        writer.u16(interrupt_vector_);
    }

    void m6510::load_state(state_reader& reader) {
        registers_.a = reader.u8();
        registers_.x = reader.u8();
        registers_.y = reader.u8();
        registers_.sp = reader.u8();
        registers_.p = reader.u8();
        registers_.pc = reader.u16();
        cycles_ = reader.u64();
        port_enabled_ = reader.boolean();
        port_ddr_ = reader.u8();
        port_data_ = reader.u8();
        port_input_ = reader.u8();
        port_fade_cycle_[0] = reader.u64();
        port_fade_cycle_[1] = reader.u64();
        port_fade_value_[0] = reader.boolean();
        port_fade_value_[1] = reader.boolean();
        ir_ = reader.u8();
        tcu_ = reader.u8();
        ea_ = reader.u16();
        operand_ = reader.u8();
        ptr_ = reader.u8();
        page_cross_ = reader.boolean();
        irq_line_ = reader.boolean();
        nmi_line_ = reader.boolean();
        nmi_pending_ = reader.boolean();
        in_interrupt_ = reader.boolean();
        interrupt_vector_ = reader.u16();
    }

    instrumentation::ichip_introspection& m6510::introspection() noexcept { return introspection_; }

    std::span<const register_descriptor> m6510::register_snapshot() noexcept {
        register_view_[0] = {"A", registers_.a, 8U, register_value_format::unsigned_integer};
        register_view_[1] = {"X", registers_.x, 8U, register_value_format::unsigned_integer};
        register_view_[2] = {"Y", registers_.y, 8U, register_value_format::unsigned_integer};
        register_view_[3] = {"SP", registers_.sp, 8U, register_value_format::unsigned_integer};
        register_view_[4] = {"P", registers_.p, 8U, register_value_format::flags};
        register_view_[5] = {"PC", registers_.pc, 16U, register_value_format::unsigned_integer};
        return register_view_;
    }

    void m6510::attach_bus(ibus& bus) noexcept { bus_ = &bus; }

    std::uint8_t m6510::read(std::uint16_t address) noexcept {
        if (port_enabled_ && address == 0x0000U) {
            return port_ddr_;
        }
        if (port_enabled_ && address == 0x0001U) {
            const auto outputs = static_cast<std::uint8_t>(port_data_ & port_ddr_);
            auto inputs =
                static_cast<std::uint8_t>(port_input_ & static_cast<std::uint8_t>(~port_ddr_));
            // Bits 6/7 are unconnected: as inputs they read the fading charge (last
            // driven value, decaying to 0), not the pull-up.
            for (unsigned b = 6U; b <= 7U; ++b) {
                const auto bit = static_cast<std::uint8_t>(1U << b);
                if ((port_ddr_ & bit) != 0U) {
                    continue; // driven as output
                }
                inputs = static_cast<std::uint8_t>(inputs & ~bit);
                const std::size_t i = b - 6U;
                if (port_fade_value_[i] && (cycles_ - port_fade_cycle_[i]) < port_falloff_cycles) {
                    inputs = static_cast<std::uint8_t>(inputs | bit);
                }
            }
            return static_cast<std::uint8_t>(outputs | inputs);
        }
        return bus_ != nullptr ? bus_->read8(address) : static_cast<std::uint8_t>(0U);
    }

    void m6510::write(std::uint16_t address, std::uint8_t value) noexcept {
        if (port_enabled_ && address == 0x0000U) {
            port_ddr_ = value;
            // A bit switched to input latches the current driven charge to fade from.
            for (unsigned b = 6U; b <= 7U; ++b) {
                const auto bit = static_cast<std::uint8_t>(1U << b);
                if ((value & bit) == 0U) {
                    const std::size_t i = b - 6U;
                    port_fade_value_[i] = (port_data_ & bit) != 0U;
                    port_fade_cycle_[i] = cycles_;
                }
            }
            return;
        }
        if (port_enabled_ && address == 0x0001U) {
            port_data_ = value;
            // While a bit is an output it (re)charges the pin to the written value.
            for (unsigned b = 6U; b <= 7U; ++b) {
                const auto bit = static_cast<std::uint8_t>(1U << b);
                if ((port_ddr_ & bit) != 0U) {
                    const std::size_t i = b - 6U;
                    port_fade_value_[i] = (value & bit) != 0U;
                    port_fade_cycle_[i] = cycles_;
                }
            }
            return;
        }
        if (bus_ != nullptr) {
            bus_->write8(address, value);
        }
    }

    void m6510::set_registers(const registers& values) noexcept {
        registers_ = values;
        tcu_ = 0U;
        in_interrupt_ = false;
    }

    void m6510::set_port_enabled(bool enabled) noexcept { port_enabled_ = enabled; }

    void m6510::set_port_input(std::uint8_t value) noexcept { port_input_ = value; }

    void m6510::set_irq_line(bool asserted) noexcept { irq_line_ = asserted; }

    void m6510::set_nmi_line(bool asserted) noexcept {
        if (asserted && !nmi_line_) {
            nmi_pending_ = true; // latch on the inactive->active edge
        }
        nmi_line_ = asserted;
    }

    bool m6510::flag(status_flag bit) const noexcept {
        const auto mask = static_cast<std::uint8_t>(1U << static_cast<unsigned>(bit));
        return (registers_.p & mask) != 0U;
    }

    void m6510::set_flag(status_flag bit, bool value) noexcept {
        const auto mask = static_cast<std::uint8_t>(1U << static_cast<unsigned>(bit));
        if (value) {
            registers_.p = static_cast<std::uint8_t>(registers_.p | mask);
        } else {
            registers_.p =
                static_cast<std::uint8_t>(registers_.p & static_cast<std::uint8_t>(~mask));
        }
    }

    m6510::introspection_surface::introspection_surface(m6510& owner) noexcept
        : trace_impl_(owner), registers_impl_(owner) {}

    void m6510::introspection_surface::trace_impl::install(callback cb) {
        if (cb) {
            // Wrap the generic (pc + cycles) trace_target callback onto the
            // 6510's PC-only trace_callback_ slot; cycles are queried at fire
            // time from the chip's own elapsed counter.
            m6510* cpu = owner_;
            owner_->trace_callback_ = [cpu, cb = std::move(cb)](std::uint32_t pc) {
                cb({.pc = pc, .cycles = cpu->elapsed_cycles()});
            };
        } else {
            owner_->trace_callback_ = {};
        }
    }

    std::span<const register_descriptor> m6510::introspection_surface::registers_impl::registers() {
        return owner_->register_snapshot();
    }

    namespace {
        [[maybe_unused]] const auto m6510_registration =
            register_factory("mos.6510", chip_class::cpu,
                             []() -> std::unique_ptr<ichip> { return std::make_unique<m6510>(); });
    } // namespace

} // namespace mnemos::chips::cpu
