// CD-ROM Mode-1 EDC + P/Q parity regeneration. Clean-room per ECMA-130; ported
// from the Emu reference (chips/circ_ecc). See circ_ecc.hpp for the sector
// layout and NOTES.md for provenance.

#include "circ_ecc.hpp"

#include <array>
#include <cstddef>

namespace mnemos::disc {

    namespace {

        // GF(2^8) with primitive polynomial x^8 + x^4 + x^3 + x^2 + 1 (0x11D).
        // The RS(n, n-2) CD-ROM codewords only need multiply-by-2 and -by-3.
        constexpr std::uint8_t gf_mul_2(std::uint8_t x) noexcept {
            const auto shifted = static_cast<std::uint8_t>(x << 1);
            return (x & 0x80U) ? static_cast<std::uint8_t>(shifted ^ 0x1DU) : shifted;
        }
        constexpr std::uint8_t gf_mul_3(std::uint8_t x) noexcept {
            return static_cast<std::uint8_t>(gf_mul_2(x) ^ x);
        }

        // CRC-32 table for the reflected EDC polynomial 0xD8018001, computed at
        // compile time (no lazy-init race, no startup cost).
        constexpr std::array<std::uint32_t, 256> kEdcTable = [] {
            std::array<std::uint32_t, 256> table{};
            constexpr std::uint32_t poly = 0xD8018001U;
            for (std::uint32_t i = 0; i < 256U; ++i) {
                std::uint32_t crc = i;
                for (int b = 0; b < 8; ++b) {
                    crc = (crc & 1U) ? (crc >> 1) ^ poly : (crc >> 1);
                }
                table[i] = crc;
            }
            return table;
        }();

        // RS(n, n-2) encoder over GF(2^8), generator g(x) = (x-a^0)(x-a^1) =
        // x^2 + 3x + 2. Returns the two parity bytes (p1 high-degree, p0 low).
        void rs_encode_2_parity(std::span<const std::uint8_t> data, std::uint8_t& p1,
                                std::uint8_t& p0) noexcept {
            std::uint8_t b1 = 0;
            std::uint8_t b0 = 0;
            for (const std::uint8_t d : data) {
                const auto fb = static_cast<std::uint8_t>(d ^ b1);
                const auto b1_new = static_cast<std::uint8_t>(b0 ^ gf_mul_3(fb));
                const std::uint8_t b0_new = gf_mul_2(fb);
                b1 = b1_new;
                b0 = b0_new;
            }
            p1 = b1;
            p0 = b0;
        }

        // Byte offset for (row, col) in the 26x43 data+P matrix, byte `layer`
        // (0 = low byte of the 16-bit word, 1 = high byte).
        constexpr std::size_t p_grid_byte_offset(unsigned row, unsigned col,
                                                 unsigned layer) noexcept {
            if (row < 24U) {
                return static_cast<std::size_t>(12U + 2U * (row * 43U + col) + layer);
            }
            const unsigned prow = row - 24U; // 0 or 1
            return static_cast<std::size_t>(2076U + 86U * prow + 2U * col + layer);
        }

        void encode_p_column(std::span<std::uint8_t> sector, unsigned col,
                             unsigned layer) noexcept {
            std::array<std::uint8_t, 24> data{};
            for (unsigned r = 0; r < 24U; ++r) {
                data[r] = sector[p_grid_byte_offset(r, col, layer)];
            }
            std::uint8_t p1 = 0;
            std::uint8_t p0 = 0;
            rs_encode_2_parity(data, p1, p0);
            sector[p_grid_byte_offset(24U, col, layer)] = p1;
            sector[p_grid_byte_offset(25U, col, layer)] = p0;
        }

        // Q codewords run along diagonals: item k of codeword i comes from grid
        // (row = (18k + i) mod 26, col = k). 18 = 44 mod 26 (ECMA-130 step 44).
        constexpr std::size_t q_data_byte_offset(unsigned i, unsigned k, unsigned layer) noexcept {
            const unsigned row = (18U * k + i) % 26U;
            return p_grid_byte_offset(row, k, layer);
        }
        constexpr std::size_t q_parity_byte_offset(unsigned i, unsigned pr,
                                                   unsigned layer) noexcept {
            return static_cast<std::size_t>(2248U + 52U * pr + 2U * i + layer);
        }

        void encode_q_codeword(std::span<std::uint8_t> sector, unsigned i,
                               unsigned layer) noexcept {
            std::array<std::uint8_t, 43> data{};
            for (unsigned k = 0; k < 43U; ++k) {
                data[k] = sector[q_data_byte_offset(i, k, layer)];
            }
            std::uint8_t p1 = 0;
            std::uint8_t p0 = 0;
            rs_encode_2_parity(data, p1, p0);
            sector[q_parity_byte_offset(i, 0U, layer)] = p1;
            sector[q_parity_byte_offset(i, 1U, layer)] = p0;
        }

    } // namespace

    std::uint32_t circ_ecc_edc(std::span<const std::uint8_t> data) {
        std::uint32_t crc = 0;
        for (const std::uint8_t byte : data) {
            crc = (crc >> 8) ^ kEdcTable[(crc ^ byte) & 0xFFU];
        }
        return crc;
    }

    void circ_ecc_regen_sector(std::span<std::uint8_t, 2352> sector) {
        // Step 1: EDC over bytes [0..2063], stored little-endian at [2064..2067].
        const std::uint32_t edc = circ_ecc_edc(sector.first(2064));
        sector[2064] = static_cast<std::uint8_t>(edc & 0xFFU);
        sector[2065] = static_cast<std::uint8_t>((edc >> 8) & 0xFFU);
        sector[2066] = static_cast<std::uint8_t>((edc >> 16) & 0xFFU);
        sector[2067] = static_cast<std::uint8_t>((edc >> 24) & 0xFFU);

        // Step 2: reserved area [2068..2075] zero-filled.
        for (std::size_t i = 2068; i < 2076; ++i) {
            sector[i] = 0;
        }

        // Step 3: P parity, 43 columns x 2 byte layers.
        const std::span<std::uint8_t> view = sector;
        for (unsigned col = 0; col < 43U; ++col) {
            encode_p_column(view, col, 0U);
            encode_p_column(view, col, 1U);
        }

        // Step 4: Q parity, 26 codewords x 2 byte layers.
        for (unsigned i = 0; i < 26U; ++i) {
            encode_q_codeword(view, i, 0U);
            encode_q_codeword(view, i, 1U);
        }
    }

} // namespace mnemos::disc
