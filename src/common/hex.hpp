#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Hex (base-16) encode/decode of byte buffers -- a base-type formatting op
// (ADR 0009). Lowercase by default; decode is case-insensitive and reports
// invalid input via nullopt rather than an ambiguous empty result.
namespace mnemos::common {

    namespace detail {

        inline constexpr char hex_lower[] = "0123456789abcdef";
        inline constexpr char hex_upper[] = "0123456789ABCDEF";

        // Nibble value 0-15 for a hex digit, or -1 for any non-hex byte.
        [[nodiscard]] constexpr int hex_nibble(unsigned char c) noexcept {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F') {
                return c - 'A' + 10;
            }
            return -1;
        }

    } // namespace detail

    [[nodiscard]] inline std::string to_hex(std::span<const std::uint8_t> data) {
        std::string out;
        out.reserve(data.size() * 2U);
        for (std::uint8_t b : data) {
            out.push_back(detail::hex_lower[(b >> 4U) & 0x0FU]);
            out.push_back(detail::hex_lower[b & 0x0FU]);
        }
        return out;
    }

    [[nodiscard]] inline std::string to_hex_upper(std::span<const std::uint8_t> data) {
        std::string out;
        out.reserve(data.size() * 2U);
        for (std::uint8_t b : data) {
            out.push_back(detail::hex_upper[(b >> 4U) & 0x0FU]);
            out.push_back(detail::hex_upper[b & 0x0FU]);
        }
        return out;
    }

    // Valid hex is an even number of [0-9a-fA-F] digits (empty counts as valid).
    [[nodiscard]] inline bool is_hex(std::string_view text) noexcept {
        if ((text.size() % 2U) != 0U) {
            return false;
        }
        for (char c : text) {
            if (detail::hex_nibble(static_cast<unsigned char>(c)) < 0) {
                return false;
            }
        }
        return true;
    }

    // Decode hex digits to bytes. nullopt on odd length or any non-hex digit;
    // an empty string decodes to an empty vector.
    [[nodiscard]] inline std::optional<std::vector<std::uint8_t>> from_hex(std::string_view text) {
        if ((text.size() % 2U) != 0U) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> out;
        out.reserve(text.size() / 2U);
        for (std::size_t i = 0; i < text.size(); i += 2U) {
            const int hi = detail::hex_nibble(static_cast<unsigned char>(text[i]));
            const int lo = detail::hex_nibble(static_cast<unsigned char>(text[i + 1U]));
            if (hi < 0 || lo < 0) {
                return std::nullopt;
            }
            out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
        }
        return out;
    }

} // namespace mnemos::common
