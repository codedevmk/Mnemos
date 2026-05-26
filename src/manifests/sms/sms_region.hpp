#pragma once

#include "region.hpp"

#include <cstdint>
#include <span>

namespace mnemos::manifests::sms {

    enum class cart_target : std::uint8_t {
        sms,
        gg,
        unknown,
    };

    struct cart_info final {
        mnemos::market market;
        cart_target target;
    };

    // High nibble of $7FFF: 3=SMS Japan, 4=SMS Export, 5/6/7=GG variants.
    [[nodiscard]] cart_info parse_cart_info(std::span<const std::uint8_t> rom) noexcept;

    [[nodiscard]] inline mnemos::market parse_market(std::span<const std::uint8_t> rom) noexcept {
        return parse_cart_info(rom).market;
    }

} // namespace mnemos::manifests::sms
