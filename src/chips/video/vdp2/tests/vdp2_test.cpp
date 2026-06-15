#include "vdp2.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <cstdint>
#include <span>

namespace {

    using mnemos::chips::video::vdp2;

    void wr16(std::span<std::uint8_t> mem, std::uint32_t byte_off, std::uint16_t value) {
        mem[byte_off + 0U] = static_cast<std::uint8_t>(value >> 8U); // big-endian word
        mem[byte_off + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
    }

    // Set CRAM entry `index` (mode 0/1 path: 2 bytes/entry, big-endian 0BGR1555).
    void set_cram_1555(vdp2& v, std::uint16_t index, std::uint8_t r5, std::uint8_t g5,
                       std::uint8_t b5) {
        const std::uint16_t raw =
            static_cast<std::uint16_t>((r5 & 0x1FU) | ((g5 & 0x1FU) << 5U) | ((b5 & 0x1FU) << 10U));
        const std::uint32_t off = static_cast<std::uint32_t>(index) * 2U;
        v.cram()[off + 0U] = static_cast<std::uint8_t>(raw >> 8U);
        v.cram()[off + 1U] = static_cast<std::uint8_t>(raw & 0xFFU);
    }

    // Run a whole frame: tick() advances one scanline per cycle, so cross the
    // vblank boundary once (rendering completes when the beam re-enters vblank).
    void render_one_frame(vdp2& v) {
        v.tick(static_cast<std::uint64_t>(vdp2::total_lines) + vdp2::render_height + 1U);
    }

} // namespace

TEST_CASE("vdp2 registers through the chip registry", "[vdp2]") {
    auto chip = mnemos::chips::create_chip("sega.vdp2");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::video);
    CHECK(chip->metadata().manufacturer == "Sega");
    CHECK(chip->metadata().part_number == "vdp2");
}

TEST_CASE("vdp2 decodes TVMD / BGON / RAMCTL on write", "[vdp2]") {
    vdp2 v;
    v.reg_write(vdp2::reg_tvmd, 0x8000U); // DISP on, hres 0 (320), vres 0 (224)
    CHECK(v.display_on());
    CHECK(v.display_width() == 320U);
    CHECK(v.display_height() == 224U);

    v.reg_write(vdp2::reg_tvmd, 0x8003U); // hres 3 -> 704
    CHECK(v.display_width() == 704U);

    v.reg_write(vdp2::reg_ramctl, 0x1000U); // CRMD = 1
    CHECK(v.cram_mode() == 1U);
}

TEST_CASE("vdp2 TVSTAT asserts VBLANK while display is off", "[vdp2]") {
    vdp2 v;
    // Display off (default): VBLANK bit (1<<3) reads set.
    CHECK((v.reg_read(vdp2::reg_tvstat) & (1U << 3U)) != 0U);
    v.reg_write(vdp2::reg_tvmd, 0x8000U); // display on, not in vblank
    CHECK((v.reg_read(vdp2::reg_tvstat) & (1U << 3U)) == 0U);
    v.set_vblank(true);
    CHECK((v.reg_read(vdp2::reg_tvstat) & (1U << 3U)) != 0U);
}

TEST_CASE("vdp2 reads back all three CRAM colour modes", "[vdp2]") {
    vdp2 v;
    // Mode 0: 15-bit. Entry 1 = full red (0BGR1555 R=0x1F).
    v.reg_write(vdp2::reg_ramctl, 0x0000U);
    set_cram_1555(v, 1U, 0x1FU, 0U, 0U);
    CHECK(v.palette_read(1U) == 0x00FF0000U);

    // Mode 2: 24-bit, 4-byte slots [unused, B, G, R].
    v.reg_write(vdp2::reg_ramctl, 0x2000U);
    v.cram()[2U * 4U + 1U] = 0x12U; // B
    v.cram()[2U * 4U + 2U] = 0x34U; // G
    v.cram()[2U * 4U + 3U] = 0x56U; // R
    CHECK(v.palette_read(2U) == 0x00563412U);
}

TEST_CASE("vdp2 renders one NBG tilemap layer through the palette", "[vdp2]") {
    vdp2 v;
    auto vram = v.vram();

    // Build an 8x8 4bpp cell for char #0x400 (cell_addr 0x8000), clear of the
    // page-0 pattern-name table [0, 0x4000). Every dot-code = 1. 4bpp packs two
    // pixels per byte; 0x11 = both pixels = 1.
    constexpr std::uint32_t cell = 0x400U * 0x20U; // char_num * 0x20
    for (std::uint32_t b = 0; b < 32U; ++b) {
        vram[cell + b] = 0x11U;
    }

    // Point every 2-word pattern-name entry in the first page at char 0x400 so
    // the whole layer renders dot-code 1.
    for (std::uint32_t e = 0; e < 0x4000U; e += 4U) {
        wr16(vram, e, 0x0000U);      // word0: palette/control = 0 (no flip)
        wr16(vram, e + 2U, 0x0400U); // word1: char number 0x400
    }

    // CRAM entry 1 = green (4bpp palette index = palette*16 + dot = 0*16 + 1).
    v.reg_write(vdp2::reg_ramctl, 0x0000U);
    set_cram_1555(v, 1U, 0U, 0x1FU, 0U);

    // Enable display + NBG0 with priority 1, 4bpp 8x8, 2-word names, plane 1x1.
    v.reg_write(vdp2::reg_tvmd, 0x8000U);
    v.reg_write(vdp2::reg_bgon, 0x0001U);   // N0ON
    v.reg_write(vdp2::reg_prina, 0x0001U);  // NBG0 priority 1
    v.reg_write(vdp2::reg_chctla, 0x0000U); // NBG0: 4bpp (bits4-6=0), 8x8 (bit0=0)
    v.reg_write(vdp2::reg_pncn0, 0x0000U);  // 2-word name format
    v.reg_write(vdp2::reg_plsz, 0x0000U);   // plane size 1x1
    v.reg_write(vdp2::reg_mpofn, 0x0000U);  // map offset 0
    v.reg_write(vdp2::reg_mpabn0, 0x0000U); // planes A/B = page 0
    v.reg_write(0x42U, 0x0000U);            // MPCDN0: planes C/D = page 0

    render_one_frame(v);

    const auto frame = v.framebuffer();
    REQUIRE(frame.pixels != nullptr);
    CHECK(frame.width == vdp2::render_width);
    CHECK(frame.height == vdp2::render_height);
    CHECK(frame.pixels[0] == 0x0000FF00U); // dot-code 1 -> CRAM entry 1 = green
    CHECK(frame.pixels[100U] == 0x0000FF00U);
}

TEST_CASE("vdp2 priority orders NBG layers and honours transparency", "[vdp2]") {
    vdp2 v;
    auto vram = v.vram();

    // NBG0 char 0x400 (dot 1) at cell 0x8000; NBG1 char 0x401 (dot 2) at cell
    // 0x8020. Both clear of their pattern-name tables (pages 0 and 1).
    for (std::uint32_t b = 0; b < 32U; ++b) {
        vram[0x8000U + b] = 0x11U; // char 0x400: all dot 1
        vram[0x8020U + b] = 0x22U; // char 0x401: all dot 2
    }
    // NBG0 name table fills page 0; NBG1 name table fills page 1 (0x4000).
    for (std::uint32_t e = 0; e < 0x4000U; e += 4U) {
        wr16(vram, e, 0x0000U);
        wr16(vram, e + 2U, 0x0400U); // NBG0 -> char 0x400
        wr16(vram, 0x4000U + e, 0x0000U);
        wr16(vram, 0x4000U + e + 2U, 0x0401U); // NBG1 -> char 0x401
    }

    v.reg_write(vdp2::reg_ramctl, 0x0000U);
    set_cram_1555(v, 1U, 0x1FU, 0U, 0U); // entry 1 red  (NBG0)
    set_cram_1555(v, 2U, 0U, 0U, 0x1FU); // entry 2 blue (NBG1)

    v.reg_write(vdp2::reg_tvmd, 0x8000U);
    v.reg_write(vdp2::reg_bgon, 0x0003U); // N0ON | N1ON
    // NBG0 priority 1, NBG1 priority 3 (higher wins).
    v.reg_write(vdp2::reg_prina, static_cast<std::uint16_t>(0x0001U | (0x0003U << 8U)));
    v.reg_write(vdp2::reg_chctla, 0x0000U);
    v.reg_write(vdp2::reg_pncn0, 0x0000U);
    v.reg_write(0x32U, 0x0000U); // PNCN1 2-word
    v.reg_write(vdp2::reg_plsz, 0x0000U);
    v.reg_write(vdp2::reg_mpofn, 0x0000U);  // map offset 0 for all
    v.reg_write(vdp2::reg_mpabn0, 0x0000U); // NBG0 planes A/B -> page 0
    v.reg_write(0x42U, 0x0000U);            // NBG0 planes C/D
    // NBG1 planes all select page 1 (plane-selection value 1 per nibble).
    v.reg_write(0x44U, 0x0101U); // MPABN1: A=1, B=1
    v.reg_write(0x46U, 0x0101U); // MPCDN1: C=1, D=1

    render_one_frame(v);

    const auto frame = v.framebuffer();
    // NBG1 (blue, priority 3) wins over NBG0 (red, priority 1).
    CHECK(frame.pixels[0] == 0x000000FFU);
}

TEST_CASE("vdp2 save/load round-trips state", "[vdp2]") {
    vdp2 v;
    v.reg_write(vdp2::reg_tvmd, 0x8000U);
    v.reg_write(vdp2::reg_ramctl, 0x1000U);
    v.vram()[10U] = 0xABU;
    v.cram()[6U] = 0xCDU;

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    v.save_state(writer);

    vdp2 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);

    CHECK(reader.ok());
    CHECK(restored.display_on());
    CHECK(restored.cram_mode() == 1U);
    CHECK(restored.vram()[10U] == 0xABU);
    CHECK(restored.cram()[6U] == 0xCDU);
}
