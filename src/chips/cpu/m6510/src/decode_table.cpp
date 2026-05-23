#include <mnemos/chips/cpu/m6510/decode_table.hpp>

namespace mnemos::chips::cpu {
    namespace {

        using op = m6510::operation;
        using mode = m6510::addressing_mode;
        using kind = m6510::access_kind;

        std::array<decoded, 256> build_decode_table() {
            std::array<decoded, 256> table{};

            table[0xEAU] = decoded{op::nop, mode::implied, kind::implied, false};

            table[0xA9U] = decoded{op::lda, mode::immediate, kind::read, false};
            table[0xA5U] = decoded{op::lda, mode::zero_page, kind::read, false};
            table[0xADU] = decoded{op::lda, mode::absolute, kind::read, false};

            return table;
        }

    } // namespace

    const std::array<decoded, 256>& decode_table() noexcept {
        static const std::array<decoded, 256> table = build_decode_table();
        return table;
    }

} // namespace mnemos::chips::cpu
