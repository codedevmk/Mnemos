#include <mnemos/chips/cpu/m6510.hpp>

#include <mnemos/chips/cpu/m6510/decode_table.hpp>

#include <mnemos/chips/common/bus.hpp>
#include <mnemos/chips/common/chip_registry.hpp>

#include <memory>

namespace mnemos::chips::cpu {
    namespace {
        [[nodiscard]] bool crosses_page(std::uint16_t base, std::uint16_t addr) noexcept {
            return ((base ^ addr) & 0xFF00U) != 0U;
        }
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
                const auto base = static_cast<std::uint16_t>(
                    ea_ | static_cast<std::uint16_t>(read(registers_.pc++) << 8U));
                ea_ = static_cast<std::uint16_t>(base + index);
                page_cross_ = crosses_page(base, ea_);
                tcu_ = 3U;
                return;
            }
            case 3U:
                if (page_cross_) {
                    static_cast<void>(read(ea_)); // extra cycle on page crossing
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
                const auto base = static_cast<std::uint16_t>(
                    ea_ |
                    static_cast<std::uint16_t>(read(static_cast<std::uint8_t>(ptr_ + 1U)) << 8U));
                ea_ = static_cast<std::uint16_t>(base + registers_.y);
                page_cross_ = crosses_page(base, ea_);
                tcu_ = 4U;
                return;
            }
            case 4U:
                if (page_cross_) {
                    static_cast<void>(read(ea_));
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
                const auto base = static_cast<std::uint16_t>(
                    ea_ | static_cast<std::uint16_t>(read(registers_.pc++) << 8U));
                ea_ = static_cast<std::uint16_t>(base + index);
                tcu_ = 3U;
                return;
            }
            case 3U:
                static_cast<void>(read(ea_)); // indexed stores always pay the fixup cycle
                tcu_ = 4U;
                return;
            default:
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
                const auto base = static_cast<std::uint16_t>(
                    ea_ |
                    static_cast<std::uint16_t>(read(static_cast<std::uint8_t>(ptr_ + 1U)) << 8U));
                ea_ = static_cast<std::uint16_t>(base + registers_.y);
                tcu_ = 4U;
                return;
            }
            case 4U:
                static_cast<void>(read(ea_)); // always pays the fixup cycle
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
        switch (op) {
        case operation::stx:
            return registers_.x;
        case operation::sty:
            return registers_.y;
        case operation::sax: // store A & X (no flags)
            return static_cast<std::uint8_t>(registers_.a & registers_.x);
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
        case operation::arr: { // AND #imm, then ROR A with special C/V
            registers_.a = static_cast<std::uint8_t>(registers_.a & operand_);
            const auto carry_in = static_cast<std::uint8_t>(flag(status_flag::carry) ? 0x80U : 0U);
            registers_.a = static_cast<std::uint8_t>((registers_.a >> 1U) | carry_in);
            set_nz(registers_.a);
            set_flag(status_flag::carry, (registers_.a & 0x40U) != 0U);
            set_flag(status_flag::overflow,
                     (((registers_.a >> 6U) ^ (registers_.a >> 5U)) & 0x01U) != 0U);
            return;
        }
        case operation::sbx: { // X = (A & X) - imm, sets C like CMP
            const auto t = static_cast<unsigned>(registers_.a & registers_.x);
            set_flag(status_flag::carry, t >= operand_);
            registers_.x = static_cast<std::uint8_t>(t - operand_);
            set_nz(registers_.x);
            return;
        }
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
                const auto base = static_cast<std::uint16_t>(
                    ea_ | static_cast<std::uint16_t>(read(registers_.pc++) << 8U));
                ea_ = static_cast<std::uint16_t>(base + index);
                tcu_ = 3U;
                return;
            }
            case 3U:
                static_cast<void>(read(ea_)); // fixup cycle (always paid)
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
                const auto base = static_cast<std::uint16_t>(
                    ea_ |
                    static_cast<std::uint16_t>(read(static_cast<std::uint8_t>(ptr_ + 1U)) << 8U));
                ea_ = static_cast<std::uint16_t>(base + registers_.y);
                tcu_ = 4U;
                return;
            }
            case 4U:
                static_cast<void>(read(ea_)); // fixup cycle (always paid)
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

        if (bus_ != nullptr) {
            const auto low = static_cast<std::uint16_t>(bus_->read8(reset_vector));
            const auto high = static_cast<std::uint16_t>(bus_->read8(reset_vector + 1U));
            registers_.pc =
                static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
        }
    }

    void m6510::save_state(state_writer& /*writer*/) const {
        // Serialization lands with the M1 save/load-state task.
    }

    void m6510::load_state(state_reader& /*reader*/) {
        // Deserialization lands with the M1 save/load-state task.
    }

    instrumentation::i_chip_introspection& m6510::introspection() noexcept {
        return introspection_;
    }

    void m6510::attach_bus(i_bus& bus) noexcept { bus_ = &bus; }

    std::uint8_t m6510::read(std::uint16_t address) noexcept {
        if (address == 0x0000U) {
            return port_ddr_;
        }
        if (address == 0x0001U) {
            const auto outputs = static_cast<std::uint8_t>(port_data_ & port_ddr_);
            const auto inputs =
                static_cast<std::uint8_t>(port_input_pull & static_cast<std::uint8_t>(~port_ddr_));
            return static_cast<std::uint8_t>(outputs | inputs);
        }
        return bus_ != nullptr ? bus_->read8(address) : static_cast<std::uint8_t>(0U);
    }

    void m6510::write(std::uint16_t address, std::uint8_t value) noexcept {
        if (address == 0x0000U) {
            port_ddr_ = value;
            return;
        }
        if (address == 0x0001U) {
            port_data_ = value;
            return;
        }
        if (bus_ != nullptr) {
            bus_->write8(address, value);
        }
    }

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

    namespace {
        [[maybe_unused]] const auto m6510_registration =
            register_factory("mos.6510", chip_class::cpu,
                             []() -> std::unique_ptr<i_chip> { return std::make_unique<m6510>(); });
    } // namespace

} // namespace mnemos::chips::cpu
