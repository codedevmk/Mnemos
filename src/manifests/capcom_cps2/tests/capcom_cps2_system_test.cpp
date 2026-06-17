#include "capcom_cps2_system.hpp"

#include "cps2_crypto.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::manifests::capcom_cps2::cps2_board_params;
    using mnemos::manifests::capcom_cps2::cps2_system;
    using mnemos::manifests::capcom_cps2::crypto_key_size;
    using mnemos::manifests::capcom_cps2::encrypt_opcodes;
    using mnemos::manifests::common::rom_set_image;

    std::array<std::uint8_t, crypto_key_size> sample_key() {
        std::array<std::uint8_t, crypto_key_size> k{};
        for (std::size_t i = 0; i < k.size(); ++i) {
            k[i] = static_cast<std::uint8_t>(i * 7U + 3U);
        }
        return k;
    }

    // A tiny 68000 program (big-endian): reset vector (SSP=$00FF0000, PC=$000008)
    // then MOVEQ #$7F,D0. Padded to an even length.
    std::vector<std::uint8_t> plain_program() {
        std::vector<std::uint8_t> p(0x40U, 0x00U);
        const auto w16 = [&](std::size_t a, std::uint16_t v) {
            p[a] = static_cast<std::uint8_t>(v >> 8U);
            p[a + 1U] = static_cast<std::uint8_t>(v);
        };
        w16(0x0U, 0x00FFU);
        w16(0x2U, 0x0000U); // SSP = 0x00FF0000
        w16(0x4U, 0x0000U);
        w16(0x6U, 0x0008U); // PC = 0x00000008
        w16(0x8U, 0x707FU); // MOVEQ #$7F,D0
        return p;
    }

    // The encrypted program a real CPS-2 board ships (encrypt the plaintext with
    // the board key); the machine must decrypt it back to boot.
    std::vector<std::uint8_t>
    encrypted_program(const std::array<std::uint8_t, crypto_key_size>& k) {
        const std::vector<std::uint8_t> plain = plain_program();
        mnemos::manifests::capcom_cps2::cps2_crypto_key key{};
        REQUIRE(decode_key(k, key));
        std::vector<std::uint8_t> enc(plain.size());
        REQUIRE(encrypt_opcodes(plain, enc, key));
        return enc;
    }
} // namespace

TEST_CASE("cps2 system boots the 68000 from the decrypted opcode image", "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);

    cps2_system sys(std::move(image), cps2_board_params{.key = k});
    REQUIRE(sys.executable());

    auto r = sys.cpu().cpu_registers();
    CHECK(r.pc == 0x00000008U);   // reset PC from the decrypted vector
    CHECK(r.a[7] == 0x00FF0000U); // SSP from the decrypted vector

    // Data reads see the encrypted ROM; opcode fetches see the decrypted image.
    CHECK(sys.bus().read16_be(0x0008U) != 0x707FU);
    CHECK(sys.bus().fetch16_be_opcode(0x0008U) == 0x707FU);

    sys.run_cycles(4); // executes the decrypted MOVEQ #$7F,D0
    r = sys.cpu().cpu_registers();
    CHECK((r.d[0] & 0xFFU) == 0x7FU);
}

TEST_CASE("cps2 system without a key is a non-executable blocker", "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);

    // No key supplied: the program stays encrypted and the board is not runnable.
    cps2_system sys(std::move(image), cps2_board_params{});
    CHECK_FALSE(sys.executable());
    // The opcode overlay is the raw encrypted bytes, so a fetch != the plaintext.
    CHECK(sys.bus().fetch16_be_opcode(0x0008U) != 0x707FU);
}

TEST_CASE("cps2 system reads the board key from a 'key' set region", "[capcom_cps2][system]") {
    const auto k = sample_key();
    rom_set_image image;
    image.regions["maincpu"] = encrypted_program(k);
    image.regions["key"].assign(k.begin(), k.end());

    cps2_system sys(std::move(image), cps2_board_params{}); // key resolved from the region
    REQUIRE(sys.executable());
    CHECK(sys.cpu().cpu_registers().pc == 0x00000008U);
}
