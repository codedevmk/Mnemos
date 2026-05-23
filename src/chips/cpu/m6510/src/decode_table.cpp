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

            return table;
        }

    } // namespace

    const std::array<decoded, 256>& decode_table() noexcept {
        static const std::array<decoded, 256> table = build_decode_table();
        return table;
    }

} // namespace mnemos::chips::cpu
