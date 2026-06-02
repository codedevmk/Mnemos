#include "genesis_cart.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::manifests::genesis::cart_sram;
    using mnemos::manifests::genesis::parse_cart_sram;

    // Build a minimal ROM image carrying the header's external-RAM fields.
    std::vector<std::uint8_t> rom_with_sram(std::uint8_t type, std::uint32_t start,
                                            std::uint32_t end, bool ra = true) {
        std::vector<std::uint8_t> rom(0x200U, 0U);
        if (ra) {
            rom[0x1B0U] = 'R';
            rom[0x1B1U] = 'A';
        }
        rom[0x1B2U] = type;
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

TEST_CASE("parse_cart_sram reads an odd-byte SRAM header ($F8, the common case)") {
    const auto rom = rom_with_sram(0xF8U, 0x200001U, 0x203FFFU);
    const auto s = parse_cart_sram(rom);
    REQUIRE(s.has_value());
    CHECK(s->start == 0x200001U);
    CHECK(s->end == 0x203FFFU);
    CHECK(s->map == cart_sram::mapping::odd_byte);
    CHECK(s->byte_count() == 0x2000U); // 8 KiB: one byte per odd address
}

TEST_CASE("parse_cart_sram returns nullopt without the RA signature") {
    const auto rom = rom_with_sram(0xF8U, 0x200001U, 0x203FFFU, /*ra=*/false);
    CHECK_FALSE(parse_cart_sram(rom).has_value());
}

TEST_CASE("parse_cart_sram handles a 16-bit (word) SRAM header") {
    const auto rom = rom_with_sram(0xE0U, 0x200000U, 0x200FFFU);
    const auto s = parse_cart_sram(rom);
    REQUIRE(s.has_value());
    CHECK(s->map == cart_sram::mapping::word);
    CHECK(s->byte_count() == 0x1000U);
}

TEST_CASE("parse_cart_sram returns nullopt for a too-short ROM") {
    const std::vector<std::uint8_t> tiny(0x100U, 0U);
    CHECK_FALSE(parse_cart_sram(tiny).has_value());
}
