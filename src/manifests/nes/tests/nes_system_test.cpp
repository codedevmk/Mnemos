// NES manifest tests: the iNES (.nes) parser, and a synthetic NROM that proves
// the assembled machine maps PRG/WRAM, honours the reset vector, runs 6502 code,
// and routes CPU writes to the PPU through the $2000-$3FFF MMIO window. No ROM
// needed -- the program is hand-assembled; real-game rendering is a data-gated
// golden / a manual player render.

#include "nes_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

    using namespace mnemos::manifests::nes;
    using mirroring = mnemos::chips::video::ppu2c02::mirroring;

    // A 16 KiB-PRG / 8 KiB-CHR NROM whose code does: LDA #$42 / STA $0200 / spin.
    // The reset vector ($FFFC, which a 16 KiB PRG mirrors to prg[0x3FFC]) -> $8000.
    std::vector<std::uint8_t> make_synthetic_nrom(std::uint8_t flags6 = 0x00U) {
        std::vector<std::uint8_t> rom(16U + 0x4000U + 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 1U; // 1 x 16 KiB PRG
        rom[5] = 1U; // 1 x 8 KiB CHR
        rom[6] = flags6;

        const std::size_t prg = 16U; // PRG starts right after the 16-byte header
        const std::array<std::uint8_t, 8> code = {
            0xA9U, 0x42U,        // LDA #$42
            0x8DU, 0x00U, 0x02U, // STA $0200
            0x4CU, 0x05U, 0x80U, // JMP $8005 (self-loop)
        };
        for (std::size_t i = 0; i < code.size(); ++i) {
            rom[prg + i] = code[i];
        }
        rom[prg + 0x3FFCU] = 0x00U; // reset vector low
        rom[prg + 0x3FFDU] = 0x80U; // reset vector high -> $8000
        return rom;
    }

} // namespace

TEST_CASE("parse_ines reads a valid NROM header", "[manifests][nes]") {
    const auto rom = make_synthetic_nrom();
    const ines_image img = parse_ines(rom);
    REQUIRE(img.valid);
    CHECK(img.mapper == 0);
    CHECK(img.prg.size() == 0x4000U);
    CHECK(img.chr.size() == 0x2000U);
    CHECK_FALSE(img.chr_is_ram);
    CHECK(img.mirroring == mirroring::horizontal);
}

TEST_CASE("parse_ines reads the mirroring flag", "[manifests][nes]") {
    const auto rom = make_synthetic_nrom(0x01U); // flags6 bit0 = vertical
    CHECK(parse_ines(rom).mirroring == mirroring::vertical);
}

TEST_CASE("parse_ines rejects a non-iNES image", "[manifests][nes]") {
    const std::vector<std::uint8_t> junk(64U, 0xFFU);
    CHECK_FALSE(parse_ines(junk).valid);
}

TEST_CASE("assembled NROM boots from the reset vector and runs code", "[manifests][nes]") {
    const auto rom = make_synthetic_nrom();
    auto sys = assemble_nes(rom);

    // PC loaded from $FFFC -> $8000.
    CHECK(sys->cpu.cpu_registers().pc == 0x8000U);

    sys->cpu.tick(12);                             // LDA(2) + STA(4) + a couple JMP spins
    CHECK(sys->wram[0x200] == 0x42U);              // STA $0200 landed in work RAM
    CHECK(sys->cpu.cpu_registers().pc == 0x8005U); // spinning on the JMP
}

TEST_CASE("CPU writes reach the PPU through the $2000-$3FFF window", "[manifests][nes]") {
    const auto rom = make_synthetic_nrom();
    auto sys = assemble_nes(rom);

    // PPUADDR := $2000 (a nametable), then PPUDATA := $AB. Reading the PPU bus
    // back at $2000 proves the MMIO mirror -> reg_write -> VRAM path is wired.
    sys->bus.write8(0x2006U, 0x20U);
    sys->bus.write8(0x2006U, 0x00U);
    sys->bus.write8(0x2007U, 0xABU);
    CHECK(sys->ppu.ppu_read(0x2000U) == 0xABU);
}
