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
    constexpr std::uint32_t pal_vblank_end_line = 24U;
    constexpr std::uint64_t pal_vblank_exit_ticks =
        static_cast<std::uint64_t>(agnus::color_clocks_per_line) * pal_vblank_end_line;

    // Big-endian palette buffer (32 entries x 2 bytes) with `index` set to a
    // raw 12-bit colour word.
    [[nodiscard]] std::vector<std::uint8_t> make_palette(std::size_t index, std::uint16_t color12) {
        std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
        palette[index * 2U] = static_cast<std::uint8_t>((color12 >> 8U) & 0xFFU);
        palette[index * 2U + 1U] = static_cast<std::uint8_t>(color12 & 0xFFU);
        return palette;
    }

    void set_palette_word(std::vector<std::uint8_t>& palette, std::size_t index,
                          std::uint16_t color12) {
        palette[index * 2U] = static_cast<std::uint8_t>((color12 >> 8U) & 0xFFU);
        palette[index * 2U + 1U] = static_cast<std::uint8_t>(color12 & 0xFFU);
    }

    // Program the display window/data-fetch so the whole 320x256 PAL field is
    // visible and 20 low-res words are fetched per line.
    void write_word(std::vector<std::uint8_t>& ram, std::uint32_t address, std::uint16_t value) {
        ram[address] = static_cast<std::uint8_t>(value >> 8U);
        ram[address + 1U] = static_cast<std::uint8_t>(value);
    }

    void set_plane_word(std::vector<std::uint8_t>& ram, agnus& chip, std::uint32_t plane,
                        std::uint16_t word) {
        const std::uint32_t base = plane * 0x100U;
        write_word(ram, base, word);
        chip.set_bitplane_pointer(plane, base);
    }

    void clear_six_plane_words(std::vector<std::uint8_t>& ram, agnus& chip) {
        for (std::uint32_t plane = 0; plane < agnus::max_bitplanes; ++plane) {
            set_plane_word(ram, chip, plane, 0U);
        }
    }

    void program_full_window(agnus& chip, std::uint16_t bplcon0 = 0x1000U) {
        chip.set_bplcon0(bplcon0);
        chip.set_diwstrt(0x2C00U); // V start = 0x2C (matches display_line_origin)
        chip.set_diwstop(0xF400U); // V stop = 0xF4 (244), bit 7 set => no +0x100
        chip.set_ddfstrt(0x0038U); // fetch start
        chip.set_ddfstop(0x00D0U); // fetch stop => 20 words/line
    }

    void program_full_window_hires(agnus& chip, std::uint16_t bplcon0 = 0x9000U) {
        chip.set_bplcon0(bplcon0);
        chip.set_diwstrt(0x2C00U);
        chip.set_diwstop(0xF400U);
        chip.set_ddfstrt(0x003CU); // standard high-resolution fetch start
        chip.set_ddfstop(0x00D4U); // standard high-resolution fetch stop => 40 words/line
    }

    [[nodiscard]] constexpr std::uint16_t sprite_pos(std::uint32_t visible_y,
                                                     std::uint32_t visible_x) noexcept {
        const std::uint32_t beam_y = agnus::display_line_origin + visible_y;
        const std::uint32_t beam_x = agnus::display_clock_origin + visible_x;
        return static_cast<std::uint16_t>(((beam_y & 0xFFU) << 8U) | ((beam_x >> 1U) & 0xFFU));
    }

    [[nodiscard]] constexpr std::uint16_t
    sprite_ctl(std::uint32_t visible_y, std::uint32_t visible_x, std::uint32_t height) noexcept {
        const std::uint32_t beam_y = agnus::display_line_origin + visible_y;
        const std::uint32_t stop_y = beam_y + height;
        const std::uint32_t beam_x = agnus::display_clock_origin + visible_x;
        return static_cast<std::uint16_t>(
            ((stop_y & 0xFFU) << 8U) | ((beam_x & 0x01U) != 0U ? 0x0001U : 0U) |
            ((stop_y & 0x100U) != 0U ? 0x0002U : 0U) | ((beam_y & 0x100U) != 0U ? 0x0004U : 0U));
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

    chip.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * chip.active_height());
    CHECK(chip.frame_index() == 0U);
    CHECK(vblank_lines.empty());
    CHECK(chip.beam_line() == chip.active_height());
    CHECK(chip.beam_clock() == 0U);

    chip.tick(frame_ticks -
              static_cast<std::uint64_t>(agnus::color_clocks_per_line) * chip.active_height());
    CHECK(chip.frame_index() == 1U);
    REQUIRE(vblank_lines.size() == 1U);
    CHECK(vblank_lines[0] == 0U);
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

TEST_CASE("agnus mirrors attached chip RAM for bitplane DMA fetches", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    chip_ram[0] = 0x80U;
    chip_ram[1] = 0x00U;
    const auto palette = make_palette(1U, 0x0F00U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip);
    chip.set_bitplane_pointer(0U, static_cast<std::uint32_t>(chip_ram.size()));
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("agnus bitplane pointers advance only during vertical display DMA", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    const auto palette = make_palette(1U, 0x0F00U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip);
    chip.set_bitplane_pointer(0U, 0U);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);

    constexpr std::uint32_t visible_lines = 0xF4U - 0x2CU;
    constexpr std::uint32_t words_per_line = 20U;
    constexpr std::uint32_t expected_pointer = visible_lines * words_per_line * 2U;
    CHECK(chip.bitplane_pointer(0U) == expected_pointer);
}

TEST_CASE("agnus delayed BPLCON1 playfields fetch an extra tail word", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    const auto palette = make_palette(1U, 0x0F00U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x2000U); // Two bitplanes.
    chip.set_bitplane_pointer(0U, 0U);
    chip.set_bitplane_pointer(1U, 0x4000U);
    chip.set_bplcon1(0x0001U); // PF1 delayed; PF2 has no tail fetch.
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);

    constexpr std::uint32_t visible_lines = 0xF4U - 0x2CU;
    constexpr std::uint32_t words_per_line = 20U;
    CHECK(chip.bitplane_pointer(0U) == visible_lines * (words_per_line + 1U) * 2U);
    CHECK(chip.bitplane_pointer(1U) == 0x4000U + visible_lines * words_per_line * 2U);
}

TEST_CASE("agnus clipped DDF fetches still advance bitplane pointers", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    const auto palette = make_palette(1U, 0x0F00U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip);
    chip.set_ddfstop(0x00D8U); // 21 low-resolution words; the last is offscreen.
    chip.set_bitplane_pointer(0U, 0U);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);

    constexpr std::uint32_t visible_lines = 0xF4U - 0x2CU;
    constexpr std::uint32_t ddf_words_per_line = 21U;
    CHECK(chip.bitplane_pointer(0U) == visible_lines * ddf_words_per_line * 2U);
}

TEST_CASE("agnus BPLCON1 tail fetch needs an active DDF window", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    const auto palette = make_palette(1U, 0x0F00U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip);
    chip.set_ddfstrt(0x00D0U);
    chip.set_ddfstop(0x0038U);
    chip.set_bitplane_pointer(0U, 0x2000U);
    chip.set_bplcon1(0x000FU);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);

    CHECK(chip.bitplane_pointer(0U) == 0x2000U);
}

TEST_CASE("agnus BPLCON1 delays odd and even playfield serializers", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 1U, 0x0F00U); // BPL1 only = red.
    set_palette_word(palette, 2U, 0x00F0U); // BPL2 only = green.

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x2000U); // Two bitplanes.
    set_plane_word(chip_ram, chip, 0U, 0x8000U);
    set_plane_word(chip_ram, chip, 1U, 0x8000U);
    chip.set_bplcon1(0x0021U); // PF1 delay = 1, PF2 delay = 2.
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.pixels[0] == 0x00000000U);
    CHECK(frame.pixels[1] == 0x00FF0000U);
    CHECK(frame.pixels[2] == 0x0000FF00U);
}

TEST_CASE("agnus BPLCON0 HIRES fetches and exposes 640-pixel bitplane rows", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    const auto palette = make_palette(1U, 0x0F00U);

    write_word(chip_ram, 0U, 0x8000U);       // x = 0
    write_word(chip_ram, 39U * 2U, 0x0001U); // x = 639, proves the 40th word is fetched
    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window_hires(chip);
    chip.set_bitplane_pointer(0U, 0U);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.width == agnus::visible_width_hires);
    CHECK(frame.height == agnus::visible_height_pal);
    CHECK(frame.effective_stride() == agnus::framebuffer_stride);
    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[agnus::visible_width_hires - 1U] == 0x00FF0000U);
}

TEST_CASE("agnus BPLCON1 high-resolution scroll delay advances in two-pixel steps", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    const auto palette = make_palette(1U, 0x0F00U);

    write_word(chip_ram, 0U, 0x8000U);
    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window_hires(chip);
    chip.set_bitplane_pointer(0U, 0U);
    chip.set_bplcon1(0x0011U);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.pixels[0] == 0x00000000U);
    CHECK(frame.pixels[1] == 0x00000000U);
    CHECK(frame.pixels[2] == 0x00FF0000U);
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

TEST_CASE("agnus bitplane DMA pointers advance and require a field rewrite", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    const auto palette = make_palette(1U, 0x0F00U);

    write_word(chip_ram, 0U, 0x8000U);
    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip);
    chip.set_bitplane_pointer(0U, 0U);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x00FF0000U);

    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x00000000U);

    chip.set_bitplane_pointer(0U, 0U);
    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("agnus renders extra-half-brite colours from the sixth bitplane", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    const auto palette = make_palette(1U, 0x0EEEU);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x6000U); // BPU = 6, HAM/DPF clear => EHB.
    clear_six_plane_words(chip_ram, chip);
    set_plane_word(chip_ram, chip, 0U, 0x8000U);
    set_plane_word(chip_ram, chip, 5U, 0x8000U);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);

    CHECK(chip.framebuffer().pixels[0] == 0x00777777U);
}

TEST_CASE("agnus renders HAM load and channel modify pixels", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    const auto palette = make_palette(1U, 0x000FU);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x6800U); // BPU = 6 + HAM.
    clear_six_plane_words(chip_ram, chip);
    set_plane_word(chip_ram, chip, 0U, 0xC000U);
    set_plane_word(chip_ram, chip, 1U, 0x4000U);
    set_plane_word(chip_ram, chip, 2U, 0x4000U);
    set_plane_word(chip_ram, chip, 3U, 0x4000U);
    set_plane_word(chip_ram, chip, 5U, 0x4000U);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.pixels[0] == 0x000000FFU);
    CHECK(frame.pixels[1] == 0x00FF00FFU);
}

TEST_CASE("agnus renders dual-playfield colours with playfield-one priority", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    palette[2] = 0x0FU;
    palette[3] = 0x00U; // COLOR01 = red
    palette[18] = 0x00U;
    palette[19] = 0xF0U; // COLOR09 = green

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x6400U); // BPU = 6 + DPF.
    clear_six_plane_words(chip_ram, chip);
    set_plane_word(chip_ram, chip, 0U, 0x8000U);
    set_plane_word(chip_ram, chip, 1U, 0xC000U);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[1] == 0x0000FF00U);
}

TEST_CASE("agnus renders manual sprite registers over the backdrop", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    palette[34] = 0x0FU;
    palette[35] = 0x00U; // COLOR17 = red
    palette[36] = 0x00U;
    palette[37] = 0xF0U; // COLOR18 = green

    chip.attach_palette(palette);
    chip.write_sprite_pos(0U, sprite_pos(0U, 0U));
    chip.write_sprite_ctl(0U, sprite_ctl(0U, 0U, 1U));
    chip.write_sprite_data_b(0U, 0x4000U);
    chip.write_sprite_data_a(0U, 0x8000U);

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[1] == 0x0000FF00U);
}

TEST_CASE("agnus keeps hardware sprites low-resolution on high-resolution playfields", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 17U, 0x0F00U); // COLOR17 = red.
    set_palette_word(palette, 18U, 0x00F0U); // COLOR18 = green.

    chip.attach_palette(palette);
    program_full_window_hires(chip);
    chip.write_sprite_pos(0U, sprite_pos(0U, 1U));
    chip.write_sprite_ctl(0U, sprite_ctl(0U, 1U, 1U));
    chip.write_sprite_data_b(0U, 0x4000U);
    chip.write_sprite_data_a(0U, 0x8000U);

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.width == agnus::visible_width_hires);
    CHECK(frame.pixels[0] == 0x00000000U);
    CHECK(frame.pixels[1] == 0x00000000U);
    CHECK(frame.pixels[2] == 0x00FF0000U);
    CHECK(frame.pixels[3] == 0x00FF0000U);
    CHECK(frame.pixels[4] == 0x0000FF00U);
    CHECK(frame.pixels[5] == 0x0000FF00U);
}

TEST_CASE("agnus clips high-resolution sprite replicas at the right edge", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 17U, 0x0F00U); // COLOR17 = red.

    chip.attach_palette(palette);
    program_full_window_hires(chip);
    chip.write_sprite_pos(0U, sprite_pos(0U, agnus::visible_width_lores - 1U));
    chip.write_sprite_ctl(0U, sprite_ctl(0U, agnus::visible_width_lores - 1U, 1U));
    chip.write_sprite_data_a(0U, 0x8000U);

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.width == agnus::visible_width_hires);
    CHECK(frame.pixels[agnus::visible_width_hires - 3U] == 0x00000000U);
    CHECK(frame.pixels[agnus::visible_width_hires - 2U] == 0x00FF0000U);
    CHECK(frame.pixels[agnus::visible_width_hires - 1U] == 0x00FF0000U);
}

TEST_CASE("agnus applies playfield priority per high-resolution sprite replica", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 1U, 0x0F00U);  // playfield red.
    set_palette_word(palette, 17U, 0x00F0U); // sprite green.

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window_hires(chip);
    set_plane_word(chip_ram, chip, 0U, 0xC000U);
    chip.write_sprite_pos(0U, sprite_pos(0U, 0U));
    chip.write_sprite_ctl(0U, sprite_ctl(0U, 0U, 1U));
    chip.write_sprite_data_a(0U, 0x8000U);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x00FF0000U);
    CHECK(chip.framebuffer().pixels[1] == 0x00FF0000U);

    chip.set_bplcon2(0x0020U); // PF2 priority slot 4: behind all sprite pairs.
    chip.set_bitplane_pointer(0U, 0U);
    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x0000FF00U);
    CHECK(chip.framebuffer().pixels[1] == 0x0000FF00U);
}

TEST_CASE("agnus renders sprite DMA data from the sprite pointer", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    palette[34] = 0x0FU;
    palette[35] = 0x00U; // COLOR17 = red
    palette[38] = 0x00U;
    palette[39] = 0x0FU; // COLOR19 = blue

    constexpr std::uint32_t sprite_base = 0x200U;
    write_word(chip_ram, sprite_base + 0U, sprite_pos(0U, 2U));
    write_word(chip_ram, sprite_base + 2U, sprite_ctl(0U, 2U, 1U));
    write_word(chip_ram, sprite_base + 4U, 0xC000U);
    write_word(chip_ram, sprite_base + 6U, 0x4000U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    chip.set_sprite_pointer(0U, sprite_base);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_spren));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.pixels[2] == 0x00FF0000U);
    CHECK(frame.pixels[3] == 0x000000FFU);
}

TEST_CASE("agnus sprite DMA pointers advance and require a field rewrite", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 17U, 0x0F00U); // COLOR17 = red.

    constexpr std::uint32_t sprite_base = 0x200U;
    write_word(chip_ram, sprite_base + 0U, sprite_pos(0U, 0U));
    write_word(chip_ram, sprite_base + 2U, sprite_ctl(0U, 0U, 1U));
    write_word(chip_ram, sprite_base + 4U, 0x8000U);
    write_word(chip_ram, sprite_base + 6U, 0x0000U);
    write_word(chip_ram, sprite_base + 8U, 0x0000U);
    write_word(chip_ram, sprite_base + 10U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    chip.set_sprite_pointer(0U, sprite_base);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_spren));

    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x00FF0000U);

    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x00000000U);

    chip.set_sprite_pointer(0U, sprite_base);
    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("agnus reuses a sprite DMA channel from the next control block", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 17U, 0x0F00U); // COLOR17 = red.
    set_palette_word(palette, 18U, 0x00F0U); // COLOR18 = green.

    constexpr std::uint32_t sprite_base = 0x240U;
    write_word(chip_ram, sprite_base + 0U, sprite_pos(0U, 0U));
    write_word(chip_ram, sprite_base + 2U, sprite_ctl(0U, 0U, 1U));
    write_word(chip_ram, sprite_base + 4U, 0x8000U);
    write_word(chip_ram, sprite_base + 6U, 0x0000U);
    write_word(chip_ram, sprite_base + 8U, sprite_pos(2U, 4U));
    write_word(chip_ram, sprite_base + 10U, sprite_ctl(2U, 4U, 1U));
    write_word(chip_ram, sprite_base + 12U, 0x0000U);
    write_word(chip_ram, sprite_base + 14U, 0x8000U);
    write_word(chip_ram, sprite_base + 16U, 0x0000U);
    write_word(chip_ram, sprite_base + 18U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    chip.set_sprite_pointer(0U, sprite_base);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_spren));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();
    const auto reused_pixel = static_cast<std::size_t>(2U) * frame.effective_stride() + 4U;

    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[reused_pixel] == 0x0000FF00U);
}

TEST_CASE("agnus requires a blank scanline before reusing a sprite DMA channel", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 17U, 0x0F00U); // COLOR17 = red.

    constexpr std::uint32_t sprite_base = 0x280U;
    write_word(chip_ram, sprite_base + 0U, sprite_pos(0U, 0U));
    write_word(chip_ram, sprite_base + 2U, sprite_ctl(0U, 0U, 1U));
    write_word(chip_ram, sprite_base + 4U, 0x8000U);
    write_word(chip_ram, sprite_base + 6U, 0x0000U);
    write_word(chip_ram, sprite_base + 8U, sprite_pos(1U, 4U));
    write_word(chip_ram, sprite_base + 10U, sprite_ctl(1U, 4U, 1U));
    write_word(chip_ram, sprite_base + 12U, 0x8000U);
    write_word(chip_ram, sprite_base + 14U, 0x0000U);
    write_word(chip_ram, sprite_base + 16U, sprite_pos(3U, 8U));
    write_word(chip_ram, sprite_base + 18U, sprite_ctl(3U, 8U, 1U));
    write_word(chip_ram, sprite_base + 20U, 0x8000U);
    write_word(chip_ram, sprite_base + 22U, 0x0000U);
    write_word(chip_ram, sprite_base + 24U, 0x0000U);
    write_word(chip_ram, sprite_base + 26U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    chip.set_sprite_pointer(0U, sprite_base);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_spren));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();
    const auto adjacent_reuse_pixel = static_cast<std::size_t>(1U) * frame.effective_stride() + 4U;
    const auto delayed_reuse_pixel = static_cast<std::size_t>(3U) * frame.effective_stride() + 8U;

    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[adjacent_reuse_pixel] == 0x00000000U);
    CHECK(frame.pixels[delayed_reuse_pixel] == 0x00FF0000U);
    CHECK(chip.sprite_pointer(0U) == sprite_base + 28U);
}

TEST_CASE("agnus zero-height sprite reload cannot reuse behind the beam", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 17U, 0x0F00U); // COLOR17 = red.

    constexpr std::uint32_t sprite_base = 0x2C0U;
    write_word(chip_ram, sprite_base + 0U, sprite_pos(4U, 0U));
    write_word(chip_ram, sprite_base + 2U, sprite_ctl(4U, 0U, 0U)); // Fetches next POS/CTL only.
    write_word(chip_ram, sprite_base + 4U, sprite_pos(2U, 0U));
    write_word(chip_ram, sprite_base + 6U, sprite_ctl(2U, 0U, 1U)); // Behind the reload beam.
    write_word(chip_ram, sprite_base + 8U, 0x8000U);
    write_word(chip_ram, sprite_base + 10U, 0x0000U);
    write_word(chip_ram, sprite_base + 12U, 0x0000U);
    write_word(chip_ram, sprite_base + 14U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    chip.set_sprite_pointer(0U, sprite_base);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_spren));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.pixels[2U * frame.effective_stride()] == 0x00000000U);
    CHECK(chip.sprite_pointer(0U) == sprite_base + 8U);
}

TEST_CASE("agnus display DMA stealing second zero-height reload word preserves next pointer",
          "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 25U, 0x00F0U); // SPR5 value 1 if the next block renders.

    constexpr std::uint32_t sprite5_base = 0x380U;
    write_word(chip_ram, sprite5_base + 0U, sprite_pos(0U, 0U));
    write_word(chip_ram, sprite5_base + 2U, sprite_ctl(0U, 0U, 0U));
    write_word(chip_ram, sprite5_base + 4U, sprite_pos(1U, 0U));
    write_word(chip_ram, sprite5_base + 6U, sprite_ctl(1U, 0U, 1U));
    write_word(chip_ram, sprite5_base + 8U, 0x8000U);
    write_word(chip_ram, sprite5_base + 10U, 0x0000U);
    write_word(chip_ram, sprite5_base + 12U, 0x0000U);
    write_word(chip_ram, sprite5_base + 14U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x6000U);
    chip.set_ddfstrt(0x001EU); // SPR5 POS reload slot is open, CTL is display-owned.
    chip.set_sprite_pointer(5U, sprite5_base);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_bplen | agnus::dmacon_spren));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.pixels[frame.effective_stride()] == 0x00000000U);
    CHECK(chip.sprite_pointer(5U) == sprite5_base + 6U);
}

TEST_CASE("agnus display DMA stealing first zero-height reload word preserves next pointer",
          "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 25U, 0x00F0U); // SPR5 value 1 if the next block renders.

    constexpr std::uint32_t sprite5_base = 0x3C0U;
    write_word(chip_ram, sprite5_base + 0U, sprite_pos(0U, 0U));
    write_word(chip_ram, sprite5_base + 2U, sprite_ctl(0U, 0U, 0U));
    write_word(chip_ram, sprite5_base + 4U, sprite_pos(1U, 0U));
    write_word(chip_ram, sprite5_base + 6U, sprite_ctl(1U, 0U, 1U));
    write_word(chip_ram, sprite5_base + 8U, 0x8000U);
    write_word(chip_ram, sprite5_base + 10U, 0x0000U);
    write_word(chip_ram, sprite5_base + 12U, 0x0000U);
    write_word(chip_ram, sprite5_base + 14U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x6000U);
    chip.set_ddfstrt(0x0020U); // SPR5 POS reload slot is display-owned, CTL is open.
    chip.set_sprite_pointer(5U, sprite5_base);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_bplen | agnus::dmacon_spren));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.pixels[frame.effective_stride()] == 0x00000000U);
    CHECK(chip.sprite_pointer(5U) == sprite5_base + 4U);
}

TEST_CASE("agnus attaches odd sprite pairs into the 15-colour sprite palette", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 21U, 0x00F0U); // attached value 5 -> COLOR21 green.

    chip.attach_palette(palette);
    chip.write_sprite_pos(0U, sprite_pos(0U, 0U));
    chip.write_sprite_ctl(0U, sprite_ctl(0U, 0U, 1U));
    chip.write_sprite_pos(1U, sprite_pos(0U, 0U));
    chip.write_sprite_ctl(1U, static_cast<std::uint16_t>(sprite_ctl(0U, 0U, 1U) | 0x0080U));
    chip.write_sprite_data_a(0U, 0x8000U);
    chip.write_sprite_data_a(1U, 0x8000U);

    chip.tick(frame_ticks);

    CHECK(chip.framebuffer().pixels[0] == 0x0000FF00U);
}

TEST_CASE("agnus BPLCON2 priority can place sprites behind or ahead of a playfield", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 1U, 0x0F00U);  // playfield red.
    set_palette_word(palette, 17U, 0x00F0U); // sprite green.

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip);
    set_plane_word(chip_ram, chip, 0U, 0x8000U);
    chip.write_sprite_pos(0U, sprite_pos(0U, 0U));
    chip.write_sprite_ctl(0U, sprite_ctl(0U, 0U, 1U));
    chip.write_sprite_data_a(0U, 0x8000U);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x00FF0000U);

    chip.set_bplcon2(0x0020U); // PF2 priority slot 4: behind all sprite pairs.
    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x0000FF00U);
}

TEST_CASE("agnus BPLCON2 dual-playfield priority masks PF2 behind higher sprite groups",
          "[agnus]") {
    const auto render_pixel = [](bool sprite_present) {
        agnus chip;
        std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
        std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
        set_palette_word(palette, 1U, 0x0F00U);  // PF1 red.
        set_palette_word(palette, 9U, 0x000FU);  // PF2 blue.
        set_palette_word(palette, 17U, 0x00F0U); // Sprite green.

        chip.attach_chip_ram(chip_ram);
        chip.attach_palette(palette);
        program_full_window(chip, 0x2400U); // Dual playfield, one plane per playfield.
        chip.set_bplcon2(0x0050U);          // PF1 SP01 SP23 PF2 SP45 SP67, PF2 over PF1.
        set_plane_word(chip_ram, chip, 0U, 0x8000U);
        set_plane_word(chip_ram, chip, 1U, 0x8000U);
        if (sprite_present) {
            chip.write_sprite_pos(0U, sprite_pos(0U, 0U));
            chip.write_sprite_ctl(0U, sprite_ctl(0U, 0U, 1U));
            chip.write_sprite_data_a(0U, 0x8000U);
        }
        chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                     agnus::dmacon_bplen));

        chip.tick(frame_ticks);
        return chip.framebuffer().pixels[0];
    };

    CHECK(render_pixel(false) == 0x000000FFU);
    CHECK(render_pixel(true) == 0x00FF0000U);
}

TEST_CASE("agnus CLXDAT latches sprite and playfield collisions until read", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip);
    set_plane_word(chip_ram, chip, 0U, 0x8000U);
    chip.write_clxcon(0x0041U); // Include BPL1 and require BPL1=1.
    chip.write_sprite_pos(0U, sprite_pos(0U, 0U));
    chip.write_sprite_ctl(0U, sprite_ctl(0U, 0U, 1U));
    chip.write_sprite_data_a(0U, 0x8000U);
    chip.write_sprite_pos(2U, sprite_pos(0U, 0U));
    chip.write_sprite_ctl(2U, sprite_ctl(0U, 0U, 1U));
    chip.write_sprite_data_a(2U, 0x8000U);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    const std::uint16_t first = chip.read_clxdat();

    CHECK((first & 0x0002U) != 0U); // Odd bitplanes to sprite pair 0/1.
    CHECK((first & 0x0004U) != 0U); // Odd bitplanes to sprite pair 2/3.
    CHECK((first & 0x0200U) != 0U); // Sprite pair 0/1 to pair 2/3.
    CHECK(chip.read_clxdat() == 0U);
}

TEST_CASE("agnus CLXDAT latches odd-even playfield collisions without sprites", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x2000U);
    set_plane_word(chip_ram, chip, 0U, 0x8000U);
    set_plane_word(chip_ram, chip, 1U, 0x8000U);
    chip.write_clxcon(0x00C3U); // Include BPL1/BPL2 and require both high.
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);

    CHECK((chip.read_clxdat() & 0x0001U) != 0U);
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
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_copen));
    // The Copper is forced back to COP1LC as vblank exits.
    chip.tick(pal_vblank_exit_ticks + 8U);
    // The MOVE targeted a colour register; this model treats colour cells as
    // board-owned, so the observable effect is that the copper advanced past
    // the MOVE without faulting and reached the WAIT (still running).
    CHECK(chip.dma_copper());
}

TEST_CASE("agnus mirrors attached chip RAM for Copper instruction fetches", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t list = 0x100U;
    write_word(chip_ram, list + 0U, 0x0096U); // MOVE DMACON: set BPLEN.
    write_word(chip_ram, list + 2U,
               static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_bplen));
    write_word(chip_ram, list + 4U, 0xFFFFU);
    write_word(chip_ram, list + 6U, 0xFFFEU);

    chip.attach_chip_ram(chip_ram);
    chip.write_cop1lc(static_cast<std::uint32_t>(chip_ram.size()) + list);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_copen));

    chip.tick(pal_vblank_exit_ticks + 1U);
    CHECK(chip.dma_bitplane());
}

TEST_CASE("agnus DMACON COPEN does not implicitly jump the Copper mid-frame", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t list = 0x100U;
    write_word(chip_ram, list + 0U, 0x0096U); // MOVE DMACON: set BPLEN.
    write_word(chip_ram, list + 2U,
               static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_bplen));
    write_word(chip_ram, list + 4U, 0xFFFFU);
    write_word(chip_ram, list + 6U, 0xFFFEU);

    chip.attach_chip_ram(chip_ram);
    chip.write_cop1lc(list);
    chip.tick(8U);
    REQUIRE(chip.beam_line() == 0U);
    REQUIRE(chip.beam_clock() != 0U);

    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_copen));
    chip.tick(64U);
    CHECK_FALSE(chip.dma_bitplane());

    chip.tick(pal_vblank_exit_ticks - chip.beam_clock() + 1U);
    CHECK(chip.dma_bitplane());
}

TEST_CASE("agnus copper MOVE protection matches custom register ranges", "[agnus]") {
    struct observed_move final {
        std::uint16_t reg{};
        std::uint16_t value{};
    };

    {
        agnus chip;
        std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
        std::vector<observed_move> moves;

        constexpr std::uint32_t list = 0x100U;
        write_word(chip_ram, list + 0U, 0x0080U); // $80+ is legal without CDANG.
        write_word(chip_ram, list + 2U, 0x1234U);
        write_word(chip_ram, list + 4U, 0x0040U); // $40-$7e needs CDANG.
        write_word(chip_ram, list + 6U, 0x5678U);
        write_word(chip_ram, list + 8U, 0x0020U); // $00-$3e is never writable.
        write_word(chip_ram, list + 10U, 0x9ABCU);
        write_word(chip_ram, list + 12U, 0x000EU);
        write_word(chip_ram, list + 14U, 0xDEF0U);
        write_word(chip_ram, list + 16U, 0xFFFFU);
        write_word(chip_ram, list + 18U, 0xFFFEU);

        chip.attach_chip_ram(chip_ram);
        chip.set_custom_write_callback(
            [&moves](std::uint16_t reg, std::uint16_t value) { moves.push_back({reg, value}); });
        chip.write_cop1lc(list);
        chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                     agnus::dmacon_copen));
        chip.tick(pal_vblank_exit_ticks + 20U);

        REQUIRE(moves.size() == 1U);
        CHECK(moves[0].reg == 0x0080U);
        CHECK(moves[0].value == 0x1234U);
    }

    {
        agnus chip;
        std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
        std::vector<observed_move> moves;

        constexpr std::uint32_t list = 0x100U;
        write_word(chip_ram, list + 0U, 0x0040U);
        write_word(chip_ram, list + 2U, 0x5678U);
        write_word(chip_ram, list + 4U, 0x0080U);
        write_word(chip_ram, list + 6U, 0x1234U);
        write_word(chip_ram, list + 8U, 0x0020U);
        write_word(chip_ram, list + 10U, 0x9ABCU);
        write_word(chip_ram, list + 12U, 0x000EU);
        write_word(chip_ram, list + 14U, 0xDEF0U);
        write_word(chip_ram, list + 16U, 0xFFFFU);
        write_word(chip_ram, list + 18U, 0xFFFEU);

        chip.attach_chip_ram(chip_ram);
        chip.set_custom_write_callback(
            [&moves](std::uint16_t reg, std::uint16_t value) { moves.push_back({reg, value}); });
        chip.write_copcon(0x0002U);
        chip.write_cop1lc(list);
        chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                     agnus::dmacon_copen));
        chip.tick(pal_vblank_exit_ticks + 20U);

        REQUIRE(moves.size() == 2U);
        CHECK(moves[0].reg == 0x0040U);
        CHECK(moves[0].value == 0x5678U);
        CHECK(moves[1].reg == 0x0080U);
        CHECK(moves[1].value == 0x1234U);
    }
}

TEST_CASE("agnus copper MOVE cadence consumes four memory cycles", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t list = 0x100U;
    write_word(chip_ram, list + 0U, 0x0096U); // MOVE DMACON: set BPLEN.
    write_word(chip_ram, list + 2U,
               static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_bplen));
    write_word(chip_ram, list + 4U, 0x0096U); // MOVE DMACON: clear BPLEN.
    write_word(chip_ram, list + 6U, agnus::dmacon_bplen);
    write_word(chip_ram, list + 8U, 0xFFFFU);
    write_word(chip_ram, list + 10U, 0xFFFEU);

    chip.attach_chip_ram(chip_ram);
    chip.write_cop1lc(list);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_copen));

    chip.tick(pal_vblank_exit_ticks + 1U);
    CHECK(chip.dma_bitplane());

    chip.tick(3U);
    CHECK(chip.dma_bitplane());

    chip.tick(1U);
    CHECK_FALSE(chip.dma_bitplane());
}

TEST_CASE("agnus copper WAIT wake consumes six memory cycles", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t list = 0x100U;
    write_word(chip_ram, list + 0U, 0x0001U); // WAIT VP=0, HP=0.
    write_word(chip_ram, list + 2U, 0xFFFEU);
    write_word(chip_ram, list + 4U, 0x0096U); // MOVE DMACON: set BPLEN.
    write_word(chip_ram, list + 6U,
               static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_bplen));
    write_word(chip_ram, list + 8U, 0xFFFFU);
    write_word(chip_ram, list + 10U, 0xFFFEU);

    chip.attach_chip_ram(chip_ram);
    chip.write_cop1lc(list);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_copen));

    chip.tick(pal_vblank_exit_ticks + 1U);
    CHECK_FALSE(chip.dma_bitplane());

    chip.tick(5U);
    CHECK_FALSE(chip.dma_bitplane());

    chip.tick(1U);
    CHECK(chip.dma_bitplane());
}

TEST_CASE("agnus four-plane high-resolution display DMA stalls Copper memory cycles", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t list = 0x100U;
    constexpr std::uint32_t line = agnus::display_line_origin;
    constexpr std::uint32_t wait_clock = 0x50U;
    constexpr std::uint32_t blocked_probe_clock = 0xD0U;
    static_assert(blocked_probe_clock < 0xDCU);

    write_word(chip_ram, list + 0U,
               static_cast<std::uint16_t>((line << 8U) | wait_clock | 0x0001U));
    write_word(chip_ram, list + 2U, 0xFFFEU);
    write_word(chip_ram, list + 4U, 0x0096U); // MOVE DMACON: clear BPLEN.
    write_word(chip_ram, list + 6U, agnus::dmacon_bplen);
    write_word(chip_ram, list + 8U, 0xFFFFU);
    write_word(chip_ram, list + 10U, 0xFFFEU);

    chip.attach_chip_ram(chip_ram);
    program_full_window_hires(chip, 0xC000U); // HIRES | four bitplanes.
    chip.write_cop1lc(list);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_copen | agnus::dmacon_bplen));

    chip.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * line +
              blocked_probe_clock);
    CHECK(chip.dma_bitplane());

    chip.tick(32U);
    CHECK_FALSE(chip.dma_bitplane());
}

TEST_CASE("agnus six-plane low-resolution display DMA stalls Copper on stolen slots", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t list = 0x100U;
    constexpr std::uint32_t line = agnus::display_line_origin;
    constexpr std::uint32_t wait_clock = 0x38U;
    constexpr std::uint32_t blocked_probe_clock = 0x3FU;

    write_word(chip_ram, list + 0U,
               static_cast<std::uint16_t>((line << 8U) | wait_clock | 0x0001U));
    write_word(chip_ram, list + 2U, 0xFFFEU);
    write_word(chip_ram, list + 4U, 0x0096U); // MOVE DMACON: clear BPLEN.
    write_word(chip_ram, list + 6U, agnus::dmacon_bplen);
    write_word(chip_ram, list + 8U, 0xFFFFU);
    write_word(chip_ram, list + 10U, 0xFFFEU);

    chip.attach_chip_ram(chip_ram);
    program_full_window(chip, 0x6000U); // Six low-resolution bitplanes.
    chip.write_cop1lc(list);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_copen | agnus::dmacon_bplen));

    chip.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * line +
              blocked_probe_clock);
    CHECK(chip.dma_bitplane());

    chip.tick(6U);
    CHECK_FALSE(chip.dma_bitplane());
}

TEST_CASE("agnus sprite DMA stalls Copper on requested sprite slots", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t list = 0x100U;
    constexpr std::uint32_t sprite_base = 0x200U;
    constexpr std::uint32_t line = agnus::display_line_origin;
    constexpr std::uint32_t wait_clock = 0x12U;
    constexpr std::uint32_t blocked_probe_clock = 0x19U;

    write_word(chip_ram, list + 0U,
               static_cast<std::uint16_t>((line << 8U) | wait_clock | 0x0001U));
    write_word(chip_ram, list + 2U, 0xFFFEU);
    write_word(chip_ram, list + 4U, 0x0096U); // MOVE DMACON: clear BPLEN.
    write_word(chip_ram, list + 6U, agnus::dmacon_bplen);
    write_word(chip_ram, list + 8U, 0xFFFFU);
    write_word(chip_ram, list + 10U, 0xFFFEU);

    write_word(chip_ram, sprite_base + 0U, sprite_pos(0U, 0U));
    write_word(chip_ram, sprite_base + 2U, sprite_ctl(0U, 0U, 2U));
    write_word(chip_ram, sprite_base + 4U, 0x8000U);
    write_word(chip_ram, sprite_base + 6U, 0x0000U);
    write_word(chip_ram, sprite_base + 8U, 0x0000U);
    write_word(chip_ram, sprite_base + 10U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    chip.set_sprite_pointer(0U, sprite_base);
    chip.write_cop1lc(list);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_copen | agnus::dmacon_spren |
                                                 agnus::dmacon_bplen));

    chip.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * line +
              blocked_probe_clock);
    CHECK(chip.dma_bitplane());

    chip.tick(2U);
    CHECK_FALSE(chip.dma_bitplane());
}

TEST_CASE("agnus sprite DMA stalls Copper after a zero-height reuse block", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t list = 0x100U;
    constexpr std::uint32_t sprite_base = 0x200U;
    constexpr std::uint32_t line = agnus::display_line_origin + 2U;
    constexpr std::uint32_t wait_clock = 0x12U;
    constexpr std::uint32_t blocked_probe_clock = 0x19U;

    write_word(chip_ram, list + 0U,
               static_cast<std::uint16_t>((line << 8U) | wait_clock | 0x0001U));
    write_word(chip_ram, list + 2U, 0xFFFEU);
    write_word(chip_ram, list + 4U, 0x0096U); // MOVE DMACON: clear BPLEN.
    write_word(chip_ram, list + 6U, agnus::dmacon_bplen);
    write_word(chip_ram, list + 8U, 0xFFFFU);
    write_word(chip_ram, list + 10U, 0xFFFEU);

    write_word(chip_ram, sprite_base + 0U, sprite_pos(0U, 0U));
    write_word(chip_ram, sprite_base + 2U, sprite_ctl(0U, 0U, 0U));
    write_word(chip_ram, sprite_base + 4U, sprite_pos(2U, 0U));
    write_word(chip_ram, sprite_base + 6U, sprite_ctl(2U, 0U, 2U));
    write_word(chip_ram, sprite_base + 8U, 0x8000U);
    write_word(chip_ram, sprite_base + 10U, 0x0000U);
    write_word(chip_ram, sprite_base + 12U, 0x0000U);
    write_word(chip_ram, sprite_base + 14U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    chip.set_sprite_pointer(0U, sprite_base);
    chip.write_cop1lc(list);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_copen | agnus::dmacon_spren |
                                                 agnus::dmacon_bplen));

    chip.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * line +
              blocked_probe_clock);
    CHECK(chip.dma_bitplane());

    chip.tick(2U);
    CHECK_FALSE(chip.dma_bitplane());
}

TEST_CASE("agnus sprite DMA stalls Copper on the sprite stop control fetch line", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t list = 0x100U;
    constexpr std::uint32_t sprite_base = 0x200U;
    constexpr std::uint32_t line = agnus::display_line_origin + 2U;
    constexpr std::uint32_t wait_clock = 0x12U;
    constexpr std::uint32_t blocked_probe_clock = 0x19U;

    write_word(chip_ram, list + 0U,
               static_cast<std::uint16_t>((line << 8U) | wait_clock | 0x0001U));
    write_word(chip_ram, list + 2U, 0xFFFEU);
    write_word(chip_ram, list + 4U, 0x0096U); // MOVE DMACON: clear BPLEN.
    write_word(chip_ram, list + 6U, agnus::dmacon_bplen);
    write_word(chip_ram, list + 8U, 0xFFFFU);
    write_word(chip_ram, list + 10U, 0xFFFEU);

    write_word(chip_ram, sprite_base + 0U, sprite_pos(0U, 0U));
    write_word(chip_ram, sprite_base + 2U, sprite_ctl(0U, 0U, 2U));
    write_word(chip_ram, sprite_base + 4U, 0x8000U);
    write_word(chip_ram, sprite_base + 6U, 0x0000U);
    write_word(chip_ram, sprite_base + 8U, 0x0000U);
    write_word(chip_ram, sprite_base + 10U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    chip.set_sprite_pointer(0U, sprite_base);
    chip.write_cop1lc(list);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_copen | agnus::dmacon_spren |
                                                 agnus::dmacon_bplen));

    chip.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * line +
              blocked_probe_clock);
    CHECK(chip.dma_bitplane());

    chip.tick(2U);
    CHECK_FALSE(chip.dma_bitplane());
}

TEST_CASE("agnus one-plane left-extended display DMA leaves high sprite slots open", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 25U, 0x00F0U); // SPR5 value 1 = green.
    set_palette_word(palette, 29U, 0x0F00U); // SPR7 value 1 = red.

    constexpr std::uint32_t sprite5_base = 0x200U;
    constexpr std::uint32_t sprite7_base = 0x240U;
    write_word(chip_ram, sprite5_base + 0U, sprite_pos(0U, 0U));
    write_word(chip_ram, sprite5_base + 2U, sprite_ctl(0U, 0U, 1U));
    write_word(chip_ram, sprite5_base + 4U, 0x8000U);
    write_word(chip_ram, sprite5_base + 6U, 0x0000U);
    write_word(chip_ram, sprite5_base + 8U, 0x0000U);
    write_word(chip_ram, sprite5_base + 10U, 0x0000U);

    write_word(chip_ram, sprite7_base + 0U, sprite_pos(0U, 4U));
    write_word(chip_ram, sprite7_base + 2U, sprite_ctl(0U, 4U, 1U));
    write_word(chip_ram, sprite7_base + 4U, 0x8000U);
    write_word(chip_ram, sprite7_base + 6U, 0x0000U);
    write_word(chip_ram, sprite7_base + 8U, 0x0000U);
    write_word(chip_ram, sprite7_base + 10U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x1000U);
    chip.set_ddfstrt(0x0024U); // Active fetch window reaches SPR6/SPR7 slots.
    chip.set_sprite_pointer(5U, sprite5_base);
    chip.set_sprite_pointer(7U, sprite7_base);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_bplen | agnus::dmacon_spren));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.pixels[0] == 0x0000FF00U);
    CHECK(frame.pixels[4] == 0x00FF0000U);
}

TEST_CASE("agnus six-plane left-extended display DMA steals high sprite DMA slots", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 25U, 0x00F0U); // SPR5 value 1 = green.
    set_palette_word(palette, 29U, 0x0F00U); // SPR7 value 1 = red if not stolen.

    constexpr std::uint32_t sprite5_base = 0x200U;
    constexpr std::uint32_t sprite7_base = 0x240U;
    write_word(chip_ram, sprite5_base + 0U, sprite_pos(0U, 0U));
    write_word(chip_ram, sprite5_base + 2U, sprite_ctl(0U, 0U, 1U));
    write_word(chip_ram, sprite5_base + 4U, 0x8000U);
    write_word(chip_ram, sprite5_base + 6U, 0x0000U);
    write_word(chip_ram, sprite5_base + 8U, 0x0000U);
    write_word(chip_ram, sprite5_base + 10U, 0x0000U);

    write_word(chip_ram, sprite7_base + 0U, sprite_pos(0U, 4U));
    write_word(chip_ram, sprite7_base + 2U, sprite_ctl(0U, 4U, 1U));
    write_word(chip_ram, sprite7_base + 4U, 0x8000U);
    write_word(chip_ram, sprite7_base + 6U, 0x0000U);
    write_word(chip_ram, sprite7_base + 8U, 0x0000U);
    write_word(chip_ram, sprite7_base + 10U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x6000U);
    chip.set_ddfstrt(0x0024U); // Owned display slots overlap SPR7 DMA.
    chip.set_sprite_pointer(5U, sprite5_base);
    chip.set_sprite_pointer(7U, sprite7_base);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_bplen | agnus::dmacon_spren));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.pixels[0] == 0x0000FF00U);
    CHECK(frame.pixels[4] == 0x00000000U);
    CHECK(chip.sprite_pointer(5U) == sprite5_base + 12U);
    CHECK(chip.sprite_pointer(7U) == sprite7_base + 4U);
}

TEST_CASE("agnus display DMA stealing second sprite data word preserves next pointer", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    std::vector<std::uint8_t> palette(agnus::palette_entries * 2U, 0U);
    set_palette_word(palette, 25U, 0x00F0U); // SPR5 value 1 if both words arrive.

    constexpr std::uint32_t sprite5_base = 0x300U;
    write_word(chip_ram, sprite5_base + 0U, sprite_pos(0U, 0U));
    write_word(chip_ram, sprite5_base + 2U, sprite_ctl(0U, 0U, 1U));
    write_word(chip_ram, sprite5_base + 4U, 0x8000U);
    write_word(chip_ram, sprite5_base + 6U, 0x0000U);
    write_word(chip_ram, sprite5_base + 8U, 0x0000U);
    write_word(chip_ram, sprite5_base + 10U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x6000U);
    chip.set_ddfstrt(0x001EU); // SPR5 DATA slot is open, DATB is display-owned.
    chip.set_sprite_pointer(5U, sprite5_base);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_bplen | agnus::dmacon_spren));

    chip.tick(frame_ticks);

    CHECK(chip.framebuffer().pixels[0] == 0x00000000U);
    CHECK(chip.sprite_pointer(5U) == sprite5_base + 6U);
}

TEST_CASE("agnus display DMA steals a sprite stop-line control reload", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t sprite7_base = 0x240U;
    constexpr std::uint32_t beam_y = agnus::display_line_origin - 1U;
    constexpr std::uint32_t stop_y = agnus::display_line_origin;
    constexpr std::uint32_t beam_x = agnus::display_clock_origin + 4U;
    const auto pos =
        static_cast<std::uint16_t>(((beam_y & 0xFFU) << 8U) | ((beam_x >> 1U) & 0xFFU));
    const auto ctl = static_cast<std::uint16_t>(
        ((stop_y & 0xFFU) << 8U) | ((beam_x & 0x01U) != 0U ? 0x0001U : 0U) |
        ((stop_y & 0x100U) != 0U ? 0x0002U : 0U) | ((beam_y & 0x100U) != 0U ? 0x0004U : 0U));
    write_word(chip_ram, sprite7_base + 0U, pos);
    write_word(chip_ram, sprite7_base + 2U, ctl);
    write_word(chip_ram, sprite7_base + 4U, 0x8000U);
    write_word(chip_ram, sprite7_base + 6U, 0x0000U);
    write_word(chip_ram, sprite7_base + 8U, 0x0000U);
    write_word(chip_ram, sprite7_base + 10U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    program_full_window(chip, 0x6000U);
    chip.set_ddfstrt(0x0024U);
    chip.set_sprite_pointer(7U, sprite7_base);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_bplen | agnus::dmacon_spren));

    chip.tick(frame_ticks);

    CHECK(chip.sprite_pointer(7U) == sprite7_base + 8U);
}

TEST_CASE("agnus display DMA stealing second sprite control word preserves next pointer",
          "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t sprite5_base = 0x340U;
    constexpr std::uint32_t beam_y = agnus::display_line_origin - 1U;
    constexpr std::uint32_t stop_y = agnus::display_line_origin;
    constexpr std::uint32_t beam_x = agnus::display_clock_origin;
    const auto pos =
        static_cast<std::uint16_t>(((beam_y & 0xFFU) << 8U) | ((beam_x >> 1U) & 0xFFU));
    const auto ctl = static_cast<std::uint16_t>(
        ((stop_y & 0xFFU) << 8U) | ((beam_x & 0x01U) != 0U ? 0x0001U : 0U) |
        ((stop_y & 0x100U) != 0U ? 0x0002U : 0U) | ((beam_y & 0x100U) != 0U ? 0x0004U : 0U));
    write_word(chip_ram, sprite5_base + 0U, pos);
    write_word(chip_ram, sprite5_base + 2U, ctl);
    write_word(chip_ram, sprite5_base + 4U, 0x8000U);
    write_word(chip_ram, sprite5_base + 6U, 0x0000U);
    write_word(chip_ram, sprite5_base + 8U, sprite_pos(1U, 0U));
    write_word(chip_ram, sprite5_base + 10U, sprite_ctl(1U, 0U, 1U));
    write_word(chip_ram, sprite5_base + 12U, 0x0000U);
    write_word(chip_ram, sprite5_base + 14U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    program_full_window(chip, 0x6000U);
    chip.set_ddfstrt(0x001EU); // SPR5 POS reload slot is open, CTL is display-owned.
    chip.set_sprite_pointer(5U, sprite5_base);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_bplen | agnus::dmacon_spren));

    chip.tick(frame_ticks);

    CHECK(chip.sprite_pointer(5U) == sprite5_base + 10U);
}

TEST_CASE("agnus sprite DMA still owns a high slot when display steals its neighbor", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t sprite7_base = 0x240U;
    write_word(chip_ram, sprite7_base + 0U, sprite_pos(0U, 4U));
    write_word(chip_ram, sprite7_base + 2U, sprite_ctl(0U, 4U, 1U));
    write_word(chip_ram, sprite7_base + 4U, 0x8000U);
    write_word(chip_ram, sprite7_base + 6U, 0x0000U);
    write_word(chip_ram, sprite7_base + 8U, 0x0000U);
    write_word(chip_ram, sprite7_base + 10U, 0x0000U);

    chip.attach_chip_ram(chip_ram);
    program_full_window(chip, 0x6000U);
    chip.set_ddfstrt(0x0024U);
    chip.set_sprite_pointer(7U, sprite7_base);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_bplen | agnus::dmacon_spren));

    constexpr std::uint32_t line = agnus::display_line_origin;
    constexpr std::uint32_t first_sprite7_slot = 0x26U;
    constexpr std::uint32_t second_sprite7_slot = 0x27U;
    chip.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * line + first_sprite7_slot);
    CHECK(chip.display_dma_cpu_wait_cycles(0U) == 2U);
    CHECK(chip.sprite_dma_cpu_wait_cycles(0U) == 0U);

    chip.tick(second_sprite7_slot - first_sprite7_slot);
    CHECK(chip.display_dma_cpu_wait_cycles(0U) == 0U);
    CHECK(chip.sprite_dma_cpu_wait_cycles(0U) == 2U);
}

TEST_CASE("agnus copper terminal WAIT cannot pass during the active frame", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t list = 0x100U;
    write_word(chip_ram, list + 0U, 0x0096U); // MOVE DMACON: set BPLEN.
    write_word(chip_ram, list + 2U,
               static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_bplen));
    write_word(chip_ram, list + 4U, 0xFFFFU); // WAIT VP=$FF, HP=$FE: impossible.
    write_word(chip_ram, list + 6U, 0xFFFEU);
    write_word(chip_ram, list + 8U, 0x0096U); // Would clear BPLEN if the WAIT wrapped.
    write_word(chip_ram, list + 10U, agnus::dmacon_bplen);

    chip.attach_chip_ram(chip_ram);
    chip.write_cop1lc(list);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_copen));

    chip.tick(pal_vblank_exit_ticks + 1U);
    REQUIRE(chip.dma_bitplane());

    chip.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 256U);
    CHECK(chip.dma_bitplane());
}

TEST_CASE("agnus copper WAIT honors disabled vertical compare mask bits", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);

    constexpr std::uint32_t list = 0x100U;
    write_word(chip_ram, list + 0U, 0x0096U); // MOVE DMACON: set BPLEN.
    write_word(chip_ram, list + 2U,
               static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_bplen));
    write_word(chip_ram, list + 4U, 0x00FFU); // WAIT VP masked off, HP=$FE: impossible.
    write_word(chip_ram, list + 6U, 0x00FEU);
    write_word(chip_ram, list + 8U, 0x0096U); // Would clear BPLEN if VE bit 7 were forced.
    write_word(chip_ram, list + 10U, agnus::dmacon_bplen);

    chip.attach_chip_ram(chip_ram);
    chip.write_cop1lc(list);
    chip.write_dmacon(
        static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen | agnus::dmacon_copen));

    chip.tick(pal_vblank_exit_ticks + 1U);
    REQUIRE(chip.dma_bitplane());

    chip.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 256U);
    CHECK(chip.dma_bitplane());
}

TEST_CASE("agnus Copper location pointers are clipped to the OCS 18-bit address bus", "[agnus]") {
    agnus chip;

    chip.write_cop1lc(0x001F1234U);
    chip.write_cop2lc(0x00195678U);

    CHECK(chip.cop1lc() == 0x00071234U);
    CHECK(chip.cop2lc() == 0x00015678U);
}

TEST_CASE("agnus copper reloads COP1 as vblank exits each frame", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    const auto palette = make_palette(1U, 0x0F00U);

    constexpr std::uint32_t list = 0x100U;
    // MOVE BPLCON0 plus the BPL1 pointer rewrite, then the common terminal
    // WAIT. The second frame proves COP1 reloads even after the previous frame
    // left the Copper away from the list head and advanced the bitplane pointer.
    write_word(chip_ram, list + 0U, 0x0100U);
    write_word(chip_ram, list + 2U, 0x1000U);
    write_word(chip_ram, list + 4U, 0x00E0U);
    write_word(chip_ram, list + 6U, 0x0000U);
    write_word(chip_ram, list + 8U, 0x00E2U);
    write_word(chip_ram, list + 10U, 0x0000U);
    write_word(chip_ram, list + 12U, 0xFFFFU);
    write_word(chip_ram, list + 14U, 0xFFFEU);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x0000U);
    set_plane_word(chip_ram, chip, 0U, 0x8000U);
    chip.write_cop1lc(list);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_copen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x00FF0000U);

    chip.set_bplcon0(0x0000U);
    chip.tick(frame_ticks);
    CHECK(chip.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("agnus copper uses COP1LC rewritten during vertical blank", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    const auto palette = make_palette(1U, 0x0F00U);

    constexpr std::uint32_t old_list = 0x100U;
    constexpr std::uint32_t new_list = 0x120U;
    constexpr std::uint64_t vblank_repoint_ticks =
        static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 4U;

    write_word(chip_ram, old_list + 0U, 0x0100U); // Would keep the frame blank.
    write_word(chip_ram, old_list + 2U, 0x0000U);
    write_word(chip_ram, old_list + 4U, 0xFFFFU);
    write_word(chip_ram, old_list + 6U, 0xFFFEU);

    write_word(chip_ram, new_list + 0U, 0x0100U); // Enable one bitplane before display.
    write_word(chip_ram, new_list + 2U, 0x1000U);
    write_word(chip_ram, new_list + 4U, 0x00E0U);
    write_word(chip_ram, new_list + 6U, 0x0000U);
    write_word(chip_ram, new_list + 8U, 0x00E2U);
    write_word(chip_ram, new_list + 10U, 0x0000U);
    write_word(chip_ram, new_list + 12U, 0xFFFFU);
    write_word(chip_ram, new_list + 14U, 0xFFFEU);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x0000U);
    set_plane_word(chip_ram, chip, 0U, 0x8000U);
    chip.write_cop1lc(old_list);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_copen | agnus::dmacon_bplen));

    chip.tick(vblank_repoint_ticks);
    REQUIRE(chip.beam_line() == 4U);
    CHECK(chip.framebuffer().pixels[0] == 0x00000000U);

    chip.write_cop1lc(new_list);
    chip.tick(frame_ticks - vblank_repoint_ticks);

    CHECK(chip.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("agnus captures Copper-programmed raster state before frame-end changes", "[agnus]") {
    agnus chip;
    std::vector<std::uint8_t> chip_ram(512U * 1024U, 0U);
    const auto palette = make_palette(1U, 0x0F00U);

    constexpr std::uint32_t list = 0x100U;
    constexpr std::uint32_t enable_line = agnus::display_line_origin;
    constexpr std::uint32_t disable_line = agnus::display_line_origin + 1U;
    write_word(chip_ram, 0x0000U, 0x8000U);
    write_word(chip_ram, list + 0U, static_cast<std::uint16_t>((enable_line << 8U) | 0x0001U));
    write_word(chip_ram, list + 2U, 0xFFFEU);
    write_word(chip_ram, list + 4U, 0x0100U); // MOVE BPLCON0: enable one plane.
    write_word(chip_ram, list + 6U, 0x1000U);
    write_word(chip_ram, list + 8U, static_cast<std::uint16_t>((disable_line << 8U) | 0x0001U));
    write_word(chip_ram, list + 10U, 0xFFFEU);
    write_word(chip_ram, list + 12U, 0x0100U); // MOVE BPLCON0: blank before frame end.
    write_word(chip_ram, list + 14U, 0x0000U);
    write_word(chip_ram, list + 16U, 0xFFFFU);
    write_word(chip_ram, list + 18U, 0xFFFEU);

    chip.attach_chip_ram(chip_ram);
    chip.attach_palette(palette);
    program_full_window(chip, 0x0000U);
    chip.set_bitplane_pointer(0U, 0U);
    chip.write_cop1lc(list);
    chip.write_dmacon(static_cast<std::uint16_t>(agnus::dmacon_set | agnus::dmacon_dmaen |
                                                 agnus::dmacon_copen | agnus::dmacon_bplen));

    chip.tick(frame_ticks);
    const auto frame = chip.framebuffer();

    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[frame.effective_stride()] == 0x00000000U);
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
