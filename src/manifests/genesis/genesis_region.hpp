#pragma once

// Genesis / Mega Drive cartridge-header region parsing.
//
// The cart's country field at $1F0..$1F3 encodes which markets the cart was
// authorised for, either as 1-4 ASCII letters {J, U, E, K} or as a single hex
// digit whose bits encode the same markets (bit 0 = Japan, bit 2 = USA,
// bit 3 = Europe). This module turns those bytes into the project-wide
// `mnemos::market` enum.
//
// The Sega 16-bit family (Genesis / 32X / Sega CD) shares this header layout;
// the 32X and Sega CD adapters can reuse the same parser by including this
// header (or by reusing the same letter table).

#include "region.hpp" // mnemos::market

#include <cstdint>
#include <span>

namespace mnemos::manifests::genesis {

    // Parse the cart-header country field at $1F0..$1F3 and return the
    // categorical market. Truncated or unrecognised headers return
    // `market::unknown`.
    [[nodiscard]] mnemos::market parse_market(std::span<const std::uint8_t> rom) noexcept;

} // namespace mnemos::manifests::genesis
