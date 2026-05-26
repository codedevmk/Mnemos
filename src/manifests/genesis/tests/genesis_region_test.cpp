// Genesis cart-header market parsing. Verifies parse_market() returns the
// right project-wide market category for the documented cart-byte encodings,
// and that the cross-system default_video_for() policy composes cleanly on
// top.

#include "genesis_region.hpp"
#include "region.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

    std::vector<std::uint8_t> rom_with_country(const char* code) {
        std::vector<std::uint8_t> rom(0x200, 0x20U); // padded with spaces
        for (std::size_t i = 0; i < 4 && code[i] != '\0'; ++i) {
            rom[0x1F0 + i] = static_cast<std::uint8_t>(code[i]);
        }
        return rom;
    }

} // namespace

TEST_CASE("genesis parse_market: pure-letter carts") {
    using mnemos::manifests::genesis::parse_market;
    CHECK(parse_market(rom_with_country("J  ")) == mnemos::market::japan);
    CHECK(parse_market(rom_with_country("U  ")) == mnemos::market::americas);
    CHECK(parse_market(rom_with_country("E  ")) == mnemos::market::europe);
    CHECK(parse_market(rom_with_country("K  ")) == mnemos::market::japan); // Korea -> Japan
}

TEST_CASE("genesis parse_market: multi-region carts") {
    using mnemos::manifests::genesis::parse_market;
    CHECK(parse_market(rom_with_country("UE ")) == mnemos::market::multi_region);
    CHECK(parse_market(rom_with_country("JU ")) == mnemos::market::multi_region);
    CHECK(parse_market(rom_with_country("JE ")) == mnemos::market::multi_region);
    CHECK(parse_market(rom_with_country("EJU")) == mnemos::market::multi_region);
}

TEST_CASE("genesis parse_market: hex-bitfield byte") {
    // bit 0 = Japan, bit 2 = USA, bit 3 = Europe.
    using mnemos::manifests::genesis::parse_market;
    CHECK(parse_market(rom_with_country("1  ")) == mnemos::market::japan);
    CHECK(parse_market(rom_with_country("4  ")) == mnemos::market::americas);
    CHECK(parse_market(rom_with_country("8  ")) == mnemos::market::europe);
    CHECK(parse_market(rom_with_country("F  ")) == mnemos::market::multi_region);
    CHECK(parse_market(rom_with_country("C  ")) == mnemos::market::multi_region); // U+E
}

TEST_CASE("genesis parse_market: ASCII 'E' is the Europe letter, never hex 0xE") {
    using mnemos::manifests::genesis::parse_market;
    // A pure 'E' cart must come back as Europe, not as the hex 0xE (which
    // would imply USA+Europe+reserved bits and collapse to multi_region).
    CHECK(parse_market(rom_with_country("E  ")) == mnemos::market::europe);
}

TEST_CASE("genesis parse_market: edge cases") {
    using mnemos::manifests::genesis::parse_market;
    CHECK(parse_market(rom_with_country("   ")) == mnemos::market::unknown);
    const std::vector<std::uint8_t> short_rom{0x00U, 0x01U, 0x02U};
    CHECK(parse_market(short_rom) == mnemos::market::unknown);
}

TEST_CASE("genesis cart -> video_region via the project-wide policy") {
    using mnemos::default_video_for;
    using mnemos::manifests::genesis::parse_market;
    // End-to-end: cart bytes -> market -> default video standard.
    CHECK(default_video_for(parse_market(rom_with_country("J  "))) == mnemos::video_region::ntsc);
    CHECK(default_video_for(parse_market(rom_with_country("U  "))) == mnemos::video_region::ntsc);
    CHECK(default_video_for(parse_market(rom_with_country("E  "))) == mnemos::video_region::pal);
    CHECK(default_video_for(parse_market(rom_with_country("UE "))) == mnemos::video_region::ntsc);
    CHECK(default_video_for(parse_market(rom_with_country("   "))) == mnemos::video_region::ntsc);
}
