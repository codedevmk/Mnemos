// Verifies the Genesis VDP's asset_source: the four CRAM palettes, the
// 2048-pattern tile sheet, and one image per SAT sprite, read through the
// system-agnostic contract only (no downcast to genesis_vdp).

#include "asset_views.hpp"
#include "genesis_vdp.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>

namespace {
    using mnemos::chips::video::genesis_vdp;
    using mnemos::instrumentation::asset_kind;

    void set_reg(genesis_vdp& v, int r, std::uint8_t value) {
        v.write16(0x04,
                  static_cast<std::uint16_t>(0x8000U | (static_cast<unsigned>(r) << 8U) | value));
    }
    void set_command(genesis_vdp& v, std::uint32_t addr, std::uint8_t code) {
        const auto first = static_cast<std::uint16_t>(((code & 0x03U) << 14U) | (addr & 0x3FFFU));
        const auto second =
            static_cast<std::uint16_t>(((code & 0x3CU) << 2U) | ((addr >> 14U) & 0x03U));
        v.write16(0x04, first);
        v.write16(0x04, second);
    }
    void write_vram(genesis_vdp& v, std::uint32_t addr,
                    std::initializer_list<std::uint16_t> words) {
        set_command(v, addr, 0x01);
        for (const auto w : words) {
            v.write16(0x00, w);
        }
    }
    void write_cram(genesis_vdp& v, int idx, std::uint16_t color) {
        set_command(v, static_cast<std::uint32_t>(idx) << 1U, 0x03);
        v.write16(0x00, color);
    }

    // A solid 8x8 tile of colour index 1 (every 4bpp nibble = 1).
    constexpr std::initializer_list<std::uint16_t> solid_tile_1 = {
        0x1111, 0x1111, 0x1111, 0x1111, 0x1111, 0x1111, 0x1111, 0x1111,
        0x1111, 0x1111, 0x1111, 0x1111, 0x1111, 0x1111, 0x1111, 0x1111};

    // CRAM channel value 7 -> 0xEF after cram_to_rgb's 3->8 bit expansion.
    constexpr std::uint32_t rgb_red = 0x00EF0000U;
} // namespace

TEST_CASE("genesis_vdp exposes an asset_source") {
    genesis_vdp vdp;
    CHECK(vdp.introspection().assets() != nullptr);
}

TEST_CASE("genesis_vdp asset palettes split CRAM into four 16-colour banks") {
    genesis_vdp vdp;
    write_cram(vdp, 1, 0x000E);  // palette 0, colour 1 = red
    write_cram(vdp, 17, 0x000E); // palette 1, colour 1 = red

    auto* src = vdp.introspection().assets();
    REQUIRE(src != nullptr);
    auto pals = src->palettes();
    REQUIRE(pals.size() == 4U);

    CHECK(pals[0].name == "pal0");
    CHECK(pals[3].name == "pal3");
    for (const auto& p : pals) {
        CHECK(p.transparent_index == 0); // index 0 is transparent in tiles
        REQUIRE(p.colors.size() == 16U);
    }
    CHECK(pals[0].colors[1] == rgb_red);
    CHECK(pals[1].colors[1] == rgb_red);
    CHECK(pals[0].colors[2] == 0U); // untouched entry stays black
}

TEST_CASE("genesis_vdp tile sheet decodes every VRAM pattern on a 16-wide grid") {
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x04);               // M5: unlock registers
    set_reg(vdp, 15, 0x02);              // auto-increment 2
    write_vram(vdp, 0x40, solid_tile_1); // tile 2 (2*32 = 0x40)

    auto assets = vdp.introspection().assets()->graphics();
    REQUIRE(assets.size() >= 1U);

    const auto& sheet = assets[0];
    CHECK(sheet.kind == asset_kind::tileset);
    CHECK(sheet.name == "patterns");
    CHECK(sheet.image.width == 128U);   // 16 tiles * 8 px
    CHECK(sheet.image.height == 1024U); // 2048 / 16 = 128 rows * 8 px
    CHECK(sheet.tile_w == 8U);
    CHECK(sheet.image.palette == 0U);
    REQUIRE(sheet.image.well_formed());

    // Tile 2 -> grid col 2, row 0 -> sheet pixel (16, 0); solid colour 1.
    CHECK(sheet.image.indices[16] == 1U);
    CHECK(sheet.image.indices[17] == 1U);
}

TEST_CASE("genesis_vdp decodes a multi-cell SAT sprite at its palette and size") {
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x04);
    set_reg(vdp, 15, 0x02);
    set_reg(vdp, 5, 0x60); // SAT base = 0x60 << 9 = 0xC000

    write_vram(vdp, 0x80, solid_tile_1); // tile 4 = top-left sprite cell

    // SAT entry 0 at 0xC000: Y=128, size 2x2 cells + link 0, tile 4 / palette 2,
    // X=128. w1 = (w-1<<10)|(h-1<<8) = 0x0500; w2 = (pal<<13)|tile = 0x4004.
    write_vram(vdp, 0xC000, {0x0080, 0x0500, 0x4004, 0x0080});

    auto assets = vdp.introspection().assets()->graphics();
    REQUIRE(assets.size() == 2U); // patterns + sprite_00 (link 0 ends the list)

    const auto& spr = assets[1];
    CHECK(spr.kind == asset_kind::sprite);
    CHECK(spr.name == "sprite_00");
    CHECK(spr.image.width == 16U); // 2 cells * 8
    CHECK(spr.image.height == 16U);
    CHECK(spr.image.palette == 2U);
    CHECK(spr.source_addr == 0xC000U);
    REQUIRE(spr.image.well_formed());
    CHECK(spr.image.indices[0] == 1U); // top-left cell (tile 4) is solid colour 1
}
