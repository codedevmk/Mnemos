#ifndef MNEMOS_MANIFESTS_CAPCOM_CPS2_CPS2_CRYPTO_HPP
#define MNEMOS_MANIFESTS_CAPCOM_CPS2_CPS2_CRYPTO_HPP

// CPS-2 68000 opcode cipher. The CPS-2 board encrypts only the 68000 INSTRUCTION
// stream (data reads are plain); a board boots only when its opcode fetches are
// routed through the decrypted image. This is the load-time transform that
// produces that image from the encrypted program ROM + the board's 20-byte key.
//
// The algorithm (two 4-round Feistel networks keyed by the address + master key)
// is the publicly documented CPS-2 cipher; see THIRD-PARTY-REFERENCES.md for its
// provenance and license. Pure data transform: no bus, no chip, no ROM/key blobs
// committed -- keys are supplied by the caller as external assets.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::manifests::capcom_cps2 {

    // The board key is a 20-byte asset (the bit-packed master key + watchdog /
    // address-range descriptor) decoded into this expanded form.
    inline constexpr std::size_t crypto_key_size = 20U;

    struct cps2_crypto_key {
        std::array<std::uint32_t, 2> master_key{}; // the 64-bit Feistel master key
        std::uint32_t lower_limit{0U};             // first decrypted word address
        std::uint32_t upper_limit{0U};             // last decrypted word address
        std::array<std::uint16_t, 10> decoded{};   // the raw decoded 16-bit words
    };

    // Decode a 20-byte board key into the master key + the address range over
    // which opcodes are encrypted. Returns false if key_data is the wrong size.
    [[nodiscard]] bool decode_key(std::span<const std::uint8_t> key_data,
                                  cps2_crypto_key& out) noexcept;

    // Decrypt the 68000 opcode stream: read big-endian words from `encrypted_be`,
    // write the decrypted big-endian words to `decrypted_be`. Words outside the
    // key's [lower_limit, upper_limit] address range are copied unchanged. The two
    // spans must be the same even byte length; they may alias. Returns false on a
    // size mismatch / odd or empty length.
    [[nodiscard]] bool decrypt_opcodes(std::span<const std::uint8_t> encrypted_be,
                                       std::span<std::uint8_t> decrypted_be,
                                       const cps2_crypto_key& key) noexcept;

    // The inverse transform (plaintext opcodes -> ciphertext), for tooling and
    // round-trip tests. Same contract as decrypt_opcodes.
    [[nodiscard]] bool encrypt_opcodes(std::span<const std::uint8_t> plain_be,
                                       std::span<std::uint8_t> encrypted_be,
                                       const cps2_crypto_key& key) noexcept;

} // namespace mnemos::manifests::capcom_cps2

#endif // MNEMOS_MANIFESTS_CAPCOM_CPS2_CPS2_CRYPTO_HPP
