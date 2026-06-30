#include "cps2_video.hpp"

#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::video::cps2_video;
    using reset_kind = mnemos::chips::reset_kind;

    void put16(std::vector<std::uint8_t>& mem, std::size_t at, std::uint16_t v) {
        mem[at] = static_cast<std::uint8_t>(v >> 8U); // big-endian
        mem[at + 1U] = static_cast<std::uint8_t>(v);
    }

    void set_sprite_pixel(std::vector<std::uint8_t>& gfx, std::uint32_t tile, int x, int y,
                          std::uint8_t pen) {
        if (x < 0 || x >= 16 || y < 0 || y >= 16) {
            return;
        }
        const std::uint32_t group = static_cast<std::uint32_t>(x >> 3);
        const auto bit = static_cast<std::uint8_t>(1U << (7 - (x & 7)));
        const std::uint32_t offset = tile * 128U + static_cast<std::uint32_t>(y) * 8U + group * 4U;
        if (offset + 3U >= gfx.size()) {
            return;
        }
        for (std::uint32_t plane = 0U; plane < 4U; ++plane) {
            const std::uint32_t byte_plane = plane;
            if (((pen >> plane) & 1U) != 0U) {
                gfx[offset + byte_plane] |= bit;
            } else {
                gfx[offset + byte_plane] =
                    static_cast<std::uint8_t>(gfx[offset + byte_plane] & ~bit);
            }
        }
    }
} // namespace

TEST_CASE("cps2 video decodes the 16-bit brightness:R:G:B colour", "[cps2_video]") {
    // brightness 0xF, R 0xF -> 0xF*0x11*(0x0F+0x1E)/0x2D = 0xFF (full red).
    CHECK(cps2_video::decode_color(0xFF00U) == 0x00FF0000U);
    CHECK(cps2_video::decode_color(0xF0F0U) == 0x0000FF00U); // full green
    CHECK(cps2_video::decode_color(0xF00FU) == 0x000000FFU); // full blue
    CHECK(cps2_video::decode_color(0x0000U) == 0x00000000U); // black
    // brightness 0, all channels max -> dimmed grey (0xF*0x11*0x0F/0x2D = 0x55).
    CHECK(cps2_video::decode_color(0x0FFFU) == 0x00555555U);
}

TEST_CASE("cps2 video DMAs the palette from video RAM and reports it back", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x10000U, 0U);
    put16(vram, 0x0000U, 0xFF00U); // colour 0 = full red
    put16(vram, 0x0002U, 0xF00FU); // colour 1 = full blue
    video.attach_video_ram(vram);

    video.copy_palette(0x0000U, 0x003FU); // all six pages
    CHECK(video.palette_color(0U) == 0xFF00U);
    CHECK(video.palette_color(1U) == 0xF00FU);
}

TEST_CASE("cps2 video renders the decoded backdrop into the framebuffer", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x10000U, 0U);
    // The CPS-2 backdrop is the last palette entry (pal_num 0xBF, pen 0xF) =
    // palette byte 0xBF*16*2 + 0xF*2 = 0x17FE. No gfx attached, so the scroll1
    // walk plots nothing transparent over it.
    put16(vram, 0x17FEU, 0xFF00U); // backdrop = full red
    video.attach_video_ram(vram);

    const std::uint64_t before = video.frame_index();
    video.render(0x0000U, 0x003FU);
    const auto fb = video.framebuffer();
    REQUIRE(fb.width == 384U);
    REQUIRE(fb.height == 224U);
    REQUIRE(fb.pixels != nullptr);
    CHECK(fb.pixels[0] == 0x00FF0000U);
    CHECK(fb.pixels[fb.width * fb.height - 1U] == 0x00FF0000U);
    CHECK(video.frame_index() == before + 1U);
}

TEST_CASE("cps2 video palette-control gates which pages copy", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x10000U, 0U);
    put16(vram, 0x0000U, 0xFF00U); // red at the source base
    video.attach_video_ram(vram);

    // Only page 1 enabled. Page 0 of the palette stays black; page 1 is copied --
    // and because no earlier page copied, the source has NOT advanced, so page 1
    // reads from the source base (the reference's source-advance quirk).
    video.copy_palette(0x0000U, 0x0002U);
    CHECK(video.palette_color(0U) == 0x0000U);     // page 0 not copied (black)
    CHECK(video.palette_color(0x200U) == 0xFF00U); // page 1 = palette byte 0x400 = source base
}

TEST_CASE("cps2 video maps graphics codes per layer category", "[cps2_video]") {
    using gfx_type = cps2_video::gfx_type;
    cps2_video video;
    std::vector<std::uint8_t> gfx(0x1000U, 0U);
    video.attach_gfx(gfx);
    std::uint32_t mapped = 0U;

    // Sprites use the code directly.
    REQUIRE(video.map_gfx_code(gfx_type::sprites, 0x1234U, mapped));
    CHECK(mapped == 0x1234U);

    // Scroll layers address the upper bank, shifted per layer (0/1/3).
    REQUIRE(video.map_gfx_code(gfx_type::scroll1, 0x00000U, mapped));
    CHECK(mapped == 0x20000U);
    REQUIRE(video.map_gfx_code(gfx_type::scroll1, 0x1FFFFU, mapped));
    CHECK(mapped == 0x3FFFFU);
    REQUIRE(video.map_gfx_code(gfx_type::scroll2, 0x00000U, mapped));
    CHECK(mapped == 0x10000U); // (0x20000 + 0) >> 1
    REQUIRE(video.map_gfx_code(gfx_type::scroll3, 0x00000U, mapped));
    CHECK(mapped == 0x04000U); // (0x20000 + 0) >> 3

    // Out-of-bank codes have no tile.
    CHECK_FALSE(video.map_gfx_code(gfx_type::scroll1, 0x20000U, mapped));
    CHECK_FALSE(video.map_gfx_code(gfx_type::scroll2, 0x10000U, mapped));

    // No gfx attached -> no tile.
    cps2_video bare;
    CHECK_FALSE(bare.map_gfx_code(gfx_type::sprites, 0U, mapped));
}

TEST_CASE("cps2 video decodes 4bpp tile texels from the gfx ROM", "[cps2_video]") {
    using gfx_type = cps2_video::gfx_type;
    cps2_video video;
    std::vector<std::uint8_t> gfx(0x1000U, 0U);
    // Sprite tile (code 0 -> offset 0). Texel (0,0): bit 7 of each packed byte
    // at gfx[0..3]. Make pen = 0b1010 = 0xA: byte1 and byte3 set.
    gfx[1] = 0x80U; // bit1 of the pen
    gfx[3] = 0x80U; // bit3 of the pen
    video.attach_gfx(gfx);

    CHECK(video.tile_pixel(gfx_type::sprites, 0U, 0, 0, 16, 0) == 0x0AU);
    CHECK(video.tile_pixel(gfx_type::sprites, 0U, 1, 0, 16, 0) == 0x00U); // bit 6 planes clear
    // An off-tile coordinate / a code whose data is past the ROM -> transparent.
    CHECK(video.tile_pixel(gfx_type::sprites, 0U, 20, 0, 16, 0) == cps2_video::transparent_pen);
    CHECK(video.tile_pixel(gfx_type::sprites, 0x9999U, 0, 0, 16, 0) == cps2_video::transparent_pen);
}

TEST_CASE("cps2 video maps raw sprite GFX byte 0 to plane 0", "[cps2_video][sprites]") {
    using gfx_type = cps2_video::gfx_type;
    cps2_video video;
    std::vector<std::uint8_t> gfx(2U * 128U, 0U);
    gfx[1U * 128U + 0U] = 0x80U;
    video.attach_gfx(gfx);
    CHECK(video.tile_pixel(gfx_type::sprites, 1U, 0, 0, 16, 0) == 0x01U);

    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0x02U, 0xFF00U);
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> obj(0x4000U, 0U);
    put16(obj, 0x0U, 0x0000U);
    put16(obj, 0x2U, 0x0000U);
    put16(obj, 0x4U, 0x0001U);
    put16(obj, 0x6U, 0x0000U);
    put16(obj, 0xAU, 0x8000U);
    video.attach_object_ram(obj);
    video.set_object_base(0U);
    video.set_sprite_offsets(0U, 0U);
    video.set_display_enable(true);

    video.latch_objects();
    video.render(0x10000U, 0x003FU);

    CHECK(video.framebuffer().pixels[0U] == 0x00FF0000U);
}

TEST_CASE("cps2 video draws the scroll1 playfield through the compositor", "[cps2_video]") {
    cps2_video video;

    // Video RAM holds both the palette DMA source (at 0) and the scroll1 name
    // table (at 0x8000). 64 KiB is plenty for both.
    std::vector<std::uint8_t> vram(0x10000U, 0U);
    // scroll1 palette page is pal_num 32+; tile texel pen 0xA -> palette index
    // 32*16 + 10 = 522 -> palette byte 0x414. Make that colour full red.
    put16(vram, 0x0414U, 0xFF00U);
    // scroll1 name table at 0x8000: tile entry 0 = code 0, attr 0 (pal 32, no flip).
    put16(vram, 0x8000U, 0x0000U); // tile code
    put16(vram, 0x8002U, 0x0000U); // attr
    video.attach_video_ram(vram);

    // scroll1 banks code 0 to tile 0x20000 (byte offset 0x20000*64 = 0x800000).
    // Set that tile's texel (0,0) to pen 0xA: bit1 (gfx[base+1]) + bit3 (gfx[base+3]).
    std::vector<std::uint8_t> gfx(0x800000U + 64U, 0U);
    gfx[0x800000U + 1U] = 0x80U; // pen bit 1
    gfx[0x800000U + 3U] = 0x80U; // pen bit 3
    video.attach_gfx(gfx);

    video.set_scroll1_base(0x8000U);
    // Scroll -64,-16 so screen (0,0) maps to world (0,0) = tile 0, texel (0,0).
    video.set_scroll1(0xFFC0U, 0xFFF0U);
    video.set_display_enable(true);

    video.render(0x0000U, 0x003FU);
    const auto fb = video.framebuffer();
    REQUIRE(fb.pixels != nullptr);
    // Screen (0,0) shows the lit texel; (1,0) is texel (1,0) of the same tile (pen
    // 0 -> palette index 512 -> black), proving the layer walk addresses per-pixel.
    CHECK(fb.pixels[0] == 0x00FF0000U);
    CHECK(fb.pixels[1] == 0x00000000U);

    // Display disabled -> the playfield is suppressed, leaving the backdrop.
    video.set_display_enable(false);
    video.render(0x0000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00000000U);
}

TEST_CASE("cps2 video flip-screen mirrors scroll pixels to the opposite corner",
          "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x10000U, 0U);
    put16(vram, 0x0414U, 0xFF00U); // scroll1 pal 32 pen 0xA = red
    put16(vram, 0x8000U, 0x0000U);
    put16(vram, 0x8002U, 0x0000U);
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(0x800000U + 64U, 0U);
    gfx[0x800000U + 1U] = 0x80U;
    gfx[0x800000U + 3U] = 0x80U;
    video.attach_gfx(gfx);

    video.set_scroll1_base(0x8000U);
    video.set_scroll1(0xFFC0U, 0xFFF0U);
    video.set_display_enable(true);

    video.render(0x0000U, 0x003FU);
    const auto fb = video.framebuffer();
    REQUIRE(fb.pixels != nullptr);
    CHECK(fb.pixels[0] == 0x00FF0000U);

    video.set_video_control(0x8000U);
    video.render(0x0000U, 0x003FU);

    CHECK(fb.pixels[0] == 0x00000000U);
    CHECK(fb.pixels[(fb.height - 1U) * fb.width + (fb.width - 1U)] == 0x00FF0000U);
}

TEST_CASE("cps2 video draws the scroll2 playfield (16x16 tiles)", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    // Palette source at 0x10000: scroll2 pal_num 64, pen 0xA -> index 1034 -> byte
    // 0x814. Make it full red.
    put16(vram, 0x10000U + 0x814U, 0xFF00U);
    // scroll1 / scroll3 name-table entry 0 = an out-of-bank (transparent) code; all
    // three layers share the same scroll so screen (0,0) maps to each layer's tile
    // index 0, isolating scroll2 at the checked pixel.
    put16(vram, 0x0100U, 0xFFFFU); // scroll1 base 0x100, entry 0
    put16(vram, 0x0200U, 0xFFFFU); // scroll3 base 0x200, entry 0
    // scroll2 name table at 0: entry 0 = code 0, attr 0 (defaults).
    video.attach_video_ram(vram);

    // code 0 maps to gfx byte 0x800000 for every scroll layer; the 16x16 tile reads
    // mapped*128 = 0x800000. Texel (0,0) = pen 0xA.
    std::vector<std::uint8_t> gfx(0x800000U + 128U, 0U);
    gfx[0x800000U + 1U] = 0x80U;
    gfx[0x800000U + 3U] = 0x80U;
    video.attach_gfx(gfx);

    video.set_scroll1_base(0x0100U);
    video.set_scroll2_base(0x0000U);
    video.set_scroll3_base(0x0200U);
    video.set_scroll1(0xFFC0U, 0xFFF0U);
    video.set_scroll2(0xFFC0U, 0xFFF0U);
    video.set_scroll3(0xFFC0U, 0xFFF0U);
    video.set_display_enable(true);

    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00FF0000U); // scroll2 pen 0xA @ pal 64
}

TEST_CASE("cps2 video gates real scroll2 enable through CPS-B and CPS-A control",
          "[cps2_video]") {
    cps2_video video;
    video.set_zero_layer_control_defaults(false);

    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0x814U, 0xFF00U); // scroll2 pal 64 pen 0xA = red
    // scroll2 name table at 0: entry 0 = code 0, attr 0.
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(0x800000U + 128U, 0U);
    gfx[0x800000U + 1U] = 0x80U;
    gfx[0x800000U + 3U] = 0x80U;
    video.attach_gfx(gfx);

    video.set_scroll2_base(0x0000U);
    video.set_scroll2(0xFFC0U, 0xFFF0U);
    video.set_display_enable(true);

    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00000000U); // real board: zero layer-control disables

    video.set_cps_b_reg(0x13U, static_cast<std::uint16_t>((2U << 6U) | 0x04U));
    video.set_video_control(0x0000U);
    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00000000U); // CPS-A scroll2 enable is also required

    video.set_video_control(0x0004U);
    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("cps2 video draws the scroll3 playfield (32x32 tiles, code masked to 14 bits)",
          "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    // scroll3 pal_num 96, pen 0xA -> index 1546 -> byte 0xC14. Make it full green.
    put16(vram, 0x10000U + 0xC14U, 0xF0F0U);
    put16(vram, 0x0100U, 0xFFFFU); // scroll1 transparent
    put16(vram, 0x0200U, 0xFFFFU); // scroll2 transparent
    // scroll3 name table at 0: entry 0 = code 0x4000 (masked to 0 -> code 0).
    put16(vram, 0x0000U, 0x4000U);
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(0x800000U + 512U, 0U); // 32x32 = mapped*512
    gfx[0x800000U + 1U] = 0x80U;
    gfx[0x800000U + 3U] = 0x80U;
    video.attach_gfx(gfx);

    video.set_scroll1_base(0x0100U);
    video.set_scroll2_base(0x0200U);
    video.set_scroll3_base(0x0000U);
    video.set_scroll1(0xFFC0U, 0xFFF0U);
    video.set_scroll2(0xFFC0U, 0xFFF0U);
    video.set_scroll3(0xFFC0U, 0xFFF0U);
    video.set_display_enable(true);

    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x0000FF00U); // scroll3 pen 0xA @ pal 96
}

TEST_CASE("cps2 video gates real scroll3 enable through CPS-B and CPS-A control",
          "[cps2_video]") {
    cps2_video video;
    video.set_zero_layer_control_defaults(false);

    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0xC14U, 0xF0F0U); // scroll3 pal 96 pen 0xA = green
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(0x800000U + 512U, 0U);
    gfx[0x800000U + 1U] = 0x80U;
    gfx[0x800000U + 3U] = 0x80U;
    video.attach_gfx(gfx);

    video.set_scroll3_base(0x0000U);
    video.set_scroll3(0xFFC0U, 0xFFF0U);
    video.set_display_enable(true);
    video.set_cps_b_reg(0x13U, static_cast<std::uint16_t>((3U << 6U) | 0x08U));

    video.set_video_control(0x0000U);
    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00000000U);

    video.set_video_control(0x0008U);
    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x0000FF00U);
}

TEST_CASE("cps2 video scroll2 row-scroll shifts a line horizontally", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0x814U, 0xFF00U); // scroll2 pal 64 pen 0xA = red
    put16(vram, 0x0100U, 0xFFFFU);           // scroll1 transparent
    put16(vram, 0x0200U, 0xFFFFU);           // scroll3 transparent
    // scroll2 name table entry 0 = code 0. Row-scroll table at 0x8000: shift the
    // top visible line (screen_y 16) by -4 (content moves right) so the lit texel
    // lands at x=4 instead of x=0.
    constexpr std::uint32_t rs_base = 0x8000U;
    put16(vram, rs_base + 16U * 2U, 0xFFFCU); // line 16 (screen row 0) -> -4
    video.attach_video_ram(vram);

    // The tile is transparent (pen 0xF) everywhere except texel (0,0) = pen 0xA, so
    // the per-line shift is observable. Row 0 group 0: pen bits 3/1 all set, bits
    // 2/0 set for texels 1-7 only (texel 0 = 0b1010, texels 1-7 = 0b1111).
    std::vector<std::uint8_t> gfx(0x800000U + 128U, 0xFFU);
    gfx[0x800000U + 0U] = 0x7FU; // pen bit 0: texel 0 clear
    gfx[0x800000U + 1U] = 0xFFU; // pen bit 1: all texels
    gfx[0x800000U + 2U] = 0x7FU; // pen bit 2: texel 0 clear
    gfx[0x800000U + 3U] = 0xFFU; // pen bit 3: all texels
    video.attach_gfx(gfx);

    video.set_scroll1_base(0x0100U);
    video.set_scroll2_base(0x0000U);
    video.set_scroll3_base(0x0200U);
    video.set_scroll1(0xFFC0U, 0xFFF0U);
    video.set_scroll2(0xFFC0U, 0xFFF0U);
    video.set_scroll3(0xFFC0U, 0xFFF0U);
    video.set_rowscroll(true, rs_base, 0U);
    video.set_display_enable(true);

    video.render(0x10000U, 0x003FU);
    const auto fb = video.framebuffer();
    CHECK(fb.pixels[0] == 0x00000000U); // -4 shift moved the lit texel off x=0
    CHECK(fb.pixels[4] == 0x00FF0000U); // now lit at x=4
}

TEST_CASE("cps2 video draws a sprite from object RAM", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    // Sprite pal_num 2, pen 0xA -> palette index 2*16+10 = 42 -> byte 0x54. Red.
    put16(vram, 0x10000U + 0x54U, 0xFF00U);
    video.attach_video_ram(vram);

    // Sprites address the gfx by code directly; 16x16 tile 1 -> byte 1*128 = 128.
    // Texel (0,0) = pen 0xA.
    std::vector<std::uint8_t> gfx(0x1000U, 0U);
    gfx[128U + 1U] = 0x80U;
    gfx[128U + 3U] = 0x80U;
    video.attach_gfx(gfx);

    // Object RAM: entry 0 = sprite x=0,y=0,tile=1,attr=2 (pal 2, single 16x16 block,
    // no flip); entry 1's y = 0xFFFF terminates the list.
    std::vector<std::uint8_t> obj(0x4000U, 0U);
    put16(obj, 0x0U, 0x0000U); // raw_x
    put16(obj, 0x2U, 0x0000U); // raw_y
    put16(obj, 0x4U, 0x0001U); // raw_tile (-> tile_code 1)
    put16(obj, 0x6U, 0x0002U); // attr: pal 2, 1x1, no flip
    put16(obj, 0xAU, 0xFFFFU); // entry 1 raw_y = terminator
    video.attach_object_ram(obj);

    video.set_object_base(0U);
    video.set_sprite_offsets(0U, 0U); // xoffs = 64, yoffs = 16 (the visible origin)
    video.set_display_enable(true);

    video.latch_objects();
    video.render(0x10000U, 0x003FU);
    const auto fb = video.framebuffer();
    // raw(0,0) + offset(64,16) - visible origin(64,16) -> the texel lands at (0,0).
    CHECK(fb.pixels[0] == 0x00FF0000U);
    // A tile_code 0 entry (the default-zero entries) draws nothing.
    put16(obj, 0x4U, 0x0000U); // tile_code 0 -> skipped
    video.latch_objects();
    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00000000U); // backdrop shows through
}

TEST_CASE("cps2 video flip-screen mirrors sprite pixels to the opposite corner",
          "[cps2_video][sprites]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0x02U, 0xFF00U); // sprite pal 0 pen 1 = red
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(2U * 128U, 0U);
    set_sprite_pixel(gfx, 1U, 0, 0, 1U);
    video.attach_gfx(gfx);

    std::vector<std::uint8_t> obj(0x4000U, 0U);
    put16(obj, 0x0U, 0x0000U);
    put16(obj, 0x2U, 0x0000U);
    put16(obj, 0x4U, 0x0001U);
    put16(obj, 0x6U, 0x0000U);
    put16(obj, 0xAU, 0x8000U);
    video.attach_object_ram(obj);
    video.set_object_base(0U);
    video.set_sprite_offsets(0U, 0U);
    video.set_display_enable(true);
    video.latch_objects();

    video.render(0x10000U, 0x003FU);
    const auto fb = video.framebuffer();
    REQUIRE(fb.pixels != nullptr);
    CHECK(fb.pixels[0] == 0x00FF0000U);

    video.set_video_control(0x8000U);
    video.render(0x10000U, 0x003FU);

    CHECK(fb.pixels[0] == 0x00000000U);
    CHECK(fb.pixels[(fb.height - 1U) * fb.width + (fb.width - 1U)] == 0x00FF0000U);
}

TEST_CASE("cps2 video offset-mode sprites re-bias against the control-register base",
          "[cps2_video][sprites]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0x02U, 0xFF00U); // sprite pal 0 pen 1 = red
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(2U * 128U, 0U);
    set_sprite_pixel(gfx, 1U, 0, 0, 1U);
    video.attach_gfx(gfx);

    std::vector<std::uint8_t> obj(0x4000U, 0U);
    put16(obj, 0x0U, 0x0000U);
    put16(obj, 0x2U, 0x0000U);
    put16(obj, 0x4U, 0x0001U);
    put16(obj, 0x6U, 0x0000U);
    put16(obj, 0xAU, 0x8000U);
    video.attach_object_ram(obj);
    video.set_object_base(0U);
    video.set_display_enable(true);

    video.set_sprite_offsets(0x0020U, 0x0010U);
    video.latch_objects();
    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00000000U);

    put16(obj, 0x6U, 0x0080U); // attr bit 7: raw coordinates are base-relative
    video.latch_objects();
    video.render(0x10000U, 0x003FU);

    CHECK(video.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("cps2 video latches object RAM at vblank", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0x54U, 0xFF00U); // sprite pal 2, pen 0xA = red
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(0x1000U, 0U);
    gfx[128U + 1U] = 0x80U;
    gfx[128U + 3U] = 0x80U;
    video.attach_gfx(gfx);

    std::vector<std::uint8_t> obj(0x4000U, 0U);
    put16(obj, 0x2U, 0xFFFFU); // entry 0 raw_y terminates the initial empty list
    video.attach_object_ram(obj);
    video.set_object_base(0U);
    video.set_sprite_offsets(0U, 0U);
    video.set_display_enable(true);

    video.latch_objects();
    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00000000U);

    put16(obj, 0x0U, 0x0000U); // raw_x
    put16(obj, 0x2U, 0x0000U); // raw_y
    put16(obj, 0x4U, 0x0001U); // raw_tile
    put16(obj, 0x6U, 0x0002U); // attr: pal 2, 1x1, no flip
    put16(obj, 0xAU, 0xFFFFU); // entry 1 raw_y terminates

    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00000000U);

    video.latch_objects();
    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00FF0000U);

    put16(obj, 0x4U, 0x0000U); // live tile_code 0 is not visible until the next latch
    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00FF0000U);

    video.latch_objects();
    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00000000U);
}

TEST_CASE("cps2 video sprite priority blocks later lower-priority shadows",
          "[cps2_video][sprites]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0x02U, 0xF0F0U); // pal 0 pen 1 = green
    put16(vram, 0x10000U + 0x04U, 0xFF00U); // pal 0 pen 2 = red
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(3U * 128U, 0U);
    set_sprite_pixel(gfx, 1U, 0, 0, 1U);
    set_sprite_pixel(gfx, 2U, 0, 0, 2U);
    video.attach_gfx(gfx);

    std::vector<std::uint8_t> obj(0x4000U, 0U);
    put16(obj, 0x0U, 1U << 13U); // entry 0: lower-priority green sprite
    put16(obj, 0x2U, 0x0000U);
    put16(obj, 0x4U, 0x0001U);
    put16(obj, 0x6U, 0x0000U);
    put16(obj, 0x8U, 5U << 13U); // entry 1: higher-priority red sprite
    put16(obj, 0xAU, 0x0000U);
    put16(obj, 0xCU, 0x0002U);
    put16(obj, 0xEU, 0x0000U);
    put16(obj, 0x12U, 0x8000U); // entry 2 terminates
    video.attach_object_ram(obj);
    video.set_object_base(0U);
    video.set_sprite_offsets(0U, 0U);
    video.set_display_enable(true);

    video.latch_objects();
    video.render(0x10000U, 0x003FU);

    CHECK(video.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("cps2 video sprite priority keeps earlier same-priority ownership",
          "[cps2_video][sprites]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0x02U, 0xF0F0U); // pal 0 pen 1 = green
    put16(vram, 0x10000U + 0x04U, 0xFF00U); // pal 0 pen 2 = red
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(3U * 128U, 0U);
    set_sprite_pixel(gfx, 1U, 0, 0, 1U);
    set_sprite_pixel(gfx, 2U, 0, 0, 2U);
    video.attach_gfx(gfx);

    std::vector<std::uint8_t> obj(0x4000U, 0U);
    put16(obj, 0x0U, 5U << 13U); // entry 0 is drawn later but has equal priority
    put16(obj, 0x2U, 0x0000U);
    put16(obj, 0x4U, 0x0001U);
    put16(obj, 0x6U, 0x0000U);
    put16(obj, 0x8U, 5U << 13U); // entry 1 owns the pixel first
    put16(obj, 0xAU, 0x0000U);
    put16(obj, 0xCU, 0x0002U);
    put16(obj, 0xEU, 0x0000U);
    put16(obj, 0x12U, 0x8000U);
    video.attach_object_ram(obj);
    video.set_object_base(0U);
    video.set_sprite_offsets(0U, 0U);
    video.set_display_enable(true);

    video.latch_objects();
    video.render(0x10000U, 0x003FU);

    CHECK(video.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("cps2 video wraps sprite coordinates on the 512 pixel raster",
          "[cps2_video][sprites]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0x02U, 0xFF00U);
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(2U * 128U, 0U);
    set_sprite_pixel(gfx, 1U, 0, 0, 1U);
    video.attach_gfx(gfx);

    std::vector<std::uint8_t> obj(0x4000U, 0U);
    put16(obj, 0x0U, 0x0200U); // x wraps to zero before the visible-origin bias
    put16(obj, 0x2U, 0x0000U);
    put16(obj, 0x4U, 0x0001U);
    put16(obj, 0x6U, 0x0000U);
    put16(obj, 0xAU, 0x8000U);
    video.attach_object_ram(obj);
    video.set_object_base(0U);
    video.set_sprite_offsets(0U, 0U);
    video.set_display_enable(true);

    video.latch_objects();
    video.render(0x10000U, 0x003FU);

    CHECK(video.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("cps2 video unflipped multi-block sprites wrap tile rows at nibble boundaries",
          "[cps2_video][sprites]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0x02U, 0xFF00U);
    put16(vram, 0x10000U + 0x04U, 0xF0F0U);
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(17U * 128U, 0U);
    set_sprite_pixel(gfx, 0U, 0, 0, 1U);
    set_sprite_pixel(gfx, 0x10U, 0, 0, 2U);
    video.attach_gfx(gfx);

    std::vector<std::uint8_t> obj(0x4000U, 0U);
    put16(obj, 0x0U, 0x0000U);
    put16(obj, 0x2U, 0x0000U);
    put16(obj, 0x4U, 0x000FU); // second block wraps to tile 0, not tile 0x10
    put16(obj, 0x6U, 0x0100U); // 2x1 blocks, no flip
    put16(obj, 0xAU, 0x8000U);
    video.attach_object_ram(obj);
    video.set_object_base(0U);
    video.set_sprite_offsets(0U, 0U);
    video.set_display_enable(true);

    video.latch_objects();
    video.render(0x10000U, 0x003FU);

    CHECK(video.framebuffer().pixels[16U] == 0x00FF0000U);
}

TEST_CASE("cps2 video flip-X multi-block sprites use linear tile order",
          "[cps2_video][sprites]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0x02U, 0xFF00U);
    put16(vram, 0x10000U + 0x04U, 0xF0F0U);
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(17U * 128U, 0U);
    set_sprite_pixel(gfx, 0U, 15, 0, 1U);
    set_sprite_pixel(gfx, 0x10U, 15, 0, 2U);
    video.attach_gfx(gfx);

    std::vector<std::uint8_t> obj(0x4000U, 0U);
    put16(obj, 0x0U, 0x0000U);
    put16(obj, 0x2U, 0x0000U);
    put16(obj, 0x4U, 0x000FU);
    put16(obj, 0x6U, 0x0120U); // 2x1 blocks, flip-X
    put16(obj, 0xAU, 0x8000U);
    video.attach_object_ram(obj);
    video.set_object_base(0U);
    video.set_sprite_offsets(0U, 0U);
    video.set_display_enable(true);

    video.latch_objects();
    video.render(0x10000U, 0x003FU);

    CHECK(video.framebuffer().pixels[0U] == 0x0000FF00U);
}

TEST_CASE("cps2 video flip-Y multi-block sprites keep linear columns",
          "[cps2_video][sprites]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0x02U, 0xFF00U);
    put16(vram, 0x10000U + 0x06U, 0xF00FU);
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(33U * 128U, 0U);
    set_sprite_pixel(gfx, 0x10U, 0, 15, 1U);
    set_sprite_pixel(gfx, 0x20U, 0, 15, 3U);
    video.attach_gfx(gfx);

    std::vector<std::uint8_t> obj(0x4000U, 0U);
    put16(obj, 0x0U, 0x0000U);
    put16(obj, 0x2U, 0x0000U);
    put16(obj, 0x4U, 0x000FU);
    put16(obj, 0x6U, 0x1140U); // 2x2 blocks, flip-Y
    put16(obj, 0xAU, 0x8000U);
    video.attach_object_ram(obj);
    video.set_object_base(0U);
    video.set_sprite_offsets(0U, 0U);
    video.set_display_enable(true);

    video.latch_objects();
    video.render(0x10000U, 0x003FU);

    CHECK(video.framebuffer().pixels[16U] == 0x000000FFU);
}

TEST_CASE("cps2 video save/load preserves latched sprite state", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    put16(vram, 0x10000U + 0x54U, 0xFF00U); // sprite pal 2, pen 0xA = red
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(0x1000U, 0U);
    gfx[128U + 1U] = 0x80U;
    gfx[128U + 3U] = 0x80U;
    video.attach_gfx(gfx);

    std::vector<std::uint8_t> obj(0x4000U, 0U);
    put16(obj, 0x0U, 0x0000U); // raw_x
    put16(obj, 0x2U, 0x0000U); // raw_y
    put16(obj, 0x4U, 0x0001U); // raw_tile
    put16(obj, 0x6U, 0x0002U); // attr: pal 2, 1x1, no flip
    put16(obj, 0xAU, 0xFFFFU); // entry 1 raw_y terminates
    video.attach_object_ram(obj);
    video.set_object_base(0U);
    video.set_sprite_offsets(0U, 0U);
    video.set_display_enable(true);
    video.latch_objects();

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    video.save_state(writer);

    cps2_video restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    restored.attach_video_ram(vram);
    restored.attach_gfx(gfx);

    restored.render(0x10000U, 0x003FU);
    CHECK(restored.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("cps2 video decodes the CPS-B layer order + sprite priority masks", "[cps2_video]") {
    std::array<int, 4> raw{};
    // A zero control word selects the default layer order.
    cps2_video::decode_layer_control(0U, raw);
    CHECK(raw == std::array<int, 4>{3, 2, 0, 1});

    // Per-slot ids come from bits 6/8/10/12.
    cps2_video::decode_layer_control(0x0E40U, raw);
    CHECK(raw == std::array<int, 4>{1, 2, 3, 0});

    std::array<int, 3> scroll{};
    std::array<std::uint16_t, 8> masks{};
    cps2_video::build_sprite_priority_masks(0U, raw, scroll, masks);
    CHECK(scroll == std::array<int, 3>{1, 2, 3}); // no disabled slot -> no collapse
    CHECK(masks[0] == 0x00FFU);                   // priority-0 sprites sit behind everything

    // A disabled (id 0) slot carries the following layer forward.
    cps2_video::decode_layer_control(0x18C0U, raw);
    CHECK(raw == std::array<int, 4>{3, 0, 2, 1});
    cps2_video::build_sprite_priority_masks(0U, raw, scroll, masks);
    CHECK(scroll == std::array<int, 3>{3, 2, 1});
}

TEST_CASE("cps2 video honours the CPS-B layer order when compositing scrolls", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x20000U, 0U);
    // scroll1 pal 32 pen 0xA -> byte 0x414 = red; scroll2 pal 64 pen 0xA -> byte
    // 0x814 = green. Both layers cover pixel (0,0) with an opaque tile, so the
    // front layer (per the CPS-B order) wins.
    put16(vram, 0x10000U + 0x414U, 0xFF00U); // scroll1 red
    put16(vram, 0x10000U + 0x814U, 0xF0F0U); // scroll2 green
    put16(vram, 0x0200U, 0xFFFFU);           // scroll3 transparent entry 0
    video.attach_video_ram(vram);

    std::vector<std::uint8_t> gfx(0x800000U + 128U, 0U); // code 0 tile, texel (0,0) pen 0xA
    gfx[0x800000U + 1U] = 0x80U;
    gfx[0x800000U + 3U] = 0x80U;
    video.attach_gfx(gfx);

    video.set_scroll1_base(0x0000U);
    video.set_scroll2_base(0x0000U);
    video.set_scroll3_base(0x0200U);
    video.set_scroll1(0xFFC0U, 0xFFF0U);
    video.set_scroll2(0xFFC0U, 0xFFF0U);
    video.set_scroll3(0xFFC0U, 0xFFF0U);
    video.set_display_enable(true);
    video.set_video_control(0x0004U);

    // layercontrol enabling scroll1+scroll2, order slot0=scroll2 (back), slot1=
    // scroll1 (front): bits 6-7 = 2 (scroll2), bits 8-9 = 1 (scroll1). Enable bits
    // 0x02|0x04. -> scroll1 in front -> pixel (0,0) is red.
    video.set_cps_b_reg(0x13U, static_cast<std::uint16_t>((2U << 6U) | (1U << 8U) | 0x06U));
    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x00FF0000U);

    // Swap the order: slot0=scroll1 (back), slot1=scroll2 (front) -> green wins.
    video.set_cps_b_reg(0x13U, static_cast<std::uint16_t>((1U << 6U) | (2U << 8U) | 0x06U));
    video.render(0x10000U, 0x003FU);
    CHECK(video.framebuffer().pixels[0] == 0x0000FF00U);
}

TEST_CASE("cps2 video save/load round-trips the palette + frame index", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x10000U, 0U);
    put16(vram, 0x0000U, 0x1234U);
    video.attach_video_ram(vram);
    video.render(0x0000U, 0x003FU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    video.save_state(writer);

    cps2_video restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.palette_color(0U) == 0x1234U);
    CHECK(restored.frame_index() == video.frame_index());
}
