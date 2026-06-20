// NES manifest tests: the iNES (.nes) parser, and a synthetic NROM that proves
// the assembled machine maps PRG/WRAM, honours the reset vector, runs 6502 code,
// and routes CPU writes to the PPU through the $2000-$3FFF MMIO window. No ROM
// needed -- the program is hand-assembled; real-game rendering is a data-gated
// golden / a manual player render.

#include "nes_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
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

    // A 2x16 KiB-PRG / CHR-RAM UxROM (mapper 2). bank0[0]=$A0, bank1[0]=$B1; the
    // reset vector (in the fixed last bank) points at $C000.
    std::vector<std::uint8_t> make_uxrom() {
        std::vector<std::uint8_t> rom(16U + 2U * 0x4000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 2U;                          // 2 x 16 KiB PRG
        rom[5] = 0U;                          // CHR count 0 -> 8 KiB CHR-RAM
        rom[6] = 0x20U;                       // flags6: mapper low nibble = 2 (UxROM)
        rom[16U + 0x0000U] = 0xA0U;           // bank 0, first byte
        rom[16U + 0x4000U] = 0xB1U;           // bank 1, first byte
        rom[16U + 0x4000U + 0x3FFCU] = 0x00U; // reset vector (fixed last bank) -> $C000
        rom[16U + 0x4000U + 0x3FFDU] = 0xC0U;
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

TEST_CASE("UxROM (mapper 2) switches the $8000 PRG bank", "[manifests][nes]") {
    auto sys = assemble_nes(make_uxrom());

    CHECK(sys->bus.read8(0x8000U) == 0xA0U); // initial: bank 0 at $8000
    CHECK(sys->bus.read8(0xC000U) == 0xB1U); // last bank fixed at $C000

    sys->bus.write8(0x8000U, 0x01U); // select bank 1 into the $8000 window
    CHECK(sys->bus.read8(0x8000U) == 0xB1U);

    sys->bus.write8(0xABCDU, 0x00U); // any $8000-$FFFF write decodes -> bank 0
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xC000U) == 0xB1U); // the fixed bank never moves
}

TEST_CASE("CHR-RAM cart accepts PPU pattern writes", "[manifests][nes]") {
    auto sys = assemble_nes(make_uxrom()); // CHR count 0 -> CHR-RAM

    // PPUADDR := $0000, then write a pattern byte and read it back.
    sys->bus.write8(0x2006U, 0x00U);
    sys->bus.write8(0x2006U, 0x00U);
    sys->bus.write8(0x2007U, 0x5AU);
    CHECK(sys->ppu.ppu_read(0x0000U) == 0x5AU);
}

TEST_CASE("controller shift register clocks buttons in $4016 read order", "[manifests][nes]") {
    auto sys = assemble_nes(make_synthetic_nrom());
    sys->set_pad(0, nes_system::btn_a | nes_system::btn_right);

    // Strobe high then low ($4016) latches the button state for serial reads.
    sys->bus.write8(0x4016U, 0x01U);
    sys->bus.write8(0x4016U, 0x00U);

    // Eight reads emit A, B, Select, Start, Up, Down, Left, Right in bit 0.
    const std::array<bool, 8> expect = {true, false, false, false, false, false, false, true};
    for (std::size_t i = 0; i < expect.size(); ++i) {
        const std::uint8_t r = sys->bus.read8(0x4016U);
        CHECK((r & 0x01U) == (expect[i] ? 0x01U : 0x00U));
    }
    // Reads past the 8th return 1 (the open-bus / shifted-in level).
    CHECK((sys->bus.read8(0x4016U) & 0x01U) == 0x01U);
    // Controller 2 ($4017) was never pressed -> first read is 0.
    sys->bus.write8(0x4016U, 0x01U);
    sys->bus.write8(0x4016U, 0x00U);
    CHECK((sys->bus.read8(0x4017U) & 0x01U) == 0x00U);
}

TEST_CASE("CPU writes reach the APU and produce audio", "[manifests][nes]") {
    const auto rom = make_synthetic_nrom();
    auto sys = assemble_nes(rom);
    sys->apu.enable_audio_capture(true);

    // Program pulse 1 through the $4000-$4017 MMIO: enable it ($4015), set duty 2
    // with a held constant volume ($4000), and load the timer + length so the
    // oscillator runs.
    sys->bus.write8(0x4015U, 0x01U); // enable pulse 1
    sys->bus.write8(0x4000U, 0xBFU); // duty 2, length-halt, constant volume 15
    sys->bus.write8(0x4002U, 0xFFU); // timer low
    sys->bus.write8(0x4003U, 0x08U); // length index 1, timer high 0

    sys->apu.tick(30000); // ~half an NTSC frame of CPU cycles
    std::vector<std::int16_t> buf(sys->apu.pending_samples() * 2U, 0);
    const std::size_t pairs = sys->apu.drain_samples(buf.data(), sys->apu.pending_samples());
    REQUIRE(pairs > 0U);

    std::int16_t peak = 0;
    for (const std::int16_t s : buf) {
        peak = std::max(peak, static_cast<std::int16_t>(std::abs(s)));
    }
    CHECK(peak > 1000); // a square wave at full volume, not silence
}
