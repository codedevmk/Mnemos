#include "genesis_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::manifests::genesis::assemble_genesis;
    using mnemos::manifests::genesis::genesis_config;
    using genesis_vdp = mnemos::chips::video::genesis_vdp;

    // A minimal 68000 boot ROM:
    //   reset: SSP=$00FFF000, PC=$00000008
    //   main:  MOVE.W #$2000,SR             ; supervisor, IPL mask 0 (enable IRQs)
    //          MOVE.W #$CAFE,($00FF0000).L  ; sentinel into work RAM
    //          MOVE.W #$8164,($00C00004).L  ; VDP reg1 = display + VINT + M5
    //          BRA.S *                      ; spin
    //   vector 30 ($78) = level-6 autovector -> handler at $40
    //   handler: MOVE.W #$BEEF,($00FF0002).L ; sentinel proving the VINT fired
    //            RTE
    std::vector<std::uint8_t> make_rom() {
        std::vector<std::uint8_t> rom(0x100, 0x00);
        const auto w16 = [&](std::size_t off, std::uint16_t v) {
            rom[off] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 1] = static_cast<std::uint8_t>(v);
        };
        const auto w32 = [&](std::size_t off, std::uint32_t v) {
            rom[off + 0] = static_cast<std::uint8_t>(v >> 24U);
            rom[off + 1] = static_cast<std::uint8_t>(v >> 16U);
            rom[off + 2] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 3] = static_cast<std::uint8_t>(v);
        };

        w32(0x00, 0x00FFF000U); // SSP
        w32(0x04, 0x00000008U); // PC (entry)
        w32(0x78, 0x00000040U); // level-6 autovector -> handler

        w16(0x08, 0x46FC);
        w16(0x0A, 0x2000); // MOVE.W #$2000,SR
        w16(0x0C, 0x33FC);
        w16(0x0E, 0xCAFE);
        w32(0x10, 0x00FF0000U); // MOVE.W #$CAFE,($00FF0000).L
        w16(0x14, 0x33FC);
        w16(0x16, 0x8164);
        w32(0x18, 0x00C00004U); // MOVE.W #$8164,($00C00004).L
        w16(0x1C, 0x60FE);      // BRA.S *

        w16(0x40, 0x33FC);
        w16(0x42, 0xBEEF);
        w32(0x44, 0x00FF0002U); // MOVE.W #$BEEF,($00FF0002).L
        w16(0x48, 0x4E73);      // RTE

        return rom;
    }

    // Advance the system roughly one frame: interleave the 68000 (~488 cycles/line)
    // with the VDP (one line per 3420 master clocks).
    void run_about_a_frame(mnemos::manifests::genesis::genesis_system& sys) {
        for (int line = 0; line < 300; ++line) {
            sys.cpu.tick(488U);
            sys.vdp.tick(genesis_vdp::master_clocks_per_line);
        }
    }
} // namespace

TEST_CASE("genesis boots from the ROM reset vectors") {
    auto sys = assemble_genesis(make_rom());
    REQUIRE(sys != nullptr);
    const auto regs = sys->cpu.cpu_registers();
    CHECK(regs.pc == 0x00000008U);   // PC from $000004
    CHECK(regs.a[7] == 0x00FFF000U); // SSP from $000000
}

TEST_CASE("genesis runs the boot program across the 24-bit bus") {
    auto sys = assemble_genesis(make_rom());
    run_about_a_frame(*sys);

    // Main wrote the sentinel into work RAM ($FF0000) and programmed the VDP.
    CHECK(sys->work_ram[0] == 0xCA);
    CHECK(sys->work_ram[1] == 0xFE);
    CHECK(sys->vdp.reg(1) == 0x64); // display + VINT + M5, via the coalesced VDP port

    // The VDP V-blank interrupt reached the 68000 and ran the handler.
    CHECK(sys->work_ram[2] == 0xBE);
    CHECK(sys->work_ram[3] == 0xEF);
}

TEST_CASE("genesis routes the A-bank devices and I/O") {
    auto sys = assemble_genesis(make_rom());

    // Z80 RAM at $A00000 (mirrored).
    sys->bus.write8(0xA00010U, 0x5A);
    CHECK(sys->z80_ram[0x10] == 0x5A);
    CHECK(sys->bus.read8(0xA02010U) == 0x5A); // mirror

    // YM2612 at $A04000/1: latch register $22 then write $08 -> LFO enabled.
    sys->bus.write8(0xA04000U, 0x22);
    sys->bus.write8(0xA04001U, 0x08);
    CHECK(sys->fm.lfo_enabled());

    // SN76489 PSG at $C00011: latch channel-0 volume to 0 (loud).
    sys->bus.write8(0xC00011U, 0x90);
    CHECK(sys->psg.volume(0) == 0x00U);

    // Version register at $A10001 (NTSC export).
    CHECK(sys->bus.read8(0xA10001U) == 0xA0U);
}

TEST_CASE("genesis VDP DMA reads from 68K address space") {
    auto sys = assemble_genesis(make_rom());
    auto& v = sys->vdp;

    // Program a 4-word 68K->VRAM DMA from ROM $000000 to VRAM $0000 (native VDP API).
    v.write16(0x04, 0x8114); // reg1  = M5 + DMA enable
    v.write16(0x04, 0x8F02); // reg15 = auto-increment 2
    v.write16(0x04, 0x9304); // reg19 = length lo = 4
    v.write16(0x04, 0x9400); // reg20 = length hi = 0
    v.write16(0x04, 0x9500); // reg21 = source lo = 0
    v.write16(0x04, 0x9600); // reg22 = source mid = 0
    v.write16(0x04, 0x9700); // reg23 = source hi / type 0 (68K transfer)
    v.write16(0x04, 0x4000); // command word 1: VRAM write + DMA, addr 0
    v.write16(0x04, 0x0080); // command word 2: triggers the DMA

    // The ROM's first words land in VRAM, fetched through the system bus.
    CHECK(v.vram16(0x0000) == 0x00FFU); // ROM[0..1] = SSP high
    CHECK(v.vram16(0x0006) == 0x0008U); // ROM[6..7] = PC low
}

TEST_CASE("genesis selects the PAL region") {
    auto sys = assemble_genesis(make_rom(), {.video_region = genesis_config::region::pal});
    CHECK((sys->bus.read8(0xA10001U) & 0x40U) != 0U); // PAL bit set in the version register
}
