#include "taito_f2_video.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
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

    [[nodiscard]] std::uint16_t read_le16(std::span<const std::uint8_t> bytes,
                                          std::size_t off) {
        REQUIRE(off + 1U < bytes.size());
        return static_cast<std::uint16_t>(bytes[off + 0U]) |
               static_cast<std::uint16_t>(bytes[off + 1U] << 8U);
    }

    [[nodiscard]] std::int16_t read_le_i16(std::span<const std::uint8_t> bytes,
                                           std::size_t off) {
        return static_cast<std::int16_t>(read_le16(bytes, off));
    }

    [[nodiscard]] std::uint32_t read_le32(std::span<const std::uint8_t> bytes,
                                          std::size_t off) {
        REQUIRE(off + 3U < bytes.size());
        return static_cast<std::uint32_t>(bytes[off + 0U]) |
               (static_cast<std::uint32_t>(bytes[off + 1U]) << 8U) |
               (static_cast<std::uint32_t>(bytes[off + 2U]) << 16U) |
               (static_cast<std::uint32_t>(bytes[off + 3U]) << 24U);
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

    void program_text_left_column(std::vector<std::uint8_t>& rom, std::uint32_t base,
                                  std::uint8_t code) {
        const std::size_t char_base = static_cast<std::size_t>(base) +
                                      static_cast<std::size_t>(code) * 8U;
        for (std::size_t y = 0; y < 8U; ++y) {
            rom[char_base + y] = 0x80U;
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

    [[nodiscard]] std::uint64_t
    register_value(std::span<const mnemos::chips::register_descriptor> regs,
                   std::string_view name) {
        for (const mnemos::chips::register_descriptor& reg : regs) {
            if (reg.name == name) {
                return reg.value;
            }
        }
        FAIL("missing register " << name);
        return 0U;
    }

    [[nodiscard]] std::int16_t
    signed16_register_value(std::span<const mnemos::chips::register_descriptor> regs,
                            std::string_view name) {
        return static_cast<std::int16_t>(
            static_cast<std::uint16_t>(register_value(regs, name)));
    }

    void set_priority_registers(taito_f2_video& chip, std::uint16_t blend_mode) {
        chip.write_priority_register(0U, blend_mode);
        chip.write_priority_register(4U, 0x00F0U);
        chip.write_priority_register(5U, 0x0020U);
        chip.write_priority_register(6U, 0x0011U);
        chip.write_priority_register(7U, 0x0033U);
    }

    [[nodiscard]] std::uint16_t
    priority_source_mask(taito_f2_video::priority_source source) {
        return static_cast<std::uint16_t>(
            1U << static_cast<std::uint8_t>(source));
    }

    [[nodiscard]] mnemos::instrumentation::memory_view*
    find_memory_view(taito_f2_video& chip, std::string_view name) {
        for (mnemos::instrumentation::memory_view* view :
             chip.introspection().memory_views()) {
            if (view != nullptr && view->name() == name) {
                return view;
            }
        }
        FAIL("missing memory view " << name);
        return nullptr;
    }

    [[nodiscard]] std::span<const std::uint8_t>
    priority_record_at(taito_f2_video& chip, std::uint32_t x, std::uint32_t y) {
        mnemos::instrumentation::memory_view* view =
            find_memory_view(chip, "priority_decisions_v1");
        const std::span<const std::uint8_t> bytes = view->bytes();
        REQUIRE(bytes.size() == static_cast<std::size_t>(taito_f2_video::visible_width) *
                                    taito_f2_video::visible_height *
                                    taito_f2_video::priority_decision_record_bytes);
        const std::size_t offset =
            pixel_at(x, y) * taito_f2_video::priority_decision_record_bytes;
        return bytes.subspan(offset, taito_f2_video::priority_decision_record_bytes);
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
    CHECK(taito_f2_video::decode_color(
              taito_f2_video::palette_format::rrrr_gggg_bbbb_rgbx,
              0xF008U) == 0xFF0000U);
    CHECK(taito_f2_video::decode_color(
              taito_f2_video::palette_format::rrrr_gggg_bbbb_rgbx,
              0x0F04U) == 0x00FF00U);
    CHECK(taito_f2_video::decode_color(
              taito_f2_video::palette_format::rrrr_gggg_bbbb_rgbx,
              0x00F2U) == 0x0000FFU);
}

TEST_CASE("taito_f2_video exposes board variant and offset diagnostics",
          "[taito_f2_video][introspection]") {
    taito_f2_video chip;
    chip.set_palette_format(taito_f2_video::palette_format::rrrr_gggg_bbbb_rgbx);
    chip.set_tilemap_variant(taito_f2_video::tilemap_variant::dual_tc0100scn);
    chip.set_tc0100scn_offsets(taito_f2_video::tc0100scn_default_bg_x_offset,
                               taito_f2_video::tc0100scn_default_text_x_offset);
    chip.set_tc0100scn_text_y_origins(-12, 14);
    chip.set_sprite_mode(taito_f2_video::sprite_mode::extension_low_as_high);
    chip.set_sprite_active_area_source(taito_f2_video::sprite_active_area_source::y_word_bit0);
    chip.set_sprite_buffer_policy(taito_f2_video::sprite_buffer_policy::partial_delayed_qzchikyu);
    chip.set_tc0480scp_offsets(35, -4, 1, 2, -1, 3);
    chip.set_roz_variant(taito_f2_video::roz_variant::tc0430grw);
    chip.set_roz_offsets(-10, 16);
    chip.set_sprite_hide_pixels(3, -3);
    chip.set_text_gfx_source(taito_f2_video::text_gfx_source::program_1bpp);

    const auto regs = chip.register_snapshot();

    CHECK(register_value(regs, "TMVAR") ==
          static_cast<std::uint8_t>(taito_f2_video::tilemap_variant::dual_tc0100scn));
    CHECK(register_value(regs, "PALFMT") ==
          static_cast<std::uint8_t>(
              taito_f2_video::palette_format::rrrr_gggg_bbbb_rgbx));
    CHECK(register_value(regs, "SPRMODE") ==
          static_cast<std::uint8_t>(taito_f2_video::sprite_mode::extension_low_as_high));
    CHECK(register_value(regs, "SPRACTS") ==
          static_cast<std::uint8_t>(
              taito_f2_video::sprite_active_area_source::y_word_bit0));
    CHECK(register_value(regs, "SPRBUF") ==
          static_cast<std::uint8_t>(
              taito_f2_video::sprite_buffer_policy::partial_delayed_qzchikyu));
    CHECK(register_value(regs, "TXGSRC") ==
          static_cast<std::uint8_t>(taito_f2_video::text_gfx_source::program_1bpp));
    CHECK(signed16_register_value(regs, "SCNBGOX") ==
          taito_f2_video::tc0100scn_default_bg_x_offset);
    CHECK(signed16_register_value(regs, "SCNTXOX") ==
          taito_f2_video::tc0100scn_default_text_x_offset);
    CHECK(signed16_register_value(regs, "SCNTYOD") == -12);
    CHECK(signed16_register_value(regs, "SCNTYOP") == 14);
    CHECK(signed16_register_value(regs, "T48BGXO") == 35);
    CHECK(signed16_register_value(regs, "T48BGYO") == -4);
    CHECK(signed16_register_value(regs, "T48TXOX") == 1);
    CHECK(signed16_register_value(regs, "T48TXOY") == 2);
    CHECK(signed16_register_value(regs, "T48FLXO") == -1);
    CHECK(signed16_register_value(regs, "T48FLYO") == 3);
    CHECK(register_value(regs, "ROZVAR") ==
          static_cast<std::uint8_t>(taito_f2_video::roz_variant::tc0430grw));
    CHECK(signed16_register_value(regs, "ROZXOFF") == -10);
    CHECK(signed16_register_value(regs, "ROZYOFF") == 16);
    CHECK(signed16_register_value(regs, "SPRHIDE") == 3);
    CHECK(signed16_register_value(regs, "SPRFHIDE") == -3);
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

    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0xFF0000U);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, taito_f2_video::tc0100scn_text_y_origin)] ==
          0xFFFF00U);
}

TEST_CASE("taito_f2_video renders TC0100SCN program-region 1bpp text glyphs",
          "[taito_f2_video][tc0100scn]") {
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_text_tile(ram, 0U, 0U, 7U, 2U);
    std::vector<std::uint8_t> program(0x100U, 0U);
    program_text_left_column(program, 0U, 7U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 2U, 1U, 0x001FU);

    taito_f2_video chip;
    chip.attach_tile_ram(ram);
    chip.attach_text_gfx(program);
    chip.attach_palette(palette);
    chip.set_text_base(taito_f2_video::text_tilemap_base, 0U);
    chip.set_text_gfx_source(taito_f2_video::text_gfx_source::program_1bpp);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(0U, taito_f2_video::tc0100scn_text_y_origin)] ==
          0xFF0000U);
    CHECK(chip.framebuffer().pixels[pixel_at(1U, taito_f2_video::tc0100scn_text_y_origin)] ==
          0x000000U);
}

TEST_CASE("taito_f2_video applies TC0100SCN text vertical scroll origin",
          "[taito_f2_video][tc0100scn]") {
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    solid_text_char(ram, 7U, 3U);
    set_text_tile(ram, 4U, 0U, 7U, 2U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 2U, 3U, 0x03FFU);

    taito_f2_video chip;
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);
    chip.set_scroll2(0U, 0xFFE0U);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(0U, taito_f2_video::tc0100scn_text_y_origin)] ==
          0xFFFF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 64U)] == 0x000000U);
}

TEST_CASE("taito_f2_video wraps TC0100SCN RAM text at 32 rows",
          "[taito_f2_video][tc0100scn]") {
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    solid_text_char(ram, 7U, 3U);
    set_text_tile(ram, 0U, 0U, 7U, 2U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 2U, 3U, 0x03FFU);

    taito_f2_video chip;
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);
    chip.set_scroll2(0U, 0xFF00U);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(0U, taito_f2_video::tc0100scn_text_y_origin)] ==
          0xFFFF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 32U)] == 0x000000U);
}

TEST_CASE("taito_f2_video applies TC0100SCN board X offsets",
          "[taito_f2_video][tc0100scn]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 1U, 2U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg0_tilemap_base, 0U, 3U, 1U, 0U);
    solid_text_char(ram, 7U, 3U);
    set_text_tile(ram, 0U, 4U, 7U, 2U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 2U, 0x03E0U);
    set_pal(palette, 2U, 3U, 0x03FFU);

    taito_f2_video chip;
    chip.attach_tile_gfx(tiles);
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);
    chip.set_tc0100scn_offsets(taito_f2_video::tc0100scn_default_bg_x_offset,
                               taito_f2_video::tc0100scn_default_text_x_offset);
    chip.set_scroll0(0x0013U, 0U);
    chip.set_scroll2(0x0013U, 0U);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(21U, 0U)] == 0x00FF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(36U, taito_f2_video::tc0100scn_text_y_origin)] ==
          0xFFFF00U);
}

TEST_CASE("taito_f2_video keeps positive-scroll TC0100SCN title text visible",
          "[taito_f2_video][tc0100scn]") {
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    solid_text_char(ram, 7U, 3U);
    set_text_tile(ram, 25U, 4U, 7U, 2U);
    set_text_tile(ram, 27U, 8U, 7U, 2U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 2U, 3U, 0x03FFU);

    taito_f2_video chip;
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);
    chip.set_tc0100scn_offsets(taito_f2_video::tc0100scn_default_bg_x_offset,
                               taito_f2_video::tc0100scn_default_text_x_offset);
    chip.set_scroll2(0x0013U, 0x0008U);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(36U, 200U)] == 0xFFFF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(68U, 216U)] == 0xFFFF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(68U, 223U)] == 0xFFFF00U);
}

TEST_CASE("taito_f2_video applies explicit TC0100SCN positive text Y origin",
          "[taito_f2_video][tc0100scn]") {
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    solid_text_char(ram, 7U, 3U);
    set_text_tile(ram, 25U, 0U, 7U, 2U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 2U, 3U, 0x03FFU);

    taito_f2_video chip;
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);
    chip.set_scroll2(0U, 0x0008U);
    chip.set_tc0100scn_text_y_origins(taito_f2_video::tc0100scn_default_text_y_origin,
                                      0);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(0U, 200U)] == 0x000000U);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 208U)] == 0xFFFF00U);

    chip.set_tc0100scn_text_y_origins(taito_f2_video::tc0100scn_default_text_y_origin,
                                      taito_f2_video::tc0100scn_positive_text_y_origin);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(0U, 200U)] == 0xFFFF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 208U)] == 0x000000U);
}

TEST_CASE("taito_f2_video can pull positive-scroll title text into the visible frame",
          "[taito_f2_video][tc0100scn]") {
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    solid_text_char(ram, 7U, 3U);
    set_text_tile(ram, 29U, 4U, 7U, 2U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 2U, 3U, 0x03FFU);

    taito_f2_video chip;
    chip.attach_tile_ram(ram);
    chip.attach_palette(palette);
    chip.set_tc0100scn_offsets(taito_f2_video::tc0100scn_default_bg_x_offset,
                               taito_f2_video::tc0100scn_default_text_x_offset);
    chip.set_scroll2(0x0013U, 0x0008U);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(36U, 216U)] == 0x000000U);

    chip.set_tc0100scn_text_y_origins(taito_f2_video::tc0100scn_default_text_y_origin,
                                      24);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(36U, 216U)] == 0xFFFF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(36U, 223U)] == 0xFFFF00U);
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

TEST_CASE("taito_f2_video keeps TC0100SCN visual order separate from TC0360PRI",
          "[taito_f2_video][tc0100scn][priority]") {
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
    chip.write_priority_register(5U, 0x0015U); // BG0 sprite priority 5, BG1 priority 1.
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[0] == 0x00FF00U);
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

TEST_CASE("taito_f2_video applies TC0480SCP BG3 row zoom RAM",
          "[taito_f2_video][tc0480scp][rowzoom]") {
    std::vector<std::uint8_t> tiles(0x400U, 0U);
    split_tile16_lsb(tiles, 1U, 1U, 2U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tc0480scp_tile(ram, 0x3000U, 32U, 0U, 0U, 1U, 0U);
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

    set16(ram, 0x6400U, 0x0080U);
    chip.write_tc0480scp_control_register(0x0FU,
                                          taito_f2_video::tc0480scp_control_row_zoom_bg3);
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[pixel_at(31U, 0U)] == 0xFF0000U);
}

TEST_CASE("taito_f2_video applies TC0480SCP board offsets to BG and text planes",
          "[taito_f2_video][tc0480scp][offsets]") {
    SECTION("background offsets shift the sampled 16x16 tile column") {
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

        chip.set_tc0480scp_offsets(16, 0, 0, 0, 0, 0);
        render_one_frame(chip);
        CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x00FF00U);
    }

    SECTION("text offsets shift the sampled RAM text tile column") {
        std::vector<std::uint8_t> ram(0x10000U, 0U);
        solid_tc0480scp_text_char(ram, 7U, 3U);
        set_tc0480scp_text_tile(ram, 0U, 1U, 7U, 2U);
        std::vector<std::uint8_t> palette(0x4000U, 0U);
        set_pal(palette, 2U, 3U, 0x03FFU);

        taito_f2_video chip;
        chip.set_tilemap_variant(taito_f2_video::tilemap_variant::tc0480scp);
        chip.attach_tile_ram(ram);
        chip.attach_palette(palette);

        render_one_frame(chip);
        CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x000000U);

        chip.set_tc0480scp_offsets(0, 0, 8, 0, 0, 0);
        render_one_frame(chip);
        CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0xFFFF00U);
    }
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

TEST_CASE("taito_f2_video selects TC0360PRI ROZ priority nibbles from PRI1",
          "[taito_f2_video][roz][priority]") {
    const auto make_roz_priority_regs =
        [](std::uint8_t selector, std::uint8_t selected_priority,
           std::uint8_t other_priority) -> std::array<std::uint16_t, 2U> {
        std::array<std::uint16_t, 2U> regs{};
        for (std::uint8_t current = 0U; current < 4U; ++current) {
            const std::uint16_t priority =
                current == selector ? selected_priority : other_priority;
            regs[current / 2U] |=
                static_cast<std::uint16_t>(priority << (4U * (current & 1U)));
        }
        return regs;
    };

    const auto render_selector = [&make_roz_priority_regs](
                                     std::uint8_t selector,
                                     std::uint8_t selected_priority,
                                     std::uint8_t other_priority) -> std::uint32_t {
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
        chip.write_priority_register(1U, static_cast<std::uint16_t>(selector << 6U));
        chip.write_priority_register(5U, 0x0020U);
        const auto regs = make_roz_priority_regs(selector, selected_priority, other_priority);
        chip.write_priority_register(8U, regs[0]);
        chip.write_priority_register(9U, regs[1]);
        render_one_frame(chip);
        return chip.framebuffer().pixels[pixel_at(0U, 0U)];
    };

    for (std::uint8_t selector = 0U; selector < 4U; ++selector) {
        CHECK(render_selector(selector, 1U, 4U) == 0x00FF00U);
        CHECK(render_selector(selector, 4U, 1U) == 0x0000FFU);
    }
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

TEST_CASE("taito_f2_video exposes per-pixel priority decisions",
          "[taito_f2_video][priority][introspection]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 1U, 1U);
    solid_tile(tiles, 2U, 2U);
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg0_tilemap_base, 0U, 0U, 1U, 0U);
    set_tile(ram, taito_f2_video::bg0_tilemap_base, 0U, 3U, 1U, 0U);
    set_tile(ram, taito_f2_video::bg1_tilemap_base, 0U, 0U, 2U, 1U);
    set_tile(ram, taito_f2_video::bg1_tilemap_base, 0U, 3U, 2U, 1U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 0, 0, 1U, 0x0000U);
    set_sprite(sprites, 1U, 24, 0, 1U, 0x0080U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 1U, 0x001FU);
    set_pal(palette, 1U, 2U, 0x03E0U);
    set_pal(palette, 128U, 3U, 0x7C00U);
    set_pal(palette, 256U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_tile_gfx(tiles);
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_tile_ram(ram);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    const std::span<const std::uint8_t> lost = priority_record_at(chip, 0U, 0U);
    CHECK(lost[0] == static_cast<std::uint8_t>(
                         taito_f2_video::priority_source::tc0100scn_bg1));
    CHECK(lost[1] == 2U);
    CHECK(lost[2] == 1U);
    CHECK((lost[3] & taito_f2_video::priority_flag_sprite_occupied) != 0U);
    CHECK((lost[3] & taito_f2_video::priority_flag_sprite_rejected_by_priority) != 0U);
    CHECK((read_le16(lost, 4U) &
           priority_source_mask(taito_f2_video::priority_source::tc0100scn_bg0)) != 0U);
    CHECK((read_le16(lost, 4U) &
           priority_source_mask(taito_f2_video::priority_source::tc0100scn_bg1)) != 0U);
    CHECK((read_le16(lost, 4U) &
           priority_source_mask(taito_f2_video::priority_source::sprite)) != 0U);
    CHECK((read_le16(lost, 6U) &
           priority_source_mask(taito_f2_video::priority_source::sprite)) != 0U);
    CHECK(read_le16(lost, 8U) == 0x0012U);
    CHECK(lost[10] == static_cast<std::uint8_t>(taito_f2_video::priority_source::sprite));
    CHECK(lost[11] == 1U);

    const std::span<const std::uint8_t> won = priority_record_at(chip, 24U, 0U);
    CHECK(won[0] == static_cast<std::uint8_t>(
                        taito_f2_video::priority_source::sprite));
    CHECK(won[1] == 3U);
    CHECK(won[2] == 3U);
    CHECK((won[3] & taito_f2_video::priority_flag_sprite_occupied) != 0U);
    CHECK((read_le16(won, 4U) &
           priority_source_mask(taito_f2_video::priority_source::tc0100scn_bg1)) != 0U);
    CHECK((read_le16(won, 4U) &
           priority_source_mask(taito_f2_video::priority_source::sprite)) != 0U);
    CHECK((read_le16(won, 6U) &
           priority_source_mask(taito_f2_video::priority_source::sprite)) == 0U);
    CHECK(read_le16(won, 8U) == 0x1003U);
}

TEST_CASE("taito_f2_video treats nonwinning front sprite pixels as occupied",
          "[taito_f2_video][priority]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 2U, 2U);
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    set_tile(ram, taito_f2_video::bg1_tilemap_base, 0U, 0U, 2U, 1U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 0U, 0U, 1U, 0x0080U);
    set_sprite(sprites, 1U, 0U, 0U, 1U, 0x0000U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 1U, 2U, 0x03E0U);
    set_pal(palette, 256U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_tile_gfx(tiles);
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_tile_ram(ram);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    set_priority_registers(chip, 0x0000U);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x00FF00U);
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

TEST_CASE("taito_f2_video keeps priority-zero sprites behind the backdrop",
          "[taito_f2_video][priority]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 0U, 0U, 1U, 0x0000U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.write_priority_register(6U, 0x0000U);
    chip.write_priority_register(7U, 0x0033U);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0U);
}

TEST_CASE("taito_f2_video maps all TC0360PRI sprite priority groups",
          "[taito_f2_video][priority]") {
    std::vector<std::uint8_t> tiles(0x1000U, 0U);
    solid_tile(tiles, 2U, 2U);
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> ram(0x10000U, 0U);
    for (std::uint32_t col = 0U; col < 16U; ++col) {
        set_tile(ram, taito_f2_video::bg1_tilemap_base, 0U, col, 2U, 1U);
    }
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 0, 0, 1U, 0x0000U);
    set_sprite(sprites, 1U, 32, 0, 1U, 0x0040U);
    set_sprite(sprites, 2U, 64, 0, 1U, 0x0080U);
    set_sprite(sprites, 3U, 96, 0, 1U, 0x00C0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 1U, 2U, 0x03E0U);
    set_pal(palette, 128U, 3U, 0x7C00U);
    set_pal(palette, 192U, 3U, 0x7C00U);
    set_pal(palette, 256U, 3U, 0x7C00U);
    set_pal(palette, 320U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_tile_gfx(tiles);
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_tile_ram(ram);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.write_priority_register(4U, 0x00F0U);
    chip.write_priority_register(5U, 0x0020U);
    chip.write_priority_register(6U, 0x0031U);
    chip.write_priority_register(7U, 0x0040U);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x00FF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(32U, 0U)] == 0x0000FFU);
    CHECK(chip.framebuffer().pixels[pixel_at(64U, 0U)] == 0x00FF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(96U, 0U)] == 0x0000FFU);
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

TEST_CASE("taito_f2_video applies board-specific sprite palette bank bases",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 0U, 0U, 1U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 0U, 3U, 0x001FU);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.sprite_palette_bank_base() == 128U);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x0000FFU);

    chip.set_sprite_palette_bank_base(0U);
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0xFF0000U);
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

TEST_CASE("taito_f2_video exposes TC0200OBJ partial buffer byte-lane profile",
          "[taito_f2_video][introspection]") {
    struct buffer_case final {
        taito_f2_video::sprite_buffer_policy policy;
        std::uint16_t word_mask;
        std::uint16_t word_count;
        std::array<std::uint8_t, 6U> words;
    };

    const std::array cases{
        buffer_case{taito_f2_video::sprite_buffer_policy::partial_delayed,
                    0x0011U,
                    2U,
                    {0U, 4U, 0xFFU, 0xFFU, 0xFFU, 0xFFU}},
        buffer_case{taito_f2_video::sprite_buffer_policy::partial_delayed_thundfox,
                    0x0013U,
                    3U,
                    {0U, 1U, 4U, 0xFFU, 0xFFU, 0xFFU}},
        buffer_case{taito_f2_video::sprite_buffer_policy::partial_delayed_qzchikyu,
                    0x00F3U,
                    6U,
                    {0U, 1U, 4U, 5U, 6U, 7U}},
    };

    for (const buffer_case& test : cases) {
        std::vector<std::uint8_t> sprites(0x10000U, 0U);
        set_sprite(sprites, 0U, 4, 6, 1U, 0U);

        taito_f2_video chip;
        chip.set_sprite_buffer_policy(test.policy);
        chip.attach_sprite_ram(sprites);
        chip.latch_sprites();

        mnemos::instrumentation::memory_view* view =
            find_memory_view(chip, "sprite_buffer_state_v1");
        const std::span<const std::uint8_t> bytes = view->bytes();
        REQUIRE(bytes.size() == taito_f2_video::sprite_buffer_state_bytes);
        CHECK(bytes[0] == 1U);
        CHECK(bytes[1] == static_cast<std::uint8_t>(test.policy));
        CHECK(bytes[2] == 1U);
        CHECK(bytes[3] == 1U);
        CHECK(read_le16(bytes, 4U) == test.word_mask);
        CHECK(read_le16(bytes, 6U) == test.word_count);
        CHECK(read_le16(bytes, 8U) == taito_f2_video::sprite_entry_bytes / 2U);
        CHECK(bytes[10] == 1U);
        CHECK(bytes[11] == 0U);
        CHECK(bytes[12] == 1U);
        CHECK(bytes[13] == 1U);
        CHECK(read_le32(bytes, 16U) == taito_f2_video::sprite_buffer_bytes);
        CHECK(read_le32(bytes, 20U) == taito_f2_video::sprite_buffer_bytes);
        CHECK(read_le32(bytes, 24U) == taito_f2_video::sprite_buffer_bytes);
        CHECK(read_le32(bytes, 28U) ==
              taito_f2_video::sprite_buffer_bytes / taito_f2_video::sprite_entry_bytes);
        CHECK(read_le32(bytes, 32U) ==
              (taito_f2_video::sprite_buffer_bytes / taito_f2_video::sprite_entry_bytes) *
                  test.word_count);
        CHECK(read_le32(bytes, 36U) ==
              (taito_f2_video::sprite_buffer_bytes /
               taito_f2_video::sprite_entry_bytes) *
                  test.word_count * 2U);
        CHECK(read_le32(bytes, 40U) == taito_f2_video::sprite_buffer_bytes);
        CHECK(read_le32(bytes, 44U) == taito_f2_video::sprite_buffer_bytes);
        for (std::size_t i = 0U; i < test.words.size(); ++i) {
            CHECK(bytes[48U + i] == test.words[i]);
        }
    }
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

TEST_CASE("taito_f2_video renders non-empty TC0200OBJ code zero cells",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 0U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 4, 6, 0U, 0U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[6U * 320U + 4U] == 0x0000FFU);
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
    CHECK(chip.sprite_bank(0U) == 0x0000U);
    CHECK(chip.framebuffer().pixels[0] == 0xFF0000U);

    chip.latch_sprites();
    render_one_frame(chip);
    CHECK(chip.sprite_bank(0U) == 0x0800U);
    CHECK(chip.framebuffer().pixels[0] == 0x00FF00U);
}

TEST_CASE("taito_f2_video exposes decoded TC0200OBJ object sidecar",
          "[taito_f2_video][introspection]") {
    std::vector<std::uint8_t> sprite_gfx((0x802U * 128U), 0U);
    solid_sprite(sprite_gfx, 0x801U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 20, 12, 0x0801U, 0x0082U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U + 0x82U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    set_priority_registers(chip, 0x0040U);
    chip.write_sprite_bank_register(2U, 1U);
    chip.latch_sprites();
    render_one_frame(chip);

    mnemos::instrumentation::memory_view* view =
        find_memory_view(chip, "decoded_sprite_objects_v1");
    const std::span<const std::uint8_t> bytes = view->bytes();
    REQUIRE(bytes.size() == taito_f2_video::decoded_sprite_object_record_bytes);
    CHECK(read_le16(bytes, 0U) == 0x0000U);
    CHECK(read_le16(bytes, 2U) == 0x0801U);
    CHECK(read_le16(bytes, 4U) == 0x0000U);
    CHECK(read_le16(bytes, 6U) == sprite_screen_x(20));
    CHECK(read_le16(bytes, 8U) == sprite_screen_y(12));
    CHECK(read_le16(bytes, 10U) == 0x0082U);
    CHECK(read_le16(bytes, 12U) == 0x0000U);
    CHECK(read_le16(bytes, 14U) == 128U + 0x82U);
    CHECK(read_le32(bytes, 16U) == 0x0801U);
    CHECK(read_le_i16(bytes, 20U) == 20);
    CHECK(read_le_i16(bytes, 22U) == 12);
    CHECK(read_le16(bytes, 24U) == 16U);
    CHECK(read_le16(bytes, 26U) == 16U);
    CHECK(read_le_i16(bytes, 28U) == -static_cast<int>(taito_f2_video::sprite_screen_x_bias));
    CHECK(read_le_i16(bytes, 30U) == 0);
    CHECK(bytes[32] == 2U);
    CHECK(bytes[33] == 3U);
    CHECK(bytes[34] == 0U);
    CHECK(bytes[35] == 0x40U);
    CHECK(read_le16(bytes, 36U) == 0x0000U);
}

TEST_CASE("taito_f2_video exposes TC0200OBJ control marker sidecar",
          "[taito_f2_video][introspection]") {
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite_control(sprites, 0U, 0x3001U);
    set16(sprites, taito_f2_video::sprite_active_area_stride + 6U, 1U);
    const std::size_t stride_index =
        taito_f2_video::sprite_active_area_stride / taito_f2_video::sprite_entry_bytes;
    set_sprite_raw(sprites, stride_index + 1U, 0xA00CU, 0x0002U, 0U, 0U);
    set_sprite_raw(sprites, stride_index + 2U, 0x5008U, 0x0003U, 0U, 0U);

    taito_f2_video chip;
    chip.attach_sprite_ram(sprites);
    chip.set_sprite_active_area_source(
        taito_f2_video::sprite_active_area_source::control_word_bit0);
    chip.set_sprite_hide_pixels(7, 11);
    chip.set_sprite_palette_bank_base(0x0123U);
    chip.latch_sprites();

    mnemos::instrumentation::memory_view* view =
        find_memory_view(chip, "sprite_control_state_v1");
    const std::span<const std::uint8_t> bytes = view->bytes();
    REQUIRE(bytes.size() == taito_f2_video::sprite_control_state_bytes);
    CHECK(bytes[0] == 1U);
    CHECK(bytes[1] == static_cast<std::uint8_t>(taito_f2_video::sprite_mode::standard));
    CHECK(bytes[2] == static_cast<std::uint8_t>(
                          taito_f2_video::sprite_active_area_source::control_word_bit0));
    CHECK(bytes[3] ==
          static_cast<std::uint8_t>(taito_f2_video::sprite_buffer_policy::immediate));
    CHECK(read_le32(bytes, 4U) == taito_f2_video::sprite_active_area_stride);
    CHECK(bytes[8] == 1U);
    CHECK(bytes[9] == 1U);
    CHECK(bytes[10] == 1U);
    CHECK(bytes[11] == 0U);
    CHECK(read_le_i16(bytes, 12U) == 12);
    CHECK(read_le_i16(bytes, 14U) == 2);
    CHECK(read_le32(bytes, 16U) == 1U);
    CHECK(read_le32(bytes, 20U) == 1U);
    CHECK(read_le32(bytes, 24U) == 1U);
    CHECK(read_le32(bytes, 28U) == 1U);
    CHECK(read_le32(bytes, 32U) == 1U);
    CHECK(read_le32(bytes, 36U) == 1U);
    CHECK(read_le32(bytes, 40U) > 0U);
    CHECK(read_le16(bytes, 44U) == 0x8000U);
    CHECK(read_le16(bytes, 46U) == 0x3001U);
    CHECK(read_le16(bytes, 48U) == 0xA00CU);
    CHECK(read_le16(bytes, 50U) == 0x0002U);
    CHECK(read_le16(bytes, 52U) == 0x5008U);
    CHECK(read_le16(bytes, 54U) == 0x0003U);
    CHECK(read_le_i16(bytes, 56U) == 7);
    CHECK(read_le_i16(bytes, 58U) == 11);
    CHECK(read_le16(bytes, 60U) == 0x0123U);
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

TEST_CASE("taito_f2_video treats zero TC0200OBJ records as sequence terminators",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 1U);
    solid_sprite(sprite_gfx, 2U, 2U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 24U, 12U, 1U, 0x0800U);
    set_sprite(sprites, 2U, 0U, 0U, 2U, 0xC000U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 1U, 0x001FU);
    set_pal(palette, 128U, 2U, 0x03E0U);

    taito_f2_video chip;
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);
    chip.latch_sprites();
    render_one_frame(chip);

    CHECK(chip.framebuffer().pixels[pixel_at(24U, 12U)] == 0xFF0000U);
    CHECK(chip.framebuffer().pixels[pixel_at(0U, 0U)] == 0x00FF00U);
    CHECK(chip.framebuffer().pixels[pixel_at(40U, 12U)] == 0x000000U);
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

TEST_CASE("taito_f2_video ignores a banked marker that points at a blank object area",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite_control(sprites, 0U, 0x0000U);
    set_sprite(sprites, 1U, 4U, 6U, 1U, 0U);
    set_sprite_control(sprites, 2U, 0x0001U);
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

    chip.latch_sprites();
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[6U * 320U + 4U] == 0x0000FFU);
}

TEST_CASE("taito_f2_video keeps banked no-active-area disable markers list-local",
          "[taito_f2_video]") {
    std::vector<std::uint8_t> sprite_gfx(0x2000U, 0U);
    solid_sprite(sprite_gfx, 1U, 3U);
    std::vector<std::uint8_t> sprites(0x10000U, 0U);
    set_sprite(sprites, 0U, 4U, 6U, 1U, 0U);
    set_sprite_control(sprites, 1U, 0x1000U);
    std::vector<std::uint8_t> palette(0x4000U, 0U);
    set_pal(palette, 128U, 3U, 0x7C00U);

    taito_f2_video chip;
    chip.set_sprite_mode(taito_f2_video::sprite_mode::banked);
    chip.set_sprite_active_area_source(taito_f2_video::sprite_active_area_source::none);
    chip.attach_sprite_gfx(sprite_gfx);
    chip.attach_sprite_ram(sprites);
    chip.attach_palette(palette);

    chip.latch_sprites();
    render_one_frame(chip);
    CHECK(chip.framebuffer().pixels[6U * 320U + 4U] == 0x0000FFU);

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
    set_pal(palette, 0U, 4U, 0xFFF0U);

    taito_f2_video a;
    a.attach_sprite_gfx(sprite_gfx);
    a.attach_sprite_ram(sprites);
    a.attach_palette(palette);
    a.set_scroll0(12U, 34U);
    a.set_layer_control(0x2000U);
    a.set_palette_format(taito_f2_video::palette_format::rgbx_444);
    a.set_tilemap_variant(taito_f2_video::tilemap_variant::tc0480scp);
    a.set_tc0480scp_palette_bank_base(256U);
    a.set_sprite_palette_bank_base(0U);
    a.set_tc0480scp_priority_model(
        taito_f2_video::tc0480scp_priority_model::deadconx_footchmp);
    a.write_sprite_bank_register(2U, 0U);
    a.write_priority_register(6U, 0x0042U);
    a.write_roz_control_register(2U, 0x0800U);
    a.write_tc0480scp_control_register(0x0FU, 0x0090U);
    a.set_roz_variant(taito_f2_video::roz_variant::tc0430grw);
    a.set_sprite_hide_pixels(3, -3);
    a.set_tc0100scn_text_y_origins(-12, 14);
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
    CHECK(b.sprite_palette_bank_base() == 0U);
    CHECK(b.current_tc0480scp_priority_model() ==
          taito_f2_video::tc0480scp_priority_model::deadconx_footchmp);
    CHECK(b.priority_register(6U) == 0x0042U);
    CHECK(b.roz_control_register(2U) == 0x0800U);
    CHECK(b.tc0480scp_control_register(0x0FU) == 0x0090U);
    CHECK(b.current_roz_variant() == taito_f2_video::roz_variant::tc0430grw);
    CHECK(b.sprite_hide_pixels() == 3);
    CHECK(b.sprite_flip_hide_pixels() == -3);
    CHECK(b.tc0100scn_text_y_origin_offset() == -12);
    CHECK(b.tc0100scn_positive_text_y_origin_offset() == 14);
    CHECK(b.current_sprite_active_area_source() ==
          taito_f2_video::sprite_active_area_source::y_word_bit0);
    CHECK(b.current_sprite_buffer_policy() ==
          taito_f2_video::sprite_buffer_policy::partial_delayed_qzchikyu);
    render_one_frame(b);
    CHECK(b.framebuffer().pixels[6U * 320U + 4U] == 0xFFFFFFU);
}
