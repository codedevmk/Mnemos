// CPS1 board, increment 1: a synthetic-cart bring-up of the 68000 + cps_a_b
// video. The board boots from the cart's reset vectors, runs one frame, and the
// video chip clears the frame to the decoded backdrop pen (black, since the
// palette is zero with no palette DMA yet).

#include "capcom_cps1_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>

namespace {

    using mnemos::manifests::capcom_cps1::assemble_cps1;
    using mnemos::manifests::capcom_cps1::cps1_rom_skeleton;
    using mnemos::manifests::common::rom_set_image;

    // Store a big-endian 32-bit word into a region.
    void poke32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 24U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value >> 16U);
        bytes[at + 2U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 3U] = static_cast<std::uint8_t>(value);
    }

    // A maincpu region whose 68000 reset vectors ($0 = SSP, $4 = PC) point the CPU
    // at a trivial program (BRA * -- branch to self, opcode 0x60FE) at $000400.
    [[nodiscard]] rom_set_image make_image() {
        rom_set_image image;
        auto& main = image.regions["maincpu"];
        main.assign(mnemos::manifests::capcom_cps1::main_rom_size, 0xFFU);
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
    CHECK(decl.regions[0].size == mnemos::manifests::capcom_cps1::main_rom_size);
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
