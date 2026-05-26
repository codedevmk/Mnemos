#pragma once

#include <array>
#include <cstdint>

// Commodore GCR (group-coded recording) 4-to-5 codec used by the 1541 on disk.
// Each 4-bit nibble maps to a 5-bit codeword chosen so the bit stream never has
// more than two consecutive zeros; four data bytes (eight nibbles) become five
// GCR bytes. Header-only.
namespace mnemos::chips::storage::c1541 {

    namespace detail {

        inline constexpr std::array<std::uint8_t, 16> gcr_encode_table = {
            0x0A, 0x0B, 0x12, 0x13, 0x0E, 0x0F, 0x16, 0x17,
            0x09, 0x19, 0x1A, 0x1B, 0x0D, 0x1D, 0x1E, 0x15,
        };

        consteval std::array<std::uint8_t, 32> make_gcr_decode_table() {
            std::array<std::uint8_t, 32> table{};
            for (auto& e : table) {
                e = 0xFFU; // illegal codeword sentinel
            }
            for (std::uint8_t nibble = 0; nibble < 16U; ++nibble) {
                table[gcr_encode_table[nibble]] = nibble;
            }
            return table;
        }

        inline constexpr std::array<std::uint8_t, 32> gcr_decode_table = make_gcr_decode_table();

    } // namespace detail

    // Encode four data bytes into five GCR bytes (MSB-first bit packing).
    inline void gcr_encode_4to5(const std::array<std::uint8_t, 4>& in,
                                std::array<std::uint8_t, 5>& out) noexcept {
        std::uint64_t bits = 0;
        for (const std::uint8_t byte : in) {
            bits = (bits << 5U) | detail::gcr_encode_table[(byte >> 4U) & 0x0FU];
            bits = (bits << 5U) | detail::gcr_encode_table[byte & 0x0FU];
        }
        for (std::size_t i = 0; i < 5U; ++i) {
            out[i] = static_cast<std::uint8_t>((bits >> (32U - 8U * i)) & 0xFFU);
        }
    }

    // Decode five GCR bytes back into four data bytes. Returns false (and leaves
    // `out` partially written) if any 5-bit group is an illegal codeword.
    [[nodiscard]] inline bool gcr_decode_5to4(const std::array<std::uint8_t, 5>& in,
                                              std::array<std::uint8_t, 4>& out) noexcept {
        std::uint64_t bits = 0;
        for (const std::uint8_t byte : in) {
            bits = (bits << 8U) | byte;
        }
        std::array<std::uint8_t, 8> nibbles{};
        for (std::size_t i = 0; i < 8U; ++i) {
            const auto codeword = static_cast<std::uint8_t>((bits >> (35U - 5U * i)) & 0x1FU);
            const std::uint8_t nibble = detail::gcr_decode_table[codeword];
            if (nibble == 0xFFU) {
                return false;
            }
            nibbles[i] = nibble;
        }
        for (std::size_t i = 0; i < 4U; ++i) {
            out[i] = static_cast<std::uint8_t>((nibbles[2U * i] << 4U) | nibbles[2U * i + 1U]);
        }
        return true;
    }

} // namespace mnemos::chips::storage::c1541
