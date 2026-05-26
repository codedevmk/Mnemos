#include "region.hpp"

#include <cctype>
#include <cstddef>

namespace mnemos::apps::player::adapters {

    video_region detect_sega16_region(std::span<const std::uint8_t> rom) noexcept {
        if (rom.size() < 0x200) {
            return video_region::ntsc;
        }
        // Port of the reference emulator's get_region() country-bitfield scan: walk the
        // 4-byte country field at $1F0, accumulate a bitfield (J=1, U=4, E=8),
        // then priority-pick USA > Japan > Europe > Japan-PAL > default USA.
        // Critically, the letter checks short-circuit the hex parse via else-if,
        // so an ASCII 'E' is only the Europe letter -- never hex 0xE (which
        // would imply USA-too and flip a pure-EU cart back to NTSC).
        int country = 0;
        const std::size_t end = std::min<std::size_t>(rom.size(), 0x1F4);
        for (std::size_t i = 0x1F0; i < end; ++i) {
            const auto raw = rom[i];
            const auto c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
            if (c == 'U') {
                country |= 4;
            } else if (c == 'J') {
                country |= 1;
            } else if (c == 'E') {
                country |= 8;
            } else if (c == 'K') {
                country |= 1; // Korea -> Japan-NTSC compatible region per the reference
            } else if (raw < 16U) {
                country |= raw;
            } else if (c >= '0' && c <= '9') {
                country |= c - '0';
            } else if (c >= 'A' && c <= 'F') {
                country |= 10 + (c - 'A');
            }
        }
        // Priority: USA > Japan-NTSC > Europe > Japan-PAL > USA-default.
        if ((country & 4) != 0) {
            return video_region::ntsc;
        }
        if ((country & 1) != 0) {
            return video_region::ntsc;
        }
        if ((country & 8) != 0) {
            return video_region::pal;
        }
        if ((country & 2) != 0) {
            return video_region::pal; // Japan-PAL (rare); PAL framing is the better fit
        }
        return video_region::ntsc;
    }

    video_region detect_sms_region(std::span<const std::uint8_t> /*rom*/) noexcept {
        // SMS adapter is not implemented yet; export/US carts are the common
        // case so default to NTSC. When the SMS adapter lands, this looks at
        // the country nibble of $7FFF (and the GG region for GG carts).
        return video_region::ntsc;
    }

} // namespace mnemos::apps::player::adapters
