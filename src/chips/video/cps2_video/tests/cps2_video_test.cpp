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
    // Sprite tile (code 0 -> offset 0). Texel (0,0): bit 7 of each of the 4 planes
    // at gfx[0..3]. Make pen = 0b1010 = 0xA: plane3(gfx[0]) and plane1(gfx[2]) set.
    gfx[0] = 0x80U; // bit3 of the pen
    gfx[2] = 0x80U; // bit1 of the pen
    video.attach_gfx(gfx);

    CHECK(video.tile_pixel(gfx_type::sprites, 0U, 0, 0, 16, 0) == 0x0AU);
    CHECK(video.tile_pixel(gfx_type::sprites, 0U, 1, 0, 16, 0) == 0x00U); // bit 6 planes clear
    // An off-tile coordinate / a code whose data is past the ROM -> transparent.
    CHECK(video.tile_pixel(gfx_type::sprites, 0U, 20, 0, 16, 0) == cps2_video::transparent_pen);
    CHECK(video.tile_pixel(gfx_type::sprites, 0x9999U, 0, 0, 16, 0) == cps2_video::transparent_pen);
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
    // Set that tile's texel (0,0) to pen 0xA: plane3 (gfx[base]) + plane1 (gfx[base+2]).
    std::vector<std::uint8_t> gfx(0x800000U + 64U, 0U);
    gfx[0x800000U + 0U] = 0x80U; // pen bit 3
    gfx[0x800000U + 2U] = 0x80U; // pen bit 1
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
