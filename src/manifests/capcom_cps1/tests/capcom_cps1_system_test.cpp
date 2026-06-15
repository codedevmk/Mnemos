// CPS1 board test. Increment 1 brings up the 68000 + cps_a_b video on a
// synthetic cart (boot, render one frame to the black backdrop, route the
// CPS-A/CPS-B register windows). Increment 2 adds the per-frame CPS-A -> video
// decode, the reg5 palette DMA (GFX RAM -> palette buffer), and the vblank
// level-6 IRQ that drives the game's main loop.

#include "capcom_cps1_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace {

    namespace cps1 = mnemos::manifests::capcom_cps1;
    using cps1::assemble_cps1;
    using cps1::cps1_rom_skeleton;
    using mnemos::manifests::common::rom_set_image;

    // Store a big-endian 32-bit word into a byte buffer (vector or array span).
    void poke32(std::span<std::uint8_t> bytes, std::size_t at, std::uint32_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 24U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value >> 16U);
        bytes[at + 2U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 3U] = static_cast<std::uint8_t>(value);
    }

    // Store a big-endian 16-bit word into a byte buffer (vector or array span).
    void poke16(std::span<std::uint8_t> bytes, std::size_t at, std::uint16_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value);
    }

    // A maincpu region whose 68000 reset vectors ($0 = SSP, $4 = PC) point the CPU
    // at a trivial program (BRA * -- branch to self, opcode 0x60FE) at $000400.
    [[nodiscard]] rom_set_image make_image() {
        rom_set_image image;
        auto& main = image.regions["maincpu"];
        main.assign(cps1::main_rom_size, 0xFFU);
        poke32(main, 0x0U, 0x00FF0000U); // initial SSP -> top of work RAM
        poke32(main, 0x4U, 0x00000400U); // initial PC -> the program below
        main[0x400U] = 0x60U;            // BRA.s
        main[0x401U] = 0xFEU;            // displacement -2 (branch to self)
        // A small graphics region so attach_gfx has real backing.
        image.regions["gfx"].assign(0x400U, 0x00U);
        return image;
    }

} // namespace

TEST_CASE("cps1 boots a synthetic cart and renders one frame", "[cps1]") {
    auto system = assemble_cps1(make_image());

    // Reset loaded the vectors from the cart.
    const auto regs = system->main_cpu.cpu_registers();
    CHECK(regs.a[7] == 0x00FF0000U); // SSP
    CHECK(regs.pc == 0x00000400U);   // PC

    REQUIRE(system->video.frame_index() == 0U);
    system->run_frame();
    REQUIRE(system->video.frame_index() == 1U);

    // The frame is the CPS1 display geometry, cleared to the decoded backdrop.
    const auto fb = system->video.framebuffer();
    REQUIRE(fb.width == 384U);
    REQUIRE(fb.height == 224U);
    bool all_black = true;
    for (std::uint32_t i = 0; i < fb.width * fb.height; ++i) {
        if (fb.pixels[i] != 0x000000U) {
            all_black = false;
            break;
        }
    }
    CHECK(all_black); // zero palette -> black backdrop (no palette DMA yet)

    // The CPU executed (the branch-to-self ran for the whole frame budget).
    CHECK(system->main_cpu.cpu_registers().pc == 0x00000400U);
}

TEST_CASE("cps1 ROM skeleton declares the program region only", "[cps1]") {
    const auto decl = cps1_rom_skeleton("synthetic");
    CHECK(decl.name == "synthetic");
    CHECK(decl.board == "capcom_cps1");
    REQUIRE(decl.regions.size() == 1U);
    CHECK(decl.regions[0].name == "maincpu");
    CHECK(decl.regions[0].size == cps1::main_rom_size);
}

TEST_CASE("cps1 routes the CPS-A and CPS-B register windows", "[cps1]") {
    auto system = assemble_cps1(make_image());

    // A 68K word write to the CPS-A window lands in the raw register file.
    system->main_bus.write16_be(0x800100U, 0x1234U);
    CHECK(system->cps_a_regs[0] == 0x1234U);

    // A CPS-B window write is reflected by the video chip's register file and
    // reads back through the same window.
    system->main_bus.write16_be(0x800140U + 4U, 0xABCDU); // CPS-B register index 2
    CHECK(system->video.cps_b_reg(2U) == 0xABCDU);
    CHECK(system->main_bus.read16_be(0x800140U + 4U) == 0xABCDU);

    // An unmapped read returns benign open-bus rather than crashing.
    CHECK(system->main_bus.read16_be(0x700000U) == 0xFFFFU);
}

TEST_CASE("cps1 reg5 palette DMA fills the palette from GFX RAM", "[cps1]") {
    auto system = assemble_cps1(make_image());

    // Make every GFX tile pixel pen 15 (all four bit-planes set) so the scroll
    // layers are fully transparent and the backdrop pen shows through unobscured.
    auto& gfx = system->roms.regions["gfx"];
    gfx.assign(0x400U, 0xFFU);
    system->video.attach_gfx(std::span<const std::uint8_t>(gfx));

    // The backdrop pen is palette page 0xBF, pen 0x0F -> palette word index
    // 0xBFF -> byte offset 0x17FE (page 5 of the 6 copied 0x400-byte pages).
    // reg5 decodes to a page-aligned GFX-RAM source; stage the colour there at
    // the same in-buffer offset the backdrop reads.
    constexpr std::uint16_t reg5 = 0x0004U; // source = reg5 << 8 = 0x400 (page-aligned)
    constexpr std::uint32_t source = static_cast<std::uint32_t>(reg5) << 8U;
    constexpr std::uint16_t backdrop_word = 0x0F00U; // brightness 0, red 0xF -> non-black
    constexpr std::uint32_t backdrop_byte = 0xBFFU * 2U;
    poke16(system->gfx_ram, source + backdrop_byte, backdrop_word);

    // Before the DMA the palette is zero -> black backdrop.
    system->run_frame();
    CHECK(system->video.framebuffer().pixels[0] == 0x000000U);

    // A word write to the CPS-A reg5 window ($80010A/B) triggers the DMA on the
    // low byte. The decoded backdrop then becomes the staged red.
    system->main_bus.write16_be(0x800100U + cps1::cps_a_palette_base * 2U, reg5);

    system->run_frame();
    const std::uint32_t backdrop_rgb = system->video.framebuffer().pixels[0];
    CHECK(backdrop_rgb != 0x000000U);                  // DMA filled the palette
    CHECK(backdrop_rgb == (0xFF0000U & backdrop_rgb)); // pure-red channel only
    CHECK((backdrop_rgb & 0x00FFFFU) == 0U);           // no green/blue
}

TEST_CASE("cps1 raises and acknowledges the level-6 vblank IRQ each frame", "[cps1]") {
    rom_set_image image;
    auto& main = image.regions["maincpu"];
    main.assign(cps1::main_rom_size, 0xFFU);
    poke32(main, 0x0U, 0x01000000U); // SSP -> top of work RAM (predecrement lands inside)
    poke32(main, 0x4U, 0x00000400U); // PC -> the program below

    // Autovector level 6 = vector 30 -> address $78. Point it at the handler.
    poke32(main, 0x78U, 0x00000500U);

    // Program @ $400: drop the interrupt mask (MOVE #$2000,SR) then BRA *.
    poke16(main, 0x400U, 0x46FCU); // MOVE #imm,SR
    poke16(main, 0x402U, 0x2000U); // S=1, IPM=0 -> level-6 IRQ now accepted
    poke16(main, 0x404U, 0x60FEU); // BRA * (branch to self)

    // Handler @ $500: ADDQ.L #1,D0 then RTE. D0 counts serviced vblanks.
    poke16(main, 0x500U, 0x5280U); // ADDQ.L #1,D0
    poke16(main, 0x502U, 0x4E73U); // RTE

    image.regions["gfx"].assign(0x400U, 0x00U);

    auto system = assemble_cps1(std::move(image));
    REQUIRE(system->vblank_irq_raised == 0U);
    REQUIRE(system->vblank_irq_acked == 0U);

    system->run_frame();

    // The vblank callback raised the IRQ; the CPU accepted it (IACK fired) and
    // vectored through autovector 6 into the handler.
    CHECK(system->vblank_irq_raised == 1U);
    CHECK(system->vblank_irq_acked == 1U);
    CHECK(system->main_cpu.cpu_registers().d[0] == 1U); // handler ran once

    system->run_frame();
    CHECK(system->vblank_irq_raised == 2U);
    CHECK(system->vblank_irq_acked == 2U);
    CHECK(system->main_cpu.cpu_registers().d[0] == 2U);
}

TEST_CASE("cps1 drives the CPS-A scroll1 base + palette page to the video", "[cps1]") {
    auto system = assemble_cps1(make_image());

    // GFX ROM: make scroll1 tile code 0 opaque pen 1 across its whole 8x8 cell.
    // decode_packed reads 4bpp planar bytes; setting plane-0 bytes (group*4 + 0)
    // and clearing the other three planes yields pen 1 at every pixel.
    auto& gfx = system->roms.regions["gfx"];
    gfx.assign(0x100U, 0x00U);
    for (std::size_t off = 0U; off < 64U; off += 4U) {
        gfx[off + 0U] = 0xFFU; // plane 0 = all set -> pen bit0
    }
    system->video.attach_gfx(std::span<const std::uint8_t>(gfx));

    // Stage two scroll1 name-table tile-index-0 entries: one at GFX offset 0
    // (what a zero scroll1_base reads) with attr -> palette page 32, and one at
    // the based offset 0x200 with attr -> palette page 33. Both code 0.
    constexpr std::uint16_t scroll1_base_reg = 0x0002U; // base = reg << 8 = 0x200
    auto stage_entry = [&](std::uint32_t base, std::uint16_t attr) {
        poke16(system->gfx_ram, base + 0U, 0x0000U); // tile code 0
        poke16(system->gfx_ram, base + 2U, attr);    // attribute
    };
    stage_entry(0x000U, 0x0000U); // page 32 entry at offset 0
    stage_entry(0x200U, 0x0001U); // page 33 entry at the based offset

    // Colour palette page 32 red and page 33 green directly (focus this case on
    // the scroll wiring, not the DMA path). pal_num = 32 + (attr & 0x1F); the
    // word index is pal_num*16 + pen, pen 1 here.
    poke16(system->palette, (32U * 16U + 1U) * 2U, 0x0F00U); // page 32 pen1 = red
    poke16(system->palette, (33U * 16U + 1U) * 2U, 0x00F0U); // page 33 pen1 = green

    // The visible top-left pixel maps to world (64,16); offset the scroll so it
    // lands on tile-index 0 (world (0,0)) -- this also requires the scroll X/Y
    // registers to reach the chip. Route base + scroll X/Y via the CPS-A window.
    system->main_bus.write16_be(0x800100U + cps1::cps_a_scroll1_base * 2U, scroll1_base_reg);
    system->main_bus.write16_be(0x800100U + cps1::cps_a_scroll1_x * 2U, 0xFFC0U); // -64
    system->main_bus.write16_be(0x800100U + cps1::cps_a_scroll1_y * 2U, 0xFFF0U); // -16

    system->run_frame();

    // The visible image is the scroll1 layer. With the scroll offsets landing on
    // tile-index 0 and the base register reaching the chip, the lookup used the
    // page-33 entry -> green; had the base stayed 0 it would be the page-32 entry
    // -> red. Green proves both the base and the scroll offsets drove the render.
    const std::uint32_t pixel = system->video.framebuffer().pixels[0];
    CHECK((pixel & 0x00FF00U) != 0U); // green present
    CHECK((pixel & 0xFF0000U) == 0U); // not red -> the CPS-A registers drove it
}
