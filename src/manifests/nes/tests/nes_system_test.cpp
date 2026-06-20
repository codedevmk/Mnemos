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

    // A 4x16 KiB-PRG / CHR-RAM MMC1 (mapper 1). bank N first byte = $A0+N; the
    // reset vector lives in the fixed last bank and points at $C000.
    std::vector<std::uint8_t> make_mmc1() {
        std::vector<std::uint8_t> rom(16U + 4U * 0x4000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 4 x 16 KiB PRG
        rom[5] = 0U;    // CHR-RAM
        rom[6] = 0x10U; // flags6: mapper low nibble = 1 (MMC1)
        for (std::size_t bank = 0; bank < 4U; ++bank) {
            // Distinct marker per bank: 0xA0, 0xB1, 0xC2, 0xD3.
            rom[16U + bank * 0x4000U] = static_cast<std::uint8_t>(0xA0U + bank * 0x11U);
        }
        rom[16U + 3U * 0x4000U + 0x3FFCU] = 0x00U; // reset vector (fixed last bank) -> $C000
        rom[16U + 3U * 0x4000U + 0x3FFDU] = 0xC0U;
        return rom;
    }

    // A 64 KiB-PRG (eight 8 KiB banks) / CHR-RAM MMC3 (mapper 4). 8 KiB bank N
    // first byte = $A0+N; the reset vector lives in the fixed last bank ($E000).
    std::vector<std::uint8_t> make_mmc3() {
        std::vector<std::uint8_t> rom(16U + 8U * 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 4 x 16 KiB = 64 KiB PRG (= eight 8 KiB banks)
        rom[5] = 0U;    // CHR-RAM
        rom[6] = 0x40U; // flags6: mapper low nibble = 4 (MMC3)
        for (std::size_t bank = 0; bank < 8U; ++bank) {
            rom[16U + bank * 0x2000U] = static_cast<std::uint8_t>(0xA0U + bank);
        }
        rom[16U + 7U * 0x2000U + 0x1FFCU] = 0x00U; // reset vector (last 8 KiB bank) -> $E000
        rom[16U + 7U * 0x2000U + 0x1FFDU] = 0xE0U;
        return rom;
    }

    // A 32 KiB-PRG / two-8 KiB-bank-CHR CNROM (mapper 3). CHR bank N byte 0 =
    // $C0+N; the reset vector points at $8000.
    std::vector<std::uint8_t> make_cnrom() {
        std::vector<std::uint8_t> rom(16U + 2U * 0x4000U + 2U * 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 2U;    // 32 KiB PRG
        rom[5] = 2U;    // 16 KiB CHR (two 8 KiB banks)
        rom[6] = 0x30U; // flags6: mapper low nibble = 3 (CNROM)
        const std::size_t chr = 16U + 2U * 0x4000U;
        rom[chr + 0x0000U] = 0xC0U; // CHR bank 0, byte 0
        rom[chr + 0x2000U] = 0xC1U; // CHR bank 1, byte 0
        rom[16U + 0x7FFCU] = 0x00U; // reset vector -> $8000
        rom[16U + 0x7FFDU] = 0x80U;
        return rom;
    }

    // A 64 KiB-PRG (two 32 KiB banks) / CHR-RAM AxROM (mapper 7). 32 KiB bank N
    // byte 0 = $E0+N; the reset vector (bank 0) points at $8000.
    std::vector<std::uint8_t> make_axrom() {
        std::vector<std::uint8_t> rom(16U + 4U * 0x4000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;                // 64 KiB PRG (= two 32 KiB banks)
        rom[5] = 0U;                // CHR-RAM
        rom[6] = 0x70U;             // flags6: mapper low nibble = 7 (AxROM)
        rom[16U + 0x0000U] = 0xE0U; // 32 KiB bank 0, byte 0
        rom[16U + 0x8000U] = 0xE1U; // 32 KiB bank 1, byte 0
        rom[16U + 0x7FFCU] = 0x00U; // reset vector (bank 0) -> $8000
        rom[16U + 0x7FFDU] = 0x80U;
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

TEST_CASE("parse_ines reads the battery flag", "[manifests][nes]") {
    CHECK_FALSE(parse_ines(make_synthetic_nrom(0x00U)).battery);
    CHECK(parse_ines(make_synthetic_nrom(0x02U)).battery); // flags6 bit1
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

TEST_CASE("MMC1 (mapper 1) serial loads switch PRG banks", "[manifests][nes]") {
    auto sys = assemble_nes(make_mmc1());
    const auto write5 = [&](std::uint16_t addr, std::uint8_t v) {
        for (int i = 0; i < 5; ++i) {
            sys->bus.write8(addr, static_cast<std::uint8_t>((v >> i) & 0x01U));
        }
    };

    // Power-on PRG mode 3: $8000 = switchable bank 0, $C000 = fixed last bank.
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xC000U) == 0xD3U);

    // Select bank 2 into the $8000 window via the PRG register ($E000).
    write5(0xE000U, 0x02U);
    CHECK(sys->bus.read8(0x8000U) == 0xC2U);
    CHECK(sys->bus.read8(0xC000U) == 0xD3U); // the fixed bank never moves

    // A bit-7 write resets a partial shift; the next full load still lands.
    sys->bus.write8(0xE000U, 0x01U); // 1 of 5
    sys->bus.write8(0xE000U, 0x80U); // reset
    write5(0xE000U, 0x01U);
    CHECK(sys->bus.read8(0x8000U) == 0xB1U); // bank 1

    // 32 KiB PRG mode (control mode 0): a bank pair (2,3) maps across $8000-$FFFF.
    write5(0x8000U, 0x00U); // control: mirroring 0, PRG mode 0 (32 KiB), CHR 8 KiB
    write5(0xE000U, 0x02U); // prg_bank 2 -> 32 KiB base bank 2
    CHECK(sys->bus.read8(0x8000U) == 0xC2U);
    CHECK(sys->bus.read8(0xC000U) == 0xD3U);
}

TEST_CASE("MMC3 (mapper 4) bank-select/data switch PRG banks", "[manifests][nes]") {
    auto sys = assemble_nes(make_mmc3());

    // Power-on PRG mode 0: $8000 = R6 (bank 0), $C000 = second-last (bank 6),
    // $E000 = last (bank 7, always fixed).
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xC000U) == 0xA6U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U);

    // Select R6 and set it to bank 3 -> the $8000 window switches.
    sys->bus.write8(0x8000U, 0x06U); // bank select: R6, PRG mode 0
    sys->bus.write8(0x8001U, 0x03U); // R6 = bank 3
    CHECK(sys->bus.read8(0x8000U) == 0xA3U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U); // the last bank stays fixed

    // PRG mode 1: $8000 becomes the fixed second-last bank, R6 moves to $C000.
    sys->bus.write8(0x8000U, 0x46U);         // mode bit (0x40) + select R6
    sys->bus.write8(0x8001U, 0x03U);         // R6 = bank 3
    CHECK(sys->bus.read8(0x8000U) == 0xA6U); // second-last (fixed in mode 1)
    CHECK(sys->bus.read8(0xC000U) == 0xA3U); // R6 = bank 3
}

TEST_CASE("MMC3 scanline IRQ counts down from the latch and acknowledges", "[manifests][nes]") {
    auto sys = assemble_nes(make_mmc3());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });

    sys->bus.write8(0xC000U, 0x02U); // IRQ latch = 2
    sys->bus.write8(0xC001U, 0x00U); // reload on the next clock
    sys->bus.write8(0xE001U, 0x00U); // enable

    sys->mapper->clock_scanline(0U); // reload -> counter = 2
    CHECK_FALSE(irq);
    sys->mapper->clock_scanline(1U); // 2 -> 1
    CHECK_FALSE(irq);
    sys->mapper->clock_scanline(2U); // 1 -> 0 -> assert
    CHECK(irq);

    sys->bus.write8(0xE000U, 0x00U); // disable acknowledges the line
    CHECK_FALSE(irq);
}

TEST_CASE("CNROM (mapper 3) switches the 8 KiB CHR bank", "[manifests][nes]") {
    auto sys = assemble_nes(make_cnrom());

    CHECK(sys->ppu.ppu_read(0x0000U) == 0xC0U); // initial CHR bank 0
    sys->bus.write8(0x8000U, 0x01U);            // select CHR bank 1
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xC1U);
    sys->bus.write8(0x9ABCU, 0x00U); // any $8000-$FFFF write decodes -> bank 0
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xC0U);
}

TEST_CASE("AxROM (mapper 7) switches the 32 KiB PRG bank", "[manifests][nes]") {
    auto sys = assemble_nes(make_axrom());

    CHECK(sys->bus.read8(0x8000U) == 0xE0U); // initial 32 KiB bank 0
    sys->bus.write8(0x8000U, 0x01U);         // select bank 1 (+ single-screen A)
    CHECK(sys->bus.read8(0x8000U) == 0xE1U);
    sys->bus.write8(0x8000U, 0x10U); // bank 0, single-screen B
    CHECK(sys->bus.read8(0x8000U) == 0xE0U);
}

TEST_CASE("cartridge work RAM at $6000-$7FFF reads back writes", "[manifests][nes]") {
    auto sys = assemble_nes(make_synthetic_nrom());
    // MMC3 (and other) games keep work variables in the 8 KiB at $6000; without it
    // they read open bus and stall. Spot-check the ends + the middle.
    sys->bus.write8(0x6000U, 0x5AU);
    sys->bus.write8(0x7000U, 0xA5U);
    sys->bus.write8(0x7FFFU, 0x3CU);
    CHECK(sys->bus.read8(0x6000U) == 0x5AU);
    CHECK(sys->bus.read8(0x7000U) == 0xA5U);
    CHECK(sys->bus.read8(0x7FFFU) == 0x3CU);
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
