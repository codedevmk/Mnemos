#include "decoding.hpp"
#include "encoding.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

using mnemos::text::decode_latin1;
using mnemos::text::encode_latin1;

TEST_CASE("encode_latin1 takes the low 8 bits of each char") {
    const auto bytes = encode_latin1(std::string("AB"));
    REQUIRE(bytes.size() == 2U);
    CHECK(bytes[0] == 0x41U);
    CHECK(bytes[1] == 0x42U);
}

TEST_CASE("encode_latin1 . decode_latin1 is identity over all 256 byte values") {
    std::vector<std::uint8_t> all(256);
    for (int i = 0; i < 256; ++i) {
        all[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(i);
    }

    const std::string decoded = decode_latin1(all);
    const std::vector<std::uint8_t> reencoded = encode_latin1(decoded);
    CHECK(reencoded == all);
}
