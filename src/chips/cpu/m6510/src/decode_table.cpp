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

            return table;
        }

    } // namespace

    const std::array<decoded, 256>& decode_table() noexcept {
        static const std::array<decoded, 256> table = build_decode_table();
        return table;
    }

} // namespace mnemos::chips::cpu
