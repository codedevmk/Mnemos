// SMS / Game Gear cart-header parsing. Verifies parse_cart_info() returns the
// right (market, target) pair for each documented country-nibble, and that
// the cross-system default_video_for() policy composes cleanly on top.

#include "region.hpp"
#include "sms_region.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    std::vector<std::uint8_t> rom_with_nibble(std::uint8_t nibble) {
        std::vector<std::uint8_t> rom(0x8000U, 0x00U);
        rom[0x7FFFU] = static_cast<std::uint8_t>((nibble & 0x0FU) << 4U);
        return rom;
    }

} // namespace

TEST_CASE("sms parse_cart_info: country-nibble table coverage") {
    using mnemos::manifests::sms::cart_target;
    using mnemos::manifests::sms::parse_cart_info;

    const auto sms_j = parse_cart_info(rom_with_nibble(0x3U));
    CHECK(sms_j.market == mnemos::market::japan);
    CHECK(sms_j.target == cart_target::sms);

    const auto sms_e = parse_cart_info(rom_with_nibble(0x4U));
    CHECK(sms_e.market == mnemos::market::multi_region);
    CHECK(sms_e.target == cart_target::sms);

    const auto gg_j = parse_cart_info(rom_with_nibble(0x5U));
    CHECK(gg_j.market == mnemos::market::japan);
    CHECK(gg_j.target == cart_target::gg);

    const auto gg_e = parse_cart_info(rom_with_nibble(0x6U));
    CHECK(gg_e.market == mnemos::market::multi_region);
    CHECK(gg_e.target == cart_target::gg);

    const auto gg_i = parse_cart_info(rom_with_nibble(0x7U));
    CHECK(gg_i.market == mnemos::market::multi_region);
    CHECK(gg_i.target == cart_target::gg);
}

TEST_CASE("sms parse_cart_info: unknown nibbles + truncated ROMs") {
    using mnemos::manifests::sms::cart_target;
    using mnemos::manifests::sms::parse_cart_info;

    const auto zero = parse_cart_info(rom_with_nibble(0x0U));
    CHECK(zero.market == mnemos::market::unknown);
    CHECK(zero.target == cart_target::unknown);

    const auto fifteen = parse_cart_info(rom_with_nibble(0xFU));
    CHECK(fifteen.market == mnemos::market::unknown);
    CHECK(fifteen.target == cart_target::unknown);

    const std::vector<std::uint8_t> short_rom{0x00U, 0x01U};
    const auto truncated = parse_cart_info(short_rom);
    CHECK(truncated.market == mnemos::market::unknown);
    CHECK(truncated.target == cart_target::unknown);
}

TEST_CASE("sms parse_market is the cart_info market projection") {
    using mnemos::manifests::sms::parse_cart_info;
    using mnemos::manifests::sms::parse_market;
    CHECK(parse_market(rom_with_nibble(0x3U)) == parse_cart_info(rom_with_nibble(0x3U)).market);
    CHECK(parse_market(rom_with_nibble(0x4U)) == parse_cart_info(rom_with_nibble(0x4U)).market);
}

TEST_CASE("sms cart -> video_region via the project-wide policy") {
    using mnemos::default_video_for;
    using mnemos::manifests::sms::parse_market;
    // SMS cart bytes never encode video standard, so the default is NTSC in
    // every case. The user picks PAL explicitly when their console is PAL.
    CHECK(default_video_for(parse_market(rom_with_nibble(0x3U))) == mnemos::video_region::ntsc);
    CHECK(default_video_for(parse_market(rom_with_nibble(0x4U))) == mnemos::video_region::ntsc);
    CHECK(default_video_for(parse_market(rom_with_nibble(0x5U))) == mnemos::video_region::ntsc);
}
