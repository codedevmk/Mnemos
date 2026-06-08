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
    vic.write(0x27U, 0x05U); // sprite 0 colour = 5 (a set hi-res pixel uses this)

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
    CHECK(spr0->image.indices[0] == 5U); // set hi-res pixel -> sprite colour 5
    CHECK(spr0->image.indices[1] == 0U); // clear pixel -> transparent (index 0)

    CHECK(find(assets, asset_kind::sprite, "sprite_7") != nullptr);
}

TEST_CASE("vic_ii decodes a multicolour sprite into its four colours") {
    vic_ii_6569 vic;
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    std::vector<std::uint8_t> char_rom(0x1000U, 0U);
    std::vector<std::uint8_t> color_ram(0x0400U, 0U);

    // Sprite 0 pointer $10 -> data $0400. Row 0 byte 0 = 0b01_10_11_00 = 0x6C:
    // pair 0 -> $D025, pair 1 -> sprite colour, pair 2 -> $D026, pair 3 -> clear.
    ram[0x07F8U] = 0x10U;
    ram[0x0400U] = 0x6CU;

    vic.attach_memory({.ram = std::span<const std::uint8_t>(ram),
                       .char_rom = std::span<const std::uint8_t>(char_rom),
                       .color_ram = std::span<const std::uint8_t>(color_ram)});
    vic.set_bank(0U);
    vic.write(0x18U, 0x14U); // VM base $0400
    vic.write(0x1CU, 0x01U); // sprite 0 multicolour
    vic.write(0x25U, 0x02U); // sprite multicolour 0 = 2
    vic.write(0x26U, 0x07U); // sprite multicolour 1 = 7
    vic.write(0x27U, 0x05U); // sprite 0 colour = 5

    auto assets = vic.introspection().assets()->graphics();
    const auto* spr0 = find(assets, asset_kind::sprite, "sprite_0");
    REQUIRE(spr0 != nullptr);
    CHECK(spr0->image.width == 24U); // multicolour pixels are doubled in width
    REQUIRE(spr0->image.well_formed());
    // Each 2-bit pair fills two columns.
    CHECK(spr0->image.indices[0] == 2U); // 01 -> $D025
    CHECK(spr0->image.indices[1] == 2U);
    CHECK(spr0->image.indices[2] == 5U); // 10 -> sprite colour
    CHECK(spr0->image.indices[4] == 7U); // 11 -> $D026
    CHECK(spr0->image.indices[6] == 0U); // 00 -> transparent
}

TEST_CASE("vic_ii emits no bitmap asset in text mode") {
    vic_ii_6569 vic; // default boot is text mode (BMM clear)
    auto assets = vic.introspection().assets()->graphics();
    CHECK(find(assets, asset_kind::bitmap, "bitmap") == nullptr);
}

TEST_CASE("vic_ii decodes a standard hi-res bitmap screen") {
    vic_ii_6569 vic;
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    std::vector<std::uint8_t> char_rom(0x1000U, 0U);
    std::vector<std::uint8_t> color_ram(0x0400U, 0U);

    // Screen cell 0 colours: hi nibble 1 (set bit), lo nibble 15 (clear bit).
    ram[0x0400U] = 0x1FU;
    // Bitmap cell 0, row 0: bit 7 set -> col 0 foreground, col 1 background.
    ram[0x2000U] = 0x80U;

    vic.attach_memory({.ram = std::span<const std::uint8_t>(ram),
                       .char_rom = std::span<const std::uint8_t>(char_rom),
                       .color_ram = std::span<const std::uint8_t>(color_ram)});
    vic.set_bank(0U);
    vic.write(0x18U, 0x18U); // VM base $0400, bitmap base $2000
    vic.write(0x11U, 0x20U); // BMM (bitmap mode)

    auto assets = vic.introspection().assets()->graphics();
    const auto* bmp = find(assets, asset_kind::bitmap, "bitmap");
    REQUIRE(bmp != nullptr);
    CHECK(bmp->image.width == 320U);
    CHECK(bmp->image.height == 200U);
    CHECK(bmp->image.palette == 0U);
    CHECK(bmp->source_addr == 0x2000U);
    REQUIRE(bmp->image.well_formed());
    CHECK(bmp->image.indices[0] == 1U);  // set pixel -> screen hi nibble
    CHECK(bmp->image.indices[1] == 15U); // clear pixel -> screen lo nibble
}

TEST_CASE("vic_ii decodes a multicolour bitmap screen") {
    vic_ii_6569 vic;
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    std::vector<std::uint8_t> char_rom(0x1000U, 0U);
    std::vector<std::uint8_t> color_ram(0x0400U, 0U);

    ram[0x0400U] = 0x12U;       // screen cell 0: hi nibble 1, lo nibble 2
    color_ram[0x0000U] = 0x03U; // colour RAM cell 0 = 3
    // Bitmap cell 0, row 0 = 0b00_01_10_11: pairs select bg0 / scr-hi / scr-lo / cram.
    ram[0x2000U] = 0x1BU;

    vic.attach_memory({.ram = std::span<const std::uint8_t>(ram),
                       .char_rom = std::span<const std::uint8_t>(char_rom),
                       .color_ram = std::span<const std::uint8_t>(color_ram)});
    vic.set_bank(0U);
    vic.write(0x18U, 0x18U); // VM base $0400, bitmap base $2000
    vic.write(0x11U, 0x20U); // BMM
    vic.write(0x16U, 0x10U); // MCM (multicolour)
    vic.write(0x21U, 0x04U); // background colour 0 = 4

    auto assets = vic.introspection().assets()->graphics();
    const auto* bmp = find(assets, asset_kind::bitmap, "bitmap");
    REQUIRE(bmp != nullptr);
    REQUIRE(bmp->image.well_formed());
    // Each 2-bit pair fills two columns: 00->bg0, 01->scr hi, 10->scr lo, 11->cram.
    CHECK(bmp->image.indices[0] == 4U);
    CHECK(bmp->image.indices[2] == 1U);
    CHECK(bmp->image.indices[4] == 2U);
    CHECK(bmp->image.indices[6] == 3U);
}
