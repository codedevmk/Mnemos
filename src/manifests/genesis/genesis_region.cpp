#include "genesis_region.hpp"

#include <array>
#include <cctype>
#include <cstddef>

namespace mnemos::manifests::genesis {

    namespace {

        struct letter_entry final {
            char letter;
            std::uint8_t bit;
        };

        // Letters route into the hex bitfield (J=1, U=4, E=8). K -> Japan.
        constexpr std::array<letter_entry, 4> letter_table = {{
            {'J', 0x01U},
            {'U', 0x04U},
            {'E', 0x08U},
            {'K', 0x01U},
        }};

        [[nodiscard]] constexpr mnemos::market market_from_bits(std::uint8_t bits) noexcept {
            const int set = static_cast<int>(((bits & 0x01U) != 0U)) +
                            static_cast<int>(((bits & 0x04U) != 0U)) +
                            static_cast<int>(((bits & 0x08U) != 0U));
            if (set == 0) {
                return mnemos::market::unknown;
            }
            if (set >= 2) {
                return mnemos::market::multi_region;
            }
            if ((bits & 0x04U) != 0U) {
                return mnemos::market::americas;
            }
            if ((bits & 0x01U) != 0U) {
                return mnemos::market::japan;
            }
            return mnemos::market::europe;
        }

        // Letters resolve first so ASCII 'E' is the Europe letter, not hex 0xE.
        [[nodiscard]] std::uint8_t bits_for_byte(std::uint8_t raw) noexcept {
            const auto c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
            for (const auto& entry : letter_table) {
                if (c == entry.letter) {
                    return entry.bit;
                }
            }
            if (raw < 16U) {
                return raw;
            }
            if (c >= '0' && c <= '9') {
                return static_cast<std::uint8_t>(c - '0');
            }
            if (c >= 'A' && c <= 'F') {
                return static_cast<std::uint8_t>(10 + (c - 'A'));
            }
            return 0U;
        }

    } // namespace

    mnemos::market parse_market(std::span<const std::uint8_t> rom) noexcept {
        if (rom.size() < 0x200) {
            return mnemos::market::unknown;
        }
        std::uint8_t bits = 0U;
        const std::size_t end = (rom.size() < 0x1F4) ? rom.size() : 0x1F4;
        for (std::size_t i = 0x1F0; i < end; ++i) {
            bits = static_cast<std::uint8_t>(bits | bits_for_byte(rom[i]));
        }
        return market_from_bits(bits);
    }

} // namespace mnemos::manifests::genesis
