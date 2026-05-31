// deflate_raw / deflate_zlib are validated by round-tripping through the
// independent inflate_raw decoder, plus structural checks on the zlib wrapper.

#include "deflate.hpp"
#include "inflate.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {

    using mnemos::compression::deflate_raw;
    using mnemos::compression::deflate_zlib;
    using mnemos::compression::inflate_raw;

    // deflate_raw(input) must inflate_raw back to exactly input.
    void check_roundtrip(const std::vector<std::uint8_t>& input) {
        const std::vector<std::uint8_t> compressed = deflate_raw(input);
        std::vector<std::uint8_t> out(input.size());
        const auto n = inflate_raw(compressed, out);
        REQUIRE(n.has_value());
        CHECK(*n == input.size());
        CHECK(out == input);
    }

    std::vector<std::uint8_t> bytes_of(std::string_view text) { return {text.begin(), text.end()}; }

    // Deterministic pseudo-random fill (LCG) -- near-incompressible input.
    std::vector<std::uint8_t> lcg_bytes(std::size_t count, std::uint32_t seed) {
        std::vector<std::uint8_t> v;
        v.reserve(count);
        std::uint32_t state = seed;
        for (std::size_t i = 0; i < count; ++i) {
            state = state * 1664525U + 1013904223U;
            v.push_back(static_cast<std::uint8_t>(state >> 24U));
        }
        return v;
    }

} // namespace

TEST_CASE("deflate_raw round-trips through inflate_raw", "[deflate]") {
    check_roundtrip({});                        // empty
    check_roundtrip({0x42});                    // single byte
    check_roundtrip(bytes_of("hello, mnemos")); // short text
    check_roundtrip(bytes_of(std::string_view(  // repetitive
        "Mnemos Mnemos Mnemos Mnemos Mnemos Mnemos Mnemos Mnemos Mnemos Mnemos")));

    std::vector<std::uint8_t> runs(4096, 0xABU); // long run of one byte
    check_roundtrip(runs);

    check_roundtrip(lcg_bytes(8192, 0x1234U)); // near-incompressible
}

TEST_CASE("deflate_raw round-trips skewed distributions (dynamic Huffman path)", "[deflate]") {
    // A dominant byte plus a long thin tail of many distinct values: a skewed
    // histogram that drives dynamic-Huffman code lengths toward (and past) the
    // 15-bit cap, exercising the length-limiter.
    std::vector<std::uint8_t> input;
    input.reserve(50000);
    std::uint32_t state = 0xC0FFEEU;
    for (std::size_t i = 0; i < 50000; ++i) {
        state = state * 1664525U + 1013904223U;
        // ~94% zeros, the rest spread across all 256 values.
        input.push_back((state >> 28U) < 15U ? static_cast<std::uint8_t>(state >> 8U) : 0x00U);
    }
    check_roundtrip(input);

    // Natural-language-ish text repeated -- the case dynamic Huffman wins on.
    std::vector<std::uint8_t> prose;
    const std::string line = "the quick brown fox jumps over the lazy dog; ";
    for (int i = 0; i < 400; ++i) {
        prose.insert(prose.end(), line.begin(), line.end());
    }
    check_roundtrip(prose);
}

TEST_CASE("deflate_raw round-trips a structured larger buffer", "[deflate]") {
    std::vector<std::uint8_t> input;
    input.reserve(40000);
    for (std::size_t i = 0; i < 40000; ++i) {
        // Periodic + slow ramp: plenty of matchable structure across the window.
        input.push_back(static_cast<std::uint8_t>((i % 251) ^ (i / 1000)));
    }
    check_roundtrip(input);
}

TEST_CASE("deflate_raw actually compresses repetitive input", "[deflate]") {
    const std::vector<std::uint8_t> runs(16384, 0x5AU);
    const std::vector<std::uint8_t> compressed = deflate_raw(runs);
    CHECK(compressed.size() < runs.size() / 8); // a 16K run is tiny once matched
}

TEST_CASE("deflate_raw never expands incompressible input badly", "[deflate]") {
    const std::vector<std::uint8_t> noise = lcg_bytes(4096, 0x99U);
    const std::vector<std::uint8_t> compressed = deflate_raw(noise);
    // Stored fallback caps overhead at 5 bytes per 64K block.
    CHECK(compressed.size() <= noise.size() + 16);
}

TEST_CASE("deflate_zlib wraps a valid RFC 1950 stream", "[deflate]") {
    const std::vector<std::uint8_t> input = bytes_of("PNG IDAT payload goes here, repeated. "
                                                     "PNG IDAT payload goes here, repeated.");
    const std::vector<std::uint8_t> z = deflate_zlib(input);

    REQUIRE(z.size() >= 6U);
    CHECK(z[0] == 0x78U);
    CHECK(z[1] == 0x9CU);
    CHECK(((static_cast<unsigned>(z[0]) << 8U) | z[1]) % 31U == 0U); // header checksum

    // Body between the 2-byte header and the 4-byte Adler-32 trailer must
    // inflate back to the input.
    const std::span<const std::uint8_t> body(z.data() + 2, z.size() - 6U);
    std::vector<std::uint8_t> out(input.size());
    const auto n = inflate_raw(body, out);
    REQUIRE(n.has_value());
    CHECK(*n == input.size());
    CHECK(out == input);

    // Adler-32 trailer (big-endian) over the original input.
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (const std::uint8_t byte : input) {
        a = (a + byte) % 65521U;
        b = (b + a) % 65521U;
    }
    const std::uint32_t adler = (b << 16U) | a;
    const std::uint32_t trailer = (static_cast<std::uint32_t>(z[z.size() - 4]) << 24U) |
                                  (static_cast<std::uint32_t>(z[z.size() - 3]) << 16U) |
                                  (static_cast<std::uint32_t>(z[z.size() - 2]) << 8U) |
                                  static_cast<std::uint32_t>(z[z.size() - 1]);
    CHECK(trailer == adler);
}
