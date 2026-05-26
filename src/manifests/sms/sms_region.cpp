#include "sms_region.hpp"

#include <array>

namespace mnemos::manifests::sms {

    namespace {

        // Country-nibble lookup table. The high nibble of $7FFF maps to both a
        // physical-system target and a market. Adding/correcting entries
        // (e.g. a future regional variant) is one row of this table.
        struct nibble_entry final {
            std::uint8_t nibble;
            cart_target target;
            mnemos::market market;
        };

        constexpr std::array<nibble_entry, 5> nibble_table = {{
            // 3 = SMS Japan: domestic-only cart.
            {0x3U, cart_target::sms, mnemos::market::japan},
            // 4 = SMS Export: sold into the USA + Europe SMS market. The cart
            // doesn't distinguish the two -- the same image shipped to both.
            {0x4U, cart_target::sms, mnemos::market::multi_region},
            // 5 = Game Gear Japan: domestic GG.
            {0x5U, cart_target::gg, mnemos::market::japan},
            // 6 = Game Gear Export: USA + Europe GG.
            {0x6U, cart_target::gg, mnemos::market::multi_region},
            // 7 = Game Gear International: global release.
            {0x7U, cart_target::gg, mnemos::market::multi_region},
        }};

    } // namespace

    cart_info parse_cart_info(std::span<const std::uint8_t> rom) noexcept {
        if (rom.size() < 0x8000U) {
            return {.market = mnemos::market::unknown, .target = cart_target::unknown};
        }
        const std::uint8_t nibble = static_cast<std::uint8_t>((rom[0x7FFFU] >> 4U) & 0x0FU);
        for (const auto& entry : nibble_table) {
            if (entry.nibble == nibble) {
                return {.market = entry.market, .target = entry.target};
            }
        }
        return {.market = mnemos::market::unknown, .target = cart_target::unknown};
    }

} // namespace mnemos::manifests::sms
