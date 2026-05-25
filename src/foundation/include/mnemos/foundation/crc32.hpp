#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

// CRC-32 (IEEE 802.3 / zlib, reflected polynomial 0xEDB88320). Header-only to fit
// the foundation INTERFACE target. Used as the fast save-state corruption check
// (see ADR 0008); not a cryptographic checksum.
namespace mnemos::foundation {

    namespace detail {

        consteval std::array<std::uint32_t, 256> make_crc32_table() {
            std::array<std::uint32_t, 256> table{};
            for (std::uint32_t i = 0; i < 256U; ++i) {
                std::uint32_t c = i;
                for (int k = 0; k < 8; ++k) {
                    c = (c & 1U) != 0U ? (0xEDB88320U ^ (c >> 1U)) : (c >> 1U);
                }
                table[i] = c;
            }
            return table;
        }

        inline constexpr std::array<std::uint32_t, 256> crc32_table = make_crc32_table();

    } // namespace detail

    // Incremental CRC-32: pass 0 (the default seed) for the first call, then feed
    // the previous result back in to continue over multiple buffers. zlib-compatible.
    [[nodiscard]] inline std::uint32_t crc32(std::span<const std::uint8_t> data,
                                             std::uint32_t crc = 0U) noexcept {
        crc = ~crc;
        for (const std::uint8_t b : data) {
            crc = detail::crc32_table[(crc ^ b) & 0xFFU] ^ (crc >> 8U);
        }
        return ~crc;
    }

    [[nodiscard]] inline std::uint32_t crc32(std::string_view text,
                                             std::uint32_t crc = 0U) noexcept {
        return crc32(std::span<const std::uint8_t>(
                         reinterpret_cast<const std::uint8_t*>(text.data()), text.size()),
                     crc);
    }

} // namespace mnemos::foundation
