#include "genesis_cart.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::manifests::genesis::parse_cart_sram;

    // Build a minimal ROM image carrying the header's external-RAM fields.
    std::vector<std::uint8_t> rom_with_sram(std::uint32_t start, std::uint32_t end,
                                            bool ra = true) {
        std::vector<std::uint8_t> rom(0x200U, 0U);
        if (ra) {
            rom[0x1B0U] = 'R';
            rom[0x1B1U] = 'A';
        }
        const auto be32 = [&rom](std::uint32_t off, std::uint32_t v) {
            rom[off] = static_cast<std::uint8_t>(v >> 24U);
            rom[off + 1U] = static_cast<std::uint8_t>(v >> 16U);
            rom[off + 2U] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 3U] = static_cast<std::uint8_t>(v);
        };
        be32(0x1B4U, start);
        be32(0x1B8U, end);
        return rom;
    }
} // namespace

TEST_CASE("parse_cart_sram reads an odd-byte SRAM window (the common case)") {
    const auto s = parse_cart_sram(rom_with_sram(0x200001U, 0x203FFFU));
    REQUIRE(s.has_value());
    CHECK(s->start == 0x200001U);
    CHECK(s->end == 0x203FFFU);
    CHECK(s->byte_count() == 0x3FFFU); // one backing cell per window address
}

TEST_CASE("parse_cart_sram reads a word-wide SRAM window") {
    const auto s = parse_cart_sram(rom_with_sram(0x200000U, 0x200FFFU));
    REQUIRE(s.has_value());
    CHECK(s->start == 0x200000U);
    CHECK(s->byte_count() == 0x1000U);
}

TEST_CASE("parse_cart_sram returns nullopt without the RA signature") {
    CHECK_FALSE(parse_cart_sram(rom_with_sram(0x200001U, 0x203FFFU, /*ra=*/false)).has_value());
}

TEST_CASE("parse_cart_sram returns nullopt for a too-short ROM") {
    const std::vector<std::uint8_t> tiny(0x100U, 0U);
    CHECK_FALSE(parse_cart_sram(tiny).has_value());
}

TEST_CASE("parse_cart_sram masks addresses to the 24-bit bus") {
    // High bytes above bit 23 are bus-invisible; only $200001 survives.
    const auto s = parse_cart_sram(rom_with_sram(0x12200001U, 0x00203FFFU));
    REQUIRE(s.has_value());
    CHECK(s->start == 0x200001U);
    CHECK(s->end == 0x203FFFU);
}

TEST_CASE("parse_cart_sram disables SRAM mislabelled at work-RAM ($FF0000)") {
    CHECK_FALSE(parse_cart_sram(rom_with_sram(0xFF0000U, 0xFFFFFFU)).has_value());
}

TEST_CASE("parse_cart_sram relocates an out-of-window start (>= $800000)") {
    const auto s = parse_cart_sram(rom_with_sram(0x00A00000U, 0x00A0FFFFU));
    REQUIRE(s.has_value());
    CHECK(s->start == 0x200000U); // forced to the canonical block
    CHECK(s->end == 0x20FFFFU);
}

TEST_CASE("parse_cart_sram clamps an oversized window to 64 KiB") {
    const auto s = parse_cart_sram(rom_with_sram(0x200000U, 0x300000U)); // 1 MB declared
    REQUIRE(s.has_value());
    CHECK(s->start == 0x200000U);
    CHECK(s->end == 0x20FFFFU); // clamped to start + 0xFFFF
    CHECK(s->byte_count() == 0x10000U);
}

TEST_CASE("parse_cart_sram clamps an inverted window") {
    const auto s = parse_cart_sram(rom_with_sram(0x200100U, 0x200000U)); // end < start
    REQUIRE(s.has_value());
    CHECK(s->start == 0x200100U);
    CHECK(s->end == 0x2100FFU); // start + 0xFFFF
}
