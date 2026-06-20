#include "ppu2c02.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::video::ppu2c02;

    constexpr std::uint64_t frame_ticks =
        static_cast<std::uint64_t>(ppu2c02::dots_per_line) * ppu2c02::lines_per_frame;

    // The first three master-palette colours we reference in the assertions
    // (canonical 2C02 NTSC values, 0x00RRGGBB).
    constexpr std::uint32_t master_06 = 0x7D0000U; // dark red   (index 0x06)
    constexpr std::uint32_t master_1A = 0x00A100U; // green      (index 0x1A)
    constexpr std::uint32_t master_12 = 0x353CFFU; // blue       (index 0x12)
    constexpr std::uint32_t master_0F = 0x000000U; // black/backdrop (index 0x0F)

    // A CHR pattern bank of `count` tiles where tile N is a SOLID block of
    // pixel value `values[N]` (0..3): plane 0 carries bit 0, plane 1 carries
    // bit 1, eight rows each, 16 bytes per tile.
    [[nodiscard]] std::vector<std::uint8_t> make_chr(const std::vector<std::uint8_t>& values) {
        std::vector<std::uint8_t> chr(values.size() * ppu2c02::tile_bytes, 0U);
        for (std::size_t tile = 0; tile < values.size(); ++tile) {
            const std::uint8_t v = values[tile];
            for (std::size_t row = 0; row < 8U; ++row) {
                chr[tile * ppu2c02::tile_bytes + row] = (v & 1U) != 0U ? 0xFFU : 0x00U;
                chr[tile * ppu2c02::tile_bytes + row + 8U] = (v & 2U) != 0U ? 0xFFU : 0x00U;
            }
        }
        return chr;
    }

    // Write one palette byte through the PPU bus ($3F00-based palette window).
    void set_palette(ppu2c02& ppu, std::uint16_t entry, std::uint8_t master_index) {
        ppu.ppu_write(static_cast<std::uint16_t>(0x3F00U + entry), master_index);
    }

} // namespace

TEST_CASE("ppu2c02 registers through the chip registry", "[ppu2c02]") {
    auto chip = mnemos::chips::create_chip("ricoh.2c02");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::video);
    CHECK(chip->metadata().part_number == "2C02");
}

TEST_CASE("ppu2c02 reset clears register and beam state", "[ppu2c02]") {
    ppu2c02 ppu;
    // Dirty the state, then reset and confirm it zeroes.
    ppu.reg_write(ppu2c02::reg_ctrl, ppu2c02::ctrl_nmi_enable);
    ppu.reg_write(ppu2c02::reg_mask, ppu2c02::mask_bg_enable);
    ppu.tick(frame_ticks); // advance the beam and bump the frame counter
    CHECK(ppu.frame_index() == 1U);

    ppu.reset(mnemos::chips::reset_kind::hard);
    CHECK(ppu.frame_index() == 0U);
    CHECK(ppu.beam_line() == 0U);
    CHECK(ppu.beam_dot() == 0U);
    CHECK_FALSE(ppu.nmi_asserted());
}

TEST_CASE("ppu2c02 fires the scanline callback and latches vblank/NMI", "[ppu2c02]") {
    ppu2c02 ppu;

    std::vector<std::uint32_t> vblank_lines;
    std::vector<std::uint32_t> scanlines;
    ppu.set_vblank_callback([&](std::uint32_t line) { vblank_lines.push_back(line); });
    ppu.set_scanline_callback([&](std::uint32_t line) { scanlines.push_back(line); });
    ppu.reg_write(ppu2c02::reg_ctrl, ppu2c02::ctrl_nmi_enable); // arm NMI on vblank

    ppu.tick(frame_ticks);
    CHECK(ppu.frame_index() == 1U);
    REQUIRE(vblank_lines.size() == 1U);
    CHECK(vblank_lines[0] == ppu2c02::vblank_line);
    REQUIRE(scanlines.size() == ppu2c02::lines_per_frame);
    CHECK(scanlines.front() == 0U);
    CHECK(scanlines.back() == ppu2c02::lines_per_frame - 1U);
    // The vblank flag is up and the NMI gate is asserted.
    CHECK(ppu.nmi_asserted());

    // Reading STATUS clears vblank + deasserts NMI and resets the write latch.
    const std::uint8_t status = ppu.reg_read(ppu2c02::reg_status);
    CHECK((status & ppu2c02::status_vblank) != 0U);
    CHECK_FALSE(ppu.nmi_asserted());
}

TEST_CASE("ppu2c02 loopy v/t/x latch through the scroll and address ports", "[ppu2c02]") {
    ppu2c02 ppu;
    // $2006 twice loads v exactly (high 6 bits then low 8); reading it back
    // through $2007's post-increment confirms the address took.
    ppu.reg_write(ppu2c02::reg_addr, 0x21U); // high
    ppu.reg_write(ppu2c02::reg_addr, 0x08U); // low -> v = $2108
    // Seed the nametable cell at $2108 and read it back (one dummy read for the
    // buffered $2007 path, then the real byte).
    ppu.ppu_write(0x2108U, 0x42U);
    ppu.reg_write(ppu2c02::reg_addr, 0x21U);
    ppu.reg_write(ppu2c02::reg_addr, 0x08U);
    (void)ppu.reg_read(ppu2c02::reg_data); // buffered: returns stale, refills
    CHECK(ppu.reg_read(ppu2c02::reg_data) == 0x42U);
}

TEST_CASE("ppu2c02 CHR-RAM accepts pattern writes; CHR-ROM drops them", "[ppu2c02]") {
    ppu2c02 ram_ppu;
    std::array<std::uint8_t, ppu2c02::pattern_size> chr_ram{};
    ram_ppu.attach_chr_ram(chr_ram);
    ram_ppu.ppu_write(0x0001U, 0x77U);
    CHECK(ram_ppu.ppu_read(0x0001U) == 0x77U); // writable + read back
    CHECK(chr_ram[1] == 0x77U);                // landed in the backing store

    ppu2c02 rom_ppu;
    const std::array<std::uint8_t, ppu2c02::pattern_size> chr_rom{};
    rom_ppu.attach_chr(chr_rom);
    rom_ppu.ppu_write(0x0001U, 0x88U);
    CHECK(rom_ppu.ppu_read(0x0001U) == 0x00U); // CHR-ROM write dropped
}

TEST_CASE("ppu2c02 renders a background tile through the palette", "[ppu2c02]") {
    ppu2c02 ppu;
    // Tile 1 = solid pixel value 1; tile 0 = blank (pixel 0).
    const auto chr = make_chr({0U, 1U});
    ppu.attach_chr(chr);
    set_palette(ppu, 0U, 0x0FU); // backdrop = black
    set_palette(ppu, 1U, 0x06U); // BG palette 0, colour 1 = dark red

    // Put tile 1 at nametable cell (0,0); leave the rest as tile 0.
    ppu.ppu_write(0x2000U, 0x01U);
    // Enable background AND the leftmost-8-pixels column (the hardware clips
    // the left column unless mask_bg_left is set).
    ppu.reg_write(ppu2c02::reg_mask,
                  static_cast<std::uint8_t>(ppu2c02::mask_bg_enable | ppu2c02::mask_bg_left));

    ppu.tick(frame_ticks);
    const auto frame = ppu.framebuffer();
    CHECK(frame.width == ppu2c02::visible_width);
    CHECK(frame.height == ppu2c02::visible_height);
    // The 8x8 cell at the origin is solid dark red.
    CHECK(frame.pixels[0] == master_06);
    CHECK(frame.pixels[7] == master_06);
    CHECK(frame.pixels[7U * frame.effective_stride() + 7U] == master_06);
    // Pixel just past the tile (column 8) falls on blank tile 0 -> backdrop.
    CHECK(frame.pixels[8] == master_0F);
}

TEST_CASE("ppu2c02 attribute table selects the background sub-palette", "[ppu2c02]") {
    ppu2c02 ppu;
    const auto chr = make_chr({0U, 1U}); // tile 1 = pixel value 1
    ppu.attach_chr(chr);
    set_palette(ppu, 0U, 0x0FU);
    set_palette(ppu, 1U, 0x06U);  // BG palette 0 colour 1
    set_palette(ppu, 13U, 0x1AU); // BG palette 3 colour 1 (entry 3*4+1 = 13)

    // Tile 1 across the top-left 4x4-tile attribute region; attribute byte
    // (the first of nametable 0, at $23C0) selects palette 3 for the top-left
    // quad (bits 1:0 = 3).
    for (std::uint16_t cell = 0; cell < 32U; ++cell) {
        ppu.ppu_write(static_cast<std::uint16_t>(0x2000U + cell), 0x01U);
    }
    ppu.ppu_write(0x23C0U, 0x03U); // top-left quad -> palette 3
    ppu.reg_write(ppu2c02::reg_mask,
                  static_cast<std::uint8_t>(ppu2c02::mask_bg_enable | ppu2c02::mask_bg_left));

    ppu.tick(frame_ticks);
    const auto frame = ppu.framebuffer();
    // (0,0) is in the top-left 2x2-tile quad -> palette 3 colour 1 (green).
    CHECK(frame.pixels[0] == master_1A);
}

TEST_CASE("ppu2c02 draws a sprite above the background through sprite palette", "[ppu2c02]") {
    ppu2c02 ppu;
    // BG tile 1 = solid pixel 1 (dark red); sprite tile 2 = solid pixel 1.
    const auto chr = make_chr({0U, 1U, 1U});
    ppu.attach_chr(chr);
    set_palette(ppu, 0U, 0x0FU);    // backdrop
    set_palette(ppu, 1U, 0x06U);    // BG colour -> dark red
    set_palette(ppu, 0x11U, 0x12U); // sprite palette 0 colour 1 -> blue

    // Fill the visible nametable with tile 1 so the background is opaque.
    for (std::uint16_t cell = 0; cell < 0x3C0U; ++cell) {
        ppu.ppu_write(static_cast<std::uint16_t>(0x2000U + cell), 0x01U);
    }
    // Sprite 0 at screen (10, 20): OAM Y is one less than the screen row.
    ppu.poke_oam(0U, static_cast<std::uint8_t>(20U - 1U)); // Y
    ppu.poke_oam(1U, 0x02U);                               // tile 2
    ppu.poke_oam(2U, 0x00U);                               // attrs: palette 0, in front
    ppu.poke_oam(3U, 10U);                                 // X
    // Sprite pattern table = table 0 (CTRL spr_pt clear). Enable BG + sprites
    // and the leftmost-8 columns for both so (0,0) shows the background.
    ppu.reg_write(ppu2c02::reg_mask,
                  static_cast<std::uint8_t>(ppu2c02::mask_bg_enable | ppu2c02::mask_spr_enable |
                                            ppu2c02::mask_bg_left | ppu2c02::mask_spr_left));

    ppu.tick(frame_ticks);
    const auto frame = ppu.framebuffer();
    const auto at = [&](std::uint32_t x, std::uint32_t y) {
        return frame.pixels[y * frame.effective_stride() + x];
    };
    CHECK(at(10U, 20U) == master_12); // sprite top-left (blue)
    CHECK(at(17U, 27U) == master_12); // sprite bottom-right (8x8)
    CHECK(at(0U, 0U) == master_06);   // background outside the sprite (dark red)
    CHECK(at(18U, 20U) == master_06); // one past the sprite's right edge
    // Sprite 0 over opaque background set the sprite-0-hit flag.
    CHECK((ppu.reg_read(ppu2c02::reg_status) & ppu2c02::status_spr0_hit) != 0U);
}

TEST_CASE("ppu2c02 honours the sprite flip-x attribute", "[ppu2c02]") {
    ppu2c02 ppu;
    // Sprite tile 1: a single set pixel at (0,0) (plane 0 row 0 MSB).
    std::vector<std::uint8_t> chr(2U * ppu2c02::tile_bytes, 0U);
    chr[1U * ppu2c02::tile_bytes + 0U] = 0x80U; // tile 1, plane 0, row 0, bit 7
    ppu.attach_chr(chr);
    set_palette(ppu, 0U, 0x0FU);
    set_palette(ppu, 0x11U, 0x12U); // sprite colour 1 -> blue

    ppu.poke_oam(0U, static_cast<std::uint8_t>(50U - 1U)); // Y -> row 50
    ppu.poke_oam(1U, 0x01U);                               // tile 1
    ppu.poke_oam(2U, 0x40U);                               // flip-x
    ppu.poke_oam(3U, 100U);                                // X
    ppu.reg_write(ppu2c02::reg_mask, ppu2c02::mask_spr_enable);

    ppu.tick(frame_ticks);
    const auto frame = ppu.framebuffer();
    const auto at = [&](std::uint32_t x, std::uint32_t y) {
        return frame.pixels[y * frame.effective_stride() + x];
    };
    // Flip-x moves the single pixel from column 0 to column 7 of the cell.
    CHECK(at(107U, 50U) == master_12);
    CHECK(at(100U, 50U) == master_0F); // original column now transparent -> backdrop
}

TEST_CASE("ppu2c02 save_state / load_state round-trips", "[ppu2c02]") {
    ppu2c02 ppu;
    // Build a distinctive state: registers, loopy address, palette, OAM.
    ppu.reg_write(ppu2c02::reg_ctrl,
                  static_cast<std::uint8_t>(ppu2c02::ctrl_nmi_enable | ppu2c02::ctrl_bg_pt));
    ppu.reg_write(ppu2c02::reg_mask, ppu2c02::mask_bg_enable);
    ppu.reg_write(ppu2c02::reg_addr, 0x24U);
    ppu.reg_write(ppu2c02::reg_addr, 0x55U); // v = $2455
    ppu.ppu_write(0x3F01U, 0x21U);           // a palette byte
    ppu.poke_oam(7U, 0xABU);
    ppu.tick(frame_ticks); // advance beam + frame counter

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    ppu.save_state(writer);

    ppu2c02 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    CHECK(reader.ok());

    CHECK(restored.frame_index() == ppu.frame_index());
    CHECK(restored.peek_oam(7U) == 0xABU);
    // The restored palette byte reads back through the PPU bus (non-mutating;
    // this must not touch the loopy address registers before the re-save).
    CHECK((restored.ppu_read(0x3F01U) & 0x3FU) == 0x21U);

    // Re-saving the restored chip yields a byte-identical blob.
    std::vector<std::uint8_t> blob2;
    mnemos::chips::state_writer writer2(blob2);
    restored.save_state(writer2);
    CHECK(blob2 == blob);
}
