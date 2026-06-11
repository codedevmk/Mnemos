#include "irem_m72_video.hpp"

#include "chip_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::video::irem_m72_video;

    constexpr std::size_t map_bytes = 64U * 64U * 4U;
    constexpr std::uint64_t frame_ticks =
        static_cast<std::uint64_t>(irem_m72_video::line_pixels) * irem_m72_video::frame_lines;

    // Identity scroll values: beam x origin 64 and the 128-biased line space
    // cancel out, so map (0,0) lands at the visible origin.
    constexpr std::uint16_t scroll_x_identity = 512U - irem_m72_video::beam_x_origin;
    constexpr std::uint16_t scroll_y_identity = irem_m72_video::line_bias;

    // A tile ROM of `count` tiles where tile N is solid pixel-value
    // `values[N]` (4 sequential bitplane banks, MSB leftmost).
    [[nodiscard]] std::vector<std::uint8_t> make_tiles(const std::vector<std::uint8_t>& values) {
        const std::size_t plane_size = values.size() * 8U;
        std::vector<std::uint8_t> tiles(plane_size * 4U, 0U);
        for (std::size_t tile = 0; tile < values.size(); ++tile) {
            for (std::uint32_t plane = 0; plane < 4U; ++plane) {
                if ((values[tile] >> plane & 1U) != 0U) {
                    for (std::size_t row = 0; row < 8U; ++row) {
                        tiles[plane * plane_size + tile * 8U + row] = 0xFFU;
                    }
                }
            }
        }
        return tiles;
    }

    // Palette with entry `index` set to the given 5-bit guns.
    [[nodiscard]] std::vector<std::uint8_t> make_palette(std::size_t index, std::uint8_t r5,
                                                         std::uint8_t g5, std::uint8_t b5) {
        std::vector<std::uint8_t> palette(0xC00U, 0U);
        palette[index * 2U] = r5;
        palette[index * 2U + 0x400U] = g5;
        palette[index * 2U + 0x800U] = b5;
        return palette;
    }

} // namespace

TEST_CASE("m72 video registers through the chip registry", "[m72_video]") {
    auto chip = mnemos::chips::create_chip("irem.m72_video");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::video);
}

TEST_CASE("m72 video fires the scanline callback for every line", "[m72_video]") {
    irem_m72_video video;

    std::vector<std::uint32_t> vblank_lines;
    std::vector<std::uint32_t> scanlines;
    video.set_vblank_callback([&](std::uint32_t line) { vblank_lines.push_back(line); });
    video.set_scanline_callback([&](std::uint32_t line) { scanlines.push_back(line); });
    video.set_raster_compare(100);

    video.tick(frame_ticks);
    CHECK(video.frame_index() == 1U);
    REQUIRE(vblank_lines.size() == 1U);
    CHECK(vblank_lines[0] == irem_m72_video::visible_height);
    REQUIRE(scanlines.size() == irem_m72_video::frame_lines);
    CHECK(scanlines.front() == 0U);
    CHECK(scanlines.back() == irem_m72_video::frame_lines - 1U);
    CHECK(video.raster_compare_matches(100U));
    CHECK_FALSE(video.raster_compare_matches(99U));

    video.tick(frame_ticks);
    CHECK(video.frame_index() == 2U);
    CHECK(vblank_lines.size() == 2U);
    CHECK(video.beam_line() == 0U);
    CHECK(video.beam_dot() == 0U);
}

TEST_CASE("m72 video renders the back playfield through the tile palette", "[m72_video]") {
    irem_m72_video video;
    const auto tiles = make_tiles({5U});                  // tile 0: solid pixel value 5
    const std::vector<std::uint8_t> vram(map_bytes, 0U);  // all tile 0, color 0
    const auto palette = make_palette(5U, 0x1FU, 0U, 0U); // color index 5 = red

    video.attach_vram_b(vram);
    video.attach_tiles_b(tiles);
    video.attach_palette_b(palette); // playfields read palette B
    video.set_scroll_b(scroll_x_identity, scroll_y_identity);

    video.tick(frame_ticks);
    const auto frame = video.framebuffer();
    CHECK(frame.width == irem_m72_video::visible_width);
    CHECK(frame.height == irem_m72_video::visible_height);
    CHECK(frame.pixels[0] == 0x00FF0000U); // 5-bit 0x1F expands to 0xFF
}

TEST_CASE("m72 video front playfield overlays with pixel-0 transparency", "[m72_video]") {
    irem_m72_video video;
    // Back: solid pixel 5 (red). Front: tile 0 = transparent (value 0),
    // tile 1 = solid pixel 3 (green); the front map shows tile 1 only in its
    // top-left tile cell.
    const auto back_tiles = make_tiles({5U});
    const auto front_tiles = make_tiles({0U, 3U});
    std::vector<std::uint8_t> back_vram(map_bytes, 0U);
    std::vector<std::uint8_t> front_vram(map_bytes, 0U);
    front_vram[0] = 1U; // map cell (0,0): tile code 1

    std::vector<std::uint8_t> palette(0xC00U, 0U);
    palette[5U * 2U] = 0x1FU;          // color 5: red
    palette[3U * 2U + 0x400U] = 0x1FU; // color 3: green

    video.attach_vram_b(back_vram);
    video.attach_tiles_b(back_tiles);
    video.attach_vram_a(front_vram);
    video.attach_tiles_a(front_tiles);
    video.attach_palette_b(palette);
    video.set_scroll_a(scroll_x_identity, scroll_y_identity);
    video.set_scroll_b(scroll_x_identity, scroll_y_identity);

    video.tick(frame_ticks);
    const auto frame = video.framebuffer();
    CHECK(frame.pixels[0] == 0x0000FF00U); // front tile 1 (green) wins at (0,0)
    CHECK(frame.pixels[8] == 0x00FF0000U); // front tile 0 transparent at (8,0)
}

TEST_CASE("m72 video honours scroll and the code-word flip bits", "[m72_video]") {
    irem_m72_video video;
    // Tile 0: single pixel set at (0,0) only (plane 0, row 0, MSB).
    std::vector<std::uint8_t> tiles(4U * 8U, 0U);
    tiles[0] = 0x80U;
    std::vector<std::uint8_t> vram(map_bytes, 0U);
    const auto palette = make_palette(1U, 0x1FU, 0x1FU, 0x1FU); // pixel 1 = white

    video.attach_vram_b(vram);
    video.attach_tiles_b(tiles);
    video.attach_palette_b(palette);
    video.set_scroll_b(scroll_x_identity, scroll_y_identity);

    SECTION("flip-x (code word bit 14) mirrors the pixel to column 7") {
        std::vector<std::uint8_t> flipped = vram;
        flipped[1] = 0x40U; // map cell (0,0): code word bit 14
        video.attach_vram_b(flipped);
        video.tick(frame_ticks);
        CHECK(video.framebuffer().pixels[7] == 0x00FFFFFFU);
        CHECK(video.framebuffer().pixels[0] == 0x00000000U);
    }

    SECTION("scroll shifts the map under the viewport") {
        video.set_scroll_b(scroll_x_identity + 1U, scroll_y_identity);
        video.tick(frame_ticks);
        // The (0,0) pixel of the next map cell's tile lands at screen x 7.
        CHECK(video.framebuffer().pixels[7] == 0x00FFFFFFU);
    }
}

TEST_CASE("m72 video tile priority groups paint above sprites", "[m72_video]") {
    irem_m72_video video;
    // Front playfield group 2: pens 1-15 render above sprites. A solid
    // pen-3 front tile must beat a solid sprite on the same cell.
    const auto front_tiles = make_tiles({3U});
    std::vector<std::uint8_t> front_vram(map_bytes, 0U);
    // group = (word1 >> 6) & 3; word 1 low byte is entry byte 2.
    for (std::size_t entry = 0; entry < map_bytes; entry += 4U) {
        front_vram[entry + 2U] = 0x80U; // group 2
    }

    std::vector<std::uint8_t> sprites(4U * 32U, 0xFFU); // cell 0 solid pen 15
    std::vector<std::uint8_t> sprite_ram(0x400U, 0U);
    // Sprite at the visible origin: y = 384 - 0 - 16 = 0x170, x = 320.
    sprite_ram[0] = 0x70U;
    sprite_ram[1] = 0x01U;
    sprite_ram[6] = 0x40U;
    sprite_ram[7] = 0x01U;

    auto tile_palette = make_palette(3U, 0U, 0x1FU, 0U);    // green tile pen
    auto sprite_palette = make_palette(15U, 0x1FU, 0U, 0U); // red sprite pen

    video.attach_vram_a(front_vram);
    video.attach_tiles_a(front_tiles);
    video.attach_palette_b(tile_palette);
    video.attach_palette_a(sprite_palette);
    video.attach_sprites(sprites);
    video.attach_sprite_ram(sprite_ram);
    video.latch_sprites();
    video.set_scroll_a(scroll_x_identity, scroll_y_identity);

    video.tick(frame_ticks);
    // Group 2 front pens cover the sprite.
    CHECK(video.framebuffer().pixels[0] == 0x0000FF00U);
}

TEST_CASE("m72 video display disable blanks the frame", "[m72_video]") {
    irem_m72_video video;
    const auto tiles = make_tiles({5U});
    const std::vector<std::uint8_t> vram(map_bytes, 0U);
    const auto palette = make_palette(5U, 0x1FU, 0U, 0U);

    video.attach_vram_b(vram);
    video.attach_tiles_b(tiles);
    video.attach_palette_b(palette);
    video.set_scroll_b(scroll_x_identity, scroll_y_identity);

    video.set_display_enable(false);
    video.tick(frame_ticks);
    CHECK(video.framebuffer().pixels[0] == 0U);

    video.set_display_enable(true);
    video.tick(frame_ticks);
    CHECK(video.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("m72 video exposes the tile-sheet debug layer", "[m72_video]") {
    irem_m72_video video;
    const auto tiles = make_tiles({15U}); // one solid-white tile
    video.attach_tiles_a(tiles);

    const auto layers = video.introspection().debug_layers();
    REQUIRE(layers.size() == 1U);
    CHECK(layers[0]->name() == "tiles_a");
    const auto sheet = layers[0]->view();
    REQUIRE(sheet.pixels != nullptr);
    CHECK(sheet.width == 256U);
    CHECK(sheet.height == 8U);
    CHECK(sheet.pixels[0] == 0x00FFFFFFU); // pixel value 15 -> white
    CHECK(sheet.pixels[8] == 0x00000000U); // past the only tile
}

namespace {

    // A sprite ROM of 16x16 cells where cell N is solid pixel-value `values[N]`
    // (4 bitplane banks, 32 bytes per cell per plane).
    [[nodiscard]] std::vector<std::uint8_t> make_sprites(const std::vector<std::uint8_t>& values) {
        const std::size_t plane_size = values.size() * 32U;
        std::vector<std::uint8_t> sprites(plane_size * 4U, 0U);
        for (std::size_t cell = 0; cell < values.size(); ++cell) {
            for (std::uint32_t plane = 0; plane < 4U; ++plane) {
                if (((values[cell] >> plane) & 1U) != 0U) {
                    for (std::size_t b = 0; b < 32U; ++b) {
                        sprites[plane * plane_size + cell * 32U + b] = 0xFFU;
                    }
                }
            }
        }
        return sprites;
    }

    // Write a hardware-format entry placing the sprite's top-left at the
    // VISIBLE (x, y): w0 = 384 - vis_y - 16*h_blocks, w3 = vis_x + 320.
    void set_sprite(std::vector<std::uint8_t>& ram, std::size_t index, std::int32_t vis_x,
                    std::int32_t vis_y, std::uint16_t code, std::uint8_t color, bool flip_x,
                    bool flip_y, unsigned log2_w = 0U, unsigned log2_h = 0U) {
        const std::size_t base = index * 8U;
        const auto y9 = static_cast<std::uint16_t>((384 - vis_y - 16 * (1 << log2_h)) & 0x1FFU);
        const auto x10 = static_cast<std::uint16_t>(
            (vis_x + 256 + static_cast<std::int32_t>(irem_m72_video::beam_x_origin)) & 0x3FFU);
        const auto w2 =
            static_cast<std::uint16_t>((color & 0x0FU) | (flip_y ? 0x0400U : 0U) |
                                       (flip_x ? 0x0800U : 0U) | (log2_h << 12U) | (log2_w << 14U));
        ram[base + 0U] = static_cast<std::uint8_t>(y9);
        ram[base + 1U] = static_cast<std::uint8_t>(y9 >> 8U);
        ram[base + 2U] = static_cast<std::uint8_t>(code);
        ram[base + 3U] = static_cast<std::uint8_t>(code >> 8U);
        ram[base + 4U] = static_cast<std::uint8_t>(w2);
        ram[base + 5U] = static_cast<std::uint8_t>(w2 >> 8U);
        ram[base + 6U] = static_cast<std::uint8_t>(x10);
        ram[base + 7U] = static_cast<std::uint8_t>(x10 >> 8U);
    }

} // namespace

TEST_CASE("m72 video draws sprites through palette A above the back playfield", "[m72_video]") {
    irem_m72_video video;
    // Back playfield: solid red everywhere (tile palette = B).
    const auto back_tiles = make_tiles({5U});
    const std::vector<std::uint8_t> back_vram(map_bytes, 0U);
    auto palette_b = make_palette(5U, 0x1FU, 0U, 0U); // red
    // Sprite cell 0: solid pixel value 1 -> sprite palette (A) entry 1 = blue.
    const auto sprites = make_sprites({1U});
    auto palette_a = make_palette(1U, 0U, 0U, 0x1FU); // blue
    std::vector<std::uint8_t> sprite_ram(0x400U, 0U);
    set_sprite(sprite_ram, 0U, 10, 5, 0U, 0U, false, false);

    video.attach_vram_b(back_vram);
    video.attach_tiles_b(back_tiles);
    video.attach_palette_a(palette_a);
    video.attach_palette_b(palette_b);
    video.attach_sprites(sprites);
    video.attach_sprite_ram(sprite_ram);
    video.latch_sprites();
    video.set_scroll_b(scroll_x_identity, scroll_y_identity);

    video.tick(frame_ticks);
    const auto frame = video.framebuffer();
    const auto at = [&](std::uint32_t x, std::uint32_t y) {
        return frame.pixels[y * frame.effective_stride() + x];
    };
    CHECK(at(10U, 5U) == 0x000000FFU);  // sprite top-left
    CHECK(at(25U, 20U) == 0x000000FFU); // sprite bottom-right
    CHECK(at(0U, 0U) == 0x00FF0000U);   // playfield outside the sprite
    CHECK(at(26U, 5U) == 0x00FF0000U);  // one past the sprite's right edge
}

TEST_CASE("m72 video renders sprites only from the latched buffer", "[m72_video]") {
    irem_m72_video video;
    const auto sprites = make_sprites({1U});
    auto palette_a = make_palette(1U, 0x1FU, 0x1FU, 0x1FU);
    std::vector<std::uint8_t> sprite_ram(0x400U, 0U);
    set_sprite(sprite_ram, 0U, 0, 0, 0U, 0U, false, false);

    video.attach_sprites(sprites);
    video.attach_palette_a(palette_a);
    video.attach_sprite_ram(sprite_ram);

    // No DMA yet: nothing renders.
    video.tick(frame_ticks);
    CHECK(video.framebuffer().pixels[0] == 0U);

    video.latch_sprites();
    video.tick(frame_ticks);
    CHECK(video.framebuffer().pixels[0] == 0x00FFFFFFU);
}

TEST_CASE("m72 video sprite flips and screen-edge clipping", "[m72_video]") {
    irem_m72_video video;
    // Cell 0: single pixel at (0,0) (plane 0, row 0, left-half byte, MSB).
    std::vector<std::uint8_t> sprites(4U * 32U, 0U);
    sprites[0] = 0x80U;
    auto palette_a = make_palette(1U, 0x1FU, 0x1FU, 0x1FU); // pixel 1: white
    std::vector<std::uint8_t> sprite_ram(0x400U, 0U);

    video.attach_sprites(sprites);
    video.attach_palette_a(palette_a);
    video.attach_sprite_ram(sprite_ram);

    SECTION("flip-x mirrors the pixel to column 15") {
        set_sprite(sprite_ram, 0U, 100, 50, 0U, 0U, true, false);
        video.latch_sprites();
        video.tick(frame_ticks);
        const auto frame = video.framebuffer();
        CHECK(frame.pixels[50U * frame.effective_stride() + 115U] == 0x00FFFFFFU);
        CHECK(frame.pixels[50U * frame.effective_stride() + 100U] == 0x00000000U);
    }

    SECTION("a sprite hanging off the right edge clips cleanly") {
        set_sprite(sprite_ram, 0U, 380, 50, 0U, 0U, true, false); // flipped: pixel at x 395
        video.latch_sprites();
        video.tick(frame_ticks);
        const auto frame = video.framebuffer();
        // The set pixel lands beyond column 383, so nothing is drawn.
        for (std::uint32_t x = 376U; x < irem_m72_video::visible_width; ++x) {
            CHECK(frame.pixels[50U * frame.effective_stride() + x] == 0U);
        }
    }
}

TEST_CASE("m72 video multi-cell sprites step 8 codes per column, 1 per row", "[m72_video]") {
    irem_m72_video video;
    // Cells 0/1 stack vertically; a 2-wide sprite's right column starts at
    // code 8. Pen values: cell 0 -> 1 (red), cell 1 -> 2 (green), cell 8 ->
    // 3 (blue).
    std::vector<std::uint8_t> values(9U, 0U);
    values[0] = 1U;
    values[1] = 2U;
    values[8] = 3U;
    const auto sprites = make_sprites(values);
    std::vector<std::uint8_t> palette_a(0xC00U, 0U);
    palette_a[1U * 2U] = 0x1FU;          // entry 1: red
    palette_a[2U * 2U + 0x400U] = 0x1FU; // entry 2: green
    palette_a[3U * 2U + 0x800U] = 0x1FU; // entry 3: blue
    std::vector<std::uint8_t> sprite_ram(0x400U, 0U);
    // Width 2, height 2 at the visible origin.
    set_sprite(sprite_ram, 0U, 0, 0, 0U, 0U, false, false, 1U, 1U);

    video.attach_sprites(sprites);
    video.attach_palette_a(palette_a);
    video.attach_sprite_ram(sprite_ram);
    video.latch_sprites();

    video.tick(frame_ticks);
    const auto frame = video.framebuffer();
    CHECK(frame.pixels[0] == 0x00FF0000U);                              // cell 0
    CHECK(frame.pixels[16U * frame.effective_stride()] == 0x0000FF00U); // cell 1
    CHECK(frame.pixels[16U] == 0x000000FFU);                            // cell 8
    CHECK(frame.pixels[48U * frame.effective_stride()] == 0x00000000U); // below
}
