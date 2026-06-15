#include "s_ppu.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::video::s_ppu;

    constexpr std::uint64_t frame_ticks =
        static_cast<std::uint64_t>(s_ppu::line_dots) * s_ppu::frame_lines;

    // Drive the chip's $2118/$2119 port to deposit `word` at VRAM word `addr`
    // with the simplest increment mode (+1, increment on the high byte).
    void poke_vram(s_ppu& ppu, std::uint16_t addr, std::uint16_t word) {
        ppu.write_register(s_ppu::reg_vmain, 0x80U); // +1, increment on $2119
        ppu.write_register(s_ppu::reg_vmaddl, static_cast<std::uint8_t>(addr & 0xFFU));
        ppu.write_register(s_ppu::reg_vmaddh, static_cast<std::uint8_t>(addr >> 8U));
        ppu.write_register(s_ppu::reg_vmdatal, static_cast<std::uint8_t>(word & 0xFFU));
        ppu.write_register(s_ppu::reg_vmdatah, static_cast<std::uint8_t>(word >> 8U));
    }

    // Drive the $2121/$2122 port to set CGRAM colour `index` to a 15-bit word.
    void poke_cgram(s_ppu& ppu, std::uint8_t index, std::uint16_t color) {
        ppu.write_register(s_ppu::reg_cgadd, index);
        ppu.write_register(s_ppu::reg_cgdata, static_cast<std::uint8_t>(color & 0xFFU));
        ppu.write_register(s_ppu::reg_cgdata, static_cast<std::uint8_t>((color >> 8U) & 0x7FU));
    }

    // Write a 4bpp character whose every pixel is value `pen` (0-15) at VRAM
    // word `char_word` (16 words per character).
    void make_solid_char(s_ppu& ppu, std::uint16_t char_word, std::uint8_t pen) {
        for (std::uint16_t row = 0; row < 8U; ++row) {
            // planes 0/1 in words 0..7, planes 2/3 in words 8..15;
            // low byte = even plane, high byte = odd plane.
            const std::uint8_t p0 = (pen & 0x01U) != 0U ? 0xFFU : 0x00U;
            const std::uint8_t p1 = (pen & 0x02U) != 0U ? 0xFFU : 0x00U;
            const std::uint8_t p2 = (pen & 0x04U) != 0U ? 0xFFU : 0x00U;
            const std::uint8_t p3 = (pen & 0x08U) != 0U ? 0xFFU : 0x00U;
            poke_vram(ppu, static_cast<std::uint16_t>(char_word + row),
                      static_cast<std::uint16_t>(p0 | (p1 << 8U)));
            poke_vram(ppu, static_cast<std::uint16_t>(char_word + 8U + row),
                      static_cast<std::uint16_t>(p2 | (p3 << 8U)));
        }
    }

} // namespace

TEST_CASE("s_ppu registers through the chip registry", "[s_ppu]") {
    auto chip = mnemos::chips::create_chip("nintendo.s_ppu");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::video);
    CHECK(chip->metadata().manufacturer == "Sony");
}

TEST_CASE("s_ppu resets into forced blank with the beam at the origin", "[s_ppu]") {
    s_ppu ppu;
    ppu.reset(mnemos::chips::reset_kind::power_on);
    CHECK(ppu.force_blank());
    CHECK(ppu.brightness() == 0U);
    CHECK(ppu.beam_line() == 0U);
    CHECK(ppu.beam_dot() == 0U);
    CHECK(ppu.frame_index() == 0U);
    CHECK(ppu.vram_address() == 0U);
}

TEST_CASE("s_ppu fires the scanline callback for every line and bumps frame_index", "[s_ppu]") {
    s_ppu ppu;
    std::vector<std::uint32_t> vblank_lines;
    std::vector<std::uint32_t> scanlines;
    ppu.set_vblank_callback([&](std::uint32_t line) { vblank_lines.push_back(line); });
    ppu.set_scanline_callback([&](std::uint32_t line) { scanlines.push_back(line); });

    ppu.tick(frame_ticks);
    CHECK(ppu.frame_index() == 1U);
    REQUIRE(vblank_lines.size() == 1U);
    CHECK(vblank_lines[0] == s_ppu::visible_height);
    REQUIRE(scanlines.size() == s_ppu::frame_lines);
    CHECK(scanlines.front() == 0U);
    CHECK(scanlines.back() == s_ppu::frame_lines - 1U);

    ppu.tick(frame_ticks);
    CHECK(ppu.frame_index() == 2U);
    CHECK(ppu.beam_line() == 0U);
    CHECK(ppu.beam_dot() == 0U);
}

TEST_CASE("s_ppu VRAM auto-increment honours VMAIN step and inc-on-high", "[s_ppu]") {
    s_ppu ppu;
    // +1 step, increment on the high byte: the low write does NOT advance,
    // the high write advances by one word.
    ppu.write_register(s_ppu::reg_vmain, 0x80U);
    ppu.write_register(s_ppu::reg_vmaddl, 0x00U);
    ppu.write_register(s_ppu::reg_vmaddh, 0x00U);
    ppu.write_register(s_ppu::reg_vmdatal, 0x34U);
    CHECK(ppu.vram_address() == 0x0000U); // low write held the address
    ppu.write_register(s_ppu::reg_vmdatah, 0x12U);
    CHECK(ppu.vram_address() == 0x0001U); // high write advanced one word

    // +32 step, increment on the low byte.
    ppu.write_register(s_ppu::reg_vmain, 0x01U);
    ppu.write_register(s_ppu::reg_vmaddl, 0x00U);
    ppu.write_register(s_ppu::reg_vmaddh, 0x00U);
    ppu.write_register(s_ppu::reg_vmdatal, 0x00U);
    CHECK(ppu.vram_address() == 0x0020U); // advanced by 32 on the low write
}

TEST_CASE("s_ppu decodes a 4bpp BG1 character through CGRAM colour", "[s_ppu]") {
    s_ppu ppu;
    // Tilemap entry 0 (map base 0) -> character 1, palette group 0.
    poke_vram(ppu, 0x0000U, 0x0001U);
    // Character 1 (word base 16) solid pen 5.
    make_solid_char(ppu, 16U, 5U);
    // Palette group 0, pen 5 -> CGRAM index 5. 15-bit BGR red full = 0x001F.
    poke_cgram(ppu, 5U, 0x001FU);
    // Leave force blank, full brightness.
    ppu.write_register(s_ppu::reg_inidisp, 0x0FU);

    ppu.tick(frame_ticks);
    const auto frame = ppu.framebuffer();
    CHECK(frame.width == s_ppu::visible_width);
    CHECK(frame.height == s_ppu::visible_height);
    CHECK(frame.pixels[0] == 0x00FF0000U); // pen 5 -> red

    SECTION("force blank paints black") {
        ppu.write_register(s_ppu::reg_inidisp, 0x8FU); // force blank, brightness 15
        ppu.tick(frame_ticks);
        CHECK(ppu.framebuffer().pixels[0] == 0x00000000U);
    }

    SECTION("half brightness halves each channel") {
        // green full at index 5; brightness 8 -> 0xFF*8/15 = 0x88.
        poke_cgram(ppu, 5U, static_cast<std::uint16_t>(0x1FU << 5U)); // green
        ppu.write_register(s_ppu::reg_inidisp, 0x08U);                // brightness 8
        ppu.tick(frame_ticks);
        CHECK(ppu.framebuffer().pixels[0] == 0x00008800U);
    }
}

TEST_CASE("s_ppu honours the tilemap flip-x bit and pen-0 transparency", "[s_ppu]") {
    s_ppu ppu;
    // Character 1 (word base 16): single pixel set at column 0 of row 0
    // (plane 0, MSB). Word 16 = planes 0/1 of row 0; MSB of the low byte set.
    poke_vram(ppu, 16U, 0x0080U);
    // Tilemap entry 0 (map base 0) -> character 1, palette 0, flip-x (bit 14).
    poke_vram(ppu, 0x0000U, 0x4001U);
    poke_cgram(ppu, 1U, 0x7FFFU); // pen 1 -> white (all guns full)
    ppu.write_register(s_ppu::reg_inidisp, 0x0FU);

    ppu.tick(frame_ticks);
    const auto frame = ppu.framebuffer();
    // Flip-x mirrors the column-0 pixel to column 7.
    CHECK(frame.pixels[7] == 0x00FFFFFFU);
    CHECK(frame.pixels[0] == 0x00000000U); // pen 0 there -> transparent backdrop (black)
}

TEST_CASE("s_ppu renders from board-attached VRAM/CGRAM spans", "[s_ppu]") {
    s_ppu ppu;
    // Synthetic raw VRAM: a 32 K-word buffer. Tilemap entry 0 (word 0) ->
    // character 1, palette group 1; character 1 (words 16..31) solid pen 1.
    // The entry word lives at word 0, so the character must NOT start there.
    std::vector<std::uint8_t> vram(s_ppu::vram_words * 2U, 0U);
    const auto put_word = [&](std::size_t word, std::uint16_t value) {
        vram[word * 2U] = static_cast<std::uint8_t>(value & 0xFFU);
        vram[word * 2U + 1U] = static_cast<std::uint8_t>(value >> 8U);
    };
    put_word(0U, static_cast<std::uint16_t>((1U << 10U) | 1U)); // entry: char 1, palette 1
    for (std::uint16_t row = 0; row < 8U; ++row) {
        put_word(16U + row, 0x00FFU);      // char 1 plane 0 solid
        put_word(16U + 8U + row, 0x0000U); // planes 2/3 empty -> pen value 1
    }

    // Synthetic CGRAM: palette group 1 pen 1 -> index 17 -> blue full.
    std::vector<std::uint8_t> cgram(s_ppu::cgram_words * 2U, 0U);
    const std::uint16_t blue = static_cast<std::uint16_t>(0x1FU << 10U);
    cgram[17U * 2U] = static_cast<std::uint8_t>(blue & 0xFFU);
    cgram[17U * 2U + 1U] = static_cast<std::uint8_t>(blue >> 8U);

    ppu.attach_vram(vram);
    ppu.attach_cgram(cgram);
    ppu.write_register(s_ppu::reg_inidisp, 0x0FU); // unblank, full brightness

    ppu.tick(frame_ticks);
    CHECK(ppu.framebuffer().pixels[0] == 0x000000FFU); // pen 1 in group 1 -> blue
}

TEST_CASE("s_ppu save_state / load_state round-trips", "[s_ppu]") {
    s_ppu ppu;
    poke_vram(ppu, 0x0000U, 0x0001U);
    make_solid_char(ppu, 16U, 5U);
    poke_cgram(ppu, 5U, 0x001FU);
    ppu.write_register(s_ppu::reg_inidisp, 0x0FU);
    ppu.tick(frame_ticks);
    REQUIRE(ppu.framebuffer().pixels[0] == 0x00FF0000U); // original renders red

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    ppu.save_state(writer);
    REQUIRE(blob.size() > 0U);

    s_ppu restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    CHECK(reader.ok());
    CHECK(restored.frame_index() == ppu.frame_index());
    CHECK(restored.brightness() == ppu.brightness());
    CHECK(restored.force_blank() == ppu.force_blank());
    CHECK(restored.vram_address() == ppu.vram_address());

    // The restored chip renders the same frame as the original.
    restored.tick(frame_ticks);
    ppu.tick(frame_ticks);
    const auto a = ppu.framebuffer();
    const auto b = restored.framebuffer();
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
    CHECK(b.pixels[0] == a.pixels[0]);
    CHECK(b.pixels[0] == 0x00FF0000U);
}
