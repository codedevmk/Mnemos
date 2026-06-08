// Verifies the SMS VDP's asset_source: the decoded graphics it exposes through
// ichip_introspection::assets() -- the two CRAM palettes, the 512-pattern tile
// sheet, and one image per active SAT sprite. The consumer reads them through
// the system-agnostic contract only (no downcast to sms_vdp).

#include "asset_views.hpp"
#include "callbacks.hpp"
#include "config.hpp"
#include "sms_vdp.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace {
    using mnemos::chips::video::sms_vdp;
    using mnemos::instrumentation::asset_kind;

    void set_addr(sms_vdp& v, std::uint16_t addr, std::uint8_t code) {
        v.ctrl_write(static_cast<std::uint8_t>(addr & 0xFFU));
        v.ctrl_write(static_cast<std::uint8_t>((code << 6U) | ((addr >> 8U) & 0x3FU)));
    }
    // Write `bytes` to VRAM (code 1) starting at `addr` via the data port.
    void write_vram(sms_vdp& v, std::uint16_t addr, std::initializer_list<std::uint8_t> bytes) {
        set_addr(v, addr, 1U);
        for (std::uint8_t b : bytes) {
            v.data_write(b);
        }
    }
    void write_cram(sms_vdp& v, std::uint16_t index, std::uint8_t value) {
        set_addr(v, index, 3U);
        v.data_write(value);
    }
} // namespace

TEST_CASE("sms_vdp exposes an asset_source") {
    sms_vdp vdp;
    CHECK(vdp.introspection().assets() != nullptr);
}

TEST_CASE("sms_vdp asset palettes split CRAM into bg + sprite") {
    sms_vdp vdp;
    // CRAM entry 1 = red (--BBGGRR, R=3); entry 17 (sprite palette idx 1) = blue.
    write_cram(vdp, 1U, 0x03U);
    write_cram(vdp, 17U, 0x30U);

    auto* src = vdp.introspection().assets();
    REQUIRE(src != nullptr);
    auto pals = src->palettes();
    REQUIRE(pals.size() == 2U);

    CHECK(pals[0].name == "bg");
    CHECK(pals[0].transparent_index == -1);
    REQUIRE(pals[0].colors.size() == 16U);
    CHECK(pals[0].colors[1] == 0xFF0000U); // red

    CHECK(pals[1].name == "sprite");
    CHECK(pals[1].transparent_index == 0); // sprite colour 0 is transparent
    REQUIRE(pals[1].colors.size() == 16U);
    CHECK(pals[1].colors[1] == 0x0000FFU); // blue
}

TEST_CASE("sms_vdp tile sheet decodes every VRAM pattern on a 16-wide grid") {
    sms_vdp vdp;
    // Tile 2, row 0, plane 0 bit 7 -> pixel (0,0) of that tile is colour index 1.
    write_vram(vdp, 0x0040U, {0x80U}); // tile 2 lives at 2*32 = 0x40

    auto assets = vdp.introspection().assets()->graphics();
    REQUIRE(assets.size() >= 1U);

    const auto& sheet = assets[0];
    CHECK(sheet.kind == asset_kind::tileset);
    CHECK(sheet.name == "patterns");
    CHECK(sheet.image.width == 128U); // 16 tiles * 8 px
    CHECK(sheet.image.height == 256U);
    CHECK(sheet.tile_w == 8U);
    CHECK(sheet.tile_h == 8U);
    CHECK(sheet.image.palette == 0U);
    REQUIRE(sheet.image.well_formed());

    // Tile 2 sits at grid col 2, row 0 -> sheet pixel (16, 0).
    CHECK(sheet.image.indices[16] == 1U);
    // Its neighbour pixel is still background (index 0).
    CHECK(sheet.image.indices[17] == 0U);
}

TEST_CASE("sms_vdp decodes active SAT sprites, stopping at the terminator") {
    sms_vdp vdp;
    // Default sprite pattern base is 0 and sprites are 8x8 / 192-line mode.
    // Sprite pattern: tile 3 (VRAM 0x60), row 0 -> pixel 0 is colour 1.
    write_vram(vdp, 0x0060U, {0x80U});

    // SAT base $3F00: sprite 0 Y=10 (active), sprite 1 Y=0xD0 (list terminator).
    write_vram(vdp, 0x3F00U, {0x0AU, 0xD0U});
    // Sprite 0 X/tile pair lives at $3F00 + 128 = $3F80: X=20, tile=3.
    write_vram(vdp, 0x3F80U, {0x14U, 0x03U});

    auto assets = vdp.introspection().assets()->graphics();
    REQUIRE(assets.size() == 2U); // patterns + sprite_00 only

    const auto& spr = assets[1];
    CHECK(spr.kind == asset_kind::sprite);
    CHECK(spr.name == "sprite_00");
    CHECK(spr.image.width == 8U);
    CHECK(spr.image.height == 8U);
    CHECK(spr.image.palette == 1U); // sprite palette
    CHECK(spr.source_addr == 0x3F80U);
    REQUIRE(spr.image.well_formed());
    CHECK(spr.image.indices[0] == 1U);
    CHECK(spr.image.indices[1] == 0U);
}

TEST_CASE("sms_vdp tall-sprite mode reports 8x16 sprite assets") {
    sms_vdp vdp;
    // Reg 1 bit 1 enables 8x16 sprites; keep display defaults otherwise.
    vdp.ctrl_write(0xA2U);
    vdp.ctrl_write(0x81U); // code 2, reg 1 <- 0xA2

    write_vram(vdp, 0x3F00U, {0x0AU, 0xD0U}); // one active sprite
    write_vram(vdp, 0x3F80U, {0x14U, 0x04U}); // tile 4 (masked to even)

    auto assets = vdp.introspection().assets()->graphics();
    REQUIRE(assets.size() == 2U);
    const auto& spr = assets[1];
    CHECK(spr.image.width == 8U);
    CHECK(spr.image.height == 16U);
    REQUIRE(spr.image.well_formed());
}

namespace {
    [[nodiscard]] const mnemos::instrumentation::graphic_asset*
    find_font(std::span<const mnemos::instrumentation::graphic_asset> assets) {
        for (const auto& a : assets) {
            if (a.kind == asset_kind::font) {
                return &a;
            }
        }
        return nullptr;
    }
} // namespace

TEST_CASE("sms_vdp emits no font asset without a manifest hint") {
    sms_vdp vdp;
    CHECK(find_font(vdp.introspection().assets()->graphics()) == nullptr);
}

TEST_CASE("sms_vdp font hint surfaces a glyph tile sheet") {
    sms_vdp vdp;
    mnemos::chips::config_table cfg;
    cfg.emplace("font_first_tile", mnemos::chips::config_value{std::int64_t{4}});
    cfg.emplace("font_count", mnemos::chips::config_value{std::int64_t{96}});
    vdp.configure(cfg, mnemos::chips::callback_table{});

    // Seed glyph tile 4, row 0, plane 0 bit 7 -> pixel (0,0) is colour 1.
    write_vram(vdp, static_cast<std::uint16_t>(4 * 32), {0x80U});

    const auto* font = find_font(vdp.introspection().assets()->graphics());
    REQUIRE(font != nullptr);
    CHECK(font->name == "font");
    CHECK(font->image.width == 128U); // 16 glyphs wide
    CHECK(font->image.height == 48U); // 96 glyphs / 16 = 6 rows * 8 px
    CHECK(font->tile_w == 8U);
    CHECK(font->tile_h == 8U);
    CHECK(font->image.palette == 0U);
    CHECK(font->source_addr == 4U * 32U);
    REQUIRE(font->image.well_formed());
    CHECK(font->image.indices[0] == 1U); // first glyph's first pixel
}

TEST_CASE("sms_vdp font hint clamps an over-long range to the tile space") {
    sms_vdp vdp;
    mnemos::chips::config_table cfg;
    cfg.emplace("font_first_tile", mnemos::chips::config_value{std::int64_t{500}});
    cfg.emplace("font_count", mnemos::chips::config_value{std::int64_t{9999}});
    vdp.configure(cfg, mnemos::chips::callback_table{});

    const auto* font = find_font(vdp.introspection().assets()->graphics());
    REQUIRE(font != nullptr);
    // 512 total tiles - 500 first = 12 glyphs -> still 16 wide, one 8px row.
    CHECK(font->image.height == 8U);
    REQUIRE(font->image.well_formed());
}
