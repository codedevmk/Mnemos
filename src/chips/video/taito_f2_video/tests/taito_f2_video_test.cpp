#include "taito_f2_video.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::video::taito_f2_video;

    void render_one_frame(taito_f2_video& chip) {
        chip.tick(static_cast<std::uint64_t>(taito_f2_video::frame_lines) *
                  taito_f2_video::line_pixels);
    }

    void set16(std::vector<std::uint8_t>& bytes, std::size_t off, std::uint16_t value) {
        bytes[off + 0U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[off + 1U] = static_cast<std::uint8_t>(value);
    }

    void set_pal(std::vector<std::uint8_t>& pal, std::uint16_t bank, std::uint8_t pen,
                 std::uint16_t value) {
        set16(pal, (static_cast<std::size_t>(bank) * 16U + pen) * 2U, value);
    }

    void solid_tile(std::vector<std::uint8_t>& gfx, std::uint32_t code, std::uint8_t pen) {
        const std::size_t base = static_cast<std::size_t>(code) * 32U;
        for (std::size_t y = 0; y < 8U; ++y) {
            for (std::size_t x = 0; x < 4U; ++x) {
                gfx[base + y * 4U + x] = static_cast<std::uint8_t>((pen << 4U) | pen);
            }
        }
    }

    void solid_sprite(std::vector<std::uint8_t>& gfx, std::uint32_t code, std::uint8_t pen) {
        const std::size_t base = static_cast<std::size_t>(code) * 128U;
        for (std::size_t y = 0; y < 16U; ++y) {
            for (std::size_t x = 0; x < 8U; ++x) {
                gfx[base + y * 8U + x] = static_cast<std::uint8_t>((pen << 4U) | pen);
            }
        }
    }

    void solid_tile16_lsb(std::vector<std::uint8_t>& gfx, std::uint32_t code,
                          std::uint8_t pen) {
        const std::size_t base = static_cast<std::size_t>(code) * 128U;
        for (std::size_t y = 0; y < 16U; ++y) {
            for (std::size_t x = 0; x < 8U; ++x) {
                gfx[base + y * 8U + x] = static_cast<std::uint8_t>((pen << 4U) | pen);
            }
        }
    }

    void split_tile16_lsb(std::vector<std::uint8_t>& gfx, std::uint32_t code,
                          std::uint8_t left_pen, std::uint8_t right_pen) {
        const std::size_t base = static_cast<std::size_t>(code) * 128U;
        for (std::size_t y = 0; y < 16U; ++y) {
            for (std::size_t x = 0; x < 8U; ++x) {
                const std::uint8_t pen = x < 4U ? left_pen : right_pen;
                gfx[base + y * 8U + x] = static_cast<std::uint8_t>((pen << 4U) | pen);
            }
        }
    }

    void set_tile(std::vector<std::uint8_t>& ram, std::uint32_t base, std::uint32_t row,
                  std::uint32_t col, std::uint16_t code, std::uint16_t attr) {
        const std::size_t off =
            base + ((row * taito_f2_video::map_tiles + col) * taito_f2_video::tile_entry_bytes);
        set16(ram, off + 0U, attr);
        set16(ram, off + 2U, code);
    }

    void set_tc0480scp_tile(std::vector<std::uint8_t>& ram, std::uint32_t base,
                            std::uint32_t columns, std::uint32_t row, std::uint32_t col,
                            std::uint16_t code, std::uint16_t attr) {
        const std::size_t off =
            base + ((row * columns + col) * taito_f2_video::tc0480scp_bg_entry_bytes);
        set16(ram, off + 0U, attr);
        set16(ram, off + 2U, code);
    }

    void set_text_tile(std::vector<std::uint8_t>& ram, std::uint32_t row, std::uint32_t col,
                       std::uint8_t code, std::uint8_t color) {
        const std::size_t off =
            taito_f2_video::text_tilemap_base +
            (row * taito_f2_video::map_tiles + col) * 2U;
        set16(ram, off, static_cast<std::uint16_t>((static_cast<std::uint16_t>(color) << 8U) |
                                                   code));
    }

    void solid_text_char(std::vector<std::uint8_t>& ram, std::uint8_t code, std::uint8_t pen) {
        const std::size_t base = taito_f2_video::text_gfx_base +
                                 static_cast<std::size_t>(code) *
                                     taito_f2_video::text_char_bytes;
        const std::uint8_t plane0 = (pen & 0x01U) != 0U ? 0xFFU : 0x00U;
        const std::uint8_t plane1 = (pen & 0x02U) != 0U ? 0xFFU : 0x00U;
        for (std::size_t y = 0; y < 8U; ++y) {
            ram[base + y * 2U + 0U] = plane0;
            ram[base + y * 2U + 1U] = plane1;
        }
    }

    void set_tc0480scp_text_tile(std::vector<std::uint8_t>& ram, std::uint32_t row,
                                 std::uint32_t col, std::uint8_t code, std::uint8_t color) {
        const std::size_t off =
            taito_f2_video::tc0480scp_text_tilemap_base +
            (row * taito_f2_video::map_tiles + col) * 2U;
        set16(ram, off, static_cast<std::uint16_t>((static_cast<std::uint16_t>(color) << 8U) |
                                                   code));
    }

    void solid_tc0480scp_text_char(std::vector<std::uint8_t>& ram, std::uint8_t code,
                                   std::uint8_t pen) {
        const std::size_t base = taito_f2_video::tc0480scp_text_gfx_base +
                                 static_cast<std::size_t>(code) *
                                     taito_f2_video::tc0480scp_text_char_bytes;
        for (std::size_t y = 0; y < 8U; ++y) {
            for (std::size_t x = 0; x < 4U; ++x) {
                ram[base + y * 4U + x] = static_cast<std::uint8_t>((pen << 4U) | pen);
            }
        }
    }

    [[nodiscard]] std::uint16_t sprite_screen_x(int x) {
        return static_cast<std::uint16_t>(
            (x + static_cast<int>(taito_f2_video::sprite_screen_x_bias)) & 0x0FFF);
    }

    [[nodiscard]] std::uint16_t sprite_screen_y(int y) {
        return static_cast<std::uint16_t>(y & 0x0FFF);
    }

    void set_sprite_raw(std::vector<std::uint8_t>& ram, std::size_t index, std::uint16_t x,
                        std::uint16_t y, std::uint16_t code, std::uint16_t attr,
                        std::uint16_t zoom = 0U) {
        const std::size_t off = index * taito_f2_video::sprite_entry_bytes;
        set16(ram, off + 0U, code);
        set16(ram, off + 2U, zoom);
        set16(ram, off + 4U, x);
        set16(ram, off + 6U, y);
        set16(ram, off + 8U, attr);
        set16(ram, off + 10U, 0U);
    }

    void set_sprite(std::vector<std::uint8_t>& ram, std::size_t index, int x, int y,
                    std::uint16_t code, std::uint16_t attr,
                    std::uint16_t zoom = 0U) {
        set_sprite_raw(ram, index, sprite_screen_x(x), sprite_screen_y(y), code, attr, zoom);
    }

    void set_sprite_control(std::vector<std::uint8_t>& ram, std::size_t index,
                            std::uint16_t ctrl) {
        const std::size_t off = index * taito_f2_video::sprite_entry_bytes;
        set16(ram, off + 6U, 0x8000U);
        set16(ram, off + 10U, ctrl);
    }

    [[nodiscard]] std::size_t pixel_at(std::uint32_t x, std::uint32_t y) {
        return static_cast<std::size_t>(y) * taito_f2_video::visible_width + x;
    }

    void set_priority_registers(taito_f2_video& chip, std::uint16_t blend_mode) {
        chip.write_priority_register(0U, blend_mode);
        chip.write_priority_register(4U, 0x00F0U);
        chip.write_priority_register(5U, 0x0020U);
        chip.write_priority_register(6U, 0x0011U);
        chip.write_priority_register(7U, 0x0033U);
    }

} // namespace

TEST_CASE("taito_f2_video registers in the chip factory", "[taito_f2_video]") {
    auto chip = mnemos::chips::create_chip("taito.f2_video");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::video);
    CHECK(chip->metadata().manufacturer == "Taito");
}

TEST_CASE("taito_f2_video decodes 15-bit Taito palette words", "[taito_f2_video]") {
    CHECK(taito_f2_video::decode_color(0x0000U) == 0x000000U);
    CHECK(taito_f2_video::decode_color(0x001FU) == 0xFF0000U);
    CHECK(taito_f2_video::decode_color(0x03E0U) == 0x00FF00U);
    CHECK(taito_f2_video::decode_color(0x7C00U) == 0x0000FFU);
    CHECK(taito_f2_video::decode_color(0x7FFFU) == 0xFFFFFFU);
    CHECK(taito_f2_video::decode_color(taito_f2_video::palette_format::rgbx_444,
                                        0xF000U) == 0xFF0000U);
    CHECK(taito_f2_video::decode_color(taito_f2_video::palette_format::rgbx_444,
                                        0x0F00U) == 0x00FF00U);
    CHECK(taito_f2_video::decode_color(taito_f2_video::palette_format::rgbx_444,
                                        0x00F0U) == 0x0000FFU);
    CHECK(taito_f2_video::decode_color(taito_f2_video::palette_format::xrgb_555,
                                        0x7C00U) == 0xFF0000U);
    CHECK(taito_f2_video::decode_color(taito_f2_video::palette_format::xrgb_555,
                                        0x03E0U) == 0x00FF00U);
    CHECK(taito_f2_video::decode_color(taito_f2_video::palette_format::xrgb_555,
                                        0x001FU) == 0x0000FFU);
}

TEST_CASE("taito_f2_video renders dual TC0100SCN layer priority",
          "[taito_f2_video][tc0100scn]") {
    taito_f2_video chip;
    std::vector<std::uint8_t> ram0(taito_f2_video::bg1_colscroll_base + 0x1000U, 0U);
    std::vector<std::uint8_t> ram1(taito_f2_video::bg1_colscroll_base + 0x1000U, 0U);
    std::vector<std::uint8_t> gfx0(0x100U, 0U);
    std::vector<std::uint8_t> gfx1(0x100U, 0U);
    std::vector<std::uint8_t> palette(0x2000U, 0U);

    solid_tile(gfx0, 1U, 1U);
    solid_tile(gfx1, 1U, 2U);
    set_tile(ram0, taito_f2_video::bg0_tilemap_base, 0U, 0U, 1U, 0U);
    set_tile(ram1, taito_f2_video::bg0_tilemap_base, 0U, 0U, 1U, 0U);
    set_pal(palette, 0U, 1U, 0x001FU);
    set_pal(palette, 0U, 2U, 0x7C00U);

    chip.attach_tile_ram(ram0);
    chip.attach_secondary_tile_ram(ram1);
    chip.attach_tile_gfx(gfx0);
    chip.attach_secondary_tile_gfx(gfx1);
    chip.attach_palette(palette);
    chip.set_tilemap_variant(taito_f2_video::tilemap_variant::dual_tc0100scn);

    chip.write_priority_register(5U, 0x0001U);
    chip.write_priority_register(9U, 0x0002U);
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[0] == 0x0000FFU);

    chip.write_priority_register(5U, 0x0004U);
    chip.write_priority_register(9U, 0x0001U);
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[0] == 0xFF0000U);
}

TEST_CASE("taito_f2_video advances beam and fires vblank once per frame", "[taito_f2_video]") {
    taito_f2_video chip;
    std::uint32_t lines = 0U;
    std::uint32_t vblanks = 0U;
    chip.set_scanline_callback([&lines](std::uint32_t) { ++lines; });
    chip.set_vblank_callback([&vblanks](std::uint32_t line) {
        CHECK(line == taito_f2_video::vblank_start);
        ++vblanks;
    });

    render_one_frame(chip);
    CHECK(lines == taito_f2_video::frame_lines);
    CHECK(vblanks == 1U);
    CHECK(chip.frame_index() == 1U);
    CHECK(chip.framebuffer().width == taito_f2_video::visible_width);
    CHECK(chip.framebuffer().height == taito_f2_video::visible_height);
}

TEST_CASE("taito_f2_video renders the opaque first tilemap", "[taito_f2_video]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 1U, 1U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg0_tilemap_base, 0U, 0U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 1U, 0x001FU);

    taito_f2_video chip;
    chip.attach_tile_gfx(tiles);
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[0] == 0xFF0000U);
    CHECK(chip.framebuffer().pixels[7U] == 0xFF0000U);
    CHECK(chip.framebuffer().pixels[8U] == 0x000000U);
}

TEST_CASE("taito_f2_video renders through the selected palette format",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 1U, 1U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg0_tilemap_base, 0U, 0U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 1U, 0xF000U);

    taito_f2_video chip;
    chip.set_palette_format(taito_f2_video::palette_format::rgbx_444);
    chip.attach_tile_gfx(tiles);
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[0] == 0xFF0000U);
}

TEST_CASE("taito_f2_video scroll registers move tilemap lookup", "[taito_f2_video]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 1U, 2U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg0_tilemap_base, 1U, 1U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 2U, 0x03E0U);

    taito_f2_video chip;
    chip.attach_tile_gfx(tiles);
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);
    chip.set_scroll0(8U, 8U);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[0] == 0x00FF00U);
}

TEST_CASE("taito_f2_video renders the TC0100SCN RAM-generated text layer on top",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 1U, 1U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg0_tilemap_base, 0U, 0U, 1U, 0U);
    solid_text_char(ram, 7U, 3U);
    set_text_tile(ram, 0U, 0U, 7U, 2U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 1U, 0x001FU);
    set_pal(palette, 2U, 3U, 0x03FFU);

    taito_f2_video chip;
    chip.attach_tile_gfx(tiles);
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[0] == 0xFFFF00U);
}

TEST_CASE("taito_f2_video honors TC0100SCN layer disable and priority swap bits",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 1U, 1U);
    solid_tile(tiles, 2U, 2U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg0_tilemap_base, 0U, 0U, 1U, 0U);
    set_tile(ram, taito_f2_video::bg1_tilemap_base, 0U, 0U, 2U, 1U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 1U, 0x001FU);
    set_pal(palette, 1U, 2U, 0x03E0U);

    taito_f2_video chip;
    chip.attach_tile_gfx(tiles);
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[0] == 0x00FF00U);

    chip.set_layer_control(taito_f2_video::control_bg1_disable);
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[0] == 0xFF0000U);

    chip.set_layer_control(taito_f2_video::control_priority_swap);
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[0] == 0xFF0000U);
}

TEST_CASE("taito_f2_video renders TC0480SCP 16x16 layers in hardware order",
          "[taito_f2_video][tc0480scp]") {
    std::vector<std::uint8_t> tiles(0x400U, 0U);
    solid_tile16_lsb(tiles, 1U, 1U);
    solid_tile16_lsb(tiles, 2U, 2U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tc0480scp_tile(ram, 0x0000U, 32U, 0U, 0U, 1U, 0U);
    set_tc0480scp_tile(ram, 0x3000U, 32U, 0U, 0U, 2U, 1U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 1U, 0x001FU);
    set_pal(palette, 1U, 2U, 0x03E0U);

    taito_f2_video chip;
    chip.set_tilemap_variant(taito_f2_video::tilemap_variant::tc0480scp);
    chip.attach_tile_gfx(tiles);
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);

    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x00FF00U);

    chip.write_tc0480scp_control_register(0x0FU, 0x0010U);
    render_one_frame(chip);
    CHECK(chip.tc0480scp_control_register(0x0FU) == 0x0010U);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0xFF0000U);
}

TEST_CASE("taito_f2_video renders the TC0480SCP RAM text plane on top",
          "[taito_f2_video][tc0480scp]") {
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    solid_tc0480scp_text_char(ram, 7U, 3U);
    set_tc0480scp_text_tile(ram, 0U, 0U, 7U, 2U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 2U, 3U, 0x03FFU);

    taito_f2_video chip;
    chip.set_tilemap_variant(taito_f2_video::tilemap_variant::tc0480scp);
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0xFFFF00U);
}

TEST_CASE("taito_f2_video applies the Footchmp TC0480SCP priority register layout",
          "[taito_f2_video][tc0480scp][priority]") {
    std::vector<std::uint8_t> tiles(0x400U, 0U);
    solid_tile16_lsb(tiles, 1U, 1U);
    solid_tile16_lsb(tiles, 2U, 2U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tc0480scp_tile(ram, 0x0000U, 32U, 0U, 0U, 1U, 0U);
    set_tc0480scp_tile(ram, 0x3000U, 32U, 0U, 0U, 2U, 1U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 1U, 0x001FU);
    set_pal(palette, 1U, 2U, 0x03E0U);

    taito_f2_video chip;
    chip.set_tilemap_variant(taito_f2_video::tilemap_variant::tc0480scp);
    chip.set_tc0480scp_priority_model(
        taito_f2_video::tc0480scp_priority_model::deadconx_footchmp);
    chip.write_priority_register(4U, 0x0051U); // BG0 priority 5, BG3 priority 1.
    chip.attach_tile_gfx(tiles);
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);

    render_one_frame(chip);

    CHECK(chip.current_tc0480scp_priority_model() ==
          taito_f2_video::tc0480scp_priority_model::deadconx_footchmp);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0xFF0000U);
}

TEST_CASE("taito_f2_video applies TC0480SCP per-row scroll RAM",
          "[taito_f2_video][tc0480scp][rowscroll]") {
    std::vector<std::uint8_t> tiles(0x400U, 0U);
    solid_tile16_lsb(tiles, 1U, 1U);
    solid_tile16_lsb(tiles, 2U, 2U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tc0480scp_tile(ram, 0x0000U, 32U, 0U, 0U, 1U, 0U);
    set_tc0480scp_tile(ram, 0x0000U, 32U, 0U, 1U, 2U, 1U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 1U, 0x001FU);
    set_pal(palette, 1U, 2U, 0x03E0U);

    taito_f2_video chip;
    chip.set_tilemap_variant(taito_f2_video::tilemap_variant::tc0480scp);
    chip.attach_tile_gfx(tiles);
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);

    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0xFF0000U);

    set16(ram, 0x4000U, 0xFFF0U);
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x00FF00U);
}

TEST_CASE("taito_f2_video applies TC0480SCP layer zoom control words",
          "[taito_f2_video][tc0480scp][zoom]") {
    std::vector<std::uint8_t> tiles(0x400U, 0U);
    split_tile16_lsb(tiles, 1U, 1U, 2U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tc0480scp_tile(ram, 0x0000U, 32U, 0U, 0U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 1U, 0x001FU);
    set_pal(palette, 0U, 2U, 0x03E0U);

    taito_f2_video chip;
    chip.set_tilemap_variant(taito_f2_video::tilemap_variant::tc0480scp);
    chip.attach_tile_gfx(tiles);
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);

    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[pixel_at(15U, 0U)] == 0x00FF00U);

    chip.write_tc0480scp_control_register(0x08U, 0x807FU);
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[pixel_at(15U, 0U)] == 0xFF0000U);
}

TEST_CASE("taito_f2_video applies TC0480SCP BG2 row zoom RAM",
          "[taito_f2_video][tc0480scp][rowzoom]") {
    std::vector<std::uint8_t> tiles(0x400U, 0U);
    split_tile16_lsb(tiles, 1U, 1U, 2U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tc0480scp_tile(ram, 0x2000U, 32U, 0U, 0U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 1U, 0x001FU);
    set_pal(palette, 0U, 2U, 0x03E0U);

    taito_f2_video chip;
    chip.set_tilemap_variant(taito_f2_video::tilemap_variant::tc0480scp);
    chip.attach_tile_gfx(tiles);
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);

    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[pixel_at(15U, 0U)] == 0x00FF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(31U, 0U)] == 0x000000U);

    set16(ram, 0x6000U, 0x0080U);
    chip.write_tc0480scp_control_register(0x0FU,
                                          taito_f2_video::tc0480scp_control_row_zoom_bg2);
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[pixel_at(31U, 0U)] == 0xFF0000U);
}

TEST_CASE("taito_f2_video renders the TC0280GRD ROZ tilemap",
          "[taito_f2_video][roz]") {
    std::vector<std::uint8_t> roz_gfx(0x1000U, 0U);
    solid_tile(roz_gfx, 1U, 5U);
    std::vector<std::uint8_t> roz_ram(taito_f2_video::roz_ram_bytes, 0U);
    set16(roz_ram, 0U, static_cast<std::uint16_t>((2U << 14U) | 1U));
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 2U, 5U, 0x03E0U);

    taito_f2_video chip;
    chip.attach_roz_gfx(roz_gfx);
    chip.attach_roz_ram(roz_ram);
    chip.attach_palette(palette);
    chip.write_roz_control_register(2U, 0x0800U);
    chip.write_roz_control_register(7U, 0x1000U);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x00FF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(8U, 0U)] == 0x000000U);
}

TEST_CASE("taito_f2_video renders the TC0430GRW ROZ tilemap with one X multiplier",
          "[taito_f2_video][roz]") {
    std::vector<std::uint8_t> roz_gfx(0x1000U, 0U);
    solid_tile(roz_gfx, 1U, 5U);
    std::vector<std::uint8_t> roz_ram(taito_f2_video::roz_ram_bytes, 0U);
    set16(roz_ram, 0U, static_cast<std::uint16_t>((2U << 14U) | 1U));
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 2U, 5U, 0x03E0U);

    taito_f2_video chip;
    chip.set_roz_variant(taito_f2_video::roz_variant::tc0430grw);
    chip.attach_roz_gfx(roz_gfx);
    chip.attach_roz_ram(roz_ram);
    chip.attach_palette(palette);
    chip.write_roz_control_register(2U, 0x1000U);
    chip.write_roz_control_register(7U, 0x1000U);
    render_one_frame(chip);

    CHECK(chip.current_roz_variant() == taito_f2_video::roz_variant::tc0430grw);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x00FF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(4U, 0U)] == 0x00FF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(8U, 0U)] == 0x000000U);
}

TEST_CASE("taito_f2_video applies TC0360PRI priority to the ROZ layer",
          "[taito_f2_video][roz][priority]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 2U, 2U);
    std::vector<std::uint8_t> roz_gfx(0x1000U, 0U);
    solid_tile(roz_gfx, 1U, 5U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg1_tilemap_base, 0U, 0U, 2U, 1U);
    std::vector<std::uint8_t> roz_ram(taito_f2_video::roz_ram_bytes, 0U);
    set16(roz_ram, 0U, 1U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 1U, 2U, 0x03E0U);
    set_pal(palette, 0U, 5U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_tile_gfx(tiles);
    chip.attach_tile_ram(ram);
    chip.attach_roz_gfx(roz_gfx);
    chip.attach_roz_ram(roz_ram);
    chip.attach_palette(palette);
    chip.write_roz_control_register(2U, 0x0800U);
    chip.write_roz_control_register(7U, 0x1000U);
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x00FF00U);

    chip.write_priority_register(5U, 0x0020U);
    chip.write_priority_register(8U, 0x0004U);
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x0000FFU);
}

TEST_CASE("taito_f2_video keeps low-priority sprites behind the front tile layer",
          "[taito_f2_video][priority]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 1U, 1U);
    solid_tile(tiles, 2U, 2U);
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg0_tilemap_base, 0U, 0U, 1U, 0U);
    set_tile(ram, taito_f2_video::bg0_tilemap_base, 0U, 1U, 1U, 0U);
    set_tile(ram, taito_f2_video::bg1_tilemap_base, 0U, 0U, 2U, 1U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 0U, 0U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 1U, 0x001FU);
    set_pal(palette, 1U, 2U, 0x03E0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_tile_gfx(tiles);
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_tile_ram(ram);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x00FF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(8U, 0U)] == 0x0000FFU);
}

TEST_CASE("taito_f2_video lets high-priority sprite groups cover the front tile layer",
          "[taito_f2_video][priority]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 1U, 1U);
    solid_tile(tiles, 2U, 2U);
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg0_tilemap_base, 0U, 0U, 1U, 0U);
    set_tile(ram, taito_f2_video::bg1_tilemap_base, 0U, 0U, 2U, 1U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 0U, 0U, 1U, 0x0080U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 1U, 0x001FU);
    set_pal(palette, 1U, 2U, 0x03E0U);
    set_pal(palette, 256U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_tile_gfx(tiles);
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_tile_ram(ram);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x0000FFU);
}

TEST_CASE("taito_f2_video applies TC0360PRI register priorities to sprite groups",
          "[taito_f2_video][priority]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 1U, 1U);
    solid_tile(tiles, 2U, 2U);
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg0_tilemap_base, 0U, 0U, 1U, 0U);
    set_tile(ram, taito_f2_video::bg1_tilemap_base, 0U, 0U, 2U, 1U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 0U, 0U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 1U, 0x001FU);
    set_pal(palette, 1U, 2U, 0x03E0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_tile_gfx(tiles);
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_tile_ram(ram);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.write_priority_register(4U, 0x00F0U);
    chip.write_priority_register(5U, 0x0020U);
    chip.write_priority_register(6U, 0x0004U);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.priority_register(6U) == 0x0004U);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x0000FFU);
}

TEST_CASE("taito_f2_video applies TC0360PRI blend mode 1 palette and pen swaps",
          "[taito_f2_video][priority]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 2U, 2U);
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg1_tilemap_base, 0U, 0U, 2U, 1U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 1U, 2U, 0x03E0U);
    set_pal(palette, 1U, 3U, 0x7C1FU);
    set_pal(palette, 128U, 2U, 0x03FFU);
    set_pal(palette, 128U, 3U, 0x7C00U);
    set_pal(palette, 256U, 3U, 0x7C00U);

    SECTION("sprite just under the tile keeps the tile pen with the sprite palette") {
        std::vector<std::uint8_t> sprites(0x10000U, 0U);
        set_sprite(sprites, 0U, 0U, 0U, 1U, 0U);

        taito_f2_video chip;
        chip.attach_tile_gfx(tiles);
        chip.attach_sprite_gfx(sprite_gfx);
        chip.attach_tile_ram(ram);
        chip.attach_sprite_ram(sprites);
        chip.attach_palette(palette);
        set_priority_registers(chip, 0x00C0U);
        chip.latch_sprites();
        render_one_frame(chip);

        CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0xFFFF00U);
    }

    SECTION("sprite just over the tile keeps the tile palette with the sprite pen") {
        std::vector<std::uint8_t> sprites(0x10000U, 0U);
        set_sprite(sprites, 0U, 0U, 0U, 1U, 0x0080U);

        taito_f2_video chip;
        chip.attach_tile_gfx(tiles);
        chip.attach_sprite_gfx(sprite_gfx);
        chip.attach_tile_ram(ram);
        chip.attach_sprite_ram(sprites);
        chip.attach_palette(palette);
        set_priority_registers(chip, 0x00C0U);
        chip.latch_sprites();
        render_one_frame(chip);

        CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0xFF00FFU);
    }
}

TEST_CASE("taito_f2_video applies TC0360PRI blend mode 2 alternate palette bit",
          "[taito_f2_video][priority]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 2U, 2U);
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg1_tilemap_base, 0U, 0U, 2U, 1U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 2U, 0x001FU);
    set_pal(palette, 1U, 2U, 0x03E0U);
    set_pal(palette, 256U, 3U, 0x7C00U);
    set_pal(palette, 257U, 3U, 0x03E0U);

    SECTION("sprite just under the tile selects the tile index with bit 4 cleared") {
        std::vector<std::uint8_t> sprites(0x10000U, 0U);
        set_sprite(sprites, 0U, 0U, 0U, 1U, 0U);

        taito_f2_video chip;
        chip.attach_tile_gfx(tiles);
        chip.attach_sprite_gfx(sprite_gfx);
        chip.attach_tile_ram(ram);
        chip.attach_sprite_ram(sprites);
        chip.attach_palette(palette);
        set_priority_registers(chip, 0x0080U);
        chip.latch_sprites();
        render_one_frame(chip);

        CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0xFF0000U);
    }

    SECTION("sprite just over the tile selects the sprite index with bit 4 cleared") {
        std::vector<std::uint8_t> sprites(0x10000U, 0U);
        set_sprite(sprites, 0U, 0U, 0U, 1U, 0x0081U);

        taito_f2_video chip;
        chip.attach_tile_gfx(tiles);
        chip.attach_sprite_gfx(sprite_gfx);
        chip.attach_tile_ram(ram);
        chip.attach_sprite_ram(sprites);
        chip.attach_palette(palette);
        set_priority_registers(chip, 0x0080U);
        chip.latch_sprites();
        render_one_frame(chip);

        CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x0000FFU);
    }
}

TEST_CASE("taito_f2_video applies TC0100SCN per-row scroll RAM", "[taito_f2_video]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 1U, 2U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg0_tilemap_base, 0U, 1U, 1U, 0U);
    set16(ram, taito_f2_video::bg0_rowscroll_base, 8U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 2U, 0x03E0U);

    taito_f2_video chip;
    chip.attach_tile_gfx(tiles);
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[0] == 0x00FF00U);
}

TEST_CASE("taito_f2_video renders sprites from the latched object buffer", "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 16U, 20U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[20U * 320U + 16U] == 0x0000FFU);
    CHECK(chip.framebuffer().pixels[35U * 320U + 31U] == 0x0000FFU);
    CHECK(chip.framebuffer().pixels[36U * 320U + 16U] == 0x000000U);
    CHECK(chip.framebuffer().pixels[0] == 0x000000U);
}

TEST_CASE("taito_f2_video applies generic partial-delayed TC0200OBJ buffering",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x3000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    solid_sprite(sprite_gfx, 2U, 4U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 20, 12, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);
    set_pal(palette, 128U, 4U, 0x03E0U);

    taito_f2_video chip;
    chip.set_sprite_buffer_policy(taito_f2_video::sprite_buffer_policy::partial_delayed);
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();

    std::fill(sprites.begin(), sprites.end(), std::uint8_t{0});
    set_sprite(sprites, 0U, 80, 12, 2U, 0U);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(20U, 12U)] == 0x00FF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(80U, 12U)] == 0x000000U);

    chip.latch_sprites();
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[pixel_at(20U, 12U)] == 0x000000U);
    CHECK(chip.framebuffer().pixels[pixel_at(80U, 12U)] == 0x00FF00U);
}

TEST_CASE("taito_f2_video save/load preserves delayed TC0200OBJ backing buffer",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 4, 6, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video a;
    a.set_sprite_buffer_policy(taito_f2_video::sprite_buffer_policy::partial_delayed);
    a.attach_sprite_gfx(sprite_gfx);
    a.attach_sprite_ram(sprites);
    a.attach_palette(palette);
    a.latch_sprites();

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    std::fill(sprites.begin(), sprites.end(), std::uint8_t{0});
    set_sprite(sprites, 0U, 40, 6, 1U, 0U);

    taito_f2_video b;
    b.attach_sprite_gfx(sprite_gfx);
    b.attach_sprite_ram(sprites);
    b.attach_palette(palette);
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(b.current_sprite_buffer_policy() ==
          taito_f2_video::sprite_buffer_policy::partial_delayed);

    b.latch_sprites();
    render_one_frame(b);
    CHECK(b.framebuffer().pixels[pixel_at(4U, 6U)] == 0x0000FFU);
    CHECK(b.framebuffer().pixels[pixel_at(40U, 6U)] == 0x000000U);
}

TEST_CASE("taito_f2_video sign-extends TC0200OBJ sprite coordinates",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, -8, 0, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[0] == 0x0000FFU);
    CHECK(chip.framebuffer().pixels[7U] == 0x0000FFU);
    CHECK(chip.framebuffer().pixels[8U] == 0x000000U);
}

TEST_CASE("taito_f2_video applies TC0200OBJ master scroll markers",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite_raw(sprites, 0U, 0xA00CU, 0x0002U, 0U, 0U);
    set_sprite(sprites, 1U, 4, 6, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[6U * 320U + 4U] == 0x000000U);
    CHECK(chip.framebuffer().pixels[8U * 320U + 16U] == 0x0000FFU);
}

TEST_CASE("taito_f2_video applies extra scroll unless a sprite requests master-only scroll",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite_raw(sprites, 0U, 0x5008U, 0x0003U, 0U, 0U);
    set_sprite(sprites, 1U, 10, 10, 1U, 0U);
    set_sprite_raw(sprites, 2U,
                   static_cast<std::uint16_t>(0x4000U | sprite_screen_x(40)),
                   sprite_screen_y(10), 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[10U * 320U + 10U] == 0x000000U);
    CHECK(chip.framebuffer().pixels[13U * 320U + 18U] == 0x0000FFU);
    CHECK(chip.framebuffer().pixels[10U * 320U + 40U] == 0x0000FFU);
}

TEST_CASE("taito_f2_video lets absolute sprites ignore latched scrolls",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite_raw(sprites, 0U, 0xA008U, 0x0002U, 0U, 0U);
    set_sprite_raw(sprites, 1U, 0x5004U, 0x0001U, 0U, 0U);
    set_sprite_raw(sprites, 2U,
                   static_cast<std::uint16_t>(0x8000U | sprite_screen_x(60)),
                   sprite_screen_y(12), 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[12U * 320U + 60U] == 0x0000FFU);
    CHECK(chip.framebuffer().pixels[28U * 320U + 80U] == 0x000000U);
}

TEST_CASE("taito_f2_video persists TC0200OBJ master scroll across sprite buffers",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite_raw(sprites, 0U, 0xA00CU, 0x0002U, 0U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    std::fill(sprites.begin(), sprites.end(), std::uint8_t{0});
    set_sprite(sprites, 0U, 4, 6, 1U, 0U);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[6U * 320U + 4U] == 0x000000U);
    CHECK(chip.framebuffer().pixels[8U * 320U + 16U] == 0x0000FFU);
}

TEST_CASE("taito_f2_video persists TC0200OBJ disable state until a marker clears it",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite_control(sprites, 0U, 0x1000U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    std::fill(sprites.begin(), sprites.end(), std::uint8_t{0});
    set_sprite(sprites, 0U, 4, 6, 1U, 0U);
    chip.latch_sprites();
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[6U * 320U + 4U] == 0x000000U);

    std::fill(sprites.begin(), sprites.end(), std::uint8_t{0});
    set_sprite_control(sprites, 0U, 0x0000U);
    set_sprite(sprites, 1U, 4, 6, 1U, 0U);
    chip.latch_sprites();
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[6U * 320U + 4U] == 0x0000FFU);
}

TEST_CASE("taito_f2_video applies TC0200OBJ sprite banks", "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx((0x802U * 128U), 0U);
    solid_sprite(sprite_gfx, 1U, 1U);
    solid_sprite(sprite_gfx, 0x801U, 2U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 0U, 0U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 1U, 0x001FU);
    set_pal(palette, 128U, 2U, 0x03E0U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[0] == 0xFF0000U);

    chip.write_sprite_bank_register(2U, 1U);
    render_one_frame(chip);
    CHECK(chip.sprite_bank(0U) == 0x0800U);
    CHECK(chip.framebuffer().pixels[0] == 0x00FF00U);
}

TEST_CASE("taito_f2_video decodes TC0200OBJ sprite extension policies",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x1235U * 128U, 0U);
    solid_sprite(sprite_gfx, 1U, 1U);
    solid_sprite(sprite_gfx, 0x1001U, 2U);
    solid_sprite(sprite_gfx, 0x1234U, 3U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 1U, 0x001FU);
    set_pal(palette, 128U, 2U, 0x03E0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    SECTION("type 1 uses low extension bits above the low ten code bits") {
        std::vector<std::uint8_t> sprites(0x10000U, 0U);
        std::vector<std::uint8_t> sprite_ext(0x4000U, 0U);
        set_sprite(sprites, 0U, 0U, 0U, 1U, 0U);
        set16(sprite_ext, 0U, 0x0004U);

        taito_f2_video chip;
        chip.attach_sprite_gfx(sprite_gfx);
        chip.attach_sprite_ram(sprites);
        chip.attach_sprite_extension_ram(sprite_ext);
        chip.attach_palette(palette);
        chip.set_sprite_mode(taito_f2_video::sprite_mode::extension_low);
        chip.latch_sprites();
        render_one_frame(chip);

        CHECK(chip.framebuffer().pixels[0] == 0x00FF00U);
    }

    SECTION("type 2 uses high extension bytes above the low eight code bits") {
        std::vector<std::uint8_t> sprites(0x10000U, 0U);
        std::vector<std::uint8_t> sprite_ext(0x4000U, 0U);
        set_sprite(sprites, 0U, 0U, 0U, 0x0034U, 0U);
        set16(sprite_ext, 0U, 0x1200U);

        taito_f2_video chip;
        chip.attach_sprite_gfx(sprite_gfx);
        chip.attach_sprite_ram(sprites);
        chip.attach_sprite_extension_ram(sprite_ext);
        chip.attach_palette(palette);
        chip.set_sprite_mode(taito_f2_video::sprite_mode::extension_high);
        chip.latch_sprites();
        render_one_frame(chip);

        CHECK(chip.framebuffer().pixels[0] == 0x0000FFU);
    }

    SECTION("type 3 promotes low extension bytes above the low eight code bits") {
        std::vector<std::uint8_t> sprites(0x10000U, 0U);
        std::vector<std::uint8_t> sprite_ext(0x4000U, 0U);
        set_sprite(sprites, 0U, 0U, 0U, 0x0034U, 0U);
        set16(sprite_ext, 0U, 0x0012U);

        taito_f2_video chip;
        chip.attach_sprite_gfx(sprite_gfx);
        chip.attach_sprite_ram(sprites);
        chip.attach_sprite_extension_ram(sprite_ext);
        chip.attach_palette(palette);
        chip.set_sprite_mode(taito_f2_video::sprite_mode::extension_low_as_high);
        chip.latch_sprites();
        render_one_frame(chip);

        CHECK(chip.framebuffer().pixels[0] == 0x0000FFU);
    }
}

TEST_CASE("taito_f2_video scales TC0200OBJ sprites from the zoom word",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 0U, 0U, 1U, 0U, 0x8080U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[0] == 0x0000FFU);
    CHECK(chip.framebuffer().pixels[7U * 320U + 7U] == 0x0000FFU);
    CHECK(chip.framebuffer().pixels[8U] == 0x000000U);
    CHECK(chip.framebuffer().pixels[8U * 320U] == 0x000000U);
}

TEST_CASE("taito_f2_video chains TC0200OBJ continuation records into multi-cell sprites",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 1U);
    solid_sprite(sprite_gfx, 2U, 2U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 10U, 12U, 1U, 0x0800U);
    set_sprite(sprites, 1U, 0U, 0U, 2U, 0xC000U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 1U, 0x001FU);
    set_pal(palette, 128U, 2U, 0x03E0U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[12U * 320U + 10U] == 0xFF0000U);
    CHECK(chip.framebuffer().pixels[12U * 320U + 25U] == 0xFF0000U);
    CHECK(chip.framebuffer().pixels[12U * 320U + 26U] == 0x00FF00U);
    CHECK(chip.framebuffer().pixels[12U * 320U + 41U] == 0x00FF00U);
}

TEST_CASE("taito_f2_video follows banked active-area control markers",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite_control(sprites, 0U, 0x0001U);
    set_sprite(sprites,
               (taito_f2_video::sprite_active_area_stride /
                taito_f2_video::sprite_entry_bytes) +
                   1U,
               4U, 6U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.set_sprite_mode(taito_f2_video::sprite_mode::banked);
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[6U * 320U + 4U] == 0x0000FFU);
}

TEST_CASE("taito_f2_video honors TC0200OBJ marker sprite disable state",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite_control(sprites, 0U, 0x1000U);
    set_sprite(sprites, 1U, 4U, 6U, 1U, 0U);
    set_sprite_control(sprites, 2U, 0x0000U);
    set_sprite(sprites, 3U, 24U, 6U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[6U * 320U + 4U] == 0x000000U);
    CHECK(chip.framebuffer().pixels[6U * 320U + 24U] == 0x0000FFU);
}

TEST_CASE("taito_f2_video applies TC0200OBJ marker flip-screen coordinates",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite_control(sprites, 0U, 0x2000U);
    set_sprite(sprites, 1U, 304U, 240U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[0] == 0x0000FFU);
    CHECK(chip.framebuffer().pixels[15U * 320U + 15U] == 0x0000FFU);
}

TEST_CASE("taito_f2_video applies board-specific sprite hide-pixel offsets",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    SECTION("normal orientation subtracts the hide pixels from sprite X") {
        std::vector<std::uint8_t> sprites(0x10000U, 0U);
        set_sprite(sprites, 0U, 3U, 0U, 1U, 0U);

        taito_f2_video chip;
        chip.set_sprite_hide_pixels(3, 3);
        chip.attach_sprite_gfx(sprite_gfx);
        chip.attach_sprite_ram(sprites);
        chip.attach_palette(palette);
        chip.latch_sprites();
        render_one_frame(chip);

        CHECK(chip.framebuffer().pixels[0] == 0x0000FFU);
        CHECK(chip.framebuffer().pixels[15U] == 0x0000FFU);
        CHECK(chip.framebuffer().pixels[16U] == 0x000000U);
    }

    SECTION("flip-screen orientation uses the flipped hide-pixel compensation") {
        std::vector<std::uint8_t> sprites(0x10000U, 0U);
        set_sprite_control(sprites, 0U, 0x2000U);
        set_sprite(sprites, 1U, 301U, 240U, 1U, 0U);

        taito_f2_video chip;
        chip.set_sprite_hide_pixels(3, 3);
        chip.attach_sprite_gfx(sprite_gfx);
        chip.attach_sprite_ram(sprites);
        chip.attach_palette(palette);
        chip.latch_sprites();
        render_one_frame(chip);

        CHECK(chip.framebuffer().pixels[0] == 0x0000FFU);
        CHECK(chip.framebuffer().pixels[15U * 320U + 15U] == 0x0000FFU);
        CHECK(chip.framebuffer().pixels[16U] == 0x000000U);
    }
}

TEST_CASE("taito_f2_video selects TC0200OBJ active area from board-specific marker bits",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    SECTION("mode-default banked sprites use control-word bit 0") {
        std::vector<std::uint8_t> sprites(0x10000U, 0U);
        set_sprite_control(sprites, 0U, 0x0001U);
        set_sprite(sprites,
                   (taito_f2_video::sprite_active_area_stride /
                    taito_f2_video::sprite_entry_bytes) +
                       1U,
                   4U, 6U, 1U, 0U);

        taito_f2_video chip;
        chip.set_sprite_mode(taito_f2_video::sprite_mode::banked);
        chip.attach_sprite_gfx(sprite_gfx);
        chip.attach_sprite_ram(sprites);
        chip.attach_palette(palette);
        chip.latch_sprites();
        render_one_frame(chip);

        CHECK(chip.framebuffer().pixels[6U * 320U + 4U] == 0x0000FFU);
    }

    SECTION("Footchmp-style boards use marker Y bit 0 instead") {
        std::vector<std::uint8_t> sprites(0x10000U, 0U);
        set16(sprites, 6U, 0x8001U);
        set16(sprites, 10U, 0x0000U);
        set_sprite(sprites,
                   (taito_f2_video::sprite_active_area_stride /
                    taito_f2_video::sprite_entry_bytes) +
                       1U,
                   4U, 6U, 1U, 0U);

        taito_f2_video chip;
        chip.set_sprite_mode(taito_f2_video::sprite_mode::banked);
        chip.set_sprite_active_area_source(
            taito_f2_video::sprite_active_area_source::y_word_bit0);
        chip.attach_sprite_gfx(sprite_gfx);
        chip.attach_sprite_ram(sprites);
        chip.attach_palette(palette);
        chip.latch_sprites();
        render_one_frame(chip);

        CHECK(chip.framebuffer().pixels[6U * 320U + 4U] == 0x0000FFU);
    }
}

TEST_CASE("taito_f2_video save/load preserves registers and latched sprites",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 4U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 4U, 6U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 4U, 0xFFF0U);

    taito_f2_video a;
    a.attach_sprite_gfx(sprite_gfx);
    a.attach_sprite_ram(sprites);
    a.attach_palette(palette);
    a.set_scroll0(12U, 34U);
    a.set_layer_control(0x2000U);
    a.set_palette_format(taito_f2_video::palette_format::rgbx_444);
    a.set_tilemap_variant(taito_f2_video::tilemap_variant::tc0480scp);
    a.set_tc0480scp_palette_bank_base(256U);
    a.set_tc0480scp_priority_model(
        taito_f2_video::tc0480scp_priority_model::deadconx_footchmp);
    a.write_sprite_bank_register(2U, 0U);
    a.write_priority_register(6U, 0x0042U);
    a.write_roz_control_register(2U, 0x0800U);
    a.write_tc0480scp_control_register(0x0FU, 0x0090U);
    a.set_roz_variant(taito_f2_video::roz_variant::tc0430grw);
    a.set_sprite_hide_pixels(3, -3);
    a.set_sprite_active_area_source(taito_f2_video::sprite_active_area_source::y_word_bit0);
    a.latch_sprites();
    a.set_sprite_buffer_policy(taito_f2_video::sprite_buffer_policy::partial_delayed_qzchikyu);
    a.tick(9U);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    taito_f2_video b;
    b.attach_sprite_gfx(sprite_gfx);
    b.attach_palette(palette);
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    CHECK(b.beam_dot() == 9U);
    CHECK(b.layer_control() == 0x2000U);
    CHECK(b.current_palette_format() == taito_f2_video::palette_format::rgbx_444);
    CHECK(b.current_tilemap_variant() == taito_f2_video::tilemap_variant::tc0480scp);
    CHECK(b.tc0480scp_palette_bank_base() == 256U);
    CHECK(b.current_tc0480scp_priority_model() ==
          taito_f2_video::tc0480scp_priority_model::deadconx_footchmp);
    CHECK(b.priority_register(6U) == 0x0042U);
    CHECK(b.roz_control_register(2U) == 0x0800U);
    CHECK(b.tc0480scp_control_register(0x0FU) == 0x0090U);
    CHECK(b.current_roz_variant() == taito_f2_video::roz_variant::tc0430grw);
    CHECK(b.sprite_hide_pixels() == 3);
    CHECK(b.sprite_flip_hide_pixels() == -3);
    CHECK(b.current_sprite_active_area_source() ==
          taito_f2_video::sprite_active_area_source::y_word_bit0);
    CHECK(b.current_sprite_buffer_policy() ==
          taito_f2_video::sprite_buffer_policy::partial_delayed_qzchikyu);
    render_one_frame(b);
    CHECK(b.framebuffer().pixels[6U * 320U + 4U] == 0xFFFFFFU);
}
