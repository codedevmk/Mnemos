#include "v9938.hpp"

#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::video::v9938;

    void write_reg(v9938& vdp, std::uint8_t reg, std::uint8_t value) {
        vdp.ctrl_write(value);
        vdp.ctrl_write(static_cast<std::uint8_t>(0x80U | (reg & 0x3FU)));
    }

    void enable_display(v9938& vdp) {
        write_reg(vdp, 1, static_cast<std::uint8_t>(vdp.reg(1) | 0x40U));
    }

    void set_write_addr(v9938& vdp, std::uint16_t addr) {
        vdp.ctrl_write(static_cast<std::uint8_t>(addr & 0xFFU));
        vdp.ctrl_write(static_cast<std::uint8_t>(0x40U | ((addr >> 8U) & 0x3FU)));
    }

    void set_write_addr17(v9938& vdp, std::uint32_t addr) {
        write_reg(vdp, 14, static_cast<std::uint8_t>((addr >> 14U) & 0x07U));
        set_write_addr(vdp, static_cast<std::uint16_t>(addr & 0x3FFFU));
    }

    std::uint8_t read_status(v9938& vdp, std::uint8_t index) {
        write_reg(vdp, 15, index);
        return vdp.ctrl_read();
    }

    void write_palette(v9938& vdp, std::uint8_t index, std::uint8_t r, std::uint8_t g,
                       std::uint8_t b) {
        write_reg(vdp, 16, index);
        vdp.palette_write(static_cast<std::uint8_t>(((r & 0x07U) << 4U) | (b & 0x07U)));
        vdp.palette_write(static_cast<std::uint8_t>(g & 0x07U));
    }

    void advance_frames(v9938& vdp, int frames) {
        const std::uint64_t cycles_per_frame = static_cast<std::uint64_t>(v9938::cycles_per_line) *
                                               static_cast<std::uint64_t>(vdp.total_scanlines());
        for (int frame = 0; frame < frames; ++frame) {
            vdp.tick(cycles_per_frame);
        }
    }

    std::uint32_t pixel(const v9938& vdp, int x, int y) {
        const auto fb = vdp.framebuffer();
        return fb.pixels[static_cast<std::size_t>(y) * fb.effective_stride() +
                         static_cast<std::size_t>(x)];
    }
} // namespace

TEST_CASE("v9938 writes registers through the control port", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 2, 0x1FU);
    write_reg(vdp, 17, 0x84U);
    CHECK(vdp.reg(2) == 0x1FU);
    CHECK(vdp.reg(17) == 0x84U);
}

TEST_CASE("v9938 reads and writes 128 KiB VRAM through the data port", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 14, 0x02U); // A14..A16 -> 0x8000 bank
    set_write_addr(vdp, 0x0010U);
    vdp.data_write(0xA5U);

    write_reg(vdp, 14, 0x02U);
    vdp.ctrl_write(0x10U);
    vdp.ctrl_write(0x00U); // read address 0x8010
    CHECK(vdp.data_read() == 0xA5U);
    CHECK(vdp.vram()[0x8010U] == 0xA5U);
}

TEST_CASE("v9938 keeps R14 fixed across VRAM address wrap in TMS-compatible modes",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 14, 0x03U);
    set_write_addr(vdp, 0x3FFFU);

    vdp.data_write(0x5AU);
    vdp.data_write(0xA5U);

    CHECK(vdp.reg(14) == 0x03U);
    CHECK(vdp.vram()[0xFFFFU] == 0x5AU);
    CHECK(vdp.vram()[0xC000U] == 0xA5U);
}

TEST_CASE("v9938 carries R14 across VRAM address wrap in TEXT2 and bitmap modes",
          "[chips][video][v9938]") {
    v9938 text2;
    write_reg(text2, 0, 0x02U);
    write_reg(text2, 1, 0x10U);
    write_reg(text2, 14, 0x03U);
    set_write_addr(text2, 0x3FFFU);

    text2.data_write(0x11U);
    text2.data_write(0x22U);

    CHECK(text2.reg(14) == 0x04U);
    CHECK(text2.vram()[0xFFFFU] == 0x11U);
    CHECK(text2.vram()[0x10000U] == 0x22U);

    v9938 bitmap;
    write_reg(bitmap, 0, 0x04U); // SCREEN 5-style bitmap
    write_reg(bitmap, 14, 0x07U);
    set_write_addr(bitmap, 0x3FFFU);

    bitmap.data_write(0x33U);
    bitmap.data_write(0x44U);

    CHECK(bitmap.reg(14) == 0x00U);
    CHECK(bitmap.vram()[0x1FFFFU] == 0x33U);
    CHECK(bitmap.vram()[0x00000U] == 0x44U);
}

TEST_CASE("v9938 renders 40-column text from name and pattern tables", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 1, 0x10U); // text mode
    enable_display(vdp);
    write_reg(vdp, 2, 0x02U); // name table at 0x0800
    write_reg(vdp, 4, 0x00U); // pattern table at 0x0000
    write_reg(vdp, 7, 0xF1U); // white ink on black paper

    set_write_addr(vdp, 0x0800U);
    vdp.data_write(1U); // first character cell uses pattern 1

    set_write_addr(vdp, 0x0008U);
    for (int i = 0; i < 8; ++i) {
        vdp.data_write(0xFCU); // six lit pixels in the text cell
    }

    vdp.render_frame();
    const int x0 = (256 - 40 * 6) / 2;
    CHECK(pixel(vdp, x0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, x0 + 5, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, x0 + 6, 0) == 0x000000U);
}

TEST_CASE("v9938 maps palette output to 32-tone grayscale when R8 BW is set",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 1, 0x08U); // GRAPHIC 2 / SCREEN 2
    enable_display(vdp);
    write_reg(vdp, 2, 0x06U); // name table at 0x1800
    write_reg(vdp, 3, 0x80U); // colour table at 0x2000
    write_reg(vdp, 4, 0x00U); // pattern table at 0x0000
    write_palette(vdp, 8U, 7U, 0U, 0U);

    set_write_addr(vdp, 0x1800U);
    vdp.data_write(1U);
    set_write_addr(vdp, 0x0008U);
    vdp.data_write(0x80U);
    set_write_addr(vdp, 0x2008U);
    vdp.data_write(0x82U);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFF0000U);

    write_reg(vdp, 8, 0x01U); // BW: black-and-white 32-tone output
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x4A4A4AU);
}

TEST_CASE("v9938 maps SCREEN 8 fixed-color output to grayscale when R8 BW is set",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU); // SCREEN 8-style fixed 3-3-2 bitmap
    enable_display(vdp);

    set_write_addr(vdp, 0x0000U);
    vdp.data_write(0xE0U); // red: R=7, G=0, B=0

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFF0000U);

    write_reg(vdp, 8, 0x01U); // BW: black-and-white 32-tone output
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x4A4A4AU);
}

TEST_CASE("v9938 renders the TEXT2 26.5-line frame", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x02U);
    write_reg(vdp, 1, 0x10U); // TEXT2 / 80-column text
    write_reg(vdp, 2, 0x05U); // low bits are ignored in TEXT2 name-table base selection
    write_reg(vdp, 4, 0x00U);
    write_reg(vdp, 7, 0xF1U);
    write_reg(vdp, 9, 0x80U);
    enable_display(vdp);

    set_write_addr(vdp, 0x1820U); // 0x1000 + row 26 * 80 columns
    vdp.data_write(1U);
    set_write_addr(vdp, 0x0008U);
    for (int i = 0; i < 8; ++i) {
        vdp.data_write(0x80U);
    }

    vdp.render_frame();
    const int x0 = (512 - 80 * 6) / 2;
    CHECK(vdp.framebuffer().height == 212U);
    CHECK(pixel(vdp, x0, 208) == 0xFFFFFFU);
    CHECK(pixel(vdp, x0, 211) == 0xFFFFFFU);
}

TEST_CASE("v9938 renders the TEXT1 26.5-line frame", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 1, 0x10U); // TEXT1 / 40-column text
    write_reg(vdp, 2, 0x00U);
    write_reg(vdp, 4, 0x00U);
    write_reg(vdp, 7, 0xF1U);
    write_reg(vdp, 9, 0x80U);
    enable_display(vdp);

    set_write_addr(vdp, static_cast<std::uint16_t>(26U * 40U));
    vdp.data_write(1U);
    set_write_addr(vdp, 0x0008U);
    for (int i = 0; i < 8; ++i) {
        vdp.data_write(0x80U);
    }

    vdp.render_frame();
    const int x0 = (256 - 40 * 6) / 2;
    CHECK(vdp.framebuffer().height == 212U);
    CHECK(pixel(vdp, x0, 208) == 0xFFFFFFU);
    CHECK(pixel(vdp, x0, 211) == 0xFFFFFFU);
}

TEST_CASE("v9938 applies TEXT2 blink attributes from the blink table", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x02U);
    write_reg(vdp, 1, 0x10U); // TEXT2 / 80-column text
    write_reg(vdp, 2, 0x04U); // name table at 0x1000
    write_reg(vdp, 3, 0x87U); // blink table at 0x12000; low bits ignored
    write_reg(vdp, 4, 0x00U);
    write_reg(vdp, 7, 0xF1U);  // normal white on black
    write_reg(vdp, 10, 0x04U); // blink table high address bit A16
    write_reg(vdp, 12, 0x21U); // blink phase color 2 on black
    write_reg(vdp, 13, 0x11U); // 1/6 second normal, 1/6 second blink
    enable_display(vdp);

    set_write_addr(vdp, 0x1000U);
    vdp.data_write(1U);
    vdp.data_write(1U);
    set_write_addr(vdp, 0x0008U);
    for (int i = 0; i < 8; ++i) {
        vdp.data_write(0x80U);
    }
    set_write_addr17(vdp, 0x12000U);
    vdp.data_write(0x80U); // first cell blinks; second cell remains normal

    vdp.render_frame();
    const int x0 = (512 - 80 * 6) / 2;
    CHECK(pixel(vdp, x0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, x0 + 6, 0) == 0xFFFFFFU);

    advance_frames(vdp, 10);
    CHECK(pixel(vdp, x0, 0) != 0xFFFFFFU);
    CHECK(pixel(vdp, x0 + 6, 0) == 0xFFFFFFU);
}

TEST_CASE("v9938 decodes SCREEN 2 through the GRAPHIC 2 mode bit", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 1, 0x08U); // GRAPHIC 2 / SCREEN 2
    enable_display(vdp);
    write_reg(vdp, 2, 0x06U); // name table at 0x1800
    write_reg(vdp, 3, 0x80U); // colour table at 0x2000
    write_reg(vdp, 4, 0x00U); // pattern table at 0x0000
    write_reg(vdp, 7, 0x01U); // non-white backdrop

    set_write_addr(vdp, 0x1800U);
    vdp.data_write(1U);
    set_write_addr(vdp, 0x0008U);
    vdp.data_write(0x80U);
    set_write_addr(vdp, 0x2008U);
    vdp.data_write(0xF2U);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, 1, 0) != 0xFFFFFFU);
}

TEST_CASE("v9938 uses 128 KiB table bases in GRAPHIC 1", "[chips][video][v9938]") {
    v9938 vdp;
    enable_display(vdp);
    write_reg(vdp, 2, 0x10U);  // name table at 0x4000
    write_reg(vdp, 3, 0x00U);  // colour table low bits at 0x10000
    write_reg(vdp, 4, 0x10U);  // pattern table at 0x8000
    write_reg(vdp, 10, 0x04U); // colour table high bits at 0x10000

    set_write_addr17(vdp, 0x4000U);
    vdp.data_write(1U);
    set_write_addr17(vdp, 0x8008U);
    vdp.data_write(0x80U);
    set_write_addr17(vdp, 0x10000U);
    vdp.data_write(0xF2U);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, 1, 0) != 0xFFFFFFU);
}

TEST_CASE("v9938 renders GRAPHIC 1 bottom rows in the 26.5-line frame", "[chips][video][v9938]") {
    v9938 vdp;
    enable_display(vdp);
    write_reg(vdp, 2, 0x06U); // name table at 0x1800
    write_reg(vdp, 3, 0x80U); // colour table at 0x2000
    write_reg(vdp, 4, 0x00U); // pattern table at 0x0000
    write_reg(vdp, 7, 0x01U);
    write_reg(vdp, 9, 0x80U);

    set_write_addr(vdp, static_cast<std::uint16_t>(0x1800U + 26U * 32U));
    vdp.data_write(1U);
    set_write_addr(vdp, 0x0008U);
    for (int i = 0; i < 8; ++i) {
        vdp.data_write(0x80U);
    }
    set_write_addr(vdp, 0x2000U);
    vdp.data_write(0xF2U);

    vdp.render_frame();
    CHECK(vdp.framebuffer().height == 212U);
    CHECK(pixel(vdp, 0, 208) == 0xFFFFFFU);
    CHECK(pixel(vdp, 1, 208) != 0xFFFFFFU);
    CHECK(pixel(vdp, 0, 211) == 0xFFFFFFU);
}

TEST_CASE("v9938 uses 128 KiB table bases in GRAPHIC 2", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 1, 0x08U); // GRAPHIC 2 / SCREEN 2
    enable_display(vdp);
    write_reg(vdp, 2, 0x10U);  // name table at 0x4000
    write_reg(vdp, 3, 0x00U);  // colour table low bits at 0x10000
    write_reg(vdp, 4, 0x10U);  // pattern table at 0x8000
    write_reg(vdp, 10, 0x04U); // colour table high bits at 0x10000

    set_write_addr17(vdp, 0x4000U);
    vdp.data_write(1U);
    set_write_addr17(vdp, 0x8008U);
    vdp.data_write(0x80U);
    set_write_addr17(vdp, 0x10008U);
    vdp.data_write(0xF2U);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, 1, 0) != 0xFFFFFFU);
}

TEST_CASE("v9938 renders GRAPHIC 3 fourth pattern third in the 26.5-line frame",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x02U); // GRAPHIC 3 / SCREEN 4
    write_reg(vdp, 1, 0x08U);
    enable_display(vdp);
    write_reg(vdp, 2, 0x10U);  // name table at 0x4000
    write_reg(vdp, 3, 0x00U);  // colour table low bits at 0x10000
    write_reg(vdp, 4, 0x10U);  // pattern table at 0x8000
    write_reg(vdp, 10, 0x04U); // colour table high bits at 0x10000
    write_reg(vdp, 9, 0x80U);

    set_write_addr17(vdp, 0x4000U + 24U * 32U);
    vdp.data_write(1U);
    set_write_addr17(vdp, 0x8000U + 3U * 0x800U + 8U);
    vdp.data_write(0x80U);
    set_write_addr17(vdp, 0x10000U + 3U * 0x800U + 8U);
    vdp.data_write(0xF2U);

    vdp.render_frame();
    CHECK(vdp.framebuffer().height == 212U);
    CHECK(pixel(vdp, 0, 192) == 0xFFFFFFU);
    CHECK(pixel(vdp, 1, 192) != 0xFFFFFFU);
}

TEST_CASE("v9938 renders SCREEN 3 multicolor 4x4 blocks", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x02U); // MULTI-COLOR / SCREEN 3
    enable_display(vdp);
    write_reg(vdp, 2, 0x06U); // name table at 0x1800
    write_reg(vdp, 4, 0x00U); // pattern generator at 0x0000
    write_reg(vdp, 7, 0x01U); // non-white backdrop

    set_write_addr(vdp, 0x1800U);
    vdp.data_write(1U);
    set_write_addr(vdp, 0x1820U);
    vdp.data_write(1U);
    set_write_addr(vdp, 0x0008U);
    vdp.data_write(0xF2U);
    vdp.data_write(0x4EU);
    vdp.data_write(0x3CU);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, 3, 3) == 0xFFFFFFU);
    CHECK(pixel(vdp, 4, 0) != 0xFFFFFFU);
    CHECK(pixel(vdp, 0, 4) != pixel(vdp, 0, 0));
    CHECK(pixel(vdp, 0, 8) != pixel(vdp, 0, 0));
}

TEST_CASE("v9938 renders MULTICOLOR bottom blocks in the 26.5-line frame",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x02U); // MULTI-COLOR / SCREEN 3
    enable_display(vdp);
    write_reg(vdp, 2, 0x06U); // name table at 0x1800
    write_reg(vdp, 4, 0x00U); // pattern generator at 0x0000
    write_reg(vdp, 7, 0x01U);
    write_reg(vdp, 9, 0x80U);

    set_write_addr(vdp, static_cast<std::uint16_t>(0x1800U + 26U * 32U));
    vdp.data_write(1U);
    set_write_addr(vdp, 0x000CU);
    vdp.data_write(0xF2U);

    vdp.render_frame();
    CHECK(vdp.framebuffer().height == 212U);
    CHECK(pixel(vdp, 0, 208) == 0xFFFFFFU);
    CHECK(pixel(vdp, 3, 211) == 0xFFFFFFU);
    CHECK(pixel(vdp, 4, 208) != 0xFFFFFFU);
}

TEST_CASE("v9938 renders SCREEN 5 style packed 4bpp bitmap pixels", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // bitmap4 mode in this implementation
    enable_display(vdp);
    set_write_addr(vdp, 0x0000U);
    vdp.data_write(0xF2U);

    vdp.render_frame();
    CHECK(vdp.framebuffer().width == 256U);
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, 1, 0) != 0x000000U);
}

TEST_CASE("v9938 blanks the framebuffer while display output is disabled",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap, display enable remains clear
    write_reg(vdp, 7, 0x0FU); // palette-backed backdrop colour
    set_write_addr(vdp, 0x0000U);
    vdp.data_write(0x20U);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, 1, 0) == 0xFFFFFFU);
}

TEST_CASE("v9938 uses the fixed RGB backdrop while SCREEN 8 output is disabled",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU); // SCREEN 8-style bitmap, display enable remains clear
    write_reg(vdp, 7, 0xE0U);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFF0000U);
}

TEST_CASE("v9938 applies the R8 color-zero transparency function to bitmap pixels",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap
    write_reg(vdp, 7, 0x0FU); // white backdrop
    enable_display(vdp);

    set_write_addr(vdp, 0x0000U);
    vdp.data_write(0x02U); // color 0 followed by color 2

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, 1, 0) != 0xFFFFFFU);

    write_reg(vdp, 8, 0x20U); // TP: color 0 comes from palette entry 0
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x000000U);
}

TEST_CASE("v9938 switches visible height through R9 in bitmap modes", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap
    enable_display(vdp);

    CHECK(vdp.framebuffer().height == 192U);

    write_reg(vdp, 9, 0x80U); // LN: expose the 212-line MSX2 bitmap frame
    CHECK(vdp.framebuffer().height == 212U);

    set_write_addr17(vdp, 200U * 128U);
    vdp.data_write(0xF0U);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 200) == 0xFFFFFFU);
}

TEST_CASE("v9938 switches PAL and NTSC scanline counts through R9", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap
    enable_display(vdp);

    CHECK(vdp.total_scanlines() == v9938::scanlines_ntsc);

    write_reg(vdp, 9, 0x82U); // LN plus *NT: 212 visible lines, PAL timing
    CHECK(vdp.framebuffer().height == 212U);
    CHECK(vdp.total_scanlines() == v9938::scanlines_pal);

    write_reg(vdp, 9, 0x80U); // keep LN, return to NTSC timing
    CHECK(vdp.framebuffer().height == 212U);
    CHECK(vdp.total_scanlines() == v9938::scanlines_ntsc);
}

TEST_CASE("v9938 selects sprite mode 2 in GRAPHIC 3", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x02U); // GRAPHIC 3 / SCREEN 4 adds sprite mode 2
    write_reg(vdp, 1, 0x08U);
    enable_display(vdp);
    write_reg(vdp, 5, 0x36U);  // sprite attribute table at 0x1B00
    write_reg(vdp, 6, 0x00U);  // sprite pattern table at 0x0000
    write_reg(vdp, 11, 0x00U); // color table is 512 bytes before attributes

    set_write_addr(vdp, 0x0008U);
    vdp.data_write(0x80U);
    vdp.data_write(0x80U);
    set_write_addr(vdp, 0x1900U);
    vdp.data_write(0x0FU);
    vdp.data_write(0x02U);

    set_write_addr(vdp, 0x1B00U);
    vdp.data_write(9U);
    vdp.data_write(12U);
    vdp.data_write(1U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    vdp.render_frame();
    CHECK(pixel(vdp, 12, 10) == 0xFFFFFFU);
    CHECK(pixel(vdp, 12, 11) != 0x000000U);
    CHECK(pixel(vdp, 12, 11) != pixel(vdp, 12, 10));
}

TEST_CASE("v9938 renders SCREEN 6 style 512-wide packed 2bpp bitmap pixels",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x08U); // GRAPHIC 5 / SCREEN 6 style 512 x 212 x 4
    enable_display(vdp);
    set_write_addr(vdp, 0x0000U);
    vdp.data_write(0xE4U); // color codes 3,2,1,0

    vdp.render_frame();
    CHECK(vdp.framebuffer().width == 512U);
    CHECK(pixel(vdp, 0, 0) != pixel(vdp, 1, 0));
    CHECK(pixel(vdp, 3, 0) == 0x000000U);
}

TEST_CASE("v9938 renders SCREEN 7 style 512-wide packed 4bpp bitmap pixels",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0AU); // GRAPHIC 6 / SCREEN 7 style 512 x 212 x 16
    enable_display(vdp);
    set_write_addr(vdp, 0x0000U);
    vdp.data_write(0xF2U);

    vdp.render_frame();
    CHECK(vdp.framebuffer().width == 512U);
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, 1, 0) != 0x000000U);
}

TEST_CASE("v9938 selects packed bitmap display pages through R2", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap
    enable_display(vdp);

    set_write_addr17(vdp, 0x00000U);
    vdp.data_write(0x20U);
    set_write_addr17(vdp, 0x08000U);
    vdp.data_write(0xF0U);

    write_reg(vdp, 2, 0x00U);
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) != 0xFFFFFFU);

    write_reg(vdp, 2, 0x20U);
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);
}

TEST_CASE("v9938 alternates SCREEN 5 display pages when R9 EO is set", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap
    enable_display(vdp);

    set_write_addr17(vdp, 0x00000U);
    vdp.data_write(0x20U);
    set_write_addr17(vdp, 0x08000U);
    vdp.data_write(0xF0U);

    write_reg(vdp, 2, 0x20U);
    write_reg(vdp, 9, 0x04U);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) != 0xFFFFFFU);

    advance_frames(vdp, 1);
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);

    advance_frames(vdp, 1);
    CHECK(pixel(vdp, 0, 0) != 0xFFFFFFU);
}

TEST_CASE("v9938 weaves SCREEN 5 interlace fields from the same page",
          "[chips][video][v9938][interlace]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap
    enable_display(vdp);

    set_write_addr17(vdp, 0x00000U);
    vdp.data_write(0xF0U);

    write_reg(vdp, 9, 0x08U); // IL: both fields use the same page
    vdp.render_frame();

    CHECK(vdp.framebuffer().height == 384U);
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, 0, 1) == 0xFFFFFFU);
    CHECK(pixel(vdp, 0, 2) == 0x000000U);
}

TEST_CASE("v9938 interlaces SCREEN 5 even and odd pages when R9 IL and EO are set",
          "[chips][video][v9938][interlace]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap
    enable_display(vdp);

    set_write_addr17(vdp, 0x00000U);
    vdp.data_write(0x20U);
    set_write_addr17(vdp, 0x08000U);
    vdp.data_write(0xF0U);

    write_reg(vdp, 2, 0x20U); // odd page selected; even page is paired below it
    write_reg(vdp, 9, 0x0CU); // IL + EO
    vdp.render_frame();

    CHECK(vdp.framebuffer().height == 384U);
    CHECK(pixel(vdp, 0, 0) != 0xFFFFFFU);
    CHECK(pixel(vdp, 0, 0) != 0x000000U);
    CHECK(pixel(vdp, 0, 1) == 0xFFFFFFU);
}

TEST_CASE("v9938 blinks SCREEN 5 display pages through R13", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap
    enable_display(vdp);

    set_write_addr17(vdp, 0x00000U);
    vdp.data_write(0x20U);
    set_write_addr17(vdp, 0x08000U);
    vdp.data_write(0xF0U);

    write_reg(vdp, 2, 0x20U);
    write_reg(vdp, 13, 0x11U); // 1/6 second even page, 1/6 second odd page

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) != 0xFFFFFFU);

    advance_frames(vdp, 10);
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);

    advance_frames(vdp, 10);
    CHECK(pixel(vdp, 0, 0) != 0xFFFFFFU);

    write_reg(vdp, 13, 0x11U);
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) != 0xFFFFFFU);
}

TEST_CASE("v9938 selects high-footprint bitmap display pages through R2", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU); // SCREEN 8-style bitmap
    enable_display(vdp);

    set_write_addr17(vdp, 0x00000U);
    vdp.data_write(0x00U);
    set_write_addr17(vdp, 0x10000U);
    vdp.data_write(0xE0U);

    write_reg(vdp, 2, 0x00U);
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x000000U);

    write_reg(vdp, 2, 0x40U);
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFF0000U);
}

TEST_CASE("v9938 alternates high-footprint bitmap pages when R9 EO is set",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU); // SCREEN 8-style bitmap
    enable_display(vdp);

    set_write_addr17(vdp, 0x00000U);
    vdp.data_write(0x00U);
    set_write_addr17(vdp, 0x10000U);
    vdp.data_write(0xE0U);

    write_reg(vdp, 2, 0x40U);
    write_reg(vdp, 9, 0x04U);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x000000U);

    advance_frames(vdp, 1);
    CHECK(pixel(vdp, 0, 0) == 0xFF0000U);

    advance_frames(vdp, 1);
    CHECK(pixel(vdp, 0, 0) == 0x000000U);
}

TEST_CASE("v9938 blinks high-footprint bitmap pages through R13", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU); // SCREEN 8-style bitmap
    enable_display(vdp);

    set_write_addr17(vdp, 0x00000U);
    vdp.data_write(0x00U);
    set_write_addr17(vdp, 0x10000U);
    vdp.data_write(0xE0U);

    write_reg(vdp, 2, 0x40U);
    write_reg(vdp, 13, 0x11U); // 1/6 second even page, 1/6 second odd page

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x000000U);

    advance_frames(vdp, 10);
    CHECK(pixel(vdp, 0, 0) == 0xFF0000U);

    advance_frames(vdp, 10);
    CHECK(pixel(vdp, 0, 0) == 0x000000U);
}

TEST_CASE("v9938 interlaces high-footprint bitmap pages with 212-line output",
          "[chips][video][v9938][interlace]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU); // SCREEN 8-style bitmap
    enable_display(vdp);

    set_write_addr17(vdp, 0x00000U);
    vdp.data_write(0x00U);
    set_write_addr17(vdp, 0x10000U);
    vdp.data_write(0xE0U);

    write_reg(vdp, 2, 0x40U); // odd high-footprint page selected
    write_reg(vdp, 9, 0x8CU); // LN + IL + EO
    vdp.render_frame();

    CHECK(vdp.framebuffer().height == 424U);
    CHECK(pixel(vdp, 0, 0) == 0x000000U);
    CHECK(pixel(vdp, 0, 1) == 0xFF0000U);
}

TEST_CASE("v9938 applies R18 display adjustment to the composed frame", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap
    enable_display(vdp);

    set_write_addr17(vdp, 128U);
    vdp.data_write(0x0FU);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) != 0xFFFFFFU);
    CHECK(pixel(vdp, 1, 1) == 0xFFFFFFU);

    write_reg(vdp, 18, 0x11U); // one pixel left/up
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, 1, 1) != 0xFFFFFFU);

    write_reg(vdp, 18, 0xFFU); // one pixel right/down
    vdp.render_frame();
    CHECK(pixel(vdp, 2, 2) == 0xFFFFFFU);
}

TEST_CASE("v9938 applies R23 vertical offset to bitmap rendering", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap
    enable_display(vdp);

    set_write_addr17(vdp, 0x00000U);
    vdp.data_write(0x20U);
    set_write_addr17(vdp, 255U * 128U);
    vdp.data_write(0xF0U);

    write_reg(vdp, 23, 255U);
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, 0, 1) != 0xFFFFFFU);
}

TEST_CASE("v9938 applies R23 vertical offset to sprite rendering", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap enables sprite mode 2
    enable_display(vdp);
    write_reg(vdp, 5, 0x36U);  // sprite attribute table at 0x1B00
    write_reg(vdp, 6, 0x00U);  // sprite pattern table at 0x0000
    write_reg(vdp, 11, 0x00U); // color table is 512 bytes before attributes
    write_reg(vdp, 23, 10U);   // visible line 0 samples source line 10

    set_write_addr17(vdp, 10U * 128U);
    vdp.data_write(0x22U); // background sentinel on the scrolled source line

    set_write_addr(vdp, 0x0008U);
    vdp.data_write(0x80U); // sprite pattern 1, first logical dot lit
    set_write_addr(vdp, 0x1900U);
    vdp.data_write(0x0FU);

    set_write_addr(vdp, 0x1B00U);
    vdp.data_write(9U); // sprite source Y is 10, matching the scrolled bitmap line
    vdp.data_write(0U);
    vdp.data_write(1U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0xFFFFFFU);
    CHECK(pixel(vdp, 1, 0) != 0x000000U);
    CHECK(pixel(vdp, 0, 10) != 0xFFFFFFU);
}

TEST_CASE("v9938 renders TMS-compatible sprites over the background", "[chips][video][v9938]") {
    v9938 vdp;
    enable_display(vdp);
    write_reg(vdp, 5, 0x36U); // sprite attribute table at 0x1B00
    write_reg(vdp, 6, 0x00U); // sprite pattern table at 0x0000

    set_write_addr(vdp, 0x0008U); // sprite pattern 1, diagonal
    for (std::uint8_t row = 0; row < 8U; ++row) {
        vdp.data_write(static_cast<std::uint8_t>(0x80U >> row));
    }

    set_write_addr(vdp, 0x1B00U);
    vdp.data_write(9U);    // Y is stored as screen Y - 1
    vdp.data_write(12U);   // X
    vdp.data_write(1U);    // pattern
    vdp.data_write(0x0FU); // white
    vdp.data_write(0xD0U); // terminate the remaining sprite list

    vdp.render_frame();
    CHECK(pixel(vdp, 12, 10) == 0xFFFFFFU);
    CHECK(pixel(vdp, 13, 10) == 0x000000U);
    CHECK(pixel(vdp, 13, 11) == 0xFFFFFFU);
}

TEST_CASE("v9938 hides sprite mode 1 sprites when R8 disables sprite display",
          "[chips][video][v9938]") {
    v9938 vdp;
    enable_display(vdp);
    write_reg(vdp, 5, 0x36U); // sprite attribute table at 0x1B00
    write_reg(vdp, 6, 0x00U); // sprite pattern table at 0x0000
    write_reg(vdp, 8, 0x02U); // SPD: disable sprite processing

    set_write_addr(vdp, 0x0008U);
    vdp.data_write(0x80U);

    set_write_addr(vdp, 0x1B00U);
    vdp.data_write(9U);
    vdp.data_write(12U);
    vdp.data_write(1U);
    vdp.data_write(0x0FU);
    vdp.data_write(0xD0U);

    vdp.render_frame();
    CHECK(pixel(vdp, 12, 10) == 0x000000U);
    CHECK((read_status(vdp, 0) & 0x60U) == 0x00U);
}

TEST_CASE("v9938 suppresses sprites in text display modes", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 1, 0x10U); // TEXT1 mode
    enable_display(vdp);
    write_reg(vdp, 5, 0x36U); // sprite attribute table at 0x1B00
    write_reg(vdp, 6, 0x00U); // sprite pattern table at 0x0000

    set_write_addr(vdp, 0x0008U);
    vdp.data_write(0x80U);

    set_write_addr(vdp, 0x1B00U);
    vdp.data_write(9U);
    vdp.data_write(12U);
    vdp.data_write(1U);
    vdp.data_write(0x0FU);
    vdp.data_write(0xD0U);

    vdp.render_frame();
    CHECK(pixel(vdp, 12, 10) == 0x000000U);
}

TEST_CASE("v9938 renders sprite mode 2 per-line colors in bitmap modes", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap enables sprite mode 2
    enable_display(vdp);
    write_reg(vdp, 5, 0x36U);  // sprite attribute table at 0x1B00
    write_reg(vdp, 6, 0x00U);  // sprite pattern table at 0x0000
    write_reg(vdp, 11, 0x00U); // color table is 512 bytes before attributes

    set_write_addr(vdp, 0x0008U); // sprite pattern 1, first two rows lit
    vdp.data_write(0x80U);
    vdp.data_write(0x80U);

    set_write_addr(vdp, 0x1900U); // sprite 0 color table
    vdp.data_write(0x0FU);
    vdp.data_write(0x02U);

    set_write_addr(vdp, 0x1B00U);
    vdp.data_write(9U);    // Y is stored as screen Y - 1
    vdp.data_write(12U);   // X
    vdp.data_write(1U);    // pattern
    vdp.data_write(0x00U); // reserved in sprite mode 2
    vdp.data_write(0xD8U); // terminate the remaining sprite list

    vdp.render_frame();
    CHECK(pixel(vdp, 12, 10) == 0xFFFFFFU);
    CHECK(pixel(vdp, 12, 11) != 0x000000U);
    CHECK(pixel(vdp, 12, 11) != pixel(vdp, 12, 10));
}

TEST_CASE("v9938 doubles sprite dots horizontally in 512-wide bitmap modes",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x08U); // SCREEN 6-style 512-wide bitmap enables sprite mode 2
    enable_display(vdp);
    write_reg(vdp, 5, 0x36U);  // sprite attribute table at 0x1B00
    write_reg(vdp, 6, 0x00U);  // sprite pattern table at 0x0000
    write_reg(vdp, 11, 0x00U); // color table is 512 bytes before attributes

    set_write_addr(vdp, 0x0008U);
    vdp.data_write(0x80U); // sprite pattern 1, first logical dot lit

    set_write_addr(vdp, 0x1900U);
    vdp.data_write(0x0FU);

    set_write_addr(vdp, 0x1B00U);
    vdp.data_write(9U);    // Y is stored as screen Y - 1
    vdp.data_write(12U);   // X is doubled in 512-wide bitmap modes
    vdp.data_write(1U);    // pattern
    vdp.data_write(0x00U); // reserved in sprite mode 2
    vdp.data_write(0xD8U); // terminate the remaining sprite list

    vdp.render_frame();
    CHECK(pixel(vdp, 23, 10) == 0x000000U);
    CHECK(pixel(vdp, 24, 10) == 0xFFFFFFU);
    CHECK(pixel(vdp, 25, 10) == 0xFFFFFFU);
    CHECK(pixel(vdp, 26, 10) == 0x000000U);
}

TEST_CASE("v9938 hides sprite mode 2 sprites when R8 disables sprite display",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap enables sprite mode 2
    enable_display(vdp);
    write_reg(vdp, 5, 0x36U); // sprite attribute table at 0x1B00
    write_reg(vdp, 6, 0x00U); // sprite pattern table at 0x0000
    write_reg(vdp, 8, 0x02U); // sprite display off

    set_write_addr(vdp, 0x0008U);
    vdp.data_write(0x80U);
    set_write_addr(vdp, 0x1900U);
    vdp.data_write(0x0FU);

    set_write_addr(vdp, 0x1B00U);
    vdp.data_write(9U);
    vdp.data_write(12U);
    vdp.data_write(1U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    vdp.render_frame();
    CHECK(pixel(vdp, 12, 10) == 0x000000U);
    CHECK((read_status(vdp, 0) & 0x60U) == 0x00U);
}

TEST_CASE("v9938 applies the R8 color-zero transparency function to sprites",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap enables sprite mode 2
    write_reg(vdp, 7, 0x0FU); // white backdrop for transparent sprite pixels
    enable_display(vdp);
    write_reg(vdp, 5, 0x36U);  // sprite attribute table at 0x1B00
    write_reg(vdp, 6, 0x00U);  // sprite pattern table at 0x0000
    write_reg(vdp, 11, 0x00U); // color table is 512 bytes before attributes

    set_write_addr(vdp, static_cast<std::uint16_t>(10U * 128U + 6U));
    vdp.data_write(0xF0U); // white background under sprite X=12

    set_write_addr(vdp, 0x0008U);
    vdp.data_write(0x80U); // sprite pattern 1, first logical dot lit
    set_write_addr(vdp, 0x1900U);
    vdp.data_write(0x00U); // sprite color 0 is transparent while TP is clear

    set_write_addr(vdp, 0x1B00U);
    vdp.data_write(9U);
    vdp.data_write(12U);
    vdp.data_write(1U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    vdp.render_frame();
    CHECK(pixel(vdp, 12, 10) == 0xFFFFFFU);

    write_reg(vdp, 8, 0x20U); // TP: sprite color 0 is opaque
    vdp.render_frame();
    CHECK(pixel(vdp, 12, 10) == 0x000000U);
}

TEST_CASE("v9938 reports sprite collision coordinates in status registers",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap enables sprite mode 2
    enable_display(vdp);
    write_reg(vdp, 5, 0x36U);  // sprite attribute table at 0x1B00
    write_reg(vdp, 6, 0x00U);  // sprite pattern table at 0x0000
    write_reg(vdp, 11, 0x00U); // color table is 512 bytes before attributes

    set_write_addr(vdp, 0x0008U);
    vdp.data_write(0x80U); // pattern 1, first pixel lit on row 0
    set_write_addr(vdp, 0x0010U);
    vdp.data_write(0x80U); // pattern 2, same solid pixel

    set_write_addr(vdp, 0x1900U);
    vdp.data_write(0x0FU); // sprite 0 color
    set_write_addr(vdp, 0x1910U);
    vdp.data_write(0x02U); // sprite 1 color

    set_write_addr(vdp, 0x1B00U);
    vdp.data_write(9U);
    vdp.data_write(12U);
    vdp.data_write(1U);
    vdp.data_write(0U);
    vdp.data_write(9U);
    vdp.data_write(12U);
    vdp.data_write(2U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    vdp.render_frame();
    CHECK((read_status(vdp, 0) & 0x20U) != 0U);
    CHECK((read_status(vdp, 0) & 0x20U) == 0x00U);
    CHECK(read_status(vdp, 3) == 12U);
    CHECK((read_status(vdp, 4) & 0x01U) == 0U);
    CHECK((read_status(vdp, 6) & 0x01U) == 0U);
    CHECK(read_status(vdp, 5) == 10U);
    CHECK(read_status(vdp, 3) == 0U);
    CHECK(read_status(vdp, 4) == 0xFEU);
    CHECK(read_status(vdp, 5) == 0U);
    CHECK(read_status(vdp, 6) == 0xFCU);
}

TEST_CASE("v9938 latches light pen coordinates and clears the flash interrupt",
          "[chips][video][v9938]") {
    v9938 vdp;
    vdp.latch_lightpen(0x123, 0x1AB, true, false);
    CHECK(read_status(vdp, 1) == 0x00U);

    bool irq_line = false;
    int rising_edges = 0;
    vdp.set_irq_callback([&](bool asserted) {
        irq_line = asserted;
        if (asserted) {
            ++rising_edges;
        }
    });
    write_reg(vdp, 0, 0x20U); // IE2: light pen interrupt enable
    write_reg(vdp, 8, 0x40U); // LP enabled, mouse disabled

    vdp.latch_lightpen(0x123, 0x1AB, true, false);
    CHECK(irq_line);
    CHECK(rising_edges == 1);
    CHECK((read_status(vdp, 1) & 0xC0U) == 0xC0U);
    CHECK_FALSE(irq_line);
    CHECK((read_status(vdp, 1) & 0xC0U) == 0x40U);
    CHECK(read_status(vdp, 3) == 0x23U);
    CHECK(read_status(vdp, 4) == 0xFFU);
    CHECK(read_status(vdp, 6) == 0xF9U);
    CHECK(read_status(vdp, 5) == 0xABU);
    CHECK(read_status(vdp, 3) == 0x00U);

    vdp.latch_lightpen(0x123, 0x1AB, false, true);
    CHECK(read_status(vdp, 6) == 0xFDU);

    write_reg(vdp, 8, 0x00U);
    CHECK(read_status(vdp, 3) == 0x00U);
    CHECK(read_status(vdp, 6) == 0xFCU);
}

TEST_CASE("v9938 latches mouse deltas through the pointing-device status registers",
          "[chips][video][v9938]") {
    v9938 vdp;
    vdp.latch_mouse_delta(static_cast<std::int8_t>(-1), static_cast<std::int8_t>(2), true, true);
    CHECK(read_status(vdp, 3) == 0x00U);

    write_reg(vdp, 8, 0x80U);  // mouse enabled, light pen disabled
    write_reg(vdp, 15, 0x03U); // selected S#3 pauses mouse counting
    vdp.latch_mouse_delta(static_cast<std::int8_t>(7), static_cast<std::int8_t>(8), true, true);
    CHECK((read_status(vdp, 1) & 0xC0U) == 0xC0U);
    CHECK(read_status(vdp, 3) == 0x00U);

    write_reg(vdp, 15, 0x00U);
    vdp.latch_mouse_delta(static_cast<std::int8_t>(-5), static_cast<std::int8_t>(12), true, false);
    CHECK((read_status(vdp, 1) & 0xC0U) == 0x40U);
    CHECK(read_status(vdp, 3) == 0xFBU);
    CHECK(read_status(vdp, 4) == 0xFEU);
    CHECK(read_status(vdp, 5) == 0x0CU);
    CHECK(read_status(vdp, 3) == 0x00U);
}

TEST_CASE("v9938 does not overwrite pointing-device coordinates on sprite collision",
          "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap enables sprite mode 2
    enable_display(vdp);
    write_reg(vdp, 5, 0x36U);  // sprite attribute table at 0x1B00
    write_reg(vdp, 6, 0x00U);  // sprite pattern table at 0x0000
    write_reg(vdp, 11, 0x00U); // color table is 512 bytes before attributes
    write_reg(vdp, 8, 0x40U);  // LP owns S#3-S#6 while selected
    vdp.latch_lightpen(0x123, 0x1AB, true, false);

    set_write_addr(vdp, 0x0008U);
    vdp.data_write(0x80U);
    set_write_addr(vdp, 0x0010U);
    vdp.data_write(0x80U);

    set_write_addr(vdp, 0x1900U);
    vdp.data_write(0x0FU);
    set_write_addr(vdp, 0x1910U);
    vdp.data_write(0x02U);

    set_write_addr(vdp, 0x1B00U);
    vdp.data_write(9U);
    vdp.data_write(12U);
    vdp.data_write(1U);
    vdp.data_write(0U);
    vdp.data_write(9U);
    vdp.data_write(12U);
    vdp.data_write(2U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    vdp.render_frame();
    CHECK((read_status(vdp, 0) & 0x20U) != 0U);
    CHECK(read_status(vdp, 3) == 0x23U);
    CHECK(read_status(vdp, 4) == 0xFFU);
    CHECK(read_status(vdp, 5) == 0xABU);
}

TEST_CASE("v9938 honors sprite mode 2 IC collision inhibit", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap enables sprite mode 2
    enable_display(vdp);
    write_reg(vdp, 5, 0x36U);  // sprite attribute table at 0x1B00
    write_reg(vdp, 6, 0x00U);  // sprite pattern table at 0x0000
    write_reg(vdp, 11, 0x00U); // color table is 512 bytes before attributes

    set_write_addr(vdp, 0x0008U);
    vdp.data_write(0x80U);
    set_write_addr(vdp, 0x0010U);
    vdp.data_write(0x80U);

    set_write_addr(vdp, 0x1900U);
    vdp.data_write(0x0FU);
    set_write_addr(vdp, 0x1910U);
    vdp.data_write(0x22U); // IC plus colour 2

    set_write_addr(vdp, 0x1B00U);
    vdp.data_write(9U);
    vdp.data_write(12U);
    vdp.data_write(1U);
    vdp.data_write(0U);
    vdp.data_write(9U);
    vdp.data_write(12U);
    vdp.data_write(2U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    vdp.render_frame();
    CHECK((read_status(vdp, 0) & 0x20U) == 0x00U);
}

TEST_CASE("v9938 sprite mode 2 CC bit ORs overlapping colors without collision",
          "[chips][video][v9938]") {
    const auto render_pair = [](std::uint8_t first_color, std::uint8_t second_color) {
        v9938 vdp;
        write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap enables sprite mode 2
        enable_display(vdp);
        write_reg(vdp, 5, 0x36U);  // sprite attribute table at 0x1B00
        write_reg(vdp, 6, 0x00U);  // sprite pattern table at 0x0000
        write_reg(vdp, 11, 0x00U); // color table is 512 bytes before attributes
        write_palette(vdp, 4U, 0U, 0U, 7U);
        write_palette(vdp, 8U, 7U, 0U, 0U);
        write_palette(vdp, 12U, 0U, 7U, 0U);

        set_write_addr(vdp, 0x0008U);
        vdp.data_write(0x80U);
        set_write_addr(vdp, 0x0010U);
        vdp.data_write(0x80U);

        set_write_addr(vdp, 0x1900U);
        vdp.data_write(first_color);
        set_write_addr(vdp, 0x1910U);
        vdp.data_write(second_color);

        set_write_addr(vdp, 0x1B00U);
        vdp.data_write(9U);
        vdp.data_write(12U);
        vdp.data_write(1U);
        vdp.data_write(0U);
        vdp.data_write(9U);
        vdp.data_write(12U);
        vdp.data_write(2U);
        vdp.data_write(0U);
        vdp.data_write(0xD8U);

        vdp.render_frame();
        CHECK((read_status(vdp, 0) & 0x20U) == 0x00U);
        return pixel(vdp, 12, 10);
    };

    CHECK(render_pair(0x08U, 0x44U) == 0x00FF00U);
    CHECK(render_pair(0x48U, 0x04U) == 0x00FF00U);
}

TEST_CASE("v9938 reports the ninth sprite on one line in sprite mode 2", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style bitmap enables sprite mode 2
    enable_display(vdp);
    write_reg(vdp, 5, 0x36U); // sprite attribute table at 0x1B00

    set_write_addr(vdp, 0x1B00U);
    for (std::uint8_t sprite = 0; sprite < 9U; ++sprite) {
        vdp.data_write(9U);
        vdp.data_write(static_cast<std::uint8_t>(sprite * 8U));
        vdp.data_write(0U);
        vdp.data_write(0U);
    }
    vdp.data_write(0xD8U);

    vdp.render_frame();
    const std::uint8_t s0 = read_status(vdp, 0);
    CHECK((s0 & 0x40U) != 0U);
    CHECK((s0 & 0x1FU) == 8U);
}

TEST_CASE("v9938 HMMV fills SCREEN 5 packed bitmap bytes", "[chips][video][v9938][command]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U); // SCREEN 5-style packed 4bpp bitmap
    enable_display(vdp);
    write_reg(vdp, 36, 0x00U); // DX
    write_reg(vdp, 38, 0x00U); // DY
    write_reg(vdp, 40, 0x04U); // NX
    write_reg(vdp, 42, 0x01U); // NY
    write_reg(vdp, 44, 0xABU); // two packed dots per byte in GRAPHIC 4
    write_reg(vdp, 46, 0xC0U); // HMMV

    CHECK(vdp.vram()[0] == 0xABU);
    CHECK(vdp.vram()[1] == 0xABU);
    CHECK((read_status(vdp, 2) & 0x01U) == 0U);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) != pixel(vdp, 1, 0));
    CHECK(pixel(vdp, 0, 0) == pixel(vdp, 2, 0));
}

TEST_CASE("v9938 HMMV fills SCREEN 6 packed 2bpp bitmap bytes", "[chips][video][v9938][command]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x08U);  // GRAPHIC 5 / SCREEN 6 style 512 x 212 x 4
    write_reg(vdp, 36, 0x00U); // DX
    write_reg(vdp, 38, 0x00U); // DY
    write_reg(vdp, 40, 0x04U); // NX: one packed byte, four dots
    write_reg(vdp, 42, 0x01U); // NY
    write_reg(vdp, 44, 0xE4U); // color codes 3,2,1,0
    write_reg(vdp, 46, 0xC0U); // HMMV

    CHECK(vdp.vram()[0] == 0xE4U);
    CHECK((read_status(vdp, 2) & 0x01U) == 0U);

    enable_display(vdp);
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) != pixel(vdp, 1, 0));
    CHECK(pixel(vdp, 3, 0) == 0x000000U);
}

TEST_CASE("v9938 LMMC streams CPU dots through R44 and updates command status",
          "[chips][video][v9938][command]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU);  // SCREEN 8-style 8bpp bitmap
    write_reg(vdp, 36, 0x02U); // DX
    write_reg(vdp, 38, 0x03U); // DY
    write_reg(vdp, 40, 0x02U); // NX
    write_reg(vdp, 42, 0x01U); // NY
    write_reg(vdp, 44, 0xE0U); // first dot
    write_reg(vdp, 46, 0xB0U); // LMMC, IMP

    CHECK((read_status(vdp, 2) & 0x81U) == 0x81U); // CE + TR while waiting
    write_reg(vdp, 17, 0xACU);                     // indirect R#44, no auto-increment
    vdp.register_indirect_write(0x1FU);            // second dot, completes command

    CHECK((read_status(vdp, 2) & 0x81U) == 0x00U);
    CHECK(vdp.vram()[3U * 256U + 2U] == 0xE0U);
    CHECK(vdp.vram()[3U * 256U + 3U] == 0x1FU);
}

TEST_CASE("v9938 HMMC streams packed bytes in SCREEN 7", "[chips][video][v9938][command]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0AU);  // GRAPHIC 6 / SCREEN 7 style 512 x 212 x 16
    write_reg(vdp, 36, 0x00U); // DX
    write_reg(vdp, 38, 0x00U); // DY
    write_reg(vdp, 40, 0x02U); // NX: one packed byte, two dots
    write_reg(vdp, 42, 0x01U); // NY
    write_reg(vdp, 44, 0xF2U); // first byte is consumed when the command starts
    write_reg(vdp, 46, 0xF0U); // HMMC

    CHECK(vdp.vram()[0] == 0xF2U);
    CHECK((read_status(vdp, 2) & 0x81U) == 0x00U);
}

TEST_CASE("v9938 HMMM copies bitmap dots through the command engine",
          "[chips][video][v9938][command]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU); // SCREEN 8-style 8bpp bitmap

    set_write_addr(vdp, static_cast<std::uint16_t>(2U * 256U + 4U));
    vdp.data_write(0x40U);
    vdp.data_write(0xE0U);

    write_reg(vdp, 32, 0x04U); // SX
    write_reg(vdp, 34, 0x02U); // SY
    write_reg(vdp, 36, 0x08U); // DX
    write_reg(vdp, 38, 0x05U); // DY
    write_reg(vdp, 40, 0x02U); // NX
    write_reg(vdp, 42, 0x01U); // NY
    write_reg(vdp, 46, 0xD0U); // HMMM

    CHECK(vdp.vram()[5U * 256U + 8U] == 0x40U);
    CHECK(vdp.vram()[5U * 256U + 9U] == 0xE0U);
}

TEST_CASE("v9938 HMMM aligns packed-mode X coordinates and width",
          "[chips][video][v9938][command]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x08U); // GRAPHIC 5 / SCREEN 6 style 512 x 212 x 4

    set_write_addr(vdp, 1U);
    vdp.data_write(0xE4U); // source byte covers dots 4..7

    write_reg(vdp, 32, 0x05U); // SX low bits are lost in G5, so source starts at 4
    write_reg(vdp, 34, 0x00U); // SY
    write_reg(vdp, 36, 0x09U); // DX low bits are lost in G5, so destination starts at 8
    write_reg(vdp, 38, 0x00U); // DY
    write_reg(vdp, 40, 0x05U); // NX low bits are lost in G5, so width is four dots
    write_reg(vdp, 42, 0x01U); // NY
    write_reg(vdp, 46, 0xD0U); // HMMM

    CHECK(vdp.vram()[2] == 0xE4U);
    CHECK(vdp.vram()[3] == 0x00U);
}

TEST_CASE("v9938 YMMM copies from DX through the selected screen edge",
          "[chips][video][v9938][command]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU); // SCREEN 8-style 8bpp bitmap

    set_write_addr(vdp, static_cast<std::uint16_t>(2U * 256U + 4U));
    vdp.data_write(0x20U);
    vdp.data_write(0x30U);
    vdp.data_write(0x40U);
    set_write_addr(vdp, static_cast<std::uint16_t>(2U * 256U + 8U));
    vdp.data_write(0xE0U);

    write_reg(vdp, 32, 0x08U); // ignored by YMMM; an HMMM alias would copy from here
    write_reg(vdp, 34, 0x02U); // SY
    write_reg(vdp, 36, 0x04U); // DX is both source and destination X
    write_reg(vdp, 38, 0x05U); // DY
    write_reg(vdp, 40, 0x01U); // ignored by YMMM; transfer runs to the right edge
    write_reg(vdp, 42, 0x01U); // NY
    write_reg(vdp, 46, 0xE0U); // YMMM, DIX right

    CHECK(vdp.vram()[5U * 256U + 4U] == 0x20U);
    CHECK(vdp.vram()[5U * 256U + 5U] == 0x30U);
    CHECK(vdp.vram()[5U * 256U + 6U] == 0x40U);
    CHECK(vdp.vram()[5U * 256U + 8U] == 0xE0U);

    set_write_addr(vdp, static_cast<std::uint16_t>(7U * 256U + 2U));
    vdp.data_write(0x50U);
    vdp.data_write(0x60U);
    vdp.data_write(0x70U);

    write_reg(vdp, 34, 0x07U); // SY
    write_reg(vdp, 36, 0x04U); // DX
    write_reg(vdp, 38, 0x09U); // DY
    write_reg(vdp, 42, 0x01U); // NY
    write_reg(vdp, 45, 0x04U); // DIX left
    write_reg(vdp, 46, 0xE0U); // YMMM

    CHECK(vdp.vram()[9U * 256U + 2U] == 0x50U);
    CHECK(vdp.vram()[9U * 256U + 3U] == 0x60U);
    CHECK(vdp.vram()[9U * 256U + 4U] == 0x70U);
}

TEST_CASE("v9938 LMCM streams bitmap dots from VRAM through status register 7",
          "[chips][video][v9938][command]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU); // SCREEN 8-style 8bpp bitmap

    set_write_addr(vdp, static_cast<std::uint16_t>(3U * 256U + 4U));
    vdp.data_write(0x10U);
    vdp.data_write(0x20U);

    write_reg(vdp, 32, 0x04U); // SX
    write_reg(vdp, 34, 0x03U); // SY
    write_reg(vdp, 40, 0x02U); // NX
    write_reg(vdp, 42, 0x01U); // NY
    write_reg(vdp, 46, 0xA0U); // LMCM

    CHECK((read_status(vdp, 2) & 0x81U) == 0x81U);
    CHECK(read_status(vdp, 7) == 0x10U);
    CHECK((read_status(vdp, 2) & 0x81U) == 0x81U);
    CHECK(read_status(vdp, 7) == 0x20U);
    CHECK((read_status(vdp, 2) & 0x81U) == 0x00U);
}

TEST_CASE("v9938 PSET and POINT operate on SCREEN 6 packed 2bpp dots",
          "[chips][video][v9938][command]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x08U);  // GRAPHIC 5 / SCREEN 6 style 512 x 212 x 4
    write_reg(vdp, 36, 0x02U); // DX
    write_reg(vdp, 38, 0x00U); // DY
    write_reg(vdp, 44, 0x03U);
    write_reg(vdp, 46, 0x50U); // PSET, IMP

    write_reg(vdp, 32, 0x02U); // SX
    write_reg(vdp, 34, 0x00U); // SY
    write_reg(vdp, 46, 0x40U); // POINT

    CHECK(vdp.vram()[0] == 0x0CU);
    CHECK(read_status(vdp, 7) == 0x03U);
}

TEST_CASE("v9938 LINE draws a hardware-command bitmap line", "[chips][video][v9938][command]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU);  // SCREEN 8-style 8bpp bitmap
    write_reg(vdp, 36, 0x02U); // DX
    write_reg(vdp, 38, 0x03U); // DY
    write_reg(vdp, 40, 0x04U); // long-side delta
    write_reg(vdp, 42, 0x02U); // short-side delta
    write_reg(vdp, 44, 0xE0U);
    write_reg(vdp, 46, 0x70U); // LINE, IMP

    CHECK(vdp.vram()[3U * 256U + 2U] == 0xE0U);
    CHECK(vdp.vram()[3U * 256U + 3U] == 0xE0U);
    CHECK(vdp.vram()[4U * 256U + 4U] == 0xE0U);
    CHECK(vdp.vram()[4U * 256U + 5U] == 0xE0U);
    CHECK(vdp.vram()[5U * 256U + 6U] == 0xE0U);
    CHECK((read_status(vdp, 2) & 0x01U) == 0U);
}

TEST_CASE("v9938 SRCH reports the matching border coordinate in status registers",
          "[chips][video][v9938][command]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU); // SCREEN 8-style 8bpp bitmap

    set_write_addr(vdp, static_cast<std::uint16_t>(4U * 256U + 6U));
    vdp.data_write(0x5AU);

    write_reg(vdp, 32, 0x02U); // SX
    write_reg(vdp, 34, 0x04U); // SY
    write_reg(vdp, 44, 0x5AU); // border color
    write_reg(vdp, 45, 0x02U); // EQ, scan right
    write_reg(vdp, 46, 0x60U); // SRCH

    CHECK((read_status(vdp, 2) & 0x11U) == 0x10U);
    CHECK(read_status(vdp, 8) == 0x06U);
    CHECK((read_status(vdp, 9) & 0x01U) == 0x00U);
}

TEST_CASE("v9938 composes fixed bits in high coordinate status registers",
          "[chips][video][v9938][command]") {
    v9938 vdp;
    CHECK(read_status(vdp, 4) == 0xFEU);
    CHECK(read_status(vdp, 6) == 0xFCU);
    CHECK(read_status(vdp, 9) == 0xFEU);

    write_reg(vdp, 0, 0x08U); // SCREEN 6-style 512-wide packed 2bpp bitmap
    set_write_addr(vdp, 65U); // X=260 is dot 0 of packed byte 65
    vdp.data_write(0x80U);    // color 2 in the high two bits

    write_reg(vdp, 32, 0x00U); // SX = 256
    write_reg(vdp, 33, 0x01U);
    write_reg(vdp, 34, 0x00U); // SY
    write_reg(vdp, 44, 0x02U); // border color
    write_reg(vdp, 45, 0x02U); // EQ, scan right
    write_reg(vdp, 46, 0x60U); // SRCH

    CHECK(read_status(vdp, 8) == 0x04U);
    CHECK(read_status(vdp, 9) == 0xFFU);
}

TEST_CASE("v9938 PSET and POINT operate on command coordinates", "[chips][video][v9938][command]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU);  // SCREEN 8-style 8bpp bitmap
    write_reg(vdp, 36, 0x04U); // DX
    write_reg(vdp, 38, 0x06U); // DY
    write_reg(vdp, 44, 0xE0U);
    write_reg(vdp, 46, 0x50U); // PSET, IMP

    write_reg(vdp, 32, 0x04U); // SX
    write_reg(vdp, 34, 0x06U); // SY
    write_reg(vdp, 46, 0x40U); // POINT

    CHECK(read_status(vdp, 7) == 0xE0U);
}

TEST_CASE("v9938 composes status register 2 fixed and command bits",
          "[chips][video][v9938][command]") {
    v9938 vdp;
    CHECK((read_status(vdp, 2) & 0x0CU) == 0x0CU);

    write_reg(vdp, 0, 0x0CU);  // SCREEN 8-style 8bpp bitmap
    write_reg(vdp, 36, 0x02U); // DX
    write_reg(vdp, 38, 0x03U); // DY
    write_reg(vdp, 40, 0x02U); // NX
    write_reg(vdp, 42, 0x01U); // NY
    write_reg(vdp, 44, 0xE0U); // first dot is consumed when the command starts
    write_reg(vdp, 46, 0xB0U); // LMMC, IMP

    CHECK((read_status(vdp, 2) & 0x8DU) == 0x8DU);
    write_reg(vdp, 17, 0xACU);          // indirect R#44, no auto-increment
    vdp.register_indirect_write(0x1FU); // second dot, completes command

    CHECK((read_status(vdp, 2) & 0x8DU) == 0x0CU);
}

TEST_CASE("v9938 reports status register 2 horizontal retrace timing", "[chips][video][v9938]") {
    v9938 vdp;
    CHECK((read_status(vdp, 2) & 0x20U) == 0x00U);

    vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line - 1));
    CHECK((read_status(vdp, 2) & 0x20U) != 0x00U);

    vdp.tick(1U);
    CHECK((read_status(vdp, 2) & 0x20U) == 0x00U);
}

TEST_CASE("v9938 reports status register 2 vertical retrace timing", "[chips][video][v9938]") {
    v9938 vdp;
    CHECK((read_status(vdp, 2) & 0x40U) == 0x00U);

    vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) * 192ULL);
    CHECK((read_status(vdp, 2) & 0x40U) != 0x00U);

    vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) *
             static_cast<std::uint64_t>(vdp.total_scanlines() - 192));
    CHECK((read_status(vdp, 2) & 0x40U) == 0x00U);
}

TEST_CASE("v9938 reports status register 2 display field", "[chips][video][v9938]") {
    v9938 vdp;
    CHECK((read_status(vdp, 2) & 0x02U) == 0x00U);

    advance_frames(vdp, 1);
    CHECK((read_status(vdp, 2) & 0x02U) != 0x00U);

    advance_frames(vdp, 1);
    CHECK((read_status(vdp, 2) & 0x02U) == 0x00U);
}

TEST_CASE("v9938 raises the frame interrupt at the 192-line vertical blank boundary",
          "[chips][video][v9938]") {
    v9938 vdp;
    int rising = 0;
    bool line = false;
    vdp.set_irq_callback([&](bool asserted) {
        line = asserted;
        if (asserted) {
            ++rising;
        }
    });
    write_reg(vdp, 1, 0x20U); // enable frame IRQ

    vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) * 192ULL - 1ULL);
    CHECK((vdp.status() & 0x80U) == 0x00U);
    CHECK_FALSE(line);

    vdp.tick(1U);
    CHECK((vdp.status() & 0x80U) != 0x00U);
    CHECK(line);
    CHECK(rising == 1);
    CHECK(vdp.frame_index() == 1U);
}

TEST_CASE("v9938 delays the frame interrupt to the 212-line LN boundary", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 9, 0x80U); // LN: 212 active display lines

    vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) * 192ULL);
    CHECK((vdp.status() & 0x80U) == 0x00U);

    vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) * 20ULL);
    CHECK((vdp.status() & 0x80U) != 0x00U);
}

TEST_CASE("v9938 raises and clears the frame interrupt", "[chips][video][v9938]") {
    v9938 vdp;
    int rising = 0;
    bool line = false;
    vdp.set_irq_callback([&](bool asserted) {
        line = asserted;
        if (asserted) {
            ++rising;
        }
    });
    write_reg(vdp, 1, 0x20U); // enable frame IRQ

    vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) *
             static_cast<std::uint64_t>(v9938::scanlines_ntsc));
    CHECK(vdp.frame_index() == 1U);
    CHECK(line);
    CHECK(rising == 1);
    CHECK((vdp.status() & 0x80U) != 0U);

    CHECK((vdp.ctrl_read() & 0x80U) != 0U);
    CHECK_FALSE(line);
    CHECK((vdp.status() & 0x80U) == 0U);
}

TEST_CASE("v9938 raises and clears the horizontal scanline interrupt", "[chips][video][v9938]") {
    v9938 vdp;
    int rising = 0;
    bool line = false;
    vdp.set_irq_callback([&](bool asserted) {
        line = asserted;
        if (asserted) {
            ++rising;
        }
    });
    write_reg(vdp, 19, 1U);   // scanline interrupt target
    write_reg(vdp, 0, 0x10U); // enable IE1

    vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line));
    CHECK(line);
    CHECK(rising == 1);

    CHECK((read_status(vdp, 1) & 0x01U) != 0U);
    CHECK_FALSE(line);
    CHECK((read_status(vdp, 1) & 0x01U) == 0U);
}

TEST_CASE("v9938 save/load round-trips register and timing state", "[chips][video][v9938]") {
    v9938 vdp;
    write_reg(vdp, 2, 0x02U);
    write_reg(vdp, 14, 0x01U);
    set_write_addr(vdp, 0x0040U);
    vdp.data_write(0x5AU);
    vdp.tick(1234U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    vdp.save_state(writer);

    v9938 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.reg(2) == 0x02U);
    CHECK(restored.vram()[0x4040U] == 0x5AU);
}

TEST_CASE("v9938 save/load preserves an active CPU read command",
          "[chips][video][v9938][command]") {
    v9938 vdp;
    write_reg(vdp, 0, 0x0CU);
    set_write_addr(vdp, static_cast<std::uint16_t>(2U * 256U + 8U));
    vdp.data_write(0x33U);

    write_reg(vdp, 32, 0x08U);
    write_reg(vdp, 34, 0x02U);
    write_reg(vdp, 40, 0x01U);
    write_reg(vdp, 42, 0x01U);
    write_reg(vdp, 46, 0xA0U); // LMCM waiting for S#7 read

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    vdp.save_state(writer);

    v9938 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK((read_status(restored, 2) & 0x81U) == 0x81U);
    CHECK(read_status(restored, 7) == 0x33U);
    CHECK((read_status(restored, 2) & 0x81U) == 0x00U);
}
