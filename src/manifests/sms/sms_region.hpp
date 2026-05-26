#pragma once

// SMS / Game Gear cartridge-header region parsing.
//
// The high nibble of $7FFF (the trailing byte of the optional TMR SEGA header
// at $7FF0..$7FFF) encodes both the target physical system (SMS vs Game Gear)
// and the cart's market in one field. This module turns that nibble into the
// project-wide `mnemos::market` plus an SMS-family `cart_target` enum.
//
// The SMS adapter is free to refuse to boot a GG-targeted cart (different
// VDP); the GG adapter will reuse the same parser when it lands.

#include "region.hpp" // mnemos::market

#include <cstdint>
#include <span>

namespace mnemos::manifests::sms {

    // Which physical system the cart was made for.
    enum class cart_target : std::uint8_t {
        sms, // Sega Master System / Mark III
        gg,  // Game Gear
        unknown,
    };

    // Combined cart-side metadata: where it targets (market) and what it
    // targets (cart_target). The two come from the same byte; returning both
    // means callers don't re-parse the byte for each question.
    struct cart_info final {
        mnemos::market market;
        cart_target target;
    };

    // Parse the country nibble at the high 4 bits of $7FFF. Truncated or
    // unrecognised headers return `{market::unknown, cart_target::unknown}`.
    [[nodiscard]] cart_info parse_cart_info(std::span<const std::uint8_t> rom) noexcept;

    // Convenience: market-only view of parse_cart_info() for callers that
    // don't need the target.
    [[nodiscard]] inline mnemos::market parse_market(std::span<const std::uint8_t> rom) noexcept {
        return parse_cart_info(rom).market;
    }

} // namespace mnemos::manifests::sms
