#include "sha256.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace {
    using mnemos::foundation::sha256;
}

TEST_CASE("sha256 matches the FIPS 180-4 reference vectors") {
    CHECK(sha256("").hex() == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256("abc").hex() ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").hex() ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha256 hashes raw bytes and compares digests") {
    const std::array<std::uint8_t, 3> bytes{0x61U, 0x62U, 0x63U}; // "abc"
    const auto digest = sha256(std::span<const std::uint8_t>(bytes));
    CHECK(digest == sha256("abc"));
    CHECK(digest.hex().size() == 64U);
    CHECK_FALSE(digest == sha256("abd"));
}
