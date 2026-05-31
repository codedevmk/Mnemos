#include "decoding.hpp"
#include "encoding.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

using mnemos::text::decode_latin1;
using mnemos::text::encode_latin1;

TEST_CASE("decode_latin1 maps each byte to one char") {
    const std::vector<std::uint8_t> bytes = {0x61U, 0x62U, 0x63U}; // "abc"
    CHECK(decode_latin1(bytes) == "abc");
    CHECK(decode_latin1(std::span<const std::uint8_t>{}).empty());
}

TEST_CASE("decode_latin1 . encode_latin1 is identity for high-bit text") {
    const std::vector<std::uint8_t> bytes = {0x80U, 0xFFU, 0x00U, 0x7FU};
    const std::string decoded = decode_latin1(bytes);
    REQUIRE(decoded.size() == bytes.size());
    CHECK(encode_latin1(decoded) == bytes);
}
