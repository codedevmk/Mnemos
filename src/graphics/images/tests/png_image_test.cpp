// png_image has no in-tree decoder to round-trip against, so the test checks
// the container structurally (signature, IHDR, per-chunk CRC) and inflates the
// IDAT back to the expected filtered scanlines.

#include "png_image.hpp"

#include "crc32.hpp"
#include "inflate.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace {

    std::uint32_t read_be32(std::span<const std::uint8_t> b, std::size_t off) {
        return (static_cast<std::uint32_t>(b[off]) << 24U) |
               (static_cast<std::uint32_t>(b[off + 1]) << 16U) |
               (static_cast<std::uint32_t>(b[off + 2]) << 8U) |
               static_cast<std::uint32_t>(b[off + 3]);
    }

    struct chunk final {
        std::array<std::uint8_t, 4> type{};
        std::vector<std::uint8_t> data;
        std::uint32_t crc{};
    };

    // Walk the PNG chunk stream after the 8-byte signature.
    std::vector<chunk> parse_chunks(std::span<const std::uint8_t> png) {
        std::vector<chunk> chunks;
        std::size_t off = 8;
        while (off + 12 <= png.size()) {
            const std::uint32_t len = read_be32(png, off);
            // Guard a malformed/oversized length so a bad encoder yields a clean
            // assertion failure rather than reading past the buffer.
            if (len > png.size() - off - 12) {
                break;
            }
            chunk c;
            for (std::size_t i = 0; i < 4; ++i) {
                c.type[i] = png[off + 4 + i];
            }
            c.data.assign(png.begin() + static_cast<std::ptrdiff_t>(off + 8),
                          png.begin() + static_cast<std::ptrdiff_t>(off + 8 + len));
            c.crc = read_be32(png, off + 8 + len);
            chunks.push_back(std::move(c));
            off += 12 + len;
        }
        return chunks;
    }

    const chunk* find(const std::vector<chunk>& chunks, const char (&name)[5]) {
        for (const chunk& c : chunks) {
            if (c.type[0] == name[0] && c.type[1] == name[1] && c.type[2] == name[2] &&
                c.type[3] == name[3]) {
                return &c;
            }
        }
        return nullptr;
    }

} // namespace

TEST_CASE("png_image emits a structurally valid truecolour PNG", "[png]") {
    // 3x2, six distinct colours.
    const std::vector<std::uint32_t> pixels = {0xFF0000U, 0x00FF00U, 0x0000FFU,
                                               0xFFFFFFU, 0x000000U, 0x123456U};
    const mnemos::graphics::images::png_image img(3, 2, pixels);
    const std::vector<std::uint8_t> png = img.encode();

    // Signature.
    const std::array<std::uint8_t, 8> sig = {0x89U, 0x50U, 0x4EU, 0x47U,
                                             0x0DU, 0x0AU, 0x1AU, 0x0AU};
    REQUIRE(png.size() > 8U);
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK(png[i] == sig[i]);
    }

    const std::vector<chunk> chunks = parse_chunks(png);

    // Every chunk's CRC must cover type+data.
    for (const chunk& c : chunks) {
        std::uint32_t crc = mnemos::security::cryptography::crc32(c.type);
        crc = mnemos::security::cryptography::crc32(c.data, crc);
        CHECK(crc == c.crc);
    }

    // IHDR: 3x2, 8-bit, colour type 2 (RGB), no interlace.
    const chunk* ihdr = find(chunks, "IHDR");
    REQUIRE(ihdr != nullptr);
    REQUIRE(ihdr->data.size() == 13U);
    CHECK(read_be32(ihdr->data, 0) == 3U);
    CHECK(read_be32(ihdr->data, 4) == 2U);
    CHECK(ihdr->data[8] == 0x08U);
    CHECK(ihdr->data[9] == 0x02U);
    CHECK(ihdr->data[12] == 0x00U);

    // IEND present and empty; IDAT present.
    const chunk* iend = find(chunks, "IEND");
    REQUIRE(iend != nullptr);
    CHECK(iend->data.empty());
    const chunk* idat = find(chunks, "IDAT");
    REQUIRE(idat != nullptr);

    // Inflate the IDAT zlib stream (strip 2-byte header + 4-byte Adler-32) and
    // compare to the expected None-filtered scanlines.
    REQUIRE(idat->data.size() > 6U);
    const std::span<const std::uint8_t> body(idat->data.data() + 2, idat->data.size() - 6U);
    std::vector<std::uint8_t> filtered((3U * 3U + 1U) * 2U);
    const auto n = mnemos::compression::inflate_raw(body, filtered);
    REQUIRE(n.has_value());
    REQUIRE(*n == filtered.size());

    const std::vector<std::uint8_t> expected = {
        0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, // row 0: filter + RGB*3
        0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x12, 0x34, 0x56, // row 1
    };
    CHECK(filtered == expected);
}

TEST_CASE("indexed_png_image emits an indexed PNG with PLTE + tRNS", "[png]") {
    // 2x2 image of indices 0..3 over a 4-colour palette; index 0 transparent.
    const std::vector<std::uint8_t> indices = {0U, 1U, 2U, 3U};
    const std::vector<std::uint32_t> palette = {0x000000U, 0xFF0000U, 0x00FF00U, 0x0000FFU};
    const mnemos::graphics::images::indexed_png_image img(2, 2, indices, palette, 0);
    const std::vector<std::uint8_t> png = img.encode();

    const std::array<std::uint8_t, 8> sig = {0x89U, 0x50U, 0x4EU, 0x47U,
                                             0x0DU, 0x0AU, 0x1AU, 0x0AU};
    REQUIRE(png.size() > 8U);
    for (std::size_t i = 0; i < 8; ++i) {
        CHECK(png[i] == sig[i]);
    }

    const std::vector<chunk> chunks = parse_chunks(png);
    for (const chunk& c : chunks) {
        std::uint32_t crc = mnemos::security::cryptography::crc32(c.type);
        crc = mnemos::security::cryptography::crc32(c.data, crc);
        CHECK(crc == c.crc);
    }

    // IHDR: 2x2, 8-bit, colour type 3 (indexed).
    const chunk* ihdr = find(chunks, "IHDR");
    REQUIRE(ihdr != nullptr);
    CHECK(read_be32(ihdr->data, 0) == 2U);
    CHECK(read_be32(ihdr->data, 4) == 2U);
    CHECK(ihdr->data[8] == 0x08U);
    CHECK(ihdr->data[9] == 0x03U);

    // PLTE holds the 4 RGB triplets in order.
    const chunk* plte = find(chunks, "PLTE");
    REQUIRE(plte != nullptr);
    REQUIRE(plte->data.size() == 12U);
    const std::vector<std::uint8_t> expected_plte = {0x00, 0x00, 0x00, 0xFF, 0x00, 0x00,
                                                     0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF};
    CHECK(plte->data == expected_plte);

    // tRNS marks index 0 transparent (single alpha byte 0x00).
    const chunk* trns = find(chunks, "tRNS");
    REQUIRE(trns != nullptr);
    REQUIRE(trns->data.size() == 1U);
    CHECK(trns->data[0] == 0x00U);

    // IDAT inflates to None-filtered index scanlines.
    const chunk* idat = find(chunks, "IDAT");
    REQUIRE(idat != nullptr);
    REQUIRE(idat->data.size() > 6U);
    const std::span<const std::uint8_t> body(idat->data.data() + 2, idat->data.size() - 6U);
    std::vector<std::uint8_t> filtered((2U + 1U) * 2U);
    const auto n = mnemos::compression::inflate_raw(body, filtered);
    REQUIRE(n.has_value());
    REQUIRE(*n == filtered.size());
    const std::vector<std::uint8_t> expected = {0x00, 0x00, 0x01,  // row 0: filter + indices
                                                0x00, 0x02, 0x03}; // row 1
    CHECK(filtered == expected);
}
