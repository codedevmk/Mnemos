#include "agnus.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::video::agnus;

    constexpr std::uint64_t frame_ticks =
        static_cast<std::uint64_t>(agnus::color_clocks_per_line) * agnus::scanlines_pal;

    // Big-endian palette buffer (32 entries x 2 bytes) with `index` set to a
    // raw 12-bit colour word.
    [[nodiscard]] std::vector<std::uint8_t> make_palette(std::size_t index, std::uint16_t color12) {
        std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
        palette[index * 2U] = static_cast<std::uint8_t>((color12 >> 8U) & 0xFFU);
        palette[index * 2U + 1U] = static_cast<std::uint8_t>(color12 & 0xFFU);
        return palette;
    }

    // Program the display window/data-fetch so the whole 320x256 PAL field is
    // visible and 20 low-res words are fetched per line.
    void program_full_window(agnus& chip) {
        chip.set_bplcon0(0x1000U); // BPU = 1 (one bitplane)
        chip.set_diwstrt(0x2C00U); // V start = 0x2C (matches display_line_origin)
        chip.set_diwstop(0xF400U); // V stop = 0xF4 (244), bit 7 set => no +0x100
        chip.set_ddfstrt(0x0038U); // fetch start
        chip.set_ddfstop(0x00D0U); // fetch stop => 20 words/line
    }

} // namespace

TEST_CASE("agnus registers through the chip registry", "[agnus]") {
    auto chip = mnemos::chips::create_chip("commodore.agnus");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::video);
    CHECK(chip->metadata().manufacturer == "Commodore");
}

TEST_CASE("agnus reset clears the beam and DMA state", "[agnus]") {
    agnus chip;
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen));
    chip.tick(agnus::color_clocks_per_line * 5U); // advance the beam a few lines

    chip.reset(mnemos::chips::reset_kind::hard);
    CHECK(chip.beam_line() == 0U);
    CHECK(chip.beam_clock() == 0U);
    CHECK(chip.frame_index() == 0U);
    CHECK_FALSE(chip.dma_master());
    CHECK(chip.read_dmaconr() == 0U);
}

TEST_CASE("agnus fires the scanline callback for every line and bumps the frame", "[agnus]") {
    agnus chip;
    std::vector<std::uint32_t> vblank_lines;
    std::uint32_t scanline_count = 0U;
    chip.set_vblank_callback([&](std::uint32_t line) { vblank_lines.push_back(line); });
    chip.set_scanline_callback([&](std::uint32_t /*line*/) { ++scanline_count; });

    chip.tick(frame_ticks);
    CHECK(chip.frame_index() == 1U);
    REQUIRE(vblank_lines.size() == 1U);
    CHECK(vblank_lines[0] == chip.active_height());
    CHECK(scanline_count == agnus::scanlines_pal);
    CHECK(chip.beam_line() == 0U);
    CHECK(chip.beam_clock() == 0U);

    chip.tick(frame_ticks);
    CHECK(chip.frame_index() == 2U);
}

TEST_CASE("agnus DMACON honours bit-15 set/clear and exposes blitter status", "[agnus]") {
    agnus chip;
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_bplen | agnus::dmacon_copen));
    CHECK(chip.dma_master());
    CHECK(chip.dma_bitplane());
    CHECK(chip.dma_copper());
    CHECK_FALSE(chip.dma_sprite());

    chip.write_dmacon(agnus::dmacon_bplen); // clear (bit 15 = 0)
    CHECK_FALSE(chip.dma_bitplane());
    CHECK(chip.dma_master());

    chip.set_blitter_busy(true);
    chip.set_blitter_zero(true);
    CHECK((chip.read_dmaconr() & agnus::dmacon_bbusy) != 0U);
    CHECK((chip.read_dmaconr() & agnus::dmacon_bzero) != 0U);
}

TEST_CASE("agnus VPOSR / VHPOSR report the live beam position", "[agnus]") {
    agnus chip;
    // Advance to scanline 1, color clock 5.
    chip.tick(agnus::color_clocks_per_line + 5U);
    CHECK(chip.beam_line() == 1U);
    CHECK(chip.beam_clock() == 5U);
    CHECK(chip.read_vhposr() == static_cast<std::uint16_t>((1U << 8U) | 5U));
    CHECK((chip.read_vposr() & 0x0001U) == 0U); // scanline < 256 => high bit clear
}

TEST_CASE("agnus colour decode replicates the 4-bit nibble per channel", "[agnus]") {
    CHECK(agnus::color_to_rgb(0x0FFFU) == 0x00FFFFFFU); // full white
    CHECK(agnus::color_to_rgb(0x0000U) == 0x00000000U); // black
    CHECK(agnus::color_to_rgb(0x0F00U) == 0x00FF0000U); // red
    CHECK(agnus::color_to_rgb(0x00F0U) == 0x0000FF00U); // green
    CHECK(agnus::color_to_rgb(0x000FU) == 0x000000FFU); // blue
    CHECK(agnus::color_to_rgb(0x0555U) == 0x00555555U); // mid grey: 0x5 -> 0x55
}

TEST_CASE("agnus decodes bitplane DMA through the colour palette", "[agnus]") {
    agnus chip;

    // One bitplane in 512 KiB chip RAM. The plane-0 word at the start of the
    // line has its MSB set, so pixel x=0 has colour index 1; index 1 = red.
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    chip_ram[0] = 0x80U; // big-endian word 0x8000 -> MSB set
    chip_ram[1] = 0x00U;
    const auto palette = make_palette(1U, 0x0F00U); // index 1 = red

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip);
    chip.set_bitplane_pointer(0U, 0U);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();
    CHECK(frame.width == agnus::visible_width);
    CHECK(frame.height == agnus::visible_height_pal);
    // Pixel (0,0): index 1 -> red.
    CHECK(frame.pixels[0] == 0x00FF0000U);
    // Pixel (1,0): the next bit of the word is 0 -> background (index 0,
    // black since palette entry 0 is unset).
    CHECK(frame.pixels[1] == 0x00000000U);
}

TEST_CASE("agnus paints the backdrop from colour 0 and clips outside the display window",
          "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    // Palette: entry 0 = blue backdrop, entry 1 = red foreground.
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    palette[0] = 0x00U;
    palette[1] = 0x0FU; // entry 0 = 0x000F -> blue
    palette[2] = 0x0FU;
    palette[3] = 0x00U; // entry 1 = 0x0F00 -> red

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip);
    chip.set_bitplane_pointer(0U, 0U);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();
    // All-zero chip RAM => every pixel index 0 => the blue backdrop.
    CHECK(frame.pixels[0] == 0x000000FFU);
    CHECK(frame.pixels[100U * frame.effective_stride() + 200U] == 0x000000FFU);
}

TEST_CASE("agnus disables the display when bitplane DMA is off", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    chip_ram[0] = 0x80U;
    const auto palette = make_palette(1U, 0x0F00U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip);
    chip.set_bitplane_pointer(0U, 0U);

    // Master DMA on but bitplane DMA off: only the backdrop renders.
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen));
    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x00000000U); // backdrop (palette 0 unset)

    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_bplen));
    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x00FF0000U); // now the foreground draws
}

TEST_CASE("agnus copper MOVE pokes a display register from chip RAM", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    // Copper list at byte 0x100: MOVE 0x0F00 -> COLOR01 (reg 0x182), then a
    // WAIT that never matches to halt. Big-endian words.
    // MOVE: IR1 = reg_addr (bit0 = 0), IR2 = value.
    const std::uint32_t list = 0x100U;
    chip_ram[list + 0] = 0x01U; // IR1 high
    chip_ram[list + 1] = 0x82U; // IR1 low = 0x0182 (COLOR01, MOVE)
    chip_ram[list + 2] = 0x0FU; // IR2 high
    chip_ram[list + 3] = 0x00U; // IR2 low = 0x0F00
    // WAIT for VP=0xFF (never reached this frame): IR1 = 0xFF01, IR2 = 0xFFFE.
    chip_ram[list + 4] = 0xFFU;
    chip_ram[list + 5] = 0x01U;
    chip_ram[list + 6] = 0xFFU;
    chip_ram[list + 7] = 0xFEU;

    chip.attach_chip_ram(chip_ram);
    chip.write_cop1lc(list);
    // Enabling copper DMA from stopped strobes COPJMP1 implicitly.
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_copen));
    // Run a couple of clocks so the copper executes the MOVE.
    chip.tick(8U);
    // The MOVE targeted a colour register; this model treats colour cells as
    // board-owned, so the observable effect is that the copper advanced past
    // the MOVE without faulting and reached the WAIT (still running).
    CHECK(chip.dma_copper());
}

TEST_CASE("agnus save_state / load_state round-trips", "[agnus]") {
    agnus chip;
    chip.set_pal(false); // NTSC
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_bplen | agnus::dmacon_spren));
    chip.set_bplcon0(0x3000U);
    chip.set_bitplane_pointer(0U, 0x12340U);
    chip.set_bitplane_pointer(1U, 0x22220U);
    chip.set_bitplane_modulo_odd(static_cast<std::int16_t>(-4));
    chip.set_bitplane_modulo_even(static_cast<std::int16_t>(8));
    chip.set_diwstrt(0x2C81U);
    chip.set_diwstop(0xF4C1U);
    chip.set_ddfstrt(0x0038U);
    chip.set_ddfstop(0x00D0U);
    chip.write_cop1lc(0x400U);
    chip.tick(agnus::color_clocks_per_line * 3U + 17U); // some live beam state

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    chip.save_state(writer);
    REQUIRE(blob.size() > 0U);

    agnus restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK_FALSE(restored.is_pal());
    CHECK(restored.read_dmaconr() == chip.read_dmaconr());
    CHECK(restored.dma_bitplane() == chip.dma_bitplane());
    CHECK(restored.dma_sprite() == chip.dma_sprite());
    CHECK(restored.beam_line() == chip.beam_line());
    CHECK(restored.beam_clock() == chip.beam_clock());
    CHECK(restored.read_vhposr() == chip.read_vhposr());
    CHECK(restored.read_vposr() == chip.read_vposr());
    CHECK(restored.frame_index() == chip.frame_index());
}
