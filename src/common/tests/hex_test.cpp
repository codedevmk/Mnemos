#include "hex.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::common::from_hex;
    using mnemos::common::is_hex;
    using mnemos::common::to_hex;
    using mnemos::common::to_hex_upper;

    const std::vector<std::uint8_t> kDeadBeef = {0xDEU, 0xADU, 0xBEU, 0xEFU};
} // namespace

TEST_CASE("to_hex encodes lower/upper, two digits per byte", "[hex]") {
    CHECK(to_hex({}).empty());
    CHECK(to_hex(std::vector<std::uint8_t>{0x00U}) == "00");
    CHECK(to_hex(std::vector<std::uint8_t>{0x0FU}) == "0f");
    CHECK(to_hex(kDeadBeef) == "deadbeef");
    CHECK(to_hex_upper(kDeadBeef) == "DEADBEEF");
}

TEST_CASE("from_hex decodes case-insensitively and round-trips", "[hex]") {
    REQUIRE(from_hex("deadbeef") == kDeadBeef);
    REQUIRE(from_hex("DeAdBeEf") == kDeadBeef); // case-insensitive
    REQUIRE(from_hex("") == std::vector<std::uint8_t>{});

    REQUIRE(from_hex(to_hex(kDeadBeef)) == kDeadBeef);
    REQUIRE(from_hex(to_hex_upper(kDeadBeef)) == kDeadBeef);
}

TEST_CASE("from_hex rejects malformed input", "[hex]") {
    CHECK_FALSE(from_hex("abc").has_value());   // odd length
    CHECK_FALSE(from_hex("zz").has_value());    // non-hex
    CHECK_FALSE(from_hex("1g").has_value());    // one bad nibble
    CHECK_FALSE(from_hex("de ad").has_value()); // space is not hex
}

TEST_CASE("is_hex validates length and alphabet", "[hex]") {
    CHECK(is_hex(""));
    CHECK(is_hex("00"));
    CHECK(is_hex("DeadBEEF"));
    CHECK_FALSE(is_hex("abc")); // odd
    CHECK_FALSE(is_hex("xy"));  // non-hex
}
