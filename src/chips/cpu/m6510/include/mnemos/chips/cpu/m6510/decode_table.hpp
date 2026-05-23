#pragma once

#include <mnemos/chips/cpu/m6510.hpp>

#include <array>

namespace mnemos::chips::cpu {

    // One decoded opcode: the operation, its addressing mode, the access pattern
    // that drives cycle timing, and whether it is an undocumented opcode. The
    // defaults describe an undecoded/illegal slot.
    struct decoded final {
        m6510::operation op{m6510::operation::kil};
        m6510::addressing_mode mode{m6510::addressing_mode::implied};
        m6510::access_kind kind{m6510::access_kind::other};
        bool illegal{true};
    };

    // The 256-entry opcode decode table, indexed by opcode byte.
    [[nodiscard]] const std::array<decoded, 256>& decode_table() noexcept;

} // namespace mnemos::chips::cpu
