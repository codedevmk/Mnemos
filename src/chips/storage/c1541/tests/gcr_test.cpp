#include <mnemos/chips/storage/c1541/gcr.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using mnemos::chips::storage::c1541::gcr_decode_5to4;
using mnemos::chips::storage::c1541::gcr_encode_4to5;

TEST_CASE("gcr 4-to-5 round-trips arbitrary data") {
    const std::array<std::array<std::uint8_t, 4>, 4> cases = {{
        {0x00, 0x00, 0x00, 0x00},
        {0xFF, 0xFF, 0xFF, 0xFF},
        {0x12, 0x34, 0x56, 0x78},
        {0xDE, 0xAD, 0xBE, 0xEF},
    }};
    for (const auto& in : cases) {
        std::array<std::uint8_t, 5> gcr{};
        gcr_encode_4to5(in, gcr);
        std::array<std::uint8_t, 4> back{};
        REQUIRE(gcr_decode_5to4(gcr, back));
        CHECK(back == in);
    }
}

TEST_CASE("gcr encodes to five bytes with no long zero runs") {
    // Every encoded nibble is a 5-bit codeword with at most two consecutive zeros,
    // so a stream of zero data still produces non-zero GCR bytes.
    std::array<std::uint8_t, 5> gcr{};
    gcr_encode_4to5({0x00, 0x00, 0x00, 0x00}, gcr);
    bool any_nonzero = false;
    for (const std::uint8_t b : gcr) {
        any_nonzero = any_nonzero || (b != 0U);
    }
    CHECK(any_nonzero);
}

TEST_CASE("gcr rejects illegal codewords") {
    std::array<std::uint8_t, 4> out{};
    // All-zero GCR bytes decode to codeword 0 (0b00000), which is illegal.
    CHECK_FALSE(gcr_decode_5to4({0x00, 0x00, 0x00, 0x00, 0x00}, out));
}
