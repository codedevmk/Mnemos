#include "v9938.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::video::v9938;

    constexpr std::uint32_t k_sprite_mode2_sat = 0x1B80U;
    constexpr std::uint32_t k_sprite_mode2_sct = k_sprite_mode2_sat - 512U;

    void write_reg(v9938& vdp, int reg, std::uint8_t value) {
        vdp.ctrl_write(value);
        vdp.ctrl_write(static_cast<std::uint8_t>(0x80U | (reg & 0x3F)));
    }

    void set_addr(v9938& vdp, std::uint32_t addr, bool write) {
        write_reg(vdp, 14, static_cast<std::uint8_t>((addr >> 14U) & 0x07U));
        vdp.ctrl_write(static_cast<std::uint8_t>(addr & 0xFFU));
        vdp.ctrl_write(static_cast<std::uint8_t>((write ? 0x40U : 0x00U) | ((addr >> 8U) & 0x3FU)));
    }

    void install_red_blue_palette(v9938& vdp) {
        write_reg(vdp, 16, 1U);
        vdp.palette_write(0x70U);
        vdp.palette_write(0x00U); // palette 1: red
        vdp.palette_write(0x07U);
        vdp.palette_write(0x00U); // palette 2: blue
    }

    void install_red_blue_green_palette(v9938& vdp) {
        install_red_blue_palette(vdp);
        write_reg(vdp, 16, 3U);
        vdp.palette_write(0x00U);
        vdp.palette_write(0x07U); // palette 3: green
    }

    void install_green_red_palette(v9938& vdp) {
        write_reg(vdp, 16, 0U);
        vdp.palette_write(0x00U);
        vdp.palette_write(0x07U); // palette 0: green
        vdp.palette_write(0x70U);
        vdp.palette_write(0x00U); // palette 1: red
    }

    std::uint32_t pixel(const v9938& vdp, int x, int y) {
        const auto fb = vdp.framebuffer();
        return fb.pixels[static_cast<std::size_t>(y) * fb.effective_stride() +
                         static_cast<std::size_t>(x)];
    }

    std::uint32_t bw32_from_rgb(std::uint32_t rgb) {
        const std::uint32_t r = (rgb >> 16U) & 0xFFU;
        const std::uint32_t g = (rgb >> 8U) & 0xFFU;
        const std::uint32_t b = rgb & 0xFFU;
        const std::uint32_t luma8 = (77U * r + 150U * g + 29U * b + 128U) >> 8U;
        const std::uint32_t luma5 = (luma8 * 31U + 127U) / 255U;
        const std::uint32_t channel = (luma5 * 255U + 15U) / 31U;
        return (channel << 16U) | (channel << 8U) | channel;
    }

    bool is_monochrome(std::uint32_t rgb) {
        return ((rgb >> 16U) & 0xFFU) == ((rgb >> 8U) & 0xFFU) &&
               ((rgb >> 8U) & 0xFFU) == (rgb & 0xFFU);
    }

    void advance_frames(v9938& vdp, std::uint64_t frames) {
        vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) *
                 static_cast<std::uint64_t>(v9938::scanlines_ntsc) * frames);
    }

    void configure_sprite_mode2(v9938& vdp, std::uint8_t mode_reg0) {
        write_reg(vdp, 0, mode_reg0);
        write_reg(vdp, 1, 0x40U);
        write_reg(vdp, 5, static_cast<std::uint8_t>(k_sprite_mode2_sat >> 7U));
        write_reg(vdp, 6, 0U);
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::ivideo, v9938>);

TEST_CASE("v9938 reports identity and registers under yamaha.v9938") {
    const v9938 vdp;
    const auto md = vdp.metadata();
    CHECK(md.manufacturer == "Yamaha");
    CHECK(md.part_number == "V9938");
    CHECK(md.family == "MSX-VIDEO");
    CHECK(md.klass == mnemos::chips::chip_class::video);

    const auto chip = mnemos::chips::create_chip("yamaha.v9938");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == std::string("V9938"));
}

TEST_CASE("v9938 direct and indirect register writes address all MSX2 registers") {
    v9938 vdp;
    write_reg(vdp, 0, 0x04U);
    write_reg(vdp, 1, 0x50U);
    write_reg(vdp, 9, 0x80U);
    CHECK(vdp.visible_height() == v9938::display_height_212);

    write_reg(vdp, 17, 0x80U | 15U); // no autoincrement
    vdp.indirect_reg_write(3U);
    CHECK(vdp.reg(15) == 3U);
    vdp.indirect_reg_write(2U);
    CHECK(vdp.reg(15) == 2U);

    write_reg(vdp, 17, 14U); // autoincrement
    vdp.indirect_reg_write(5U);
    vdp.indirect_reg_write(6U);
    CHECK(vdp.reg(14) == 5U);
    CHECK(vdp.reg(15) == 6U);
    CHECK((vdp.reg(17) & 0x3FU) == 16U);
}

TEST_CASE("v9938 R9 NT bit selects 50 and 60 Hz frame cadence") {
    v9938 vdp;
    REQUIRE_FALSE(vdp.is_pal());

    write_reg(vdp, 9, 0x02U);
    CHECK(vdp.is_pal());
    vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) *
             static_cast<std::uint64_t>(v9938::scanlines_ntsc));
    CHECK(vdp.frame_index() == 0U);

    vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) *
             static_cast<std::uint64_t>(v9938::scanlines_pal - v9938::scanlines_ntsc));
    CHECK(vdp.frame_index() == 1U);

    write_reg(vdp, 9, 0x00U);
    CHECK_FALSE(vdp.is_pal());
    vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) *
             static_cast<std::uint64_t>(v9938::scanlines_ntsc));
    CHECK(vdp.frame_index() == 2U);
}

TEST_CASE("v9938 CPU VRAM port accesses the full 128 KiB address space") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U); // Graphic 4 enables R#14 carry auto-increment.
    set_addr(vdp, 0x1BFFE, true);
    vdp.data_write(0x11U);
    vdp.data_write(0x22U);
    vdp.data_write(0x33U);

    CHECK(vdp.reg(14) == 7U);
    set_addr(vdp, 0x1BFFE, false);
    CHECK(vdp.data_read() == 0x11U);
    CHECK(vdp.data_read() == 0x22U);
    CHECK(vdp.data_read() == 0x33U);
}

TEST_CASE("v9938 R45 banks CPU port access between VRAM and expansion RAM") {
    v9938 vdp;

    set_addr(vdp, 0x01234U, true);
    vdp.data_write(0x11U);

    write_reg(vdp, 45, 0x40U); // MXD selects the 64 KiB expansion RAM plane.
    set_addr(vdp, 0x01234U, true);
    vdp.data_write(0x22U);

    set_addr(vdp, 0x01234U, false);
    CHECK(vdp.data_read() == 0x22U);

    write_reg(vdp, 45, 0x00U);
    set_addr(vdp, 0x01234U, false);
    CHECK(vdp.data_read() == 0x11U);

    write_reg(vdp, 45, 0x40U);
    set_addr(vdp, 0x11234U, true);
    vdp.data_write(0x33U);
    set_addr(vdp, 0x01234U, false);
    CHECK(vdp.data_read() == 0x33U);
}

TEST_CASE("v9938 TMS-compatible modes hold R14 when the VRAM port crosses 16 KiB") {
    v9938 vdp;

    set_addr(vdp, 0x03FFFU, true);
    vdp.data_write(0x11U);
    vdp.data_write(0x22U);

    CHECK(vdp.mode() == v9938::display_mode::graphics_i);
    CHECK(vdp.reg(14) == 0U);
    CHECK(vdp.vram()[0x03FFFU] == 0x11U);
    CHECK(vdp.vram()[0x00000U] == 0x22U);

    write_reg(vdp, 0, 0x02U); // Graphics II is also TMS-compatible for R#14 carry.
    set_addr(vdp, 0x07FFFU, true);
    vdp.data_write(0x33U);
    vdp.data_write(0x44U);

    CHECK(vdp.mode() == v9938::display_mode::graphics_ii);
    CHECK(vdp.reg(14) == 1U);
    CHECK(vdp.vram()[0x07FFFU] == 0x33U);
    CHECK(vdp.vram()[0x04000U] == 0x44U);
}

TEST_CASE("v9938 palette writes commit RGB333 entries and autoincrement") {
    v9938 vdp;
    write_reg(vdp, 16, 2U);
    vdp.palette_write(0x75U); // R=7, B=5
    vdp.palette_write(0x03U); // G=3

    CHECK(vdp.palette(2) == 0x01DDU);
    CHECK(vdp.reg(16) == 3U);
}

TEST_CASE("v9938 reset palette matches MSX2 RGB333 defaults") {
    v9938 vdp;

    CHECK(vdp.palette(1) == 0x000U);
    CHECK(vdp.palette(2) == 0x071U);
    CHECK(vdp.palette(4) == 0x04FU);
    CHECK(vdp.palette(15) == 0x1FFU);

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x01U); // name table: top-left tile uses pattern 1
    set_addr(vdp, 0x0008U, true);
    vdp.data_write(0x80U); // pattern 1 row 0: first pixel set
    set_addr(vdp, 0x0200U, true);
    vdp.data_write(0x24U); // foreground medium green, background dark blue

    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 3, 0x08U);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x0024DA24U);
    CHECK(pixel(vdp, 1, 0) == 0x002424FFU);
}

TEST_CASE("v9938 renders TMS-compatible Graphics I tiles") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x01U); // name table: top-left tile uses pattern 1
    set_addr(vdp, 0x0008U, true);
    vdp.data_write(0x80U); // pattern 1 row 0: first pixel set
    set_addr(vdp, 0x0200U, true);
    vdp.data_write(0x12U); // foreground red, background blue

    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 3, 0x08U);
    vdp.render_frame();

    CHECK(vdp.mode() == v9938::display_mode::graphics_i);
    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 0) == 0x000000FFU);
}

TEST_CASE("v9938 TMS-compatible renderers honor upper VRAM table base bits") {
    {
        v9938 vdp;
        install_red_blue_palette(vdp);

        set_addr(vdp, 0x10000U, true);
        vdp.data_write(0x01U); // name table in upper VRAM
        set_addr(vdp, 0x10008U, true);
        vdp.data_write(0x80U); // pattern table in upper VRAM
        set_addr(vdp, 0x12000U, true);
        vdp.data_write(0x12U); // colour table from R#10/R#3 upper address bits

        write_reg(vdp, 1, 0x40U);
        write_reg(vdp, 2, 0x40U); // name table base $10000
        write_reg(vdp, 3, 0x80U); // colour table A13
        write_reg(vdp, 4, 0x20U); // pattern table A16
        write_reg(vdp, 10, 0x04U); // colour table A16
        vdp.render_frame();

        CHECK(vdp.mode() == v9938::display_mode::graphics_i);
        CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
        CHECK(pixel(vdp, 1, 0) == 0x000000FFU);
    }

    {
        v9938 vdp;
        install_red_blue_palette(vdp);

        set_addr(vdp, 0x10000U, true);
        vdp.data_write(0x01U); // name table in upper VRAM
        set_addr(vdp, 0x10008U, true);
        vdp.data_write(0x80U); // pattern table in upper VRAM
        set_addr(vdp, 0x12008U, true);
        vdp.data_write(0x12U); // Graphic II colour table in upper VRAM

        write_reg(vdp, 0, 0x02U); // M3 -> Graphics II
        write_reg(vdp, 1, 0x40U);
        write_reg(vdp, 2, 0x40U); // name table base $10000
        write_reg(vdp, 3, 0x80U); // colour table A13, low mask closed
        write_reg(vdp, 4, 0x20U); // pattern table A16, low mask closed
        write_reg(vdp, 10, 0x04U); // colour table A16
        vdp.render_frame();

        CHECK(vdp.mode() == v9938::display_mode::graphics_ii);
        CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
        CHECK(pixel(vdp, 1, 0) == 0x000000FFU);
    }
}

TEST_CASE("v9938 renders TMS-compatible Text I cells") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x01U);
    set_addr(vdp, 0x0008U, true);
    vdp.data_write(0x80U);

    write_reg(vdp, 1, 0x50U); // display enable + M1 -> Text I
    write_reg(vdp, 7, 0x12U); // foreground red, background blue
    vdp.render_frame();

    CHECK(vdp.mode() == v9938::display_mode::text_i);
    CHECK(pixel(vdp, 8, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 9, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 0, 0) == 0x000000FFU);
}

TEST_CASE("v9938 renders Text II 80-column cells from the 4 KiB name table") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x1000U, true);
    vdp.data_write(0x01U);
    set_addr(vdp, 0x0008U, true);
    vdp.data_write(0x80U);

    write_reg(vdp, 0, 0x04U); // M4
    write_reg(vdp, 1, 0x50U); // display enable + M1 -> Text II
    write_reg(vdp, 2, 0x04U); // name table base $01000
    write_reg(vdp, 7, 0x12U);
    vdp.render_frame();

    CHECK(vdp.mode() == v9938::display_mode::text_ii);
    CHECK(vdp.framebuffer().width == 512U);
    CHECK(pixel(vdp, 16, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 17, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 0, 0) == 0x000000FFU);
}

TEST_CASE("v9938 Text II exposes the upper half of row 26 in 212-line mode") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x1000U + 26U * 80U, true);
    vdp.data_write(0x01U);
    set_addr(vdp, 0x0008U, true);
    vdp.data_write(0x80U);

    write_reg(vdp, 0, 0x04U);
    write_reg(vdp, 1, 0x50U);
    write_reg(vdp, 2, 0x04U);
    write_reg(vdp, 7, 0x12U);
    write_reg(vdp, 9, 0x80U);
    vdp.render_frame();

    CHECK(vdp.framebuffer().height == 212U);
    CHECK(pixel(vdp, 16, 208) == 0x00FF0000U);
}

TEST_CASE("v9938 Text II blink table switches marked cells to R12 colours") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x1000U, true);
    vdp.data_write(0x01U);
    vdp.data_write(0x01U);
    set_addr(vdp, 0x0008U, true);
    vdp.data_write(0x80U);
    set_addr(vdp, 0x1800U, true);
    vdp.data_write(0x80U); // blink the first cell; keep the second cell in normal colours

    write_reg(vdp, 0, 0x04U);
    write_reg(vdp, 1, 0x50U);
    write_reg(vdp, 2, 0x04U);
    write_reg(vdp, 3, 0x67U); // blink table base $01800
    write_reg(vdp, 7, 0x12U);
    write_reg(vdp, 12, 0x21U);
    write_reg(vdp, 13, 0x11U);
    vdp.render_frame();

    CHECK(pixel(vdp, 16, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 17, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 22, 0) == 0x00FF0000U);

    advance_frames(vdp, 10U);
    vdp.render_frame();

    CHECK(pixel(vdp, 16, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 17, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 22, 0) == 0x00FF0000U);
}

TEST_CASE("v9938 renders TMS-compatible Graphics II masks") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x0008U, true);
    vdp.data_write(0x80U); // pattern 1 mirrored into the middle screen third
    set_addr(vdp, 0x0900U, true);
    vdp.data_write(0x01U); // name table row 8, column 0 -> line 64
    set_addr(vdp, 0x2008U, true);
    vdp.data_write(0x12U);

    write_reg(vdp, 0, 0x02U); // M3 -> Graphics II
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 2, 0x02U);
    write_reg(vdp, 3, 0x9FU);
    write_reg(vdp, 4, 0x00U);
    vdp.render_frame();

    CHECK(vdp.mode() == v9938::display_mode::graphics_ii);
    CHECK(pixel(vdp, 0, 64) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 64) == 0x000000FFU);
}

TEST_CASE("v9938 renders Graphic 3 as Graphics II with sprite mode 2") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x80U); // sprite pattern 0 row 0
    set_addr(vdp, 0x0008U, true);
    vdp.data_write(0x80U); // background pattern 1 row 0
    set_addr(vdp, 0x0800U, true);
    vdp.data_write(0x01U);
    set_addr(vdp, 0x2008U, true);
    vdp.data_write(0x12U);

    set_addr(vdp, k_sprite_mode2_sat, true);
    vdp.data_write(0xFFU);
    vdp.data_write(10U);
    vdp.data_write(0U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    set_addr(vdp, k_sprite_mode2_sct, true);
    vdp.data_write(0x01U);

    configure_sprite_mode2(vdp, 0x04U);
    write_reg(vdp, 2, 0x02U);
    write_reg(vdp, 3, 0x9FU);
    write_reg(vdp, 4, 0U);
    vdp.render_frame();

    CHECK(vdp.mode() == v9938::display_mode::graphics_iii);
    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 10, 0) == 0x00FF0000U);
}

TEST_CASE("v9938 LN bit only extends Text II and Graphic 4 through 7") {
    v9938 vdp;

    write_reg(vdp, 9, 0x80U);
    write_reg(vdp, 0, 0x04U);
    write_reg(vdp, 1, 0x40U);
    CHECK(vdp.mode() == v9938::display_mode::graphics_iii);
    CHECK(vdp.visible_height() == v9938::display_height_192);

    write_reg(vdp, 1, 0x50U);
    CHECK(vdp.mode() == v9938::display_mode::text_ii);
    CHECK(vdp.visible_height() == v9938::display_height_212);

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    CHECK(vdp.mode() == v9938::display_mode::graphics_iv);
    CHECK(vdp.visible_height() == v9938::display_height_212);
}

TEST_CASE("v9938 R9 IL doubles public framebuffer height from the field height") {
    v9938 vdp;

    write_reg(vdp, 9, 0x08U);
    CHECK(vdp.mode() == v9938::display_mode::graphics_i);
    CHECK(vdp.visible_height() == v9938::display_height_384);

    write_reg(vdp, 0, 0x04U);
    write_reg(vdp, 1, 0x50U);
    CHECK(vdp.mode() == v9938::display_mode::text_ii);
    CHECK(vdp.visible_height() == v9938::display_height_384);

    write_reg(vdp, 9, 0x88U);
    CHECK(vdp.visible_height() == v9938::display_height_424);

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    CHECK(vdp.mode() == v9938::display_mode::graphics_iv);
    CHECK(vdp.visible_height() == v9938::display_height_424);
}

TEST_CASE("v9938 renders mode-1 sprites and reports S0 collision and overflow") {
    v9938 vdp;

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x80U);

    set_addr(vdp, 0x1B00U, true);
    vdp.data_write(0xFFU); // two visible sprites overlap on line 0
    vdp.data_write(0x00U);
    vdp.data_write(0x00U);
    vdp.data_write(0x0FU);
    vdp.data_write(0xFFU);
    vdp.data_write(0x00U);
    vdp.data_write(0x00U);
    vdp.data_write(0x0FU);
    vdp.data_write(0xFFU);
    vdp.data_write(0x08U);
    vdp.data_write(0x00U);
    vdp.data_write(0x0FU);
    vdp.data_write(0xFFU);
    vdp.data_write(0x10U);
    vdp.data_write(0x00U);
    vdp.data_write(0x0FU);
    vdp.data_write(0xFFU); // fifth visible sprite latches overflow index 4
    vdp.data_write(0x18U);
    vdp.data_write(0x00U);
    vdp.data_write(0x0FU);
    vdp.data_write(0xFFU); // sixth sprite must not replace the first overflow index
    vdp.data_write(0x28U);
    vdp.data_write(0x00U);
    vdp.data_write(0x0FU);
    vdp.data_write(0xD0U);

    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 5, 0x36U);
    write_reg(vdp, 6, 0x00U);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x00FFFFFFU);
    CHECK((vdp.status(0) & 0x40U) != 0U);
    CHECK((vdp.status(0) & 0x1FU) == 4U);
    CHECK((vdp.status(0) & 0x20U) != 0U);

    const std::uint8_t s0 = vdp.status_read();
    CHECK((s0 & 0x60U) == 0x60U);
    CHECK((vdp.status(0) & 0x60U) == 0x60U);
    CHECK((vdp.status(0) & 0x80U) == 0U);
}

TEST_CASE("v9938 R8 SPD disables TMS-compatible sprite display") {
    v9938 vdp;

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x80U); // sprite pattern 0 row 0

    set_addr(vdp, 0x1B00U, true);
    vdp.data_write(0xFFU);
    vdp.data_write(0x00U);
    vdp.data_write(0x00U);
    vdp.data_write(0x0FU);
    vdp.data_write(0xD0U);

    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 5, 0x36U);
    write_reg(vdp, 6, 0x00U);
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x00FFFFFFU);

    write_reg(vdp, 8, 0x02U);
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x00000000U);
}

TEST_CASE("v9938 sprite mode 1 honors upper VRAM sprite attribute base bits") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x80U); // sprite pattern 0 row 0

    set_addr(vdp, 0x11B00U, true);
    vdp.data_write(0xFFU);
    vdp.data_write(0x00U);
    vdp.data_write(0x00U);
    vdp.data_write(0x01U);
    vdp.data_write(0xD0U);

    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 5, 0x36U);
    write_reg(vdp, 6, 0x00U);
    write_reg(vdp, 11, 0x02U); // sprite attribute table base $11B00
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
}

TEST_CASE("v9938 R8 TP controls sprite mode 1 colour 0 visibility and collision") {
    const auto configure_pair = [](v9938& vdp) {
        install_green_red_palette(vdp);

        set_addr(vdp, 0x0000U, true);
        vdp.data_write(0x80U);

        set_addr(vdp, 0x1B00U, true);
        vdp.data_write(0xFFU);
        vdp.data_write(20U);
        vdp.data_write(0U);
        vdp.data_write(0x00U);
        vdp.data_write(0xFFU);
        vdp.data_write(20U);
        vdp.data_write(0U);
        vdp.data_write(0x01U);
        vdp.data_write(0xD0U);

        write_reg(vdp, 1, 0x40U);
        write_reg(vdp, 5, 0x36U);
        write_reg(vdp, 6, 0x00U);
    };

    {
        v9938 vdp;
        configure_pair(vdp);
        vdp.render_frame();

        CHECK(pixel(vdp, 20, 0) == 0x00FF0000U);
        CHECK((vdp.status(0) & 0x20U) == 0U);
    }

    {
        v9938 vdp;
        configure_pair(vdp);
        write_reg(vdp, 8, 0x20U);
        vdp.render_frame();

        CHECK(pixel(vdp, 20, 0) == 0x0000FF00U);
        CHECK((vdp.status(0) & 0x20U) != 0U);
    }
}

TEST_CASE("v9938 renders sprite mode 2 per-line colours") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x80U);
    vdp.data_write(0x80U);

    set_addr(vdp, k_sprite_mode2_sat, true);
    vdp.data_write(0xFFU);
    vdp.data_write(10U);
    vdp.data_write(0U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    set_addr(vdp, k_sprite_mode2_sct, true);
    vdp.data_write(0x01U);
    vdp.data_write(0x02U);

    configure_sprite_mode2(vdp, 0x06U);
    vdp.render_frame();

    CHECK(pixel(vdp, 10, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 10, 1) == 0x000000FFU);
}

TEST_CASE("v9938 sprite mode 2 reports collision coordinates") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x80U);

    set_addr(vdp, k_sprite_mode2_sat, true);
    vdp.data_write(0xFFU);
    vdp.data_write(20U);
    vdp.data_write(0U);
    vdp.data_write(0U);
    vdp.data_write(0xFFU);
    vdp.data_write(20U);
    vdp.data_write(0U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    set_addr(vdp, k_sprite_mode2_sct, true);
    vdp.data_write(0x01U);
    set_addr(vdp, k_sprite_mode2_sct + 16U, true);
    vdp.data_write(0x02U);

    configure_sprite_mode2(vdp, 0x06U);
    vdp.render_frame();

    CHECK(pixel(vdp, 20, 0) == 0x00FF0000U);
    CHECK((vdp.status(0) & 0x20U) != 0U);
    CHECK(vdp.status(3) == 32U);
    CHECK(vdp.status(4) == 0xFEU);
    CHECK(vdp.status(5) == 8U);
    CHECK(vdp.status(6) == 0xFCU);

    write_reg(vdp, 15, 5U);
    CHECK(vdp.status_read() == 8U);
    CHECK(vdp.status(3) == 0U);
    CHECK(vdp.status(4) == 0xFEU);
    CHECK(vdp.status(5) == 0U);
    CHECK(vdp.status(6) == 0xFCU);
}

TEST_CASE("v9938 R8 TP controls sprite mode 2 colour 0 visibility and collision") {
    const auto configure_pair = [](v9938& vdp) {
        install_green_red_palette(vdp);

        set_addr(vdp, 0x0000U, true);
        vdp.data_write(0x80U);

        set_addr(vdp, k_sprite_mode2_sat, true);
        vdp.data_write(0xFFU);
        vdp.data_write(20U);
        vdp.data_write(0U);
        vdp.data_write(0U);
        vdp.data_write(0xFFU);
        vdp.data_write(20U);
        vdp.data_write(0U);
        vdp.data_write(0U);
        vdp.data_write(0xD8U);

        set_addr(vdp, k_sprite_mode2_sct, true);
        vdp.data_write(0x00U);
        set_addr(vdp, k_sprite_mode2_sct + 16U, true);
        vdp.data_write(0x01U);

        configure_sprite_mode2(vdp, 0x06U);
    };

    {
        v9938 vdp;
        configure_pair(vdp);
        vdp.render_frame();

        CHECK(pixel(vdp, 20, 0) == 0x00FF0000U);
        CHECK((vdp.status(0) & 0x20U) == 0U);
    }

    {
        v9938 vdp;
        configure_pair(vdp);
        write_reg(vdp, 8, 0x20U);
        vdp.render_frame();

        CHECK(pixel(vdp, 20, 0) == 0x0000FF00U);
        CHECK((vdp.status(0) & 0x20U) != 0U);
    }
}

TEST_CASE("v9938 sprite mode 2 colour-combine ORs overlapping sprite pens") {
    v9938 vdp;
    install_red_blue_green_palette(vdp);

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x80U);

    set_addr(vdp, k_sprite_mode2_sat, true);
    vdp.data_write(0xFFU);
    vdp.data_write(20U);
    vdp.data_write(0U);
    vdp.data_write(0U);
    vdp.data_write(0xFFU);
    vdp.data_write(20U);
    vdp.data_write(0U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    set_addr(vdp, k_sprite_mode2_sct, true);
    vdp.data_write(0x01U);
    set_addr(vdp, k_sprite_mode2_sct + 16U, true);
    vdp.data_write(0x42U); // CC + palette 2 combines with palette 1 -> palette 3

    configure_sprite_mode2(vdp, 0x06U);
    vdp.render_frame();

    CHECK(pixel(vdp, 20, 0) == 0x0000FF00U);
    CHECK((vdp.status(0) & 0x60U) == 0U);
}

TEST_CASE("v9938 sprite mode 2 ignore-collision preserves priority without latching S0") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x80U);

    set_addr(vdp, k_sprite_mode2_sat, true);
    vdp.data_write(0xFFU);
    vdp.data_write(20U);
    vdp.data_write(0U);
    vdp.data_write(0U);
    vdp.data_write(0xFFU);
    vdp.data_write(20U);
    vdp.data_write(0U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    set_addr(vdp, k_sprite_mode2_sct, true);
    vdp.data_write(0x01U);
    set_addr(vdp, k_sprite_mode2_sct + 16U, true);
    vdp.data_write(0x22U); // IC + palette 2 suppresses collision but stays behind sprite 0

    configure_sprite_mode2(vdp, 0x06U);
    vdp.render_frame();

    CHECK(pixel(vdp, 20, 0) == 0x00FF0000U);
    CHECK((vdp.status(0) & 0x20U) == 0U);
    CHECK(vdp.status(3) == 0U);
    CHECK(vdp.status(4) == 0xFEU);
    CHECK(vdp.status(5) == 0U);
    CHECK(vdp.status(6) == 0xFCU);
}

TEST_CASE("v9938 sprite mode 2 reports ninth sprite overflow") {
    v9938 vdp;

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x80U);

    set_addr(vdp, k_sprite_mode2_sat, true);
    for (std::uint8_t i = 0; i < 9U; ++i) {
        vdp.data_write(0xFFU);
        vdp.data_write(static_cast<std::uint8_t>(i * 8U));
        vdp.data_write(0U);
        vdp.data_write(0U);
    }
    vdp.data_write(0xD8U);

    for (std::uint32_t i = 0; i < 9U; ++i) {
        set_addr(vdp, k_sprite_mode2_sct + i * 16U, true);
        vdp.data_write(0x01U);
    }

    configure_sprite_mode2(vdp, 0x06U);
    vdp.render_frame();

    CHECK((vdp.status(0) & 0x40U) != 0U);
    CHECK((vdp.status(0) & 0x1FU) == 8U);
}

TEST_CASE("v9938 S0 read preserves sprite overflow and collision state") {
    v9938 vdp;

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x80U);

    set_addr(vdp, k_sprite_mode2_sat, true);
    for (std::uint8_t i = 0; i < 9U; ++i) {
        vdp.data_write(0xFFU);
        vdp.data_write(20U);
        vdp.data_write(0U);
        vdp.data_write(0U);
    }
    vdp.data_write(0xD8U);

    for (std::uint32_t i = 0; i < 9U; ++i) {
        set_addr(vdp, k_sprite_mode2_sct + i * 16U, true);
        vdp.data_write(0x01U);
    }

    configure_sprite_mode2(vdp, 0x06U);
    vdp.render_frame();

    CHECK((vdp.status(0) & 0x60U) == 0x60U);
    CHECK((vdp.status(0) & 0x1FU) == 8U);

    const std::uint8_t read_status = vdp.status_read();
    CHECK((read_status & 0x60U) == 0x60U);
    CHECK((read_status & 0x1FU) == 8U);
    CHECK((vdp.status(0) & 0x60U) == 0x60U);
    CHECK((vdp.status(0) & 0x1FU) == 8U);
}

TEST_CASE("v9938 sprite mode 2 respects sprite disable and 512-mode horizontal scaling") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x80U);

    set_addr(vdp, k_sprite_mode2_sat, true);
    vdp.data_write(0xFFU);
    vdp.data_write(10U);
    vdp.data_write(0U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    set_addr(vdp, k_sprite_mode2_sct, true);
    vdp.data_write(0x01U);

    configure_sprite_mode2(vdp, 0x08U);
    write_reg(vdp, 8, 0x02U);
    vdp.render_frame();
    CHECK(pixel(vdp, 10, 0) == 0U);

    write_reg(vdp, 8, 0x00U);
    vdp.render_frame();
    CHECK(pixel(vdp, 10, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 11, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 12, 0) == 0U);
}

TEST_CASE("v9938 renders Graphic 4 SCREEN 5 nibbles through the palette") {
    v9938 vdp;

    install_red_blue_palette(vdp);

    set_addr(vdp, 0x08000U, true);
    vdp.data_write(0x12U);

    write_reg(vdp, 0, 0x06U); // M4 + M3 -> Graphic 4 / SCREEN 5
    write_reg(vdp, 1, 0x40U); // display enable
    write_reg(vdp, 2, 0x20U); // bitmap base $08000
    write_reg(vdp, 9, 0x80U); // 212 visible lines
    vdp.render_frame();

    CHECK(vdp.mode() == v9938::display_mode::graphics_iv);
    CHECK(vdp.framebuffer().width == 256U);
    CHECK(vdp.framebuffer().height == 212U);
    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 0) == 0x000000FFU);
}

TEST_CASE("v9938 colour code 0 resolves to backdrop unless R8 disables transparency") {
    v9938 vdp;

    write_reg(vdp, 16, 0U);
    vdp.palette_write(0x00U);
    vdp.palette_write(0x07U); // palette 0: green
    vdp.palette_write(0x70U);
    vdp.palette_write(0x00U); // palette 1: red
    vdp.palette_write(0x07U);
    vdp.palette_write(0x00U); // palette 2: blue backdrop

    set_addr(vdp, 0x08000U, true);
    vdp.data_write(0x01U);

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 2, 0x20U);
    write_reg(vdp, 7, 0x02U);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 1, 0) == 0x00FF0000U);

    write_reg(vdp, 8, 0x20U);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x0000FF00U);
    CHECK(pixel(vdp, 1, 0) == 0x00FF0000U);
}

TEST_CASE("v9938 R8 BW converts paletted output to 32-tone monochrome") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x08000U, true);
    vdp.data_write(0x12U);

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 2, 0x20U);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 0) == 0x000000FFU);

    write_reg(vdp, 8, 0x01U);
    vdp.render_frame();

    const std::uint32_t red_bw = pixel(vdp, 0, 0);
    const std::uint32_t blue_bw = pixel(vdp, 1, 0);
    CHECK(red_bw == bw32_from_rgb(0x00FF0000U));
    CHECK(blue_bw == bw32_from_rgb(0x000000FFU));
    CHECK(is_monochrome(red_bw));
    CHECK(is_monochrome(blue_bw));
    CHECK(red_bw != 0x00FF0000U);
    CHECK(blue_bw != 0x000000FFU);
}

TEST_CASE("v9938 R18 shifts the active display with backdrop fill") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x00000U, true);
    vdp.data_write(0x12U);
    set_addr(vdp, 128U, true);
    vdp.data_write(0x21U);

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);

    write_reg(vdp, 18, 0x01U); // H=1 moves the picture left.
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 255, 0) == 0x00000000U);

    write_reg(vdp, 18, 0x0FU); // H=15 moves the picture right by one pixel.
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x00000000U);
    CHECK(pixel(vdp, 1, 0) == 0x00FF0000U);

    write_reg(vdp, 18, 0x10U); // V=1 moves the picture up.
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 1, 0) == 0x00FF0000U);

    write_reg(vdp, 18, 0xF0U); // V=15 moves the picture down by one line.
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x00000000U);
    CHECK(pixel(vdp, 0, 1) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 1) == 0x000000FFU);
}

TEST_CASE("v9938 R9 IL maps paired framebuffer lines to one Graphic 4 field line") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0U, true);
    vdp.data_write(0x12U);
    set_addr(vdp, 128U, true);
    vdp.data_write(0x21U);

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 9, 0x08U);
    vdp.render_frame();

    CHECK(vdp.framebuffer().height == static_cast<std::uint32_t>(v9938::display_height_384));
    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 0, 1) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 1) == 0x000000FFU);
    CHECK(pixel(vdp, 0, 2) == 0x000000FFU);
    CHECK(pixel(vdp, 1, 2) == 0x00FF0000U);
}

TEST_CASE("v9938 alternates Graphic 4 even and odd pages with R13 timing") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x00000U, true);
    vdp.data_write(0x12U); // even page
    set_addr(vdp, 0x08000U, true);
    vdp.data_write(0x21U); // odd page

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 2, 0x20U); // select odd page 1
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 1, 0) == 0x00FF0000U);

    write_reg(vdp, 13, 0x11U); // one sixth second even, one sixth second odd
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 0) == 0x000000FFU);

    advance_frames(vdp, 10U);
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 1, 0) == 0x00FF0000U);
}

TEST_CASE("v9938 R9 IL and EO interleave Graphic 4 pages by display field") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x00000U, true);
    vdp.data_write(0x12U); // even field page
    set_addr(vdp, 0x08000U, true);
    vdp.data_write(0x21U); // odd field page

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 2, 0x20U);
    write_reg(vdp, 9, 0x0CU);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 0, 1) == 0x000000FFU);
    CHECK(pixel(vdp, 1, 1) == 0x00FF0000U);
}

TEST_CASE("v9938 R9 EO alternates Graphic 4 pages every display field") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x00000U, true);
    vdp.data_write(0x12U); // even page
    set_addr(vdp, 0x08000U, true);
    vdp.data_write(0x21U); // odd page

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 2, 0x20U); // select odd page 1
    write_reg(vdp, 9, 0x04U); // EO: even page on first field, odd page on second field
    write_reg(vdp, 15, 2U);

    vdp.render_frame();
    CHECK((vdp.status_read() & 0x02U) == 0U);
    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 0) == 0x000000FFU);

    advance_frames(vdp, 1U);
    vdp.render_frame();
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x02U) != 0U);
    CHECK(pixel(vdp, 0, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 1, 0) == 0x00FF0000U);

    advance_frames(vdp, 1U);
    vdp.render_frame();
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x02U) == 0U);
    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 0) == 0x000000FFU);
}

TEST_CASE("v9938 R23 vertically scrolls Graphic 4 through the 256-line page") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 2U * 128U, true);
    vdp.data_write(0x12U);
    set_addr(vdp, 255U * 128U, true);
    vdp.data_write(0x21U);

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 23, 2U);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 0) == 0x000000FFU);

    write_reg(vdp, 23, 255U);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x000000FFU);
    CHECK(pixel(vdp, 1, 0) == 0x00FF0000U);
}

TEST_CASE("v9938 R23 scrolls sprite mode 2 with the bitmap source line") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    set_addr(vdp, 0x0000U, true);
    vdp.data_write(0x80U);

    set_addr(vdp, k_sprite_mode2_sat, true);
    vdp.data_write(3U); // visible Y = 4
    vdp.data_write(10U);
    vdp.data_write(0U);
    vdp.data_write(0U);
    vdp.data_write(0xD8U);

    set_addr(vdp, k_sprite_mode2_sct, true);
    vdp.data_write(0x01U);

    configure_sprite_mode2(vdp, 0x06U);
    write_reg(vdp, 23, 4U);
    vdp.render_frame();

    CHECK(pixel(vdp, 10, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 10, 4) == 0U);
}

TEST_CASE("v9938 renders Graphic 5 SCREEN 6 packed pairs through the palette") {
    v9938 vdp;

    install_red_blue_palette(vdp);

    set_addr(vdp, 0x08000U, true);
    vdp.data_write(0x1BU); // colours 0, 1, 2, 3

    write_reg(vdp, 0, 0x08U); // M5 -> Graphic 5 / SCREEN 6
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 2, 0x20U); // bitmap base $08000
    vdp.render_frame();

    CHECK(vdp.mode() == v9938::display_mode::graphics_v);
    CHECK(vdp.framebuffer().width == 512U);
    CHECK(pixel(vdp, 1, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 2, 0) == 0x000000FFU);
}

TEST_CASE("v9938 renders Graphic 6 SCREEN 7 nibbles through the high page") {
    v9938 vdp;

    install_red_blue_palette(vdp);

    set_addr(vdp, 0x10000U, true);
    vdp.data_write(0x12U);

    write_reg(vdp, 0, 0x0AU); // M5 + M3 -> Graphic 6 / SCREEN 7
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 2, 0x40U); // bitmap base $10000
    vdp.render_frame();

    CHECK(vdp.mode() == v9938::display_mode::graphics_vi);
    CHECK(vdp.framebuffer().width == 512U);
    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 0) == 0x000000FFU);
}

TEST_CASE("v9938 renders Graphic 7 SCREEN 8 bytes through the high page") {
    v9938 vdp;

    set_addr(vdp, 0x10000U, true);
    vdp.data_write(0x1CU);

    write_reg(vdp, 0, 0x0EU); // M5 + M4 + M3 -> Graphic 7 / SCREEN 8
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 2, 0x40U); // bitmap base $10000
    vdp.render_frame();

    CHECK(vdp.mode() == v9938::display_mode::graphics_vii);
    CHECK(vdp.framebuffer().width == 256U);
    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
}

TEST_CASE("v9938 alternates Graphic 7 even and odd 64 KiB pages with R13 timing") {
    v9938 vdp;

    set_addr(vdp, 0x00000U, true);
    vdp.data_write(0x1CU); // red
    set_addr(vdp, 0x10000U, true);
    vdp.data_write(0xE0U); // green

    write_reg(vdp, 0, 0x0EU);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 2, 0x40U); // select odd page 1
    write_reg(vdp, 13, 0x11U);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);

    advance_frames(vdp, 10U);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x0000FF00U);
}

TEST_CASE("v9938 R9 EO alternates Graphic 7 64 KiB pages every display field") {
    v9938 vdp;

    set_addr(vdp, 0x00000U, true);
    vdp.data_write(0x1CU); // even page: red
    set_addr(vdp, 0x10000U, true);
    vdp.data_write(0xE0U); // odd page: green

    write_reg(vdp, 0, 0x0EU);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 2, 0x40U); // select odd page 1
    write_reg(vdp, 9, 0x04U);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);

    advance_frames(vdp, 1U);
    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x0000FF00U);
}

TEST_CASE("v9938 screen off uses the 8-bit R7 backdrop in Graphic 7") {
    v9938 vdp;

    write_reg(vdp, 0, 0x0EU);
    write_reg(vdp, 1, 0x00U);
    write_reg(vdp, 7, 0x1CU);
    vdp.render_frame();

    CHECK(vdp.mode() == v9938::display_mode::graphics_vii);
    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
}

TEST_CASE("v9938 R8 BW converts Graphic 7 fixed-colour output and backdrop") {
    v9938 vdp;

    set_addr(vdp, 0x00000U, true);
    vdp.data_write(0x1CU); // red
    vdp.data_write(0xE0U); // green

    write_reg(vdp, 0, 0x0EU);
    write_reg(vdp, 1, 0x40U);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U);
    CHECK(pixel(vdp, 1, 0) == 0x0000FF00U);

    write_reg(vdp, 8, 0x01U);
    vdp.render_frame();

    const std::uint32_t red_bw = pixel(vdp, 0, 0);
    const std::uint32_t green_bw = pixel(vdp, 1, 0);
    CHECK(red_bw == bw32_from_rgb(0x00FF0000U));
    CHECK(green_bw == bw32_from_rgb(0x0000FF00U));
    CHECK(is_monochrome(red_bw));
    CHECK(is_monochrome(green_bw));

    write_reg(vdp, 1, 0x00U);
    write_reg(vdp, 7, 0x1CU);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == bw32_from_rgb(0x00FF0000U));
}

TEST_CASE("v9938 HMMV command fills Graphic 4 rectangles and clears CE after command time") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 36, 4U);    // DX
    write_reg(vdp, 38, 3U);    // DY
    write_reg(vdp, 40, 4U);    // NX
    write_reg(vdp, 42, 2U);    // NY
    write_reg(vdp, 44, 0x12U); // CLR
    write_reg(vdp, 46, 0xC0U); // HMMV

    CHECK(vdp.vram()[3U * 128U + 2U] == 0x12U);
    CHECK(vdp.vram()[4U * 128U + 2U] == 0x12U);

    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x91U) == 0x01U);

    vdp.tick(128U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x91U) == 0x01U);

    vdp.tick(240U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x91U) == 0U);

    vdp.render_frame();
    CHECK(pixel(vdp, 4, 3) == 0x00FF0000U);
    CHECK(pixel(vdp, 5, 3) == 0x000000FFU);
}

TEST_CASE("v9938 command timing follows screen and sprite access slots") {
    const auto start_hmmv = [](v9938& vdp, std::uint8_t r1, std::uint8_t r8) {
        write_reg(vdp, 0, 0x06U);
        write_reg(vdp, 1, r1);
        write_reg(vdp, 8, r8);
        write_reg(vdp, 36, 4U);
        write_reg(vdp, 38, 3U);
        write_reg(vdp, 40, 4U);
        write_reg(vdp, 42, 1U);
        write_reg(vdp, 44, 0x12U);
        write_reg(vdp, 46, 0xC0U);
    };

    v9938 screen_off;
    start_hmmv(screen_off, 0x00U, 0x00U);
    screen_off.tick(32U);
    write_reg(screen_off, 15, 2U);
    CHECK((screen_off.status_read() & 0x01U) == 0U);

    v9938 sprites_off;
    start_hmmv(sprites_off, 0x40U, 0x02U);
    sprites_off.tick(32U);
    write_reg(sprites_off, 15, 2U);
    CHECK((sprites_off.status_read() & 0x01U) != 0U);
    sprites_off.tick(24U);
    write_reg(sprites_off, 15, 2U);
    CHECK((sprites_off.status_read() & 0x01U) == 0U);

    v9938 sprites_on;
    start_hmmv(sprites_on, 0x40U, 0x00U);
    sprites_on.tick(56U);
    write_reg(sprites_on, 15, 2U);
    CHECK((sprites_on.status_read() & 0x01U) != 0U);
    sprites_on.tick(103U);
    write_reg(sprites_on, 15, 2U);
    CHECK((sprites_on.status_read() & 0x01U) == 0U);
}

TEST_CASE("v9938 command completion updates documented register end state") {
    {
        v9938 vdp;

        write_reg(vdp, 0, 0x06U);
        write_reg(vdp, 1, 0x40U);
        write_reg(vdp, 36, 4U);    // DX
        write_reg(vdp, 38, 0xFEU); // DY = 1022
        write_reg(vdp, 39, 0x03U);
        write_reg(vdp, 40, 2U); // NX
        write_reg(vdp, 42, 4U); // NY
        write_reg(vdp, 44, 0x12U);
        write_reg(vdp, 46, 0xC0U); // HMMV

        CHECK(vdp.reg(38) == 0U);
        CHECK((vdp.reg(39) & 0x03U) == 0U);
        CHECK(vdp.reg(42) == 2U);
        CHECK((vdp.reg(43) & 0x03U) == 0U);
        CHECK((vdp.reg(46) & 0xF0U) == 0U);
    }

    {
        v9938 vdp;

        write_reg(vdp, 0, 0x06U);
        write_reg(vdp, 1, 0x40U);
        write_reg(vdp, 34, 5U);    // SY
        write_reg(vdp, 36, 0U);    // DX
        write_reg(vdp, 38, 7U);    // DY
        write_reg(vdp, 42, 3U);    // NY
        write_reg(vdp, 46, 0xE0U); // YMMM

        CHECK(vdp.reg(34) == 8U);
        CHECK(vdp.reg(38) == 10U);
        CHECK(vdp.reg(42) == 0U);
        CHECK((vdp.reg(46) & 0xF0U) == 0U);
    }

    {
        v9938 vdp;

        write_reg(vdp, 0, 0x06U);
        write_reg(vdp, 1, 0x40U);
        set_addr(vdp, 4U * 128U + 3U, true);
        vdp.data_write(0x0FU);
        write_reg(vdp, 32, 7U); // SX
        write_reg(vdp, 34, 4U); // SY
        write_reg(vdp, 44, 0U);
        write_reg(vdp, 46, 0x40U); // POINT

        CHECK(vdp.status(7) == 0x0FU);
        CHECK(vdp.reg(44) == 0x0FU);
        CHECK((vdp.reg(46) & 0xF0U) == 0U);
    }

    {
        v9938 vdp;

        write_reg(vdp, 0, 0x06U);
        write_reg(vdp, 1, 0x40U);
        write_reg(vdp, 36, 1U); // DX
        write_reg(vdp, 38, 1U); // DY
        write_reg(vdp, 40, 2U); // MAJ
        write_reg(vdp, 42, 2U); // MIN
        write_reg(vdp, 44, 0x0AU);
        write_reg(vdp, 45, 0x01U); // major axis is Y
        write_reg(vdp, 46, 0x70U); // LINE

        CHECK(vdp.reg(36) == 1U);
        CHECK((vdp.reg(37) & 0x01U) == 0U);
        CHECK(vdp.reg(38) == 4U);
        CHECK((vdp.reg(39) & 0x03U) == 0U);
        CHECK((vdp.reg(46) & 0xF0U) == 0U);
    }

    {
        v9938 vdp;

        write_reg(vdp, 0, 0x06U);
        write_reg(vdp, 1, 0x40U);
        write_reg(vdp, 36, 1U); // DX
        write_reg(vdp, 38, 1U); // DY
        write_reg(vdp, 40, 3U); // MAJ
        write_reg(vdp, 42, 2U); // MIN
        write_reg(vdp, 44, 0x0AU);
        write_reg(vdp, 45, 0x00U); // major axis is X
        write_reg(vdp, 46, 0x70U); // LINE

        CHECK(vdp.reg(36) == 1U);
        CHECK((vdp.reg(37) & 0x01U) == 0U);
        CHECK(vdp.reg(38) == 3U);
        CHECK((vdp.reg(39) & 0x03U) == 0U);
        CHECK((vdp.reg(46) & 0xF0U) == 0U);
    }
}

TEST_CASE("v9938 S2 exposes fixed bits and retrace timing flags") {
    v9938 vdp;

    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x6CU) == 0x0CU);

    vdp.tick(v9938::cycles_per_line - 32U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x20U) != 0U);

    vdp.tick(32U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x20U) == 0U);

    v9938 vblank_vdp;
    vblank_vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) *
                    static_cast<std::uint64_t>(v9938::display_height_192));
    write_reg(vblank_vdp, 15, 2U);
    CHECK((vblank_vdp.status_read() & 0x40U) != 0U);

    vblank_vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) *
                    static_cast<std::uint64_t>(v9938::scanlines_ntsc - v9938::display_height_192));
    write_reg(vblank_vdp, 15, 2U);
    CHECK((vblank_vdp.status_read() & 0x40U) == 0U);
}

TEST_CASE("v9938 coordinate status registers expose documented fixed bits") {
    v9938 vdp;

    CHECK(vdp.status(4) == 0xFEU);
    CHECK(vdp.status(6) == 0xFCU);
    CHECK(vdp.status(9) == 0xFEU);

    write_reg(vdp, 15, 4U);
    CHECK(vdp.status_read() == 0xFEU);
    write_reg(vdp, 15, 6U);
    CHECK(vdp.status_read() == 0xFCU);
    write_reg(vdp, 15, 9U);
    CHECK(vdp.status_read() == 0xFEU);

    advance_frames(vdp, 1U);
    CHECK(vdp.status(6) == 0xFEU);
    write_reg(vdp, 15, 6U);
    CHECK(vdp.status_read() == 0xFEU);
}

TEST_CASE("v9938 HMMV command fills Graphic 5 packed pixels") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    write_reg(vdp, 0, 0x08U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 36, 5U);    // DX low bits ignored by high-speed commands
    write_reg(vdp, 38, 3U);    // DY
    write_reg(vdp, 40, 5U);    // NX low bits ignored by high-speed commands
    write_reg(vdp, 42, 1U);    // NY
    write_reg(vdp, 44, 0x1BU); // CLR
    write_reg(vdp, 46, 0xC0U); // HMMV

    CHECK(vdp.vram()[3U * 128U + 1U] == 0x1BU);
    CHECK(vdp.vram()[3U * 128U + 2U] == 0x00U);

    vdp.render_frame();
    CHECK(pixel(vdp, 5, 3) == 0x00FF0000U);
    CHECK(pixel(vdp, 6, 3) == 0x000000FFU);
}

TEST_CASE("v9938 HMMV command fills Graphic 7 bytes") {
    v9938 vdp;

    write_reg(vdp, 0, 0x0EU);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 36, 2U);    // DX
    write_reg(vdp, 38, 3U);    // DY
    write_reg(vdp, 40, 2U);    // NX
    write_reg(vdp, 42, 1U);    // NY
    write_reg(vdp, 44, 0xE3U); // CLR
    write_reg(vdp, 46, 0xC0U); // HMMV

    CHECK(vdp.mode() == v9938::display_mode::graphics_vii);
    CHECK(vdp.vram()[3U * 256U + 2U] == 0xE3U);
    CHECK(vdp.vram()[3U * 256U + 3U] == 0xE3U);
}

TEST_CASE("v9938 HMMM command copies Graphic 4 source pixels") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    set_addr(vdp, 5U * 128U + 4U, true);
    vdp.data_write(0x34U);

    write_reg(vdp, 32, 8U);  // SX
    write_reg(vdp, 34, 5U);  // SY
    write_reg(vdp, 36, 10U); // DX
    write_reg(vdp, 38, 6U);  // DY
    write_reg(vdp, 40, 2U);  // NX
    write_reg(vdp, 42, 1U);  // NY
    write_reg(vdp, 46, 0xD0U);

    CHECK(vdp.vram()[6U * 128U + 5U] == 0x34U);
}

TEST_CASE("v9938 HMMM command copies Graphic 5 source pixels") {
    v9938 vdp;

    write_reg(vdp, 0, 0x08U);
    write_reg(vdp, 1, 0x40U);
    set_addr(vdp, 4U * 128U + 1U, true);
    vdp.data_write(0x1BU);

    write_reg(vdp, 32, 4U); // SX
    write_reg(vdp, 34, 4U); // SY
    write_reg(vdp, 36, 8U); // DX
    write_reg(vdp, 38, 5U); // DY
    write_reg(vdp, 40, 4U); // NX
    write_reg(vdp, 42, 1U); // NY
    write_reg(vdp, 46, 0xD0U);

    CHECK(vdp.vram()[5U * 128U + 2U] == 0x1BU);
}

TEST_CASE("v9938 HMMM command copies overlapping Graphic 4 bytes in reverse X direction") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    set_addr(vdp, 0U, true);
    vdp.data_write(0x12U); // pixels 0,1
    vdp.data_write(0x34U); // pixels 2,3
    vdp.data_write(0x00U); // destination pixels 4,5

    write_reg(vdp, 32, 2U);    // SX starts at the right source byte
    write_reg(vdp, 34, 0U);    // SY
    write_reg(vdp, 36, 4U);    // DX starts at the right destination byte
    write_reg(vdp, 38, 0U);    // DY
    write_reg(vdp, 40, 4U);    // NX covers two Graphic 4 high-speed bytes
    write_reg(vdp, 42, 1U);    // NY
    write_reg(vdp, 45, 0x04U); // DIX = left
    write_reg(vdp, 46, 0xD0U); // HMMM

    CHECK(vdp.vram()[0] == 0x12U);
    CHECK(vdp.vram()[1] == 0x12U);
    CHECK(vdp.vram()[2] == 0x34U);
}

TEST_CASE("v9938 R45 selects expansion RAM for command source and destination") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);

    write_reg(vdp, 45, 0x40U); // HMMV destination is expansion RAM.
    write_reg(vdp, 36, 4U);    // DX
    write_reg(vdp, 38, 3U);    // DY
    write_reg(vdp, 40, 2U);    // NX, one Graphic 4 byte
    write_reg(vdp, 42, 1U);    // NY
    write_reg(vdp, 44, 0x34U);
    write_reg(vdp, 46, 0xC0U); // HMMV

    CHECK(vdp.vram()[3U * 128U + 2U] == 0U);
    set_addr(vdp, 3U * 128U + 2U, false);
    CHECK(vdp.data_read() == 0x34U);

    write_reg(vdp, 45, 0x20U); // HMMM source is expansion RAM, destination is VRAM.
    write_reg(vdp, 32, 4U);    // SX
    write_reg(vdp, 34, 3U);    // SY
    write_reg(vdp, 36, 8U);    // DX
    write_reg(vdp, 38, 4U);    // DY
    write_reg(vdp, 40, 2U);    // NX
    write_reg(vdp, 42, 1U);    // NY
    write_reg(vdp, 46, 0xD0U); // HMMM

    CHECK(vdp.vram()[4U * 128U + 4U] == 0x34U);
}

TEST_CASE("v9938 HMMC command streams CPU bytes through R44") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 36, 4U);    // DX
    write_reg(vdp, 38, 3U);    // DY
    write_reg(vdp, 40, 4U);    // NX
    write_reg(vdp, 42, 1U);    // NY
    write_reg(vdp, 44, 0x12U); // first CPU transfer byte
    write_reg(vdp, 46, 0xF0U); // HMMC

    CHECK(vdp.vram()[3U * 128U + 2U] == 0x12U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0x01U);

    write_reg(vdp, 44, 0x34U);
    CHECK(vdp.vram()[3U * 128U + 3U] == 0U);

    vdp.tick(16U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0x01U);

    vdp.tick(24U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0x81U);

    write_reg(vdp, 44, 0x34U);
    CHECK(vdp.vram()[3U * 128U + 3U] == 0x34U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0U);
}

TEST_CASE("v9938 STOP cancels an active CPU transfer command") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 36, 4U);
    write_reg(vdp, 38, 3U);
    write_reg(vdp, 40, 4U);
    write_reg(vdp, 42, 1U);
    write_reg(vdp, 44, 0x12U);
    write_reg(vdp, 46, 0xF0U);
    CHECK(vdp.vram()[3U * 128U + 2U] == 0x12U);

    write_reg(vdp, 46, 0x00U);

    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0U);
    write_reg(vdp, 44, 0x34U);
    CHECK(vdp.vram()[3U * 128U + 3U] == 0U);
}

TEST_CASE("v9938 LMMC command streams exact Graphic 5 pixels") {
    v9938 vdp;

    write_reg(vdp, 0, 0x08U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 36, 5U);    // DX
    write_reg(vdp, 38, 2U);    // DY
    write_reg(vdp, 40, 2U);    // NX
    write_reg(vdp, 42, 1U);    // NY
    write_reg(vdp, 44, 0x01U); // first CPU transfer pixel
    write_reg(vdp, 46, 0xB2U); // LMMC OR
    CHECK(vdp.vram()[2U * 128U + 1U] == 0x10U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0x01U);

    write_reg(vdp, 44, 0x02U);
    CHECK(vdp.vram()[2U * 128U + 1U] == 0x10U);

    vdp.tick(24U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0x01U);

    vdp.tick(56U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0x81U);

    write_reg(vdp, 44, 0x02U);
    CHECK(vdp.vram()[2U * 128U + 1U] == 0x18U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0U);
}

TEST_CASE("v9938 LMCM command streams VRAM pixels through S7") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    set_addr(vdp, 2U * 128U + 2U, true);
    vdp.data_write(0xABU);

    write_reg(vdp, 32, 4U); // SX
    write_reg(vdp, 34, 2U); // SY
    write_reg(vdp, 40, 2U); // NX
    write_reg(vdp, 42, 1U); // NY
    write_reg(vdp, 46, 0xA0U);

    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0x01U);
    write_reg(vdp, 15, 7U);
    CHECK(vdp.status_read() == 0U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0x01U);
    write_reg(vdp, 15, 7U);
    CHECK(vdp.status_read() == 0U);

    vdp.tick(16U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0x01U);
    write_reg(vdp, 15, 7U);
    CHECK(vdp.status_read() == 0U);

    vdp.tick(24U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0x81U);
    write_reg(vdp, 15, 7U);
    CHECK(vdp.status_read() == 0x0AU);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0x01U);

    vdp.tick(40U);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0x81U);
    write_reg(vdp, 15, 7U);
    CHECK(vdp.status_read() == 0x0BU);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0U);
}

TEST_CASE("v9938 CPU transfer commands update register end state on completion") {
    {
        v9938 vdp;

        write_reg(vdp, 0, 0x06U);
        write_reg(vdp, 1, 0x40U);
        write_reg(vdp, 36, 4U);    // DX
        write_reg(vdp, 38, 3U);    // DY
        write_reg(vdp, 40, 4U);    // NX
        write_reg(vdp, 42, 1U);    // NY
        write_reg(vdp, 44, 0x12U); // first CPU transfer byte
        write_reg(vdp, 46, 0xF0U); // HMMC

        vdp.tick(40U);
        write_reg(vdp, 44, 0x34U);

        CHECK(vdp.reg(38) == 4U);
        CHECK(vdp.reg(42) == 0U);
        CHECK((vdp.reg(46) & 0xF0U) == 0U);
        write_reg(vdp, 15, 2U);
        CHECK((vdp.status_read() & 0x81U) == 0U);
    }

    {
        v9938 vdp;

        write_reg(vdp, 0, 0x06U);
        write_reg(vdp, 1, 0x40U);
        set_addr(vdp, 2U * 128U + 2U, true);
        vdp.data_write(0xABU);

        write_reg(vdp, 32, 4U);    // SX
        write_reg(vdp, 34, 2U);    // SY
        write_reg(vdp, 40, 2U);    // NX
        write_reg(vdp, 42, 1U);    // NY
        write_reg(vdp, 46, 0xA0U); // LMCM

        write_reg(vdp, 15, 7U);
        CHECK(vdp.status_read() == 0U);
        vdp.tick(40U);
        write_reg(vdp, 15, 7U);
        CHECK(vdp.status_read() == 0x0AU);
        vdp.tick(40U);
        write_reg(vdp, 15, 7U);
        CHECK(vdp.status_read() == 0x0BU);

        CHECK(vdp.reg(34) == 3U);
        CHECK(vdp.reg(42) == 0U);
        CHECK(vdp.reg(44) == 0x0BU);
        CHECK((vdp.reg(46) & 0xF0U) == 0U);
        write_reg(vdp, 15, 2U);
        CHECK((vdp.status_read() & 0x81U) == 0U);
    }
}

TEST_CASE("v9938 preserves active CPU transfer command state across snapshots") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 36, 4U);
    write_reg(vdp, 38, 3U);
    write_reg(vdp, 40, 4U);
    write_reg(vdp, 42, 1U);
    write_reg(vdp, 44, 0x12U); // first CPU transfer byte
    write_reg(vdp, 46, 0xF0U);
    CHECK(vdp.vram()[3U * 128U + 2U] == 0x12U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    vdp.save_state(writer);

    v9938 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    write_reg(restored, 15, 2U);
    CHECK((restored.status_read() & 0x81U) == 0x01U);
    CHECK(restored.vram()[3U * 128U + 2U] == 0x12U);
    write_reg(restored, 44, 0x34U);
    CHECK(restored.vram()[3U * 128U + 3U] == 0U);

    restored.tick(40U);
    write_reg(restored, 15, 2U);
    CHECK((restored.status_read() & 0x81U) == 0x81U);
    write_reg(restored, 44, 0x34U);
    CHECK(restored.vram()[3U * 128U + 3U] == 0x34U);
    write_reg(restored, 15, 2U);
    CHECK((restored.status_read() & 0x81U) == 0U);
}

TEST_CASE("v9938 preserves active LMCM fetch delay across snapshots") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    set_addr(vdp, 2U * 128U + 2U, true);
    vdp.data_write(0xABU);

    write_reg(vdp, 32, 4U);
    write_reg(vdp, 34, 2U);
    write_reg(vdp, 40, 2U);
    write_reg(vdp, 42, 1U);
    write_reg(vdp, 46, 0xA0U);

    vdp.tick(16U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    vdp.save_state(writer);

    v9938 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    write_reg(restored, 15, 2U);
    CHECK((restored.status_read() & 0x81U) == 0x01U);
    write_reg(restored, 15, 7U);
    CHECK(restored.status_read() == 0U);

    restored.tick(24U);
    write_reg(restored, 15, 2U);
    CHECK((restored.status_read() & 0x81U) == 0x81U);
    write_reg(restored, 15, 7U);
    CHECK(restored.status_read() == 0x0AU);

    restored.tick(40U);
    write_reg(restored, 15, 7U);
    CHECK(restored.status_read() == 0x0BU);
    write_reg(restored, 15, 2U);
    CHECK((restored.status_read() & 0x81U) == 0U);
}

TEST_CASE("v9938 preserves expansion RAM and banked CPU transfer state across snapshots") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 45, 0x40U); // HMMC destination is expansion RAM.
    write_reg(vdp, 36, 4U);
    write_reg(vdp, 38, 3U);
    write_reg(vdp, 40, 4U);
    write_reg(vdp, 42, 1U);
    write_reg(vdp, 44, 0x12U); // first CPU transfer byte
    write_reg(vdp, 46, 0xF0U);
    CHECK(vdp.vram()[3U * 128U + 2U] == 0U);

    set_addr(vdp, 3U * 128U + 2U, false);
    CHECK(vdp.data_read() == 0x12U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    vdp.save_state(writer);

    v9938 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.vram()[3U * 128U + 2U] == 0U);
    set_addr(restored, 3U * 128U + 2U, false);
    CHECK(restored.data_read() == 0x12U);

    restored.tick(40U);
    write_reg(restored, 44, 0x34U);

    CHECK(restored.vram()[3U * 128U + 3U] == 0U);
    set_addr(restored, 3U * 128U + 3U, false);
    CHECK(restored.data_read() == 0x34U);
}

TEST_CASE("v9938 YMMM command copies vertically to the screen edge") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    set_addr(vdp, 1U * 128U, true);
    vdp.data_write(0x56U);

    write_reg(vdp, 34, 1U); // SY
    write_reg(vdp, 36, 0U); // DX
    write_reg(vdp, 38, 2U); // DY
    write_reg(vdp, 42, 1U); // NY
    write_reg(vdp, 46, 0xE0U);

    CHECK(vdp.vram()[2U * 128U] == 0x56U);
}

TEST_CASE("v9938 LMMV command fills unaligned Graphic 5 pixels") {
    v9938 vdp;
    install_red_blue_palette(vdp);

    write_reg(vdp, 0, 0x08U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 36, 5U);    // DX
    write_reg(vdp, 38, 2U);    // DY
    write_reg(vdp, 40, 3U);    // NX
    write_reg(vdp, 42, 1U);    // NY
    write_reg(vdp, 44, 0x02U); // CLR
    write_reg(vdp, 46, 0x80U); // LMMV IMP

    CHECK(vdp.vram()[2U * 128U + 1U] == 0x2AU);

    vdp.render_frame();
    CHECK(pixel(vdp, 5, 2) == 0x000000FFU);
    CHECK(pixel(vdp, 6, 2) == 0x000000FFU);
    CHECK(pixel(vdp, 7, 2) == 0x000000FFU);
}

TEST_CASE("v9938 LMMM command copies Graphic 6 pixels with logical OR") {
    v9938 vdp;

    write_reg(vdp, 0, 0x0AU);
    write_reg(vdp, 1, 0x40U);

    set_addr(vdp, 1U * 256U + 1U, true);
    vdp.data_write(0x0BU);
    set_addr(vdp, 2U * 256U + 2U, true);
    vdp.data_write(0x04U);

    write_reg(vdp, 32, 3U); // SX
    write_reg(vdp, 34, 1U); // SY
    write_reg(vdp, 36, 5U); // DX
    write_reg(vdp, 38, 2U); // DY
    write_reg(vdp, 40, 1U); // NX
    write_reg(vdp, 42, 1U); // NY
    write_reg(vdp, 46, 0x92U);

    CHECK((vdp.vram()[2U * 256U + 2U] & 0x0FU) == 0x0FU);
}

TEST_CASE("v9938 LMMM command copies overlapping Graphic 4 pixels in reverse X direction") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    set_addr(vdp, 0U, true);
    vdp.data_write(0x12U); // pixels 0,1
    vdp.data_write(0x30U); // pixel 2, destination pixel 3

    write_reg(vdp, 32, 2U);    // SX starts at the right edge of the source span
    write_reg(vdp, 34, 0U);    // SY
    write_reg(vdp, 36, 3U);    // DX starts at the right edge of the destination span
    write_reg(vdp, 38, 0U);    // DY
    write_reg(vdp, 40, 3U);    // NX
    write_reg(vdp, 42, 1U);    // NY
    write_reg(vdp, 45, 0x04U); // DIX = left
    write_reg(vdp, 46, 0x90U); // LMMM IMP

    CHECK(vdp.vram()[0] == 0x11U);
    CHECK(vdp.vram()[1] == 0x23U);
}

TEST_CASE("v9938 PSET and POINT commands access Graphic 4 pixels") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 36, 7U);    // DX
    write_reg(vdp, 38, 4U);    // DY
    write_reg(vdp, 44, 0x0AU); // CLR
    write_reg(vdp, 46, 0x50U); // PSET IMP

    CHECK((vdp.vram()[4U * 128U + 3U] & 0x0FU) == 0x0AU);

    write_reg(vdp, 44, 0x05U);
    write_reg(vdp, 46, 0x52U); // PSET OR
    CHECK((vdp.vram()[4U * 128U + 3U] & 0x0FU) == 0x0FU);

    write_reg(vdp, 32, 7U); // SX
    write_reg(vdp, 34, 4U); // SY
    write_reg(vdp, 46, 0x40U);
    write_reg(vdp, 15, 7U);
    CHECK(vdp.status_read() == 0x0FU);
}

TEST_CASE("v9938 logical pixel commands apply transparent and non-transparent ops") {
    const auto pset_low_nibble = [](std::uint8_t dest, std::uint8_t source,
                                    std::uint8_t op) -> std::uint8_t {
        v9938 vdp;

        write_reg(vdp, 0, 0x06U);
        write_reg(vdp, 1, 0x40U);
        set_addr(vdp, 0U, true);
        vdp.data_write(static_cast<std::uint8_t>(0xC0U | (dest & 0x0FU)));

        write_reg(vdp, 36, 1U); // low nibble of byte 0
        write_reg(vdp, 38, 0U);
        write_reg(vdp, 44, source);
        write_reg(vdp, 46, static_cast<std::uint8_t>(0x50U | (op & 0x0FU)));

        const std::uint8_t packed = vdp.vram()[0];
        CHECK((packed & 0xF0U) == 0xC0U);
        return static_cast<std::uint8_t>(packed & 0x0FU);
    };

    CHECK(pset_low_nibble(0x06U, 0x03U, 0x00U) == 0x03U); // IMP
    CHECK(pset_low_nibble(0x06U, 0x03U, 0x01U) == 0x02U); // AND
    CHECK(pset_low_nibble(0x06U, 0x03U, 0x02U) == 0x07U); // OR
    CHECK(pset_low_nibble(0x06U, 0x03U, 0x03U) == 0x05U); // XOR
    CHECK(pset_low_nibble(0x06U, 0x03U, 0x04U) == 0x0CU); // NOT

    CHECK(pset_low_nibble(0x06U, 0x00U, 0x08U) == 0x06U); // TIMP
    CHECK(pset_low_nibble(0x06U, 0x03U, 0x08U) == 0x03U);
    CHECK(pset_low_nibble(0x06U, 0x00U, 0x09U) == 0x06U); // TAND
    CHECK(pset_low_nibble(0x06U, 0x03U, 0x09U) == 0x02U);
    CHECK(pset_low_nibble(0x06U, 0x00U, 0x0AU) == 0x06U); // TOR
    CHECK(pset_low_nibble(0x06U, 0x03U, 0x0AU) == 0x07U);
    CHECK(pset_low_nibble(0x06U, 0x00U, 0x0BU) == 0x06U); // TXOR
    CHECK(pset_low_nibble(0x06U, 0x03U, 0x0BU) == 0x05U);
    CHECK(pset_low_nibble(0x06U, 0x00U, 0x0CU) == 0x06U); // TNOT
    CHECK(pset_low_nibble(0x06U, 0x03U, 0x0CU) == 0x0CU);
}

TEST_CASE("v9938 PSET and POINT commands access Graphic 6 pixels") {
    v9938 vdp;

    write_reg(vdp, 0, 0x0AU);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 36, 3U);    // DX
    write_reg(vdp, 38, 2U);    // DY
    write_reg(vdp, 44, 0x0BU); // CLR
    write_reg(vdp, 46, 0x50U); // PSET IMP

    CHECK((vdp.vram()[2U * 256U + 1U] & 0x0FU) == 0x0BU);

    write_reg(vdp, 32, 3U); // SX
    write_reg(vdp, 34, 2U); // SY
    write_reg(vdp, 46, 0x40U);
    write_reg(vdp, 15, 7U);
    CHECK(vdp.status_read() == 0x0BU);
}

TEST_CASE("v9938 LINE command draws inclusive major-axis pixels") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 36, 1U);    // DX
    write_reg(vdp, 38, 1U);    // DY
    write_reg(vdp, 40, 3U);    // MAJ
    write_reg(vdp, 42, 0U);    // MIN
    write_reg(vdp, 44, 0x0AU); // CLR
    write_reg(vdp, 46, 0x70U);

    CHECK((vdp.vram()[1U * 128U] & 0x0FU) == 0x0AU);
    CHECK(vdp.vram()[1U * 128U + 1U] == 0xAAU);
    CHECK((vdp.vram()[1U * 128U + 2U] & 0xF0U) == 0xA0U);
}

TEST_CASE("v9938 SRCH command reports the matching border colour coordinate when EQ is clear") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    set_addr(vdp, 2U * 128U + 2U, true);
    vdp.data_write(0x1AU);

    write_reg(vdp, 32, 4U);    // SX
    write_reg(vdp, 34, 2U);    // SY
    write_reg(vdp, 44, 0x0AU); // border colour
    write_reg(vdp, 45, 0x00U); // search right, stop on border colour
    write_reg(vdp, 46, 0x60U);

    CHECK((vdp.status(2) & 0x10U) != 0U);
    CHECK(vdp.status(8) == 5U);
    CHECK(vdp.status(9) == 0xFEU);
}

TEST_CASE("v9938 SRCH command reports the first non-border colour when EQ is set") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    set_addr(vdp, 2U * 128U + 2U, true);
    vdp.data_write(0xAAU);
    vdp.data_write(0x1AU);

    write_reg(vdp, 32, 4U);    // SX
    write_reg(vdp, 34, 2U);    // SY
    write_reg(vdp, 44, 0x0AU); // border colour
    write_reg(vdp, 45, 0x02U); // search right, stop on non-border colour
    write_reg(vdp, 46, 0x60U);

    CHECK((vdp.status(2) & 0x10U) != 0U);
    CHECK(vdp.status(8) == 6U);
    CHECK(vdp.status(9) == 0xFEU);
}

TEST_CASE("v9938 SRCH command reads the R45 source expansion bank") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);

    write_reg(vdp, 45, 0x40U); // CPU port writes expansion RAM through the destination selector.
    set_addr(vdp, 2U * 128U + 2U, true);
    vdp.data_write(0xA0U);
    CHECK(vdp.vram()[2U * 128U + 2U] == 0U);

    write_reg(vdp, 32, 4U);    // SX
    write_reg(vdp, 34, 2U);    // SY
    write_reg(vdp, 44, 0x0AU); // border colour
    write_reg(vdp, 45, 0x20U); // SRCH source is expansion RAM; destination remains normal VRAM.
    write_reg(vdp, 46, 0x60U);

    CHECK((vdp.status(2) & 0x10U) != 0U);
    CHECK(vdp.status(8) == 4U);
    CHECK(vdp.status(9) == 0xFEU);
}

TEST_CASE("v9938 YMMM command reads the R45 source expansion bank") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);

    write_reg(vdp, 45, 0x40U); // CPU port writes expansion RAM through the destination selector.
    set_addr(vdp, 2U * 128U + 2U, true);
    vdp.data_write(0x56U);
    CHECK(vdp.vram()[2U * 128U + 2U] == 0U);

    write_reg(vdp, 34, 2U);    // SY
    write_reg(vdp, 36, 4U);    // DX / x origin
    write_reg(vdp, 38, 3U);    // DY
    write_reg(vdp, 42, 1U);    // NY
    write_reg(vdp, 45, 0x20U); // YMMM source is expansion RAM; destination remains normal VRAM.
    write_reg(vdp, 46, 0xE0U);

    CHECK(vdp.vram()[3U * 128U + 2U] == 0x56U);
}

TEST_CASE("v9938 status pointer selects status registers and S0 clears frame IRQ") {
    v9938 vdp;
    bool irq = false;
    vdp.set_irq_callback([&](bool asserted) { irq = asserted; });
    write_reg(vdp, 1, 0x60U);
    write_reg(vdp, 9, 0x80U);

    vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) * (v9938::display_height_212 + 1));
    CHECK(irq);
    CHECK((vdp.status(0) & 0x80U) != 0U);

    write_reg(vdp, 15, 0U);
    const std::uint8_t s0 = vdp.status_read();
    CHECK((s0 & 0x80U) != 0U);
    CHECK_FALSE(irq);
    CHECK((vdp.status(0) & 0x80U) == 0U);

    write_reg(vdp, 15, 9U);
    CHECK(vdp.status_read() == 0xFEU);
}

TEST_CASE("v9938 scanline interrupt sets S1 and clears when S1 is read") {
    v9938 vdp;
    bool irq = false;
    vdp.set_irq_callback([&](bool asserted) { irq = asserted; });

    write_reg(vdp, 19, 3U);
    vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) * 4U);

    CHECK((vdp.status(1) & 0x01U) != 0U);
    CHECK_FALSE(irq);

    write_reg(vdp, 0, 0x10U);
    CHECK(irq);

    write_reg(vdp, 15, 1U);
    CHECK((vdp.status_read() & 0x01U) != 0U);
    CHECK((vdp.status(1) & 0x01U) == 0U);
    CHECK_FALSE(irq);
}

TEST_CASE("v9938 round-trips register, palette, VRAM, and timing state") {
    v9938 vdp;
    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 9, 0x80U);
    write_reg(vdp, 16, 4U);
    vdp.palette_write(0x41U);
    vdp.palette_write(0x02U);
    set_addr(vdp, 0x12345U, true);
    vdp.data_write(0x5AU);
    vdp.tick(v9938::cycles_per_line * 3U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    vdp.save_state(writer);

    v9938 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.visible_height() == v9938::display_height_212);
    CHECK(restored.palette(4) == 0x0111U);
    set_addr(restored, 0x12345U, false);
    CHECK(restored.data_read() == 0x5AU);
}

TEST_CASE("v9938 exposes VRAM and register bytes through introspection") {
    v9938 vdp;
    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 16, 4U);
    vdp.palette_write(0x41U);
    vdp.palette_write(0x02U);
    set_addr(vdp, 0x12345U, true);
    vdp.data_write(0x5AU);

    auto& intro = vdp.introspection();
    const auto memories = intro.memory_views();
    REQUIRE(memories.size() == 5U);
    REQUIRE(memories[0] != nullptr);
    REQUIRE(memories[1] != nullptr);
    REQUIRE(memories[2] != nullptr);
    REQUIRE(memories[3] != nullptr);
    REQUIRE(memories[4] != nullptr);
    CHECK(memories[0]->name() == "vram");
    CHECK(memories[0]->bytes()[0x12345U] == 0x5AU);
    CHECK(memories[1]->name() == "expanded_vram");
    CHECK(memories[2]->name() == "registers");
    CHECK(memories[2]->bytes()[0U] == 0x06U);
    CHECK(memories[2]->bytes()[1U] == 0x40U);
    CHECK(memories[3]->name() == "status");
    CHECK(memories[4]->name() == "palette");
    REQUIRE(memories[4]->bytes().size() == static_cast<std::size_t>(v9938::palette_count * 2));
    CHECK(memories[4]->bytes()[4U] == 0x71U);
    CHECK(memories[4]->bytes()[5U] == 0x00U);
    CHECK(memories[4]->bytes()[8U] == 0x11U);
    CHECK(memories[4]->bytes()[9U] == 0x01U);
}
