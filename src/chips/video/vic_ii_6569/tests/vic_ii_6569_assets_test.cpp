// Verifies the VIC-II's asset_source: the fixed 16-colour C64 palette, the
// current character generator as a "font" sheet, and the 8 hardware sprites,
// read through the system-agnostic contract only (no downcast to vic_ii_6569).

#include "asset_views.hpp"
#include "vic_ii_6569.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {
    using mnemos::chips::video::vic_ii_6569;
    using mnemos::instrumentation::asset_kind;

    [[nodiscard]] const mnemos::instrumentation::graphic_asset*
    find(std::span<const mnemos::instrumentation::graphic_asset> assets, asset_kind kind,
         std::string_view name) {
        for (const auto& a : assets) {
            if (a.kind == kind && a.name == name) {
                return &a;
            }
        }
        return nullptr;
    }
} // namespace

TEST_CASE("vic_ii exposes an asset_source") {
    vic_ii_6569 vic;
    CHECK(vic.introspection().assets() != nullptr);
}

TEST_CASE("vic_ii asset palette is the fixed 16-colour C64 palette") {
    vic_ii_6569 vic;
    auto* src = vic.introspection().assets();
    REQUIRE(src != nullptr);
    auto pals = src->palettes();
    REQUIRE(pals.size() == 1U);
    CHECK(pals[0].name == "c64");
    REQUIRE(pals[0].colors.size() == 16U);
    // The palette matches the chip's own colour table.
    for (std::uint8_t i = 0; i < 16U; ++i) {
        CHECK(pals[0].colors[i] == vic_ii_6569::color_rgb888(i));
    }
}

TEST_CASE("vic_ii surfaces the character generator as a font sheet and 8 sprites") {
    vic_ii_6569 vic;
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    std::vector<std::uint8_t> char_rom(0x1000U, 0U);
    std::vector<std::uint8_t> color_ram(0x0400U, 0U);

    // Glyph 0, row 0, bit 7 -> font pixel (0,0) is set. (Bank 0 shadows the
    // char ROM into VIC $1000-$1FFF, the default char base.)
    char_rom[0x0000U] = 0x80U;
    // Sprite 0 pointer at $07F8 -> $10; sprite data at $0400, row 0 bit 7 set.
    ram[0x07F8U] = 0x10U;
    ram[0x0400U] = 0x80U;

    vic.attach_memory({.ram = std::span<const std::uint8_t>(ram),
                       .char_rom = std::span<const std::uint8_t>(char_rom),
                       .color_ram = std::span<const std::uint8_t>(color_ram)});
    vic.set_bank(0U);
    vic.write(0x18U, 0x14U); // VM base $0400, char base $1000 (ROM shadow)

    auto assets = vic.introspection().assets()->graphics();
    // charset + 8 sprites.
    REQUIRE(assets.size() == 9U);

    const auto* font = find(assets, asset_kind::font, "charset");
    REQUIRE(font != nullptr);
    CHECK(font->image.width == 128U);  // 16 glyphs wide
    CHECK(font->image.height == 128U); // 256 / 16 = 16 rows * 8 px
    CHECK(font->tile_w == 8U);
    CHECK(font->image.palette == 0U);
    REQUIRE(font->image.well_formed());
    CHECK(font->image.indices[0] == 1U); // glyph 0's top-left pixel

    const auto* spr0 = find(assets, asset_kind::sprite, "sprite_0");
    REQUIRE(spr0 != nullptr);
    CHECK(spr0->image.width == 24U);
    CHECK(spr0->image.height == 21U);
    CHECK(spr0->source_addr == 0x0400U); // pointer $10 * 64
    REQUIRE(spr0->image.well_formed());
    CHECK(spr0->image.indices[0] == 1U); // top-left sprite pixel

    CHECK(find(assets, asset_kind::sprite, "sprite_7") != nullptr);
}
