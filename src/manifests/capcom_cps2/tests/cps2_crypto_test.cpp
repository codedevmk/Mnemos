#include "cps2_crypto.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::manifests::capcom_cps2::cps2_crypto_key;
    using mnemos::manifests::capcom_cps2::crypto_key_size;
    using mnemos::manifests::capcom_cps2::decode_key;
    using mnemos::manifests::capcom_cps2::decrypt_opcodes;
    using mnemos::manifests::capcom_cps2::encrypt_opcodes;

    // A deterministic 20-byte board key (key[i] = i*7+3).
    std::array<std::uint8_t, crypto_key_size> sample_key() {
        std::array<std::uint8_t, crypto_key_size> k{};
        for (std::size_t i = 0; i < k.size(); ++i) {
            k[i] = static_cast<std::uint8_t>(i * 7U + 3U);
        }
        return k;
    }
} // namespace

// The expected values are the reference cipher's output for the fixed inputs
// below (cross-generated once from the reference implementation, baked in here as
// golden vectors). They pin the exact s-box transcription -- a round-trip alone
// would not catch a wrong s-box constant.
TEST_CASE("cps2 crypto decodes a 20-byte key to the master key + address range",
          "[capcom_cps2][crypto]") {
    const auto k = sample_key();
    cps2_crypto_key key{};
    REQUIRE(decode_key(k, key));

    CHECK(key.master_key[0] == 0x46057B38U);
    CHECK(key.master_key[1] == 0xDA99EBA8U);
    CHECK(key.lower_limit == 0x000000U);
    CHECK(key.upper_limit == 0x400000U);
    const std::array<std::uint16_t, 10> expected_decoded{
        0x4605U, 0x7B38U, 0xDA99U, 0xEBA8U, 0x2A49U, 0x0B70U, 0xB2D1U, 0x93E0U, 0x6221U, 0x4300U};
    CHECK(key.decoded == expected_decoded);
}

TEST_CASE("cps2 crypto decrypts opcodes to the reference golden bytes", "[capcom_cps2][crypto]") {
    const auto k = sample_key();
    cps2_crypto_key key{};
    REQUIRE(decode_key(k, key));

    // 64-byte (32-word) big-endian input: in[i] = i*5+1. Addresses 0..31 are all
    // inside [0, 0x400000], so every word is decrypted.
    std::array<std::uint8_t, 64> in{};
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = static_cast<std::uint8_t>(i * 5U + 1U);
    }
    const std::array<std::uint8_t, 64> golden{
        0x2E, 0x27, 0x2B, 0xA6, 0x9B, 0x72, 0xAF, 0xF2, 0xA5, 0xFF, 0x7A, 0x19, 0x96,
        0x3D, 0x87, 0xF3, 0x1D, 0x34, 0x9F, 0x2C, 0xA0, 0xAA, 0x59, 0x94, 0x55, 0xD3,
        0x66, 0x1D, 0x73, 0xF2, 0x52, 0x78, 0x44, 0x79, 0xEA, 0x3C, 0xD0, 0x73, 0xA2,
        0x33, 0x28, 0x3C, 0x17, 0xE3, 0x0E, 0xD4, 0xC3, 0xC9, 0x0E, 0x0F, 0x8F, 0xCC,
        0xD5, 0x33, 0x63, 0xE2, 0x1C, 0x9E, 0xAD, 0xD7, 0xBF, 0x45, 0xDC, 0x13};

    std::array<std::uint8_t, 64> out{};
    REQUIRE(decrypt_opcodes(in, out, key));
    CHECK(out == golden);
}

TEST_CASE("cps2 crypto encrypt/decrypt round-trips within the address range",
          "[capcom_cps2][crypto]") {
    const auto k = sample_key();
    cps2_crypto_key key{};
    REQUIRE(decode_key(k, key));

    std::vector<std::uint8_t> plain(0x200);
    for (std::size_t i = 0; i < plain.size(); ++i) {
        plain[i] = static_cast<std::uint8_t>((i * 131U + 17U) & 0xFFU);
    }
    std::vector<std::uint8_t> enc(plain.size());
    std::vector<std::uint8_t> dec(plain.size());
    REQUIRE(encrypt_opcodes(plain, enc, key));
    REQUIRE(decrypt_opcodes(enc, dec, key));
    CHECK(dec == plain);
    // The cipher actually changed the bytes (not an accidental identity).
    CHECK(enc != plain);
}

TEST_CASE("cps2 crypto: a dead-board (all-FF) key decrypts only the top range",
          "[capcom_cps2][crypto]") {
    std::array<std::uint8_t, crypto_key_size> ff{};
    ff.fill(0xFFU);
    cps2_crypto_key key{};
    REQUIRE(decode_key(ff, key));
    CHECK(key.master_key[0] == 0xFFFFFFFFU);
    CHECK(key.master_key[1] == 0xFFFFFFFFU);
    CHECK(key.lower_limit == 0xFF0000U);
    CHECK(key.upper_limit == 0xFFFFFFU);

    // A buffer at word addresses 0..31 is entirely below 0xFF0000, so it passes
    // through unchanged.
    std::array<std::uint8_t, 64> in{};
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = static_cast<std::uint8_t>(i * 5U + 1U);
    }
    std::array<std::uint8_t, 64> out{};
    REQUIRE(decrypt_opcodes(in, out, key));
    CHECK(out == in);
}

TEST_CASE("cps2 crypto rejects malformed inputs", "[capcom_cps2][crypto]") {
    cps2_crypto_key key{};
    // Wrong key size.
    std::array<std::uint8_t, 19> short_key{};
    CHECK_FALSE(decode_key(short_key, key));

    REQUIRE(decode_key(sample_key(), key));
    // Odd / mismatched / empty opcode buffers.
    std::array<std::uint8_t, 3> odd{};
    std::array<std::uint8_t, 3> odd_out{};
    CHECK_FALSE(decrypt_opcodes(odd, odd_out, key));
    std::array<std::uint8_t, 4> a{};
    std::array<std::uint8_t, 6> b{};
    CHECK_FALSE(decrypt_opcodes(a, b, key));
    std::array<std::uint8_t, 0> empty{};
    CHECK_FALSE(decrypt_opcodes(empty, empty, key));
}
