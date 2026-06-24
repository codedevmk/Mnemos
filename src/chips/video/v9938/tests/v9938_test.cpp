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
    vdp.data_write(0xFFU); // transparent sprite on line 0
    vdp.data_write(0x00U);
    vdp.data_write(0x00U);
    vdp.data_write(0x00U);
    vdp.data_write(0xFFU); // visible sprite overlaps the transparent sprite
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
    CHECK((vdp.status(0) & 0xE0U) == 0U);
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
    CHECK(vdp.status(4) == 0U);
    CHECK(vdp.status(5) == 0U);
    CHECK(vdp.status(6) == 0U);
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

TEST_CASE("v9938 HMMV command fills Graphic 4 rectangles and clears CE") {
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
    CHECK((vdp.status_read() & 0x91U) == 0U);

    vdp.render_frame();
    CHECK(pixel(vdp, 4, 3) == 0x00FF0000U);
    CHECK(pixel(vdp, 5, 3) == 0x000000FFU);
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

TEST_CASE("v9938 HMMC command streams CPU bytes through R44") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 36, 4U);    // DX
    write_reg(vdp, 38, 3U);    // DY
    write_reg(vdp, 40, 4U);    // NX
    write_reg(vdp, 42, 1U);    // NY
    write_reg(vdp, 44, 0x12U); // first byte
    write_reg(vdp, 46, 0xF0U); // HMMC

    CHECK(vdp.vram()[3U * 128U + 2U] == 0x12U);
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
    write_reg(vdp, 44, 0x01U); // first colour
    write_reg(vdp, 46, 0xB2U); // LMMC OR
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
    CHECK((vdp.status_read() & 0x81U) == 0x81U);
    write_reg(vdp, 15, 7U);
    CHECK(vdp.status_read() == 0x0AU);
    write_reg(vdp, 15, 7U);
    CHECK(vdp.status_read() == 0x0BU);
    write_reg(vdp, 15, 2U);
    CHECK((vdp.status_read() & 0x81U) == 0U);
}

TEST_CASE("v9938 preserves active CPU transfer command state across snapshots") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 36, 4U);
    write_reg(vdp, 38, 3U);
    write_reg(vdp, 40, 4U);
    write_reg(vdp, 42, 1U);
    write_reg(vdp, 44, 0x12U);
    write_reg(vdp, 46, 0xF0U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    vdp.save_state(writer);

    v9938 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    write_reg(restored, 15, 2U);
    CHECK((restored.status_read() & 0x81U) == 0x81U);
    write_reg(restored, 44, 0x34U);
    CHECK(restored.vram()[3U * 128U + 2U] == 0x12U);
    CHECK(restored.vram()[3U * 128U + 3U] == 0x34U);
    write_reg(restored, 15, 2U);
    CHECK((restored.status_read() & 0x81U) == 0U);
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

TEST_CASE("v9938 SRCH command reports the matching border colour coordinate") {
    v9938 vdp;

    write_reg(vdp, 0, 0x06U);
    write_reg(vdp, 1, 0x40U);
    set_addr(vdp, 2U * 128U + 2U, true);
    vdp.data_write(0x1AU);

    write_reg(vdp, 32, 4U);    // SX
    write_reg(vdp, 34, 2U);    // SY
    write_reg(vdp, 44, 0x0AU); // border colour
    write_reg(vdp, 45, 0x00U); // search right, equal
    write_reg(vdp, 46, 0x60U);

    CHECK((vdp.status(2) & 0x10U) != 0U);
    CHECK(vdp.status(8) == 5U);
    CHECK(vdp.status(9) == 0xFEU);
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
    CHECK(vdp.status_read() == 0U);
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
