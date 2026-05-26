// Sega 16-bit region detection. The Mega Drive ROM header format is shared
// by Genesis, 32X, and Sega CD, so this exercises the one detector that all
// three family adapters will reuse.

#include "region.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::apps::player::adapters::detect_sega16_region;
    using mnemos::apps::player::adapters::detect_sms_region;
    using mnemos::apps::player::adapters::video_region;

    std::vector<std::uint8_t> rom_with_region_code(const char* code) {
        std::vector<std::uint8_t> rom(0x200, 0x20U); // padded with spaces
        for (std::size_t i = 0; i < 3 && code[i] != '\0'; ++i) {
            rom[0x1F0 + i] = static_cast<std::uint8_t>(code[i]);
        }
        return rom;
    }

} // namespace

TEST_CASE("detect_sega16_region: pure-region carts") {
    CHECK(detect_sega16_region(rom_with_region_code("J  ")) == video_region::ntsc);
    CHECK(detect_sega16_region(rom_with_region_code("U  ")) == video_region::ntsc);
    CHECK(detect_sega16_region(rom_with_region_code("E  ")) == video_region::pal);
}

TEST_CASE("detect_sega16_region: multi-region carts prefer PAL when Europe present") {
    CHECK(detect_sega16_region(rom_with_region_code("UE ")) == video_region::pal);
    CHECK(detect_sega16_region(rom_with_region_code("JE ")) == video_region::pal);
    CHECK(detect_sega16_region(rom_with_region_code("EJU")) == video_region::pal);

    // No Europe -> NTSC even when multi-region.
    CHECK(detect_sega16_region(rom_with_region_code("JU ")) == video_region::ntsc);
}

TEST_CASE("detect_sega16_region: hex-bitfield region byte") {
    // bit 0=J, bit 1=U, bit 2=E.
    CHECK(detect_sega16_region(rom_with_region_code("4  ")) == video_region::pal);  // E only
    CHECK(detect_sega16_region(rom_with_region_code("F  ")) == video_region::pal);  // J+U+E+extra
    CHECK(detect_sega16_region(rom_with_region_code("3  ")) == video_region::ntsc); // J+U
    CHECK(detect_sega16_region(rom_with_region_code("1  ")) == video_region::ntsc); // J only
}

TEST_CASE("detect_sega16_region: edge cases") {
    // Blank region -> safe default NTSC.
    CHECK(detect_sega16_region(rom_with_region_code("   ")) == video_region::ntsc);
    // Truncated ROM -> safe default NTSC.
    const std::vector<std::uint8_t> short_rom{0x00U, 0x01U, 0x02U};
    CHECK(detect_sega16_region(short_rom) == video_region::ntsc);
}

TEST_CASE("detect_sms_region stub: NTSC default until the SMS adapter lands") {
    std::vector<std::uint8_t> rom(0x8000, 0x00U);
    CHECK(detect_sms_region(rom) == video_region::ntsc);
}
