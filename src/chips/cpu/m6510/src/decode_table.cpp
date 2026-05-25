#include <mnemos/chips/cpu/m6510/decode_table.hpp>

namespace mnemos::chips::cpu {
    namespace {

        using op = m6510::operation;
        using mode = m6510::addressing_mode;
        using kind = m6510::access_kind;

        std::array<decoded, 256> build_decode_table() {
            std::array<decoded, 256> table{};

            table[0xEAU] = decoded{op::nop, mode::implied, kind::implied, false};

            // LDA
            table[0xA9U] = decoded{op::lda, mode::immediate, kind::read, false};
            table[0xA5U] = decoded{op::lda, mode::zero_page, kind::read, false};
            table[0xB5U] = decoded{op::lda, mode::zero_page_x, kind::read, false};
            table[0xADU] = decoded{op::lda, mode::absolute, kind::read, false};
            table[0xBDU] = decoded{op::lda, mode::absolute_x, kind::read, false};
            table[0xB9U] = decoded{op::lda, mode::absolute_y, kind::read, false};
            table[0xA1U] = decoded{op::lda, mode::indexed_indirect, kind::read, false};
            table[0xB1U] = decoded{op::lda, mode::indirect_indexed, kind::read, false};

            // LDX
            table[0xA2U] = decoded{op::ldx, mode::immediate, kind::read, false};
            table[0xA6U] = decoded{op::ldx, mode::zero_page, kind::read, false};
            table[0xB6U] = decoded{op::ldx, mode::zero_page_y, kind::read, false};
            table[0xAEU] = decoded{op::ldx, mode::absolute, kind::read, false};
            table[0xBEU] = decoded{op::ldx, mode::absolute_y, kind::read, false};

            // LDY
            table[0xA0U] = decoded{op::ldy, mode::immediate, kind::read, false};
            table[0xA4U] = decoded{op::ldy, mode::zero_page, kind::read, false};
            table[0xB4U] = decoded{op::ldy, mode::zero_page_x, kind::read, false};
            table[0xACU] = decoded{op::ldy, mode::absolute, kind::read, false};
            table[0xBCU] = decoded{op::ldy, mode::absolute_x, kind::read, false};

            // STA
            table[0x85U] = decoded{op::sta, mode::zero_page, kind::write, false};
            table[0x95U] = decoded{op::sta, mode::zero_page_x, kind::write, false};
            table[0x8DU] = decoded{op::sta, mode::absolute, kind::write, false};
            table[0x9DU] = decoded{op::sta, mode::absolute_x, kind::write, false};
            table[0x99U] = decoded{op::sta, mode::absolute_y, kind::write, false};
            table[0x81U] = decoded{op::sta, mode::indexed_indirect, kind::write, false};
            table[0x91U] = decoded{op::sta, mode::indirect_indexed, kind::write, false};

            // STX
            table[0x86U] = decoded{op::stx, mode::zero_page, kind::write, false};
            table[0x96U] = decoded{op::stx, mode::zero_page_y, kind::write, false};
            table[0x8EU] = decoded{op::stx, mode::absolute, kind::write, false};

            // STY
            table[0x84U] = decoded{op::sty, mode::zero_page, kind::write, false};
            table[0x94U] = decoded{op::sty, mode::zero_page_x, kind::write, false};
            table[0x8CU] = decoded{op::sty, mode::absolute, kind::write, false};

            // Register transfers (implied, 2 cycles)
            table[0xAAU] = decoded{op::tax, mode::implied, kind::implied, false};
            table[0xA8U] = decoded{op::tay, mode::implied, kind::implied, false};
            table[0x8AU] = decoded{op::txa, mode::implied, kind::implied, false};
            table[0x98U] = decoded{op::tya, mode::implied, kind::implied, false};
            table[0xBAU] = decoded{op::tsx, mode::implied, kind::implied, false};
            table[0x9AU] = decoded{op::txs, mode::implied, kind::implied, false};

            // Index register increment/decrement (implied)
            table[0xE8U] = decoded{op::inx, mode::implied, kind::implied, false};
            table[0xC8U] = decoded{op::iny, mode::implied, kind::implied, false};
            table[0xCAU] = decoded{op::dex, mode::implied, kind::implied, false};
            table[0x88U] = decoded{op::dey, mode::implied, kind::implied, false};

            // Stack
            table[0x48U] = decoded{op::pha, mode::implied, kind::stack, false};
            table[0x08U] = decoded{op::php, mode::implied, kind::stack, false};
            table[0x68U] = decoded{op::pla, mode::implied, kind::stack, false};
            table[0x28U] = decoded{op::plp, mode::implied, kind::stack, false};

            // AND
            table[0x29U] = decoded{op::and_, mode::immediate, kind::read, false};
            table[0x25U] = decoded{op::and_, mode::zero_page, kind::read, false};
            table[0x35U] = decoded{op::and_, mode::zero_page_x, kind::read, false};
            table[0x2DU] = decoded{op::and_, mode::absolute, kind::read, false};
            table[0x3DU] = decoded{op::and_, mode::absolute_x, kind::read, false};
            table[0x39U] = decoded{op::and_, mode::absolute_y, kind::read, false};
            table[0x21U] = decoded{op::and_, mode::indexed_indirect, kind::read, false};
            table[0x31U] = decoded{op::and_, mode::indirect_indexed, kind::read, false};

            // ORA
            table[0x09U] = decoded{op::ora, mode::immediate, kind::read, false};
            table[0x05U] = decoded{op::ora, mode::zero_page, kind::read, false};
            table[0x15U] = decoded{op::ora, mode::zero_page_x, kind::read, false};
            table[0x0DU] = decoded{op::ora, mode::absolute, kind::read, false};
            table[0x1DU] = decoded{op::ora, mode::absolute_x, kind::read, false};
            table[0x19U] = decoded{op::ora, mode::absolute_y, kind::read, false};
            table[0x01U] = decoded{op::ora, mode::indexed_indirect, kind::read, false};
            table[0x11U] = decoded{op::ora, mode::indirect_indexed, kind::read, false};

            // EOR
            table[0x49U] = decoded{op::eor, mode::immediate, kind::read, false};
            table[0x45U] = decoded{op::eor, mode::zero_page, kind::read, false};
            table[0x55U] = decoded{op::eor, mode::zero_page_x, kind::read, false};
            table[0x4DU] = decoded{op::eor, mode::absolute, kind::read, false};
            table[0x5DU] = decoded{op::eor, mode::absolute_x, kind::read, false};
            table[0x59U] = decoded{op::eor, mode::absolute_y, kind::read, false};
            table[0x41U] = decoded{op::eor, mode::indexed_indirect, kind::read, false};
            table[0x51U] = decoded{op::eor, mode::indirect_indexed, kind::read, false};

            // ADC
            table[0x69U] = decoded{op::adc, mode::immediate, kind::read, false};
            table[0x65U] = decoded{op::adc, mode::zero_page, kind::read, false};
            table[0x75U] = decoded{op::adc, mode::zero_page_x, kind::read, false};
            table[0x6DU] = decoded{op::adc, mode::absolute, kind::read, false};
            table[0x7DU] = decoded{op::adc, mode::absolute_x, kind::read, false};
            table[0x79U] = decoded{op::adc, mode::absolute_y, kind::read, false};
            table[0x61U] = decoded{op::adc, mode::indexed_indirect, kind::read, false};
            table[0x71U] = decoded{op::adc, mode::indirect_indexed, kind::read, false};

            // SBC
            table[0xE9U] = decoded{op::sbc, mode::immediate, kind::read, false};
            table[0xE5U] = decoded{op::sbc, mode::zero_page, kind::read, false};
            table[0xF5U] = decoded{op::sbc, mode::zero_page_x, kind::read, false};
            table[0xEDU] = decoded{op::sbc, mode::absolute, kind::read, false};
            table[0xFDU] = decoded{op::sbc, mode::absolute_x, kind::read, false};
            table[0xF9U] = decoded{op::sbc, mode::absolute_y, kind::read, false};
            table[0xE1U] = decoded{op::sbc, mode::indexed_indirect, kind::read, false};
            table[0xF1U] = decoded{op::sbc, mode::indirect_indexed, kind::read, false};

            // CMP
            table[0xC9U] = decoded{op::cmp, mode::immediate, kind::read, false};
            table[0xC5U] = decoded{op::cmp, mode::zero_page, kind::read, false};
            table[0xD5U] = decoded{op::cmp, mode::zero_page_x, kind::read, false};
            table[0xCDU] = decoded{op::cmp, mode::absolute, kind::read, false};
            table[0xDDU] = decoded{op::cmp, mode::absolute_x, kind::read, false};
            table[0xD9U] = decoded{op::cmp, mode::absolute_y, kind::read, false};
            table[0xC1U] = decoded{op::cmp, mode::indexed_indirect, kind::read, false};
            table[0xD1U] = decoded{op::cmp, mode::indirect_indexed, kind::read, false};

            // CPX / CPY
            table[0xE0U] = decoded{op::cpx, mode::immediate, kind::read, false};
            table[0xE4U] = decoded{op::cpx, mode::zero_page, kind::read, false};
            table[0xECU] = decoded{op::cpx, mode::absolute, kind::read, false};
            table[0xC0U] = decoded{op::cpy, mode::immediate, kind::read, false};
            table[0xC4U] = decoded{op::cpy, mode::zero_page, kind::read, false};
            table[0xCCU] = decoded{op::cpy, mode::absolute, kind::read, false};

            // BIT
            table[0x24U] = decoded{op::bit, mode::zero_page, kind::read, false};
            table[0x2CU] = decoded{op::bit, mode::absolute, kind::read, false};

            // ASL
            table[0x0AU] = decoded{op::asl, mode::accumulator, kind::read_modify_write, false};
            table[0x06U] = decoded{op::asl, mode::zero_page, kind::read_modify_write, false};
            table[0x16U] = decoded{op::asl, mode::zero_page_x, kind::read_modify_write, false};
            table[0x0EU] = decoded{op::asl, mode::absolute, kind::read_modify_write, false};
            table[0x1EU] = decoded{op::asl, mode::absolute_x, kind::read_modify_write, false};

            // LSR
            table[0x4AU] = decoded{op::lsr, mode::accumulator, kind::read_modify_write, false};
            table[0x46U] = decoded{op::lsr, mode::zero_page, kind::read_modify_write, false};
            table[0x56U] = decoded{op::lsr, mode::zero_page_x, kind::read_modify_write, false};
            table[0x4EU] = decoded{op::lsr, mode::absolute, kind::read_modify_write, false};
            table[0x5EU] = decoded{op::lsr, mode::absolute_x, kind::read_modify_write, false};

            // ROL
            table[0x2AU] = decoded{op::rol, mode::accumulator, kind::read_modify_write, false};
            table[0x26U] = decoded{op::rol, mode::zero_page, kind::read_modify_write, false};
            table[0x36U] = decoded{op::rol, mode::zero_page_x, kind::read_modify_write, false};
            table[0x2EU] = decoded{op::rol, mode::absolute, kind::read_modify_write, false};
            table[0x3EU] = decoded{op::rol, mode::absolute_x, kind::read_modify_write, false};

            // ROR
            table[0x6AU] = decoded{op::ror, mode::accumulator, kind::read_modify_write, false};
            table[0x66U] = decoded{op::ror, mode::zero_page, kind::read_modify_write, false};
            table[0x76U] = decoded{op::ror, mode::zero_page_x, kind::read_modify_write, false};
            table[0x6EU] = decoded{op::ror, mode::absolute, kind::read_modify_write, false};
            table[0x7EU] = decoded{op::ror, mode::absolute_x, kind::read_modify_write, false};

            // INC
            table[0xE6U] = decoded{op::inc, mode::zero_page, kind::read_modify_write, false};
            table[0xF6U] = decoded{op::inc, mode::zero_page_x, kind::read_modify_write, false};
            table[0xEEU] = decoded{op::inc, mode::absolute, kind::read_modify_write, false};
            table[0xFEU] = decoded{op::inc, mode::absolute_x, kind::read_modify_write, false};

            // DEC
            table[0xC6U] = decoded{op::dec, mode::zero_page, kind::read_modify_write, false};
            table[0xD6U] = decoded{op::dec, mode::zero_page_x, kind::read_modify_write, false};
            table[0xCEU] = decoded{op::dec, mode::absolute, kind::read_modify_write, false};
            table[0xDEU] = decoded{op::dec, mode::absolute_x, kind::read_modify_write, false};

            // Flag operations (implied, 2 cycles)
            table[0x18U] = decoded{op::clc, mode::implied, kind::implied, false};
            table[0x38U] = decoded{op::sec, mode::implied, kind::implied, false};
            table[0x58U] = decoded{op::cli, mode::implied, kind::implied, false};
            table[0x78U] = decoded{op::sei, mode::implied, kind::implied, false};
            table[0xD8U] = decoded{op::cld, mode::implied, kind::implied, false};
            table[0xF8U] = decoded{op::sed, mode::implied, kind::implied, false};
            table[0xB8U] = decoded{op::clv, mode::implied, kind::implied, false};

            // Jumps and subroutine/interrupt control
            table[0x4CU] = decoded{op::jmp, mode::absolute, kind::jump, false};
            table[0x6CU] = decoded{op::jmp, mode::indirect, kind::jump, false};
            table[0x20U] = decoded{op::jsr, mode::absolute, kind::jump, false};
            table[0x60U] = decoded{op::rts, mode::implied, kind::jump, false};
            table[0x40U] = decoded{op::rti, mode::implied, kind::jump, false};
            table[0x00U] = decoded{op::brk, mode::implied, kind::jump, false};

            // Branches (relative)
            table[0x10U] = decoded{op::bpl, mode::relative, kind::relative, false};
            table[0x30U] = decoded{op::bmi, mode::relative, kind::relative, false};
            table[0x50U] = decoded{op::bvc, mode::relative, kind::relative, false};
            table[0x70U] = decoded{op::bvs, mode::relative, kind::relative, false};
            table[0x90U] = decoded{op::bcc, mode::relative, kind::relative, false};
            table[0xB0U] = decoded{op::bcs, mode::relative, kind::relative, false};
            table[0xD0U] = decoded{op::bne, mode::relative, kind::relative, false};
            table[0xF0U] = decoded{op::beq, mode::relative, kind::relative, false};

            // ---- Stable undocumented opcodes (illegal = true) ----

            // LAX (LDA + LDX)
            table[0xA7U] = decoded{op::lax, mode::zero_page, kind::read, true};
            table[0xB7U] = decoded{op::lax, mode::zero_page_y, kind::read, true};
            table[0xAFU] = decoded{op::lax, mode::absolute, kind::read, true};
            table[0xBFU] = decoded{op::lax, mode::absolute_y, kind::read, true};
            table[0xA3U] = decoded{op::lax, mode::indexed_indirect, kind::read, true};
            table[0xB3U] = decoded{op::lax, mode::indirect_indexed, kind::read, true};

            // SAX (store A & X)
            table[0x87U] = decoded{op::sax, mode::zero_page, kind::write, true};
            table[0x97U] = decoded{op::sax, mode::zero_page_y, kind::write, true};
            table[0x8FU] = decoded{op::sax, mode::absolute, kind::write, true};
            table[0x83U] = decoded{op::sax, mode::indexed_indirect, kind::write, true};

            // DCP (DEC + CMP)
            table[0xC7U] = decoded{op::dcp, mode::zero_page, kind::read_modify_write, true};
            table[0xD7U] = decoded{op::dcp, mode::zero_page_x, kind::read_modify_write, true};
            table[0xCFU] = decoded{op::dcp, mode::absolute, kind::read_modify_write, true};
            table[0xDFU] = decoded{op::dcp, mode::absolute_x, kind::read_modify_write, true};
            table[0xDBU] = decoded{op::dcp, mode::absolute_y, kind::read_modify_write, true};
            table[0xC3U] = decoded{op::dcp, mode::indexed_indirect, kind::read_modify_write, true};
            table[0xD3U] = decoded{op::dcp, mode::indirect_indexed, kind::read_modify_write, true};

            // ISC (INC + SBC)
            table[0xE7U] = decoded{op::isc, mode::zero_page, kind::read_modify_write, true};
            table[0xF7U] = decoded{op::isc, mode::zero_page_x, kind::read_modify_write, true};
            table[0xEFU] = decoded{op::isc, mode::absolute, kind::read_modify_write, true};
            table[0xFFU] = decoded{op::isc, mode::absolute_x, kind::read_modify_write, true};
            table[0xFBU] = decoded{op::isc, mode::absolute_y, kind::read_modify_write, true};
            table[0xE3U] = decoded{op::isc, mode::indexed_indirect, kind::read_modify_write, true};
            table[0xF3U] = decoded{op::isc, mode::indirect_indexed, kind::read_modify_write, true};

            // SLO (ASL + ORA)
            table[0x07U] = decoded{op::slo, mode::zero_page, kind::read_modify_write, true};
            table[0x17U] = decoded{op::slo, mode::zero_page_x, kind::read_modify_write, true};
            table[0x0FU] = decoded{op::slo, mode::absolute, kind::read_modify_write, true};
            table[0x1FU] = decoded{op::slo, mode::absolute_x, kind::read_modify_write, true};
            table[0x1BU] = decoded{op::slo, mode::absolute_y, kind::read_modify_write, true};
            table[0x03U] = decoded{op::slo, mode::indexed_indirect, kind::read_modify_write, true};
            table[0x13U] = decoded{op::slo, mode::indirect_indexed, kind::read_modify_write, true};

            // RLA (ROL + AND)
            table[0x27U] = decoded{op::rla, mode::zero_page, kind::read_modify_write, true};
            table[0x37U] = decoded{op::rla, mode::zero_page_x, kind::read_modify_write, true};
            table[0x2FU] = decoded{op::rla, mode::absolute, kind::read_modify_write, true};
            table[0x3FU] = decoded{op::rla, mode::absolute_x, kind::read_modify_write, true};
            table[0x3BU] = decoded{op::rla, mode::absolute_y, kind::read_modify_write, true};
            table[0x23U] = decoded{op::rla, mode::indexed_indirect, kind::read_modify_write, true};
            table[0x33U] = decoded{op::rla, mode::indirect_indexed, kind::read_modify_write, true};

            // SRE (LSR + EOR)
            table[0x47U] = decoded{op::sre, mode::zero_page, kind::read_modify_write, true};
            table[0x57U] = decoded{op::sre, mode::zero_page_x, kind::read_modify_write, true};
            table[0x4FU] = decoded{op::sre, mode::absolute, kind::read_modify_write, true};
            table[0x5FU] = decoded{op::sre, mode::absolute_x, kind::read_modify_write, true};
            table[0x5BU] = decoded{op::sre, mode::absolute_y, kind::read_modify_write, true};
            table[0x43U] = decoded{op::sre, mode::indexed_indirect, kind::read_modify_write, true};
            table[0x53U] = decoded{op::sre, mode::indirect_indexed, kind::read_modify_write, true};

            // RRA (ROR + ADC)
            table[0x67U] = decoded{op::rra, mode::zero_page, kind::read_modify_write, true};
            table[0x77U] = decoded{op::rra, mode::zero_page_x, kind::read_modify_write, true};
            table[0x6FU] = decoded{op::rra, mode::absolute, kind::read_modify_write, true};
            table[0x7FU] = decoded{op::rra, mode::absolute_x, kind::read_modify_write, true};
            table[0x7BU] = decoded{op::rra, mode::absolute_y, kind::read_modify_write, true};
            table[0x63U] = decoded{op::rra, mode::indexed_indirect, kind::read_modify_write, true};
            table[0x73U] = decoded{op::rra, mode::indirect_indexed, kind::read_modify_write, true};

            // Immediate-operand undocumented ALU ops
            table[0x0BU] = decoded{op::anc, mode::immediate, kind::read, true};
            table[0x2BU] = decoded{op::anc, mode::immediate, kind::read, true};
            table[0x4BU] = decoded{op::alr, mode::immediate, kind::read, true};
            table[0x6BU] = decoded{op::arr, mode::immediate, kind::read, true};
            table[0xCBU] = decoded{op::sbx, mode::immediate, kind::read, true};
            table[0xEBU] = decoded{op::sbc, mode::immediate, kind::read, true}; // alias of SBC #

            // Unstable undocumented opcodes.
            table[0x8BU] = decoded{op::ane, mode::immediate, kind::read, true};
            table[0xABU] = decoded{op::lxa, mode::immediate, kind::read, true};
            table[0xBBU] = decoded{op::las, mode::absolute_y, kind::read, true};
            table[0x9FU] = decoded{op::sha, mode::absolute_y, kind::write, true};
            table[0x93U] = decoded{op::sha, mode::indirect_indexed, kind::write, true};
            table[0x9EU] = decoded{op::shx, mode::absolute_y, kind::write, true};
            table[0x9CU] = decoded{op::shy, mode::absolute_x, kind::write, true};
            table[0x9BU] = decoded{op::tas, mode::absolute_y, kind::write, true};

            // Undocumented NOPs (no operation, but consume operand bytes/cycles)
            for (unsigned code : {0x1AU, 0x3AU, 0x5AU, 0x7AU, 0xDAU, 0xFAU}) {
                table[code] = decoded{op::nop, mode::implied, kind::implied, true};
            }
            for (unsigned code : {0x80U, 0x82U, 0x89U, 0xC2U, 0xE2U}) {
                table[code] = decoded{op::nop, mode::immediate, kind::read, true};
            }
            for (unsigned code : {0x04U, 0x44U, 0x64U}) {
                table[code] = decoded{op::nop, mode::zero_page, kind::read, true};
            }
            for (unsigned code : {0x14U, 0x34U, 0x54U, 0x74U, 0xD4U, 0xF4U}) {
                table[code] = decoded{op::nop, mode::zero_page_x, kind::read, true};
            }
            table[0x0CU] = decoded{op::nop, mode::absolute, kind::read, true};
            for (unsigned code : {0x1CU, 0x3CU, 0x5CU, 0x7CU, 0xDCU, 0xFCU}) {
                table[code] = decoded{op::nop, mode::absolute_x, kind::read, true};
            }

            return table;
        }

    } // namespace

    const std::array<decoded, 256>& decode_table() noexcept {
        static const std::array<decoded, 256> table = build_decode_table();
        return table;
    }

} // namespace mnemos::chips::cpu
