#include "region.hpp"

#include <cctype>
#include <cstddef>

namespace mnemos::apps::player::adapters {

    video_region detect_sega16_region(std::span<const std::uint8_t> rom) noexcept {
        if (rom.size() < 0x200) {
            return video_region::ntsc;
        }
        bool has_e = false;
        bool has_non_e = false;
        for (std::size_t i = 0x1F0; i < 0x1F3; ++i) {
            const auto raw = rom[i];
            const auto c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
            if (c == 'E') {
                has_e = true;
            } else if (c == 'J' || c == 'U') {
                has_non_e = true;
            }
            // Hex-bitfield region byte (newer carts): bit 0=Japan, bit 1=USA, bit 2=Europe.
            int hex = -1;
            if (c >= '0' && c <= '9') {
                hex = c - '0';
            } else if (c >= 'A' && c <= 'F') {
                hex = 10 + (c - 'A');
            } else if (raw <= 0x0FU) {
                hex = static_cast<int>(raw);
            }
            if (hex >= 0) {
                if ((hex & 0x04) != 0) {
                    has_e = true;
                }
                if ((hex & 0x03) != 0) {
                    has_non_e = true;
                }
            }
        }
        // Favor PAL when Europe is supported -- PAL screens render at the right
        // rate, and the V30 + full vertical border budget that PAL-aware screens
        // assume becomes available.
        (void)has_non_e; // documented branch; pure-EU and multi-EU both go PAL.
        return has_e ? video_region::pal : video_region::ntsc;
    }

    video_region detect_sms_region(std::span<const std::uint8_t> /*rom*/) noexcept {
        // SMS adapter is not implemented yet; export/US carts are the common
        // case so default to NTSC. When the SMS adapter lands, this looks at
        // the country nibble of $7FFF (and the GG region for GG carts).
        return video_region::ntsc;
    }

} // namespace mnemos::apps::player::adapters
