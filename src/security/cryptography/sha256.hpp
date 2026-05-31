#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// SHA-256 (FIPS 180-4). Header-only to fit the security::cryptography INTERFACE
// target. Used for ROM verification (manifest loader) and framebuffer hashing
// (runtime/golden tests).
namespace mnemos::security::cryptography {

    struct sha256_digest final {
        std::array<std::uint8_t, 32> bytes{};

        [[nodiscard]] bool operator==(const sha256_digest&) const = default;

        [[nodiscard]] std::string hex() const {
            static constexpr char digits[] = "0123456789abcdef";
            std::string out;
            out.reserve(64);
            for (std::uint8_t b : bytes) {
                out.push_back(digits[(b >> 4U) & 0x0FU]);
                out.push_back(digits[b & 0x0FU]);
            }
            return out;
        }
    };

    namespace detail {

        [[nodiscard]] inline std::uint32_t sha256_rotr(std::uint32_t x, unsigned n) noexcept {
            return (x >> n) | (x << (32U - n));
        }

        inline constexpr std::array<std::uint32_t, 64> sha256_k = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
            0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
            0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
            0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
            0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
            0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
            0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
            0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
        };

    } // namespace detail

    [[nodiscard]] inline sha256_digest sha256(std::span<const std::uint8_t> data) {
        std::array<std::uint32_t, 8> h = {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                          0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

        const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8U;

        // Build the padded message: data || 0x80 || 0x00.. || 64-bit big-endian length.
        std::vector<std::uint8_t> msg(data.begin(), data.end());
        msg.push_back(0x80U);
        while ((msg.size() % 64U) != 56U) {
            msg.push_back(0x00U);
        }
        for (int shift = 56; shift >= 0; shift -= 8) {
            msg.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xFFU));
        }

        std::array<std::uint32_t, 64> w{};
        for (std::size_t block = 0; block < msg.size(); block += 64U) {
            for (std::size_t i = 0; i < 16U; ++i) {
                const std::size_t o = block + i * 4U;
                w[i] = (static_cast<std::uint32_t>(msg[o]) << 24U) |
                       (static_cast<std::uint32_t>(msg[o + 1U]) << 16U) |
                       (static_cast<std::uint32_t>(msg[o + 2U]) << 8U) |
                       static_cast<std::uint32_t>(msg[o + 3U]);
            }
            for (std::size_t i = 16U; i < 64U; ++i) {
                const std::uint32_t s0 = detail::sha256_rotr(w[i - 15U], 7U) ^
                                         detail::sha256_rotr(w[i - 15U], 18U) ^ (w[i - 15U] >> 3U);
                const std::uint32_t s1 = detail::sha256_rotr(w[i - 2U], 17U) ^
                                         detail::sha256_rotr(w[i - 2U], 19U) ^ (w[i - 2U] >> 10U);
                w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
            }

            std::uint32_t a = h[0];
            std::uint32_t b = h[1];
            std::uint32_t c = h[2];
            std::uint32_t d = h[3];
            std::uint32_t e = h[4];
            std::uint32_t f = h[5];
            std::uint32_t g = h[6];
            std::uint32_t hh = h[7];

            for (std::size_t i = 0; i < 64U; ++i) {
                const std::uint32_t big_s1 = detail::sha256_rotr(e, 6U) ^
                                             detail::sha256_rotr(e, 11U) ^
                                             detail::sha256_rotr(e, 25U);
                const std::uint32_t ch = (e & f) ^ (~e & g);
                const std::uint32_t t1 = hh + big_s1 + ch + detail::sha256_k[i] + w[i];
                const std::uint32_t big_s0 = detail::sha256_rotr(a, 2U) ^
                                             detail::sha256_rotr(a, 13U) ^
                                             detail::sha256_rotr(a, 22U);
                const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                const std::uint32_t t2 = big_s0 + maj;
                hh = g;
                g = f;
                f = e;
                e = d + t1;
                d = c;
                c = b;
                b = a;
                a = t1 + t2;
            }

            h[0] += a;
            h[1] += b;
            h[2] += c;
            h[3] += d;
            h[4] += e;
            h[5] += f;
            h[6] += g;
            h[7] += hh;
        }

        sha256_digest digest;
        for (std::size_t i = 0; i < 8U; ++i) {
            digest.bytes[i * 4U] = static_cast<std::uint8_t>((h[i] >> 24U) & 0xFFU);
            digest.bytes[i * 4U + 1U] = static_cast<std::uint8_t>((h[i] >> 16U) & 0xFFU);
            digest.bytes[i * 4U + 2U] = static_cast<std::uint8_t>((h[i] >> 8U) & 0xFFU);
            digest.bytes[i * 4U + 3U] = static_cast<std::uint8_t>(h[i] & 0xFFU);
        }
        return digest;
    }

    [[nodiscard]] inline sha256_digest sha256(std::string_view text) {
        return sha256(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }

} // namespace mnemos::security::cryptography
