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

    // Palette with entry `index` set to a pure 5-bit red ramp value.
    [[nodiscard]] std::vector<std::uint8_t> make_palette(std::size_t index, std::uint8_t r5,
                                                         std::uint8_t g5, std::uint8_t b5) {
        std::vector<std::uint8_t> palette(0xC00U, 0U);
        palette[index] = r5;
        palette[index + 0x400U] = g5;
        palette[index + 0x800U] = b5;
        return palette;
    }

} // namespace

TEST_CASE("m72 video registers through the chip registry", "[m72_video]") {
    auto chip = mnemos::chips::create_chip("irem.m72_video");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::video);
}

TEST_CASE("m72 video frame timing fires vblank and raster callbacks", "[m72_video]") {
    irem_m72_video video;

    std::vector<std::uint32_t> vblank_lines;
    std::vector<std::uint32_t> raster_lines;
    video.set_vblank_callback([&](std::uint32_t line) { vblank_lines.push_back(line); });
    video.set_raster_callback([&](std::uint32_t line) { raster_lines.push_back(line); });
    video.set_raster_compare(100U);

    video.tick(frame_ticks);
    CHECK(video.frame_index() == 1U);
    REQUIRE(vblank_lines.size() == 1U);
    CHECK(vblank_lines[0] == irem_m72_video::visible_height);
    REQUIRE(raster_lines.size() == 1U);
    CHECK(raster_lines[0] == 100U);

    video.tick(frame_ticks);
    CHECK(video.frame_index() == 2U);
    CHECK(vblank_lines.size() == 2U);
    CHECK(video.beam_line() == 0U);
    CHECK(video.beam_dot() == 0U);
}

TEST_CASE("m72 video renders the back playfield through the palette", "[m72_video]") {
    irem_m72_video video;
    const auto tiles = make_tiles({5U});                  // tile 0: solid pixel value 5
    const std::vector<std::uint8_t> vram(map_bytes, 0U);  // all tile 0, color 0
    const auto palette = make_palette(5U, 0x1FU, 0U, 0U); // color index 5 = red

    video.attach_vram_b(vram);
    video.attach_tiles_b(tiles);
    video.attach_palette_a(palette);

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
    palette[5U] = 0x1FU;          // color 5: red
    palette[3U + 0x400U] = 0x1FU; // color 3: green

    video.attach_vram_b(back_vram);
    video.attach_tiles_b(back_tiles);
    video.attach_vram_a(front_vram);
    video.attach_tiles_a(front_tiles);
    video.attach_palette_a(palette);

    video.tick(frame_ticks);
    const auto frame = video.framebuffer();
    CHECK(frame.pixels[0] == 0x0000FF00U); // front tile 1 (green) wins at (0,0)
    CHECK(frame.pixels[8] == 0x00FF0000U); // front tile 0 transparent at (8,0)
}

TEST_CASE("m72 video honours scroll and flip attributes", "[m72_video]") {
    irem_m72_video video;
    // Tile 0: single pixel set at (0,0) only (plane 0, row 0, MSB).
    std::vector<std::uint8_t> tiles(4U * 8U, 0U);
    tiles[0] = 0x80U;
    std::vector<std::uint8_t> vram(map_bytes, 0U);
    const auto palette = make_palette(1U, 0x1FU, 0x1FU, 0x1FU); // pixel 1 = white

    video.attach_vram_b(vram);
    video.attach_tiles_b(tiles);
    video.attach_palette_a(palette);

    SECTION("flip-x mirrors the pixel to column 7") {
        std::vector<std::uint8_t> flipped = vram;
        flipped[3] = 0x01U; // map cell (0,0): flip-x
        video.attach_vram_b(flipped);
        video.tick(frame_ticks);
        CHECK(video.framebuffer().pixels[7] == 0x00FFFFFFU);
        CHECK(video.framebuffer().pixels[0] == 0x00000000U);
    }

    SECTION("scroll shifts the map under the viewport") {
        video.set_scroll_b(1U, 0U); // map x 1 appears at screen x 0
        video.tick(frame_ticks);
        // The (0,0) pixel of the next map cell's tile lands at screen x 7.
        CHECK(video.framebuffer().pixels[7] == 0x00FFFFFFU);
    }
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

    void set_sprite(std::vector<std::uint8_t>& ram, std::size_t index, std::uint16_t x,
                    std::uint16_t y, std::uint16_t code, std::uint8_t color,
                    std::uint8_t attributes) {
        const std::size_t base = index * 8U;
        ram[base + 0U] = static_cast<std::uint8_t>(y);
        ram[base + 1U] = static_cast<std::uint8_t>((y >> 8U) & 1U);
        ram[base + 2U] = static_cast<std::uint8_t>(code);
        ram[base + 3U] = static_cast<std::uint8_t>(code >> 8U);
        ram[base + 4U] = color;
        ram[base + 5U] = attributes;
        ram[base + 6U] = static_cast<std::uint8_t>(x);
        ram[base + 7U] = static_cast<std::uint8_t>((x >> 8U) & 1U);
    }

} // namespace

TEST_CASE("m72 video draws sprites through palette B above the playfields", "[m72_video]") {
    irem_m72_video video;
    // Back playfield: solid red everywhere.
    const auto back_tiles = make_tiles({5U});
    const std::vector<std::uint8_t> back_vram(map_bytes, 0U);
    std::vector<std::uint8_t> palette_a(0xC00U, 0U);
    palette_a[5U] = 0x1FU; // red
    // Sprite cell 0: solid pixel value 1 -> palette B entry 1 = blue.
    const auto sprites = make_sprites({1U});
    std::vector<std::uint8_t> palette_b(0xC00U, 0U);
    palette_b[1U + 0x800U] = 0x1FU; // blue
    std::vector<std::uint8_t> sprite_ram(0x400U, 0U);
    set_sprite(sprite_ram, 0U, 10U, 5U, 0U, 0U, 0U);

    video.attach_vram_b(back_vram);
    video.attach_tiles_b(back_tiles);
    video.attach_palette_a(palette_a);
    video.attach_palette_b(palette_b);
    video.attach_sprites(sprites);
    video.attach_sprite_ram(sprite_ram);

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

TEST_CASE("m72 video sprite flips and screen-edge clipping", "[m72_video]") {
    irem_m72_video video;
    // Cell 0: single pixel at (0,0) (plane 0, row 0, left byte, MSB).
    std::vector<std::uint8_t> sprites(4U * 32U, 0U);
    sprites[0] = 0x80U;
    std::vector<std::uint8_t> palette_b(0xC00U, 0U);
    palette_b[1U] = 0x1FU;          // pixel 1: red
    palette_b[1U + 0x400U] = 0x1FU; // + green
    palette_b[1U + 0x800U] = 0x1FU; // + blue = white
    std::vector<std::uint8_t> sprite_ram(0x400U, 0U);

    video.attach_sprites(sprites);
    video.attach_palette_b(palette_b);
    video.attach_sprite_ram(sprite_ram);

    SECTION("flip-x mirrors the pixel to column 15") {
        set_sprite(sprite_ram, 0U, 100U, 50U, 0U, 0U, 0x01U);
        video.tick(frame_ticks);
        const auto frame = video.framebuffer();
        CHECK(frame.pixels[50U * frame.effective_stride() + 115U] == 0x00FFFFFFU);
        CHECK(frame.pixels[50U * frame.effective_stride() + 100U] == 0x00000000U);
    }

    SECTION("a sprite hanging off the right edge clips cleanly") {
        set_sprite(sprite_ram, 0U, 380U, 50U, 0U, 0U, 0x01U); // flipped: pixel at x 395
        video.tick(frame_ticks);
        const auto frame = video.framebuffer();
        // The set pixel lands beyond column 383, so nothing is drawn.
        for (std::uint32_t x = 376U; x < irem_m72_video::visible_width; ++x) {
            CHECK(frame.pixels[50U * frame.effective_stride() + x] == 0U);
        }
    }
}

TEST_CASE("m72 video multi-block sprites take consecutive cells column-major", "[m72_video]") {
    irem_m72_video video;
    const auto sprites = make_sprites({1U, 2U}); // cell 0 solid 1, cell 1 solid 2
    std::vector<std::uint8_t> palette_b(0xC00U, 0U);
    palette_b[1U] = 0x1FU;          // entry 1: red
    palette_b[2U + 0x400U] = 0x1FU; // entry 2: green
    std::vector<std::uint8_t> sprite_ram(0x400U, 0U);
    // Height 2 blocks (attr bits 4-5 = 01): cell 0 on top, cell 1 below.
    set_sprite(sprite_ram, 0U, 0U, 0U, 0U, 0U, 0x10U);

    video.attach_sprites(sprites);
    video.attach_palette_b(palette_b);
    video.attach_sprite_ram(sprite_ram);

    video.tick(frame_ticks);
    const auto frame = video.framebuffer();
    CHECK(frame.pixels[0] == 0x00FF0000U);                              // cell 0
    CHECK(frame.pixels[16U * frame.effective_stride()] == 0x0000FF00U); // cell 1
    CHECK(frame.pixels[32U * frame.effective_stride()] == 0x00000000U); // below the sprite
}
