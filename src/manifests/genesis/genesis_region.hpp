#pragma once

#include "region.hpp"

#include <cstdint>
#include <span>

namespace mnemos::manifests::genesis {

    // Country field at $1F0..$1F3: 1-4 ASCII letters {J, U, E, K} or a hex
    // digit (bit 0 = Japan, bit 2 = USA, bit 3 = Europe).
    [[nodiscard]] mnemos::market parse_market(std::span<const std::uint8_t> rom) noexcept;

} // namespace mnemos::manifests::genesis
