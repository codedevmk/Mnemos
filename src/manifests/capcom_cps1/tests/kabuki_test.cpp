#include "kabuki.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using mnemos::manifests::capcom_cps1::kabuki_decode;
    using mnemos::manifests::capcom_cps1::kabuki_game;
    using mnemos::manifests::capcom_cps1::kabuki_keys_for;

    // Decode three probe bytes for `game` and return the opcode/data streams.
    // Bytes: addr 0x0000 = 0x5A, addr 0x0001 = 0x00, addr 0x1234 = 0xFF.
    struct streams final {
        std::vector<std::uint8_t> opcode;
        std::vector<std::uint8_t> data;
    };
    streams decode_probes(kabuki_game game) {
        std::vector<std::uint8_t> enc(0x8000U, 0U);
        enc[0x0000U] = 0x5AU;
        enc[0x0001U] = 0x00U;
        enc[0x1234U] = 0xFFU;
        streams s{std::vector<std::uint8_t>(0x8000U), std::vector<std::uint8_t>(0x8000U)};
        kabuki_decode(enc, kabuki_keys_for(game), s.opcode, s.data);
        return s;
    }

} // namespace

// Goldens from the independent reference port (scripts/gen_kabuki_goldens.py).
TEST_CASE("kabuki decodes the dino streams", "[kabuki]") {
    const auto s = decode_probes(kabuki_game::dino);
    CHECK(s.opcode[0x0000U] == 0x9BU);
    CHECK(s.data[0x0000U] == 0x3DU);
    CHECK(s.opcode[0x1234U] == 0xF1U);
    CHECK(s.data[0x1234U] == 0xF1U);
    CHECK(s.opcode[0x0001U] == 0x07U);
    CHECK(s.data[0x0001U] == 0x07U);
}

TEST_CASE("kabuki decodes the punisher streams", "[kabuki]") {
    const auto s = decode_probes(kabuki_game::punisher);
    CHECK(s.opcode[0x0000U] == 0xEBU);
    CHECK(s.data[0x0000U] == 0xBDU);
    CHECK(s.opcode[0x1234U] == 0xF6U);
    CHECK(s.data[0x1234U] == 0xF6U);
    CHECK(s.opcode[0x0001U] == 0x88U);
    CHECK(s.data[0x0001U] == 0x90U);
}

TEST_CASE("kabuki decodes the wof streams", "[kabuki]") {
    const auto s = decode_probes(kabuki_game::wof);
    CHECK(s.opcode[0x0000U] == 0xFEU);
    CHECK(s.data[0x0000U] == 0x52U);
    CHECK(s.opcode[0x1234U] == 0x75U);
    CHECK(s.data[0x1234U] == 0x3EU);
    CHECK(s.opcode[0x0001U] == 0x46U);
    CHECK(s.data[0x0001U] == 0x86U);
}

TEST_CASE("kabuki produces distinct opcode and data streams", "[kabuki]") {
    // The whole point of Kabuki: a byte decrypts differently as an opcode vs data
    // (different select), so the two streams diverge across the program.
    const auto s = decode_probes(kabuki_game::dino);
    CHECK(s.opcode[0x0000U] != s.data[0x0000U]);
}
