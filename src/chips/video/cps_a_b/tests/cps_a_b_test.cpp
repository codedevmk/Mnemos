// cps_a_b skeleton: the beam, the backdrop clear, and the CPS1 brightness
// colour decode. The decode reference values are hand-computed from the
// hardware formula (intensity = 0x0F + 2*brightness, gun*0x11*intensity/0x2D).

#include "cps_a_b.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::video::cps_a_b;

    // A 16 KB palette with the backdrop word (entry 0xBFF, byte offset 0x17FE)
    // set big-endian.
    std::array<std::uint8_t, 0x4000> make_palette(std::uint16_t backdrop_entry) {
        std::array<std::uint8_t, 0x4000> pal{};
        constexpr std::size_t off = 0xBFFU * 2U;
        pal[off] = static_cast<std::uint8_t>(backdrop_entry >> 8U);
        pal[off + 1U] = static_cast<std::uint8_t>(backdrop_entry & 0xFFU);
        return pal;
    }

    // Tick exactly one whole frame so the beam crosses the vblank line once.
    void render_one_frame(cps_a_b& chip) {
        chip.tick(static_cast<std::uint64_t>(cps_a_b::frame_lines) * cps_a_b::line_pixels);
    }

    // Fill a tile's planar gfx bytes so every pixel decodes to `pen`: the four
    // consecutive planar bytes of each 8-pixel group carry the pen's bit.
    void carve_solid_tile(std::vector<std::uint8_t>& gfx, std::size_t base, std::size_t bytes,
                          std::uint8_t pen) {
        for (std::size_t i = 0; i < bytes && base + i < gfx.size(); ++i) {
            gfx[base + i] = ((pen >> (i & 3U)) & 1U) != 0U ? 0xFFU : 0x00U;
        }
    }

    // Fill a name-table region with a fixed (code, attr) entry (big-endian).
    void fill_nametable(std::vector<std::uint8_t>& tile_ram, std::uint32_t base,
                        std::size_t entries, std::uint16_t code, std::uint16_t attr) {
        for (std::size_t i = 0; i < entries; ++i) {
            const std::size_t off = base + i * 4U;
            if (off + 3U >= tile_ram.size()) {
                break;
            }
            tile_ram[off + 0U] = static_cast<std::uint8_t>(code >> 8U);
            tile_ram[off + 1U] = static_cast<std::uint8_t>(code & 0xFFU);
            tile_ram[off + 2U] = static_cast<std::uint8_t>(attr >> 8U);
            tile_ram[off + 3U] = static_cast<std::uint8_t>(attr & 0xFFU);
        }
    }

    // Set one scroll2 (16x16) name-table cell, addressed via the hardware index.
    void set_scroll2_cell(std::vector<std::uint8_t>& tile_ram, std::uint32_t base,
                          std::uint32_t row, std::uint32_t col, std::uint16_t code) {
        const std::uint32_t idx = (row & 0x0FU) + ((col & 0x3FU) << 4U) + ((row & 0x30U) << 6U);
        const std::size_t off = base + idx * 4U;
        if (off + 3U >= tile_ram.size()) {
            return;
        }
        tile_ram[off + 0U] = static_cast<std::uint8_t>(code >> 8U);
        tile_ram[off + 1U] = static_cast<std::uint8_t>(code & 0xFFU);
    }

    // Set palette entry (pal_num*16 + pen) to a 16-bit big-endian word.
    void set_pal(std::array<std::uint8_t, 0x4000>& pal, std::uint16_t pal_num, std::uint8_t pen,
                 std::uint16_t value) {
        const std::size_t off = (static_cast<std::size_t>(pal_num) * 16U + pen) * 2U;
        pal[off] = static_cast<std::uint8_t>(value >> 8U);
        pal[off + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
    }

    // Write one 8-byte object entry (big-endian x, y, code, attr).
    void set_sprite(std::vector<std::uint8_t>& obj, std::size_t index, std::uint16_t x,
                    std::uint16_t y, std::uint16_t code, std::uint16_t attr) {
        const std::size_t off = index * 8U;
        if (off + 7U >= obj.size()) {
            return;
        }
        obj[off + 0U] = static_cast<std::uint8_t>(x >> 8U);
        obj[off + 1U] = static_cast<std::uint8_t>(x & 0xFFU);
        obj[off + 2U] = static_cast<std::uint8_t>(y >> 8U);
        obj[off + 3U] = static_cast<std::uint8_t>(y & 0xFFU);
        obj[off + 4U] = static_cast<std::uint8_t>(code >> 8U);
        obj[off + 5U] = static_cast<std::uint8_t>(code & 0xFFU);
        obj[off + 6U] = static_cast<std::uint8_t>(attr >> 8U);
        obj[off + 7U] = static_cast<std::uint8_t>(attr & 0xFFU);
    }

} // namespace

TEST_CASE("cps_a_b registers in the chip factory as a video chip", "[cps_a_b][video]") {
    auto chip = mnemos::chips::create_chip("capcom.cps_a_b");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == mnemos::chips::chip_class::video);
    REQUIRE(chip->metadata().manufacturer == "Capcom");
}

TEST_CASE("cps_a_b decodes palette words with the brightness curve", "[cps_a_b][video]") {
    // Full brightness (intensity 0x2D) maps a gun nibble straight to 8 bits.
    REQUIRE(cps_a_b::decode_color(0x0000) == 0x000000U);
    REQUIRE(cps_a_b::decode_color(0xFFFF) == 0xFFFFFFU); // i=F, all guns F -> white
    REQUIRE(cps_a_b::decode_color(0xFF00) == 0xFF0000U); // full-bright red
    REQUIRE(cps_a_b::decode_color(0xF0F0) == 0x00FF00U); // full-bright green
    REQUIRE(cps_a_b::decode_color(0xF00F) == 0x0000FFU); // full-bright blue
    // Brightness 0 (intensity 0x0F): gun 0xF -> 0xF*0x11*0x0F/0x2D = 85.
    REQUIRE(cps_a_b::decode_color(0x0FFF) == 0x555555U);
    REQUIRE(cps_a_b::decode_color(0x0F00) == 0x550000U);
    // Mid brightness 8 (intensity 0x1F): gun 8 -> 8*0x11*0x1F/0x2D = 93.
    REQUIRE(cps_a_b::decode_color(0x8888) == 0x5D5D5DU);
}

TEST_CASE("cps_a_b clears the frame to the decoded backdrop pen", "[cps_a_b][video]") {
    const auto palette = make_palette(0xFF00); // full-bright red backdrop
    cps_a_b chip;
    chip.attach_palette(palette);

    REQUIRE(chip.frame_index() == 0U);
    render_one_frame(chip);
    REQUIRE(chip.frame_index() == 1U);

    const auto fb = chip.framebuffer();
    REQUIRE(fb.width == 384U);
    REQUIRE(fb.height == 224U);
    bool all_red = true;
    for (std::uint32_t i = 0; i < fb.width * fb.height; ++i) {
        if (fb.pixels[i] != 0xFF0000U) {
            all_red = false;
            break;
        }
    }
    REQUIRE(all_red);
}

TEST_CASE("cps_a_b blanks to black when display is disabled", "[cps_a_b][video]") {
    const auto palette = make_palette(0xFF00);
    cps_a_b chip;
    chip.attach_palette(palette);
    chip.set_display_enable(false);

    render_one_frame(chip);
    const auto fb = chip.framebuffer();
    for (std::uint32_t i = 0; i < fb.width * fb.height; ++i) {
        INFO("pixel " << i);
        REQUIRE(fb.pixels[i] == 0x000000U);
    }
}

TEST_CASE("cps_a_b fires per-line and one vblank callback per frame", "[cps_a_b][video]") {
    cps_a_b chip;
    std::uint32_t lines = 0U;
    std::uint32_t vblanks = 0U;
    std::uint32_t vblank_line = 0xFFFFFFFFU;
    chip.set_scanline_callback([&lines](std::uint32_t) { ++lines; });
    chip.set_vblank_callback([&vblanks, &vblank_line](std::uint32_t line) {
        ++vblanks;
        vblank_line = line;
    });

    render_one_frame(chip);
    REQUIRE(lines == cps_a_b::frame_lines);
    REQUIRE(vblanks == 1U);
    REQUIRE(vblank_line == cps_a_b::vblank_start);
}

TEST_CASE("cps_a_b raster-compare matches only the programmed line", "[cps_a_b][video]") {
    cps_a_b chip;
    chip.set_raster_compare(100);
    REQUIRE(chip.raster_compare_matches(100U));
    REQUIRE_FALSE(chip.raster_compare_matches(99U));
    REQUIRE_FALSE(chip.raster_compare_matches(101U));
}

TEST_CASE("cps_a_b save/load round-trips beam, registers, and display state", "[cps_a_b][video]") {
    const auto palette = make_palette(0xFF00);
    cps_a_b a;
    a.attach_palette(palette);
    a.set_layer_control(0x1234);
    a.set_raster_compare(57);
    a.set_display_enable(false);
    a.tick(3U * cps_a_b::line_pixels + 17U); // partway into line 3

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    cps_a_b b;
    b.attach_palette(palette); // spans are host-owned: re-attach on load
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    REQUIRE(b.beam_line() == a.beam_line());
    REQUIRE(b.beam_dot() == a.beam_dot());
    REQUIRE(b.frame_index() == a.frame_index());
    REQUIRE(b.layer_control() == 0x1234U);
    REQUIRE(b.raster_compare_matches(57U));

    // Display-disabled state survived: the restored chip still blanks to black.
    render_one_frame(b);
    REQUIRE(b.framebuffer().pixels[0] == 0x000000U);
}

TEST_CASE("cps_a_b draws the scroll2 16x16 playfield", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU); // all pen 15 (transparent)
    carve_solid_tile(gfx, 1U * 128U, 128U, 1U);     // scroll2 tile code 1 -> pen 1
    std::vector<std::uint8_t> tile_ram(0x30000U, 0U);
    fill_nametable(tile_ram, 0U, 4096U, 1U, 0U); // name table -> tile 1
    auto pal = make_palette(0x000FU);            // blue-ish backdrop
    set_pal(pal, 64U, 1U, 0xFF00U);              // scroll2 bank 0 pen 1 -> red

    cps_a_b chip;
    chip.attach_gfx(gfx);
    chip.attach_tile_ram(tile_ram);
    chip.attach_palette(pal);
    chip.set_scroll2_base(0U);
    render_one_frame(chip);

    const auto fb = chip.framebuffer();
    // scroll1 reads code 1 at the 8x8 offset (64, still 0xFF) and scroll3 at the
    // 32x32 offset (512, 0xFF) -- both transparent -- so only scroll2 paints.
    REQUIRE(fb.pixels[0] == 0xFF0000U);
    REQUIRE(fb.pixels[120U * 384U + 200U] == 0xFF0000U);
    REQUIRE(fb.pixels[fb.width * fb.height - 1U] == 0xFF0000U);
}

TEST_CASE("cps_a_b draws the scroll1 8x8 playfield", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 64U, 64U, 2U); // scroll1 tile 1 fills the 16-wide cell
    std::vector<std::uint8_t> tile_ram(0x30000U, 0U);
    fill_nametable(tile_ram, 0U, 4096U, 1U, 0U);
    auto pal = make_palette(0x000FU);
    set_pal(pal, 32U, 2U, 0xF0F0U); // scroll1 bank 0 pen 2 -> green

    cps_a_b chip;
    chip.attach_gfx(gfx);
    chip.attach_tile_ram(tile_ram);
    chip.attach_palette(pal);
    chip.set_scroll1_base(0U);
    render_one_frame(chip);

    REQUIRE(chip.framebuffer().pixels[0] == 0x00FF00U);
    REQUIRE(chip.framebuffer().pixels[100U * 384U + 50U] == 0x00FF00U);
}

TEST_CASE("cps_a_b draws the scroll3 32x32 playfield", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 512U, 512U, 3U);
    std::vector<std::uint8_t> tile_ram(0x30000U, 0U);
    fill_nametable(tile_ram, 0U, 4096U, 1U, 0U);
    auto pal = make_palette(0x000FU);
    set_pal(pal, 96U, 3U, 0xF00FU); // scroll3 bank 0 pen 3 -> blue

    cps_a_b chip;
    chip.attach_gfx(gfx);
    chip.attach_tile_ram(tile_ram);
    chip.attach_palette(pal);
    chip.set_scroll3_base(0U);
    render_one_frame(chip);

    REQUIRE(chip.framebuffer().pixels[0] == 0x0000FFU);
}

TEST_CASE("cps_a_b shows the backdrop where every tile pen is transparent", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x1000U, 0xFFU); // pen 15 everywhere
    std::vector<std::uint8_t> tile_ram(0x30000U, 0U);
    fill_nametable(tile_ram, 0U, 4096U, 1U, 0U);
    auto pal = make_palette(0xFF00U); // red backdrop

    cps_a_b chip;
    chip.attach_gfx(gfx);
    chip.attach_tile_ram(tile_ram);
    chip.attach_palette(pal);
    render_one_frame(chip);

    REQUIRE(chip.framebuffer().pixels[0] == 0xFF0000U);
    REQUIRE(chip.framebuffer().pixels[500U] == 0xFF0000U);
}

TEST_CASE("cps_a_b selects the palette bank from the tile attribute", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 128U, 128U, 1U);
    std::vector<std::uint8_t> tile_ram(0x30000U, 0U);
    fill_nametable(tile_ram, 0U, 4096U, 1U, 0x0003U); // attr low bits -> palette bank 3
    auto pal = make_palette(0x000FU);
    set_pal(pal, 64U + 3U, 1U, 0xF0F0U); // scroll2 bank 3 pen 1 -> green

    cps_a_b chip;
    chip.attach_gfx(gfx);
    chip.attach_tile_ram(tile_ram);
    chip.attach_palette(pal);
    chip.set_scroll2_base(0U);
    render_one_frame(chip);

    REQUIRE(chip.framebuffer().pixels[0] == 0x00FF00U);
}

TEST_CASE("cps_a_b scroll2 row-scroll shifts a single line horizontally", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 128U, 128U, 1U); // tile 1 -> pen 1 (red); code 0 stays transparent
    std::vector<std::uint8_t> tile_ram(0x30000U, 0U);
    // A single red column at scroll2 col 10 across the visible rows.
    for (std::uint32_t row = 0; row < 16U; ++row) {
        set_scroll2_cell(tile_ram, 0U, row, 10U, 1U);
    }
    // Row-scroll table at 0x20000: only the first visible line (screen_y 16)
    // shifts by +16 pixels (one tile); all other lines read 0.
    const std::uint32_t rowscroll_base = 0x20000U;
    tile_ram[rowscroll_base + 16U * 2U + 0U] = 0x00U;
    tile_ram[rowscroll_base + 16U * 2U + 1U] = 0x10U; // line_scroll = 16

    auto pal = make_palette(0x000FU); // backdrop 0x000055
    set_pal(pal, 64U, 1U, 0xFF00U);   // red

    cps_a_b chip;
    chip.attach_gfx(gfx);
    chip.attach_tile_ram(tile_ram);
    chip.attach_palette(pal);
    chip.set_scroll2_base(0U);
    chip.set_rowscroll(true, rowscroll_base, 0U);
    render_one_frame(chip);

    const auto fb = chip.framebuffer();
    // Line 0 is shifted +16: the red column (world col 10) lands at px 80..95.
    REQUIRE(fb.pixels[0U * 384U + 88U] == 0xFF0000U);
    REQUIRE(fb.pixels[0U * 384U + 104U] == 0x000055U); // backdrop
    // Line 1 is unshifted: the red column lands at px 96..111.
    REQUIRE(fb.pixels[1U * 384U + 88U] == 0x000055U); // backdrop
    REQUIRE(fb.pixels[1U * 384U + 104U] == 0xFF0000U);
}

TEST_CASE("cps_a_b save/load preserves scroll bases and renders identically", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 128U, 128U, 1U);
    std::vector<std::uint8_t> tile_ram(0x30000U, 0U);
    fill_nametable(tile_ram, 0U, 4096U, 1U, 0U);
    auto pal = make_palette(0x000FU);
    set_pal(pal, 64U, 1U, 0xFF00U);

    cps_a_b a;
    a.attach_gfx(gfx);
    a.attach_tile_ram(tile_ram);
    a.attach_palette(pal);
    a.set_scroll2_base(0U);
    a.set_rowscroll(true, 0x10000U, 4U); // table reads zero -> no shift, just exercises the path

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    cps_a_b b;
    b.attach_gfx(gfx);
    b.attach_tile_ram(tile_ram);
    b.attach_palette(pal);
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    render_one_frame(b);
    REQUIRE(b.framebuffer().pixels[0] == 0xFF0000U); // scroll2_base survived the round-trip
}

TEST_CASE("cps_a_b draws a single-cell object at its screen position", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 128U, 128U, 1U); // sprite block tile 1 -> pen 1
    std::vector<std::uint8_t> obj(0x800U, 0U);
    set_sprite(obj, 0U, 114U, 46U, 1U, 0x0000U); // -> visible (50, 30), pal bank 0
    set_sprite(obj, 1U, 0U, 0U, 0U, 0xFF00U);    // end marker
    auto pal = make_palette(0x000FU);            // backdrop 0x000055
    set_pal(pal, 0U, 1U, 0xFF00U);               // sprite bank 0 pen 1 -> red

    cps_a_b chip;
    chip.attach_gfx(gfx);
    chip.attach_palette(pal);
    chip.attach_object_ram(obj); // no tile RAM: the tilemaps stay idle
    render_one_frame(chip);      // draw_sprites auto-latches

    const auto fb = chip.framebuffer();
    REQUIRE(fb.pixels[30U * 384U + 50U] == 0xFF0000U); // top-left of the cell
    REQUIRE(fb.pixels[45U * 384U + 65U] == 0xFF0000U); // bottom-right of the 16x16 cell
    REQUIRE(fb.pixels[30U * 384U + 49U] == 0x000055U); // just left -> backdrop
    REQUIRE(fb.pixels[46U * 384U + 50U] == 0x000055U); // just below -> backdrop
}

TEST_CASE("cps_a_b draws a multi-cell 2x2 object block", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    // 2x2 blocks of base code 1 resolve to tiles 1, 2, 17, 18.
    for (std::uint32_t tile : {1U, 2U, 17U, 18U}) {
        carve_solid_tile(gfx, tile * 128U, 128U, 1U);
    }
    std::vector<std::uint8_t> obj(0x800U, 0U);
    set_sprite(obj, 0U, 114U, 46U, 1U, 0x1100U); // blocks_x=2, blocks_y=2 -> 32x32
    set_sprite(obj, 1U, 0U, 0U, 0U, 0xFF00U);
    auto pal = make_palette(0x000FU);
    set_pal(pal, 0U, 1U, 0xFF00U);

    cps_a_b chip;
    chip.attach_gfx(gfx);
    chip.attach_palette(pal);
    chip.attach_object_ram(obj);
    render_one_frame(chip);

    const auto fb = chip.framebuffer();
    REQUIRE(fb.pixels[30U * 384U + 50U] == 0xFF0000U); // top-left cell
    REQUIRE(fb.pixels[61U * 384U + 81U] == 0xFF0000U); // bottom-right cell corner (32x32)
    REQUIRE(fb.pixels[61U * 384U + 50U] == 0xFF0000U);
    REQUIRE(fb.pixels[30U * 384U + 81U] == 0xFF0000U);
    REQUIRE(fb.pixels[30U * 384U + 82U] == 0x000055U); // just past the block -> backdrop
}

TEST_CASE("cps_a_b skips the 0xF000 object and the end marker terminates the list",
          "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 128U, 128U, 1U);
    auto pal = make_palette(0x000FU);
    set_pal(pal, 0U, 1U, 0xFF00U);

    SECTION("y == 0xF000 hides the object") {
        std::vector<std::uint8_t> obj(0x800U, 0U);
        set_sprite(obj, 0U, 114U, 0xF000U, 1U, 0x0000U); // hidden
        set_sprite(obj, 1U, 0U, 0U, 0U, 0xFF00U);
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_palette(pal);
        chip.attach_object_ram(obj);
        render_one_frame(chip);
        REQUIRE(chip.framebuffer().pixels[30U * 384U + 50U] == 0x000055U); // backdrop only
    }

    SECTION("entries after the end marker are not drawn") {
        std::vector<std::uint8_t> obj(0x800U, 0U);
        set_sprite(obj, 0U, 114U, 46U, 1U, 0x0000U);  // drawn -> (50, 30)
        set_sprite(obj, 1U, 0U, 0U, 0U, 0xFF00U);     // end marker
        set_sprite(obj, 2U, 178U, 110U, 1U, 0x0000U); // past the marker -> (114, 94)
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_palette(pal);
        chip.attach_object_ram(obj);
        render_one_frame(chip);
        REQUIRE(chip.framebuffer().pixels[30U * 384U + 50U] == 0xFF0000U);  // entry 0 drawn
        REQUIRE(chip.framebuffer().pixels[94U * 384U + 114U] == 0x000055U); // entry 2 skipped
    }
}

TEST_CASE("cps_a_b sprite order decides which overlapping object is on top", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 128U, 128U, 1U);
    std::vector<std::uint8_t> obj(0x800U, 0U);
    set_sprite(obj, 0U, 114U, 46U, 1U, 0x0000U); // pal bank 0
    set_sprite(obj, 1U, 114U, 46U, 1U, 0x0001U); // pal bank 1, same position
    set_sprite(obj, 2U, 0U, 0U, 0U, 0xFF00U);
    auto pal = make_palette(0x000FU);
    set_pal(pal, 0U, 1U, 0xFF00U); // bank 0 -> red
    set_pal(pal, 1U, 1U, 0xF0F0U); // bank 1 -> green

    // MAME sprite-to-sprite priority: the FIRST entry drawn keeps the pixel; a
    // later overlapping entry does not paint over it. The draw order (ascending =
    // 0..N, descending = N..0) therefore selects which list entry shows on top.
    SECTION("ascending: the earlier entry stays on top (first sprite wins)") {
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_palette(pal);
        chip.attach_object_ram(obj);
        chip.set_sprite_order(cps_a_b::sprite_order::ascending);
        render_one_frame(chip);
        REQUIRE(chip.framebuffer().pixels[30U * 384U + 50U] == 0xFF0000U); // entry 0 (red)
    }

    SECTION("descending: the later entry stays on top (drawn first)") {
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_palette(pal);
        chip.attach_object_ram(obj);
        chip.set_sprite_order(cps_a_b::sprite_order::descending);
        render_one_frame(chip);
        REQUIRE(chip.framebuffer().pixels[30U * 384U + 50U] == 0x00FF00U); // entry 1 (green)
    }
}

TEST_CASE("cps_a_b sprites draw on top of the tilemaps", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 128U, 128U, 1U); // tile/sprite cell 1 -> pen 1
    std::vector<std::uint8_t> tile_ram(0x30000U, 0U);
    fill_nametable(tile_ram, 0U, 4096U, 1U, 0U); // scroll2 covers the screen
    std::vector<std::uint8_t> obj(0x800U, 0U);
    set_sprite(obj, 0U, 114U, 46U, 1U, 0x0000U); // a sprite over the tilemap
    set_sprite(obj, 1U, 0U, 0U, 0U, 0xFF00U);
    auto pal = make_palette(0x000FU);
    set_pal(pal, 64U, 1U, 0xFF00U); // scroll2 bank 0 pen 1 -> red
    set_pal(pal, 0U, 1U, 0xF0F0U);  // sprite bank 0 pen 1 -> green

    cps_a_b chip;
    chip.attach_gfx(gfx);
    chip.attach_tile_ram(tile_ram);
    chip.attach_palette(pal);
    chip.attach_object_ram(obj);
    chip.set_scroll2_base(0U);
    render_one_frame(chip);

    const auto fb = chip.framebuffer();
    REQUIRE(fb.pixels[30U * 384U + 50U] == 0x00FF00U); // sprite (green) over the tile
    REQUIRE(fb.pixels[0] == 0xFF0000U);                // tilemap (red) where no sprite
}

TEST_CASE("cps_a_b save/load preserves the latched object buffer", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 128U, 128U, 1U);
    std::vector<std::uint8_t> obj(0x800U, 0U);
    set_sprite(obj, 0U, 114U, 46U, 1U, 0x0000U);
    set_sprite(obj, 1U, 0U, 0U, 0U, 0xFF00U);
    auto pal = make_palette(0x000FU);
    set_pal(pal, 0U, 1U, 0xFF00U);

    cps_a_b a;
    a.attach_gfx(gfx);
    a.attach_palette(pal);
    a.attach_object_ram(obj);
    a.set_sprite_order(cps_a_b::sprite_order::descending);
    a.latch_sprites();

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    cps_a_b b;
    b.attach_gfx(gfx);
    b.attach_palette(pal);
    // Deliberately do NOT re-attach object RAM: the latched buffer is serialized.
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    render_one_frame(b);
    REQUIRE(b.framebuffer().pixels[30U * 384U + 50U] ==
            0xFF0000U); // sprite from the restored buffer
}

TEST_CASE("cps_a_b high-priority tile pen occludes the sprite above it", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 64U, 64U, 1U);   // scroll1 tile 1 -> pen 1
    carve_solid_tile(gfx, 3U * 128U, 128U, 2U); // sprite cell 3 -> pen 2
    std::vector<std::uint8_t> tile_ram(0x30000U, 0U);
    fill_nametable(tile_ram, 0U, 8192U, 1U, 0x0080U); // scroll1 tile 1, priority group 1
    std::vector<std::uint8_t> obj(0x800U, 0U);
    set_sprite(obj, 0U, 114U, 46U, 3U, 0x0000U); // sprite over (50, 30)
    set_sprite(obj, 1U, 0U, 0U, 0U, 0xFF00U);
    auto pal = make_palette(0x000FU);
    set_pal(pal, 32U, 1U, 0xFF00U); // scroll1 bank 0 pen 1 -> red
    set_pal(pal, 0U, 2U, 0xF0F0U);  // sprite bank 0 pen 2 -> green

    SECTION("priority register set: the tile pen wins, the sprite is hidden") {
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_tile_ram(tile_ram);
        chip.attach_palette(pal);
        chip.attach_object_ram(obj);
        chip.set_scroll1_base(0U);
        chip.set_cps_b_reg(5U, 0x0002U); // group-1 priority reg (offset 0x0A), pen 1 bit set
        render_one_frame(chip);
        REQUIRE(chip.framebuffer().pixels[30U * 384U + 50U] == 0xFF0000U); // scroll1 red shows
    }

    SECTION("priority register clear: the sprite paints over the tile") {
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_tile_ram(tile_ram);
        chip.attach_palette(pal);
        chip.attach_object_ram(obj);
        chip.set_scroll1_base(0U);
        render_one_frame(chip);
        REQUIRE(chip.framebuffer().pixels[30U * 384U + 50U] == 0x00FF00U); // sprite green on top
    }
}

TEST_CASE("cps_a_b flip-screen mirrors the image", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 128U, 128U, 1U);
    std::vector<std::uint8_t> obj(0x800U, 0U);
    set_sprite(obj, 0U, 114U, 46U, 1U, 0x0000U); // unflipped -> visible (50, 30)
    set_sprite(obj, 1U, 0U, 0U, 0U, 0xFF00U);
    auto pal = make_palette(0x000FU);
    set_pal(pal, 0U, 1U, 0xFF00U);

    SECTION("no flip") {
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_palette(pal);
        chip.attach_object_ram(obj);
        render_one_frame(chip);
        REQUIRE(chip.framebuffer().pixels[30U * 384U + 50U] == 0xFF0000U);
        REQUIRE(chip.framebuffer().pixels[180U * 384U + 320U] == 0x000055U);
    }

    SECTION("flip-screen") {
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_palette(pal);
        chip.attach_object_ram(obj);
        chip.set_video_control(0x8000U); // flip-screen bit
        render_one_frame(chip);
        // The cell mirrors to the opposite corner (512-16-114, 256-16-46).
        REQUIRE(chip.framebuffer().pixels[30U * 384U + 50U] == 0x000055U);
        REQUIRE(chip.framebuffer().pixels[180U * 384U + 320U] == 0xFF0000U);
    }
}

TEST_CASE("cps_a_b layer-control sets the stack order", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 128U, 128U, 1U); // scroll2 tile 1 -> pen 1
    carve_solid_tile(gfx, 3U * 128U, 128U, 2U); // sprite cell 3 -> pen 2
    std::vector<std::uint8_t> tile_ram(0x30000U, 0U);
    fill_nametable(tile_ram, 0U, 4096U, 1U, 0U); // scroll2 covers the screen
    std::vector<std::uint8_t> obj(0x800U, 0U);
    set_sprite(obj, 0U, 114U, 46U, 3U, 0x0000U); // sprite over (50, 30)
    set_sprite(obj, 1U, 0U, 0U, 0U, 0xFF00U);
    auto pal = make_palette(0x000FU);
    set_pal(pal, 64U, 1U, 0xFF00U); // scroll2 bank 0 pen 1 -> red
    set_pal(pal, 0U, 2U, 0xF0F0U);  // sprite bank 0 pen 2 -> green

    SECTION("default order draws sprites on top") {
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_tile_ram(tile_ram);
        chip.attach_palette(pal);
        chip.attach_object_ram(obj);
        chip.set_scroll2_base(0U);
        render_one_frame(chip);
        REQUIRE(chip.framebuffer().pixels[30U * 384U + 50U] == 0x00FF00U); // sprite green
    }

    SECTION("layer-control puts scroll2 above sprites") {
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_tile_ram(tile_ram);
        chip.attach_palette(pal);
        chip.attach_object_ram(obj);
        chip.set_scroll2_base(0U);
        // l0=sprites, l1=scroll1, l2=scroll3, l3=scroll2 (top).
        chip.set_layer_control(0x2D00U);
        render_one_frame(chip);
        REQUIRE(chip.framebuffer().pixels[30U * 384U + 50U] == 0xFF0000U); // scroll2 red on top
    }
}

TEST_CASE("cps_a_b layer-enable mask plus video-control gates a layer", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 128U, 128U, 1U);
    std::vector<std::uint8_t> tile_ram(0x30000U, 0U);
    fill_nametable(tile_ram, 0U, 4096U, 1U, 0U);
    auto pal = make_palette(0x000FU); // backdrop 0x000055
    set_pal(pal, 64U, 1U, 0xFF00U);   // scroll2 red

    cps_a_b::cps_b_profile profile;         // legacy defaults
    profile.layer_enable_mask[1] = 0xFFFFU; // scroll2 (layer 2) gated by video-control bit 2

    SECTION("video-control bit clear disables scroll2") {
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_tile_ram(tile_ram);
        chip.attach_palette(pal);
        chip.set_scroll2_base(0U);
        chip.set_cps_b_profile(profile);
        chip.set_layer_control(0x2D00U); // scroll2 in the order
        chip.set_video_control(0x0000U); // bit 2 clear
        render_one_frame(chip);
        REQUIRE(chip.framebuffer().pixels[0] == 0x000055U); // backdrop only
    }

    SECTION("video-control bit set enables scroll2") {
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_tile_ram(tile_ram);
        chip.attach_palette(pal);
        chip.set_scroll2_base(0U);
        chip.set_cps_b_profile(profile);
        chip.set_layer_control(0x2D00U);
        chip.set_video_control(0x0004U); // bit 2 set
        render_one_frame(chip);
        REQUIRE(chip.framebuffer().pixels[0] == 0xFF0000U); // scroll2 red
    }
}

TEST_CASE("cps_a_b save/load preserves the CPS-B register file and priority", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 64U, 64U, 1U);
    carve_solid_tile(gfx, 3U * 128U, 128U, 2U);
    std::vector<std::uint8_t> tile_ram(0x30000U, 0U);
    fill_nametable(tile_ram, 0U, 8192U, 1U, 0x0080U);
    std::vector<std::uint8_t> obj(0x800U, 0U);
    set_sprite(obj, 0U, 114U, 46U, 3U, 0x0000U);
    set_sprite(obj, 1U, 0U, 0U, 0U, 0xFF00U);
    auto pal = make_palette(0x000FU);
    set_pal(pal, 32U, 1U, 0xFF00U);
    set_pal(pal, 0U, 2U, 0xF0F0U);

    cps_a_b a;
    a.attach_gfx(gfx);
    a.attach_tile_ram(tile_ram);
    a.attach_palette(pal);
    a.attach_object_ram(obj);
    a.set_scroll1_base(0U);
    a.set_cps_b_reg(5U, 0x0002U); // group-1 priority, pen 1

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    cps_a_b b;
    b.attach_gfx(gfx);
    b.attach_tile_ram(tile_ram);
    b.attach_palette(pal);
    b.attach_object_ram(obj);
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    render_one_frame(b);
    // The restored priority register still occludes the sprite -> scroll1 red.
    REQUIRE(b.framebuffer().pixels[30U * 384U + 50U] == 0xFF0000U);
}

TEST_CASE("cps_a_b legacy flag controls the zero-layer-control fallback", "[cps_a_b][video]") {
    std::vector<std::uint8_t> gfx(0x10000U, 0xFFU);
    carve_solid_tile(gfx, 1U * 128U, 128U, 1U);
    std::vector<std::uint8_t> tile_ram(0x30000U, 0U);
    fill_nametable(tile_ram, 0U, 4096U, 1U, 0U);
    auto pal = make_palette(0x000FU); // backdrop 0x000055
    set_pal(pal, 64U, 1U, 0xFF00U);   // scroll2 red

    SECTION("legacy profile: a zero latch uses the canonical fallback (scroll2 drawn)") {
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_tile_ram(tile_ram);
        chip.attach_palette(pal);
        chip.set_scroll2_base(0U);
        render_one_frame(chip); // default profile is legacy, layer-control 0
        REQUIRE(chip.framebuffer().pixels[0] == 0xFF0000U);
    }

    SECTION("real profile: a zero latch decodes literally (all-sprite order, scroll2 idle)") {
        cps_a_b::cps_b_profile profile;
        profile.legacy = false;
        cps_a_b chip;
        chip.attach_gfx(gfx);
        chip.attach_tile_ram(tile_ram);
        chip.attach_palette(pal);
        chip.set_scroll2_base(0U);
        chip.set_cps_b_profile(profile);
        render_one_frame(chip);
        REQUIRE(chip.framebuffer().pixels[0] == 0x000055U); // backdrop, scroll2 not in the order
    }
}

TEST_CASE("cps_a_b retains the board profile + gfx mapper across reset", "[cps_a_b]") {
    // Regression: reset() used to wipe profile_ back to default, dropping the
    // board's gfx-code mapper -- so every game silently ran on an identity mapper
    // (and the legacy layer order), which only renders correctly for boards whose
    // mapper happens to be identity. The profile is board configuration set once
    // by the board and must survive reset, like the attached gfx / palette spans.
    static constexpr std::array<cps_a_b::gfx_bank_range, 1> ranges{{
        {0x04U /* scroll2 */, 0x0000U, 0x7FFFU, 1U}, // route scroll2 codes into bank 1
    }};
    cps_a_b::cps_b_profile profile;
    profile.legacy = false;
    profile.mapper.bank_size = {0x8000U, 0x8000U, 0U, 0U};
    profile.mapper.ranges = ranges;

    cps_a_b chip;
    chip.set_cps_b_profile(profile);
    // scroll2 shift = 1: code 0x1000 -> expanded 0x2000 -> bank 1 (base 0x8000)
    // -> (0x8000 + 0x2000) >> 1 = 0x5000, i.e. non-identity.
    const std::uint32_t mapped = chip.mapped_gfx_code(cps_a_b::gfx_type::scroll2, 0x1000U);
    REQUIRE(mapped == 0x5000U);

    chip.reset(reset_kind::power_on);
    // The mapper must still apply after reset (the bug returned the identity code).
    CHECK(chip.mapped_gfx_code(cps_a_b::gfx_type::scroll2, 0x1000U) == 0x5000U);
}
