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

    // A 64 KiB-PRG (eight 8 KiB banks) / 8 KiB CHR-ROM MMC5 (mapper 5). PRG bank N
    // byte 0 = $A0+N; CHR 1 KiB bank N byte 0 = $D0+N.
    std::vector<std::uint8_t> make_mmc5() {
        std::vector<std::uint8_t> rom(16U + 8U * 0x2000U + 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 64 KiB PRG (eight 8 KiB banks)
        rom[5] = 1U;    // 8 KiB CHR-ROM (eight 1 KiB banks)
        rom[6] = 0x50U; // flags6: mapper low nibble = 5 (MMC5)
        rom[7] = 0x00U;
        for (std::size_t bank = 0; bank < 8U; ++bank) {
            rom[16U + bank * 0x2000U] = static_cast<std::uint8_t>(0xA0U + bank);
        }
        const std::size_t chr = 16U + 8U * 0x2000U;
        for (std::size_t b = 0; b < 8U; ++b) {
            rom[chr + b * 0x0400U] = static_cast<std::uint8_t>(0xD0U + b); // 1 KiB bank b
        }
        return rom;
    }

    // A 64 KiB-PRG (eight 8 KiB banks) / 8 KiB CHR-ROM Namco-118 (mapper 206).
    // PRG bank N byte 0 = $A0+N; CHR 1 KiB bank N byte 0 = $D0+N; the reset vector
    // lives in the fixed last PRG bank ($E000).
    std::vector<std::uint8_t> make_namco118() {
        std::vector<std::uint8_t> rom(16U + 8U * 0x2000U + 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 64 KiB PRG (eight 8 KiB banks)
        rom[5] = 1U;    // 8 KiB CHR-ROM (eight 1 KiB banks)
        rom[6] = 0xE0U; // flags6: mapper low nibble = 0xE, horizontal mirroring
        rom[7] = 0xC0U; // flags7: mapper high nibble = 0xC -> mapper 206
        for (std::size_t bank = 0; bank < 8U; ++bank) {
            rom[16U + bank * 0x2000U] = static_cast<std::uint8_t>(0xA0U + bank);
        }
        const std::size_t chr = 16U + 8U * 0x2000U;
        for (std::size_t b = 0; b < 8U; ++b) {
            rom[chr + b * 0x0400U] = static_cast<std::uint8_t>(0xD0U + b); // 1 KiB bank b
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

    // A 64 KiB-PRG (eight 8 KiB banks) / 8 KiB CHR-ROM Sunsoft FME-7/5B (mapper 69).
    // PRG bank N byte 0 = $A0+N; CHR 1 KiB bank N byte 0 = $D0+N; the reset vector
    // lives in the fixed last PRG bank ($E000).
    std::vector<std::uint8_t> make_sunsoft5b() {
        std::vector<std::uint8_t> rom(16U + 8U * 0x2000U + 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 64 KiB PRG (eight 8 KiB banks)
        rom[5] = 1U;    // 8 KiB CHR-ROM (eight 1 KiB banks)
        rom[6] = 0x50U; // flags6: mapper low nibble = 5
        rom[7] = 0x40U; // flags7: mapper high nibble = 4 -> mapper 69
        for (std::size_t bank = 0; bank < 8U; ++bank) {
            rom[16U + bank * 0x2000U] = static_cast<std::uint8_t>(0xA0U + bank);
        }
        const std::size_t chr = 16U + 8U * 0x2000U;
        for (std::size_t b = 0; b < 8U; ++b) {
            rom[chr + b * 0x0400U] = static_cast<std::uint8_t>(0xD0U + b); // 1 KiB bank b
        }
        rom[16U + 7U * 0x2000U + 0x1FFCU] = 0x00U; // reset vector (last 8 KiB bank) -> $E000
        rom[16U + 7U * 0x2000U + 0x1FFDU] = 0xE0U;
        return rom;
    }

    // A 64 KiB-PRG (eight 8 KiB banks) / 8 KiB CHR-ROM Konami VRC7 (mapper 85).
    // PRG bank N byte 0 = $A0+N; CHR 1 KiB bank N byte 0 = $D0+N; the reset vector
    // lives in the fixed last PRG bank ($E000).
    std::vector<std::uint8_t> make_vrc7() {
        std::vector<std::uint8_t> rom(16U + 8U * 0x2000U + 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 64 KiB PRG (eight 8 KiB banks)
        rom[5] = 1U;    // 8 KiB CHR-ROM (eight 1 KiB banks)
        rom[6] = 0x50U; // flags6: mapper low nibble = 5
        rom[7] = 0x50U; // flags7: mapper high nibble = 5 -> mapper 85
        for (std::size_t bank = 0; bank < 8U; ++bank) {
            rom[16U + bank * 0x2000U] = static_cast<std::uint8_t>(0xA0U + bank);
        }
        const std::size_t chr = 16U + 8U * 0x2000U;
        for (std::size_t b = 0; b < 8U; ++b) {
            rom[chr + b * 0x0400U] = static_cast<std::uint8_t>(0xD0U + b); // 1 KiB bank b
        }
        rom[16U + 7U * 0x2000U + 0x1FFCU] = 0x00U; // reset vector (last 8 KiB bank) -> $E000
        rom[16U + 7U * 0x2000U + 0x1FFDU] = 0xE0U;
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

TEST_CASE("MMC3 scanline IRQ only clocks while the PPU is rendering", "[manifests][nes]") {
    using ppu = mnemos::chips::video::ppu2c02;
    constexpr std::uint64_t frame_ticks =
        static_cast<std::uint64_t>(ppu::dots_per_line) * ppu::lines_per_frame;

    auto sys = assemble_nes(make_mmc3());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });
    sys->bus.write8(0xC000U, 0x01U); // IRQ latch = 1 (fires after reload + one clock)
    sys->bus.write8(0xC001U, 0x00U); // reload on the next clock
    sys->bus.write8(0xE001U, 0x00U); // enable

    // Rendering disabled (PPUMASK = 0): a full frame of scanline callbacks must not
    // clock the counter -- the A12 toggles that drive it only happen while rendering.
    sys->ppu.reg_write(ppu::reg_mask, 0x00U);
    sys->ppu.tick(frame_ticks);
    CHECK_FALSE(irq);

    // Enable background rendering: the per-line clocks now fire the IRQ.
    sys->ppu.reg_write(ppu::reg_mask, ppu::mask_bg_enable);
    sys->ppu.tick(frame_ticks);
    CHECK(irq);
}

TEST_CASE("MMC5 (mapper 5) banks PRG/CHR and serves the hardware multiplier", "[manifests][nes]") {
    auto sys = assemble_nes(make_mmc5());

    // Power-on: PRG mode 3, all bank registers $FF -> every 8 KiB window is the
    // last bank (bank 7 of eight).
    CHECK(sys->bus.read8(0x8000U) == 0xA7U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U);

    // PRG mode 3 (8 KiB banks): $5114 = bank 2 (ROM, bit 7), $5117 = bank 5.
    sys->bus.write8(0x5100U, 0x03U);
    sys->bus.write8(0x5114U, 0x82U); // $8000 = ROM bank 2
    sys->bus.write8(0x5117U, 0x05U); // $E000 = bank 5 ($5117 is always ROM)
    CHECK(sys->bus.read8(0x8000U) == 0xA2U);
    CHECK(sys->bus.read8(0xE000U) == 0xA5U);

    // CHR 1 KiB mode: set-A register $5120 -> the $0000 slot. With 8x8 sprites the
    // background reads set A, so the PPU sees the selected bank.
    sys->bus.write8(0x5101U, 0x03U); // CHR mode 3 (1 KiB)
    sys->bus.write8(0x5120U, 0x03U); // slot 0 = CHR bank 3
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xD3U);

    // Hardware multiplier: $5205 * $5206 -> 16-bit product across $5205 (low) /
    // $5206 (high).
    sys->bus.write8(0x5205U, 0x10U); // 16
    sys->bus.write8(0x5206U, 0x10U); // 16 -> 256 = $0100
    CHECK(sys->bus.read8(0x5205U) == 0x00U);
    CHECK(sys->bus.read8(0x5206U) == 0x01U);
}

TEST_CASE("MMC3 mapper save_state/load_state round-trips the banking", "[manifests][nes]") {
    auto a = assemble_nes(make_mmc3());
    // Configure: PRG mode 1, R6 = bank 3, mirroring horizontal.
    a->bus.write8(0x8000U, 0x46U);
    a->bus.write8(0x8001U, 0x03U);
    a->bus.write8(0xA000U, 0x01U);
    const std::uint8_t a8000 = a->bus.read8(0x8000U);
    const std::uint8_t aC000 = a->bus.read8(0xC000U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    a->mapper->save_state(writer);

    // A fresh machine powers up with different banks; loading restores them and
    // re-points the bus.
    auto b = assemble_nes(make_mmc3());
    CHECK(b->bus.read8(0x8000U) != a8000); // power-on differs
    mnemos::chips::state_reader reader(blob);
    b->mapper->load_state(reader);
    CHECK(reader.ok());
    CHECK(b->bus.read8(0x8000U) == a8000);
    CHECK(b->bus.read8(0xC000U) == aC000);
}

TEST_CASE("MMC5 scanline IRQ fires at the target line and acknowledges on $5204 read",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_mmc5());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });

    sys->bus.write8(0x5203U, 0x08U); // IRQ target = scanline 8
    sys->bus.write8(0x5204U, 0x80U); // enable

    sys->mapper->clock_scanline(7U); // before the target -> no IRQ
    CHECK_FALSE(irq);
    sys->mapper->clock_scanline(8U); // target -> assert
    CHECK(irq);

    // $5204 read: bit 7 (pending) + bit 6 (in-frame) set, and the read acknowledges
    // the IRQ (drops the line).
    const std::uint8_t status = sys->bus.read8(0x5204U);
    CHECK((status & 0x80U) != 0U);
    CHECK((status & 0x40U) != 0U);
    CHECK_FALSE(irq);

    // Re-fire, then disabling ($5204 bit 7 clear) drops the line.
    sys->mapper->clock_scanline(8U);
    CHECK(irq);
    sys->bus.write8(0x5204U, 0x00U);
    CHECK_FALSE(irq);
}

TEST_CASE("Namco 118 (mapper 206) banks PRG/CHR but ignores the MMC3 mode/inversion bits",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_namco118());

    // Power-on PRG mode 0: $8000 = R6 (bank 0), $A000 = R7 (bank 0), $C000 =
    // second-last (bank 6), $E000 = last (bank 7, always fixed).
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xA000U) == 0xA0U);
    CHECK(sys->bus.read8(0xC000U) == 0xA6U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U);

    // R6 -> bank 3 switches the $8000 window.
    sys->bus.write8(0x8000U, 0x06U);
    sys->bus.write8(0x8001U, 0x03U);
    CHECK(sys->bus.read8(0x8000U) == 0xA3U);

    // The MMC3 PRG-mode bit (0x40) does not exist on 206: $8000 stays R6 -- an MMC3
    // would here swap in the fixed second-last bank ($A6).
    sys->bus.write8(0x8000U, 0x46U);
    sys->bus.write8(0x8001U, 0x03U);
    CHECK(sys->bus.read8(0x8000U) == 0xA3U); // still R6, NOT $A6

    // CHR: R2 is the 1 KiB bank at $1000.
    sys->bus.write8(0x8000U, 0x02U);
    sys->bus.write8(0x8001U, 0x05U);
    CHECK(sys->ppu.ppu_read(0x1000U) == 0xD5U);

    // The MMC3 CHR-A12-inversion bit (0x80) does not exist on 206: $1000 still
    // tracks R2 -- an MMC3 would here remap $1000 to a 2 KiB R0 bank ($D0).
    sys->bus.write8(0x8000U, 0x82U);
    sys->bus.write8(0x8001U, 0x05U);
    CHECK(sys->ppu.ppu_read(0x1000U) == 0xD5U);
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

TEST_CASE("Sunsoft 5B (mapper 69) banks PRG and CHR through the command/parameter ports",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_sunsoft5b());

    // Power-on: every PRG window is bank 0; $E000 is the always-fixed last bank.
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U);

    // Command 9 -> $8000 bank, A -> $A000 bank, B -> $C000 bank (command at $8000,
    // parameter at $A000).
    sys->bus.write8(0x8000U, 0x09U);
    sys->bus.write8(0xA000U, 0x03U);
    CHECK(sys->bus.read8(0x8000U) == 0xA3U);
    sys->bus.write8(0x8000U, 0x0AU);
    sys->bus.write8(0xA000U, 0x05U);
    CHECK(sys->bus.read8(0xA000U) == 0xA5U);
    sys->bus.write8(0x8000U, 0x0BU);
    sys->bus.write8(0xA000U, 0x02U);
    CHECK(sys->bus.read8(0xC000U) == 0xA2U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U); // the last bank never moves

    // Commands 0-7 are the eight 1 KiB CHR banks: slot 0 ($0000) <- bank 4, slot 7
    // ($1C00) <- bank 1.
    sys->bus.write8(0x8000U, 0x00U);
    sys->bus.write8(0xA000U, 0x04U);
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xD4U);
    sys->bus.write8(0x8000U, 0x07U);
    sys->bus.write8(0xA000U, 0x01U);
    CHECK(sys->ppu.ppu_read(0x1C00U) == 0xD1U);
}

TEST_CASE("Sunsoft 5B (mapper 69) routes the YM2149 ports and produces audio", "[manifests][nes]") {
    auto sys = assemble_nes(make_sunsoft5b());
    auto* ea = sys->mapper->expansion_audio();
    REQUIRE(ea != nullptr); // the 5B exposes its on-board sound chip

    // Program channel A through $C000 (register select) / $E000 (data): enable tone A
    // in the mixer, set a period, full volume.
    const auto ay = [&](std::uint8_t reg, std::uint8_t val) {
        sys->bus.write8(0xC000U, reg);
        sys->bus.write8(0xE000U, val);
    };
    ay(0x07U, 0xFEU); // mixer: tone A enabled (active-low bit 0 = 0), everything else off
    ay(0x00U, 0x40U); // channel A period low
    ay(0x01U, 0x00U); // channel A period high -> period $040
    ay(0x08U, 0x0FU); // channel A volume = 15

    ea->tick(20000); // advance the sound chip (its /16 prescaler => ~1250 samples)
    const std::size_t avail = sys->mapper->expansion_audio_pending();
    REQUIRE(avail > 0U);
    std::vector<std::int16_t> buf(avail * 2U, 0);
    const std::size_t got = sys->mapper->drain_expansion_audio(buf.data(), avail);
    REQUIRE(got > 0U);

    std::int16_t peak = 0;
    for (const std::int16_t s : buf) {
        peak = std::max(peak, static_cast<std::int16_t>(std::abs(s)));
    }
    CHECK(peak > 0); // a square wave at full volume, not silence
}

TEST_CASE("Sunsoft 5B (mapper 69) FME-7 IRQ counter fires on underflow and acknowledges",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_sunsoft5b());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });

    const auto cmd = [&](std::uint8_t c, std::uint8_t v) {
        sys->bus.write8(0x8000U, c);
        sys->bus.write8(0xA000U, v);
    };
    cmd(0x0FU, 0x00U); // counter high = 0
    cmd(0x0EU, 0x64U); // counter low = 100
    cmd(0x0DU, 0x81U); // enable the counter (bit 7) + the IRQ (bit 0)
    CHECK_FALSE(irq);

    sys->mapper->clock_cpu_timer(50U); // 100 -> 50, no wrap
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(60U); // 50 -> wraps past zero -> assert
    CHECK(irq);

    cmd(0x0DU, 0x00U); // writing the IRQ-control command acknowledges (and disables)
    CHECK_FALSE(irq);
}

TEST_CASE("Sunsoft 5B (mapper 69) save_state/load_state round-trips banking", "[manifests][nes]") {
    auto a = assemble_nes(make_sunsoft5b());
    a->bus.write8(0x8000U, 0x09U); // $8000 = PRG bank 3
    a->bus.write8(0xA000U, 0x03U);
    a->bus.write8(0x8000U, 0x00U); // CHR slot 0 = bank 4
    a->bus.write8(0xA000U, 0x04U);
    const std::uint8_t a8000 = a->bus.read8(0x8000U);
    const std::uint8_t achr = a->ppu.ppu_read(0x0000U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    a->mapper->save_state(writer);

    auto b = assemble_nes(make_sunsoft5b());
    CHECK(b->bus.read8(0x8000U) != a8000); // power-on differs
    mnemos::chips::state_reader reader(blob);
    b->mapper->load_state(reader);
    CHECK(reader.ok());
    CHECK(b->bus.read8(0x8000U) == a8000);
    CHECK(b->ppu.ppu_read(0x0000U) == achr);
}

TEST_CASE("VRC7 (mapper 85) banks PRG and CHR through the A4-decoded registers",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_vrc7());

    // Power-on: all PRG windows are bank 0; $E000 is the fixed last bank.
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U);

    // $8000 -> $8000 window, $8010 -> $A000 window, $9000 -> $C000 window.
    sys->bus.write8(0x8000U, 0x03U);
    CHECK(sys->bus.read8(0x8000U) == 0xA3U);
    sys->bus.write8(0x8010U, 0x05U);
    CHECK(sys->bus.read8(0xA000U) == 0xA5U);
    sys->bus.write8(0x9000U, 0x02U);
    CHECK(sys->bus.read8(0xC000U) == 0xA2U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U); // the last bank never moves

    // CHR: $A000 -> slot 0 ($0000), $D010 -> slot 7 ($1C00).
    sys->bus.write8(0xA000U, 0x04U);
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xD4U);
    sys->bus.write8(0xD010U, 0x01U);
    CHECK(sys->ppu.ppu_read(0x1C00U) == 0xD1U);
}

TEST_CASE("VRC7 (mapper 85) routes the OPLL ports and produces audio", "[manifests][nes]") {
    auto sys = assemble_nes(make_vrc7());
    auto* ea = sys->mapper->expansion_audio();
    REQUIRE(ea != nullptr); // the VRC7 exposes its OPLL

    // Key channel 0 with a preset instrument through $9010 (register select) /
    // $9030 (data) -- the same note-on the OPLL unit test uses.
    const auto opll = [&](std::uint8_t reg, std::uint8_t val) {
        sys->bus.write8(0x9010U, reg);
        sys->bus.write8(0x9030U, val);
    };
    opll(0x30U, 0x70U); // instrument 7, volume 0 (loud)
    opll(0x10U, 0xA0U); // F-number low
    opll(0x20U, 0x18U); // key-on, block 4

    ea->tick(20000); // advance the OPLL (CPU/36 => ~555 samples)
    const std::size_t avail = sys->mapper->expansion_audio_pending();
    REQUIRE(avail > 0U);
    std::vector<std::int16_t> buf(avail * 2U, 0);
    const std::size_t got = sys->mapper->drain_expansion_audio(buf.data(), avail);
    REQUIRE(got > 0U);

    std::int16_t peak = 0;
    for (const std::int16_t s : buf) {
        peak = std::max(peak, static_cast<std::int16_t>(std::abs(s)));
    }
    CHECK(peak > 0); // a keyed FM voice, not silence
}

TEST_CASE("VRC7 (mapper 85) VRC IRQ counts in cycle mode and acknowledges", "[manifests][nes]") {
    auto sys = assemble_nes(make_vrc7());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });

    sys->bus.write8(0xE010U, 0xFEU); // IRQ latch = 0xFE
    sys->bus.write8(0xF000U, 0x06U); // enable (bit 1) + cycle mode (bit 2) -> counter = 0xFE
    CHECK_FALSE(irq);

    sys->mapper->clock_cpu_timer(1U); // 0xFE -> 0xFF
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(1U); // 0xFF -> overflow -> reload + assert
    CHECK(irq);

    sys->bus.write8(0xF010U, 0x00U); // acknowledge drops the line
    CHECK_FALSE(irq);
}

TEST_CASE("VRC7 (mapper 85) VRC IRQ scanline-mode prescaler clocks once per line",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_vrc7());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });

    sys->bus.write8(0xE010U, 0xFFU); // latch 0xFF -> the first counter tick overflows
    sys->bus.write8(0xF000U, 0x02U); // enable, scanline mode (bit 2 clear)

    // One scanline (114 CPU cycles): the +3/cycle prescaler reaches 341 once -> a
    // single counter tick -> overflow -> IRQ.
    sys->mapper->clock_cpu_timer(114U);
    CHECK(irq);
}

TEST_CASE("VRC7 (mapper 85) save_state/load_state round-trips banking", "[manifests][nes]") {
    auto a = assemble_nes(make_vrc7());
    a->bus.write8(0x8000U, 0x03U); // $8000 = PRG bank 3
    a->bus.write8(0xA000U, 0x04U); // CHR slot 0 = bank 4
    const std::uint8_t a8000 = a->bus.read8(0x8000U);
    const std::uint8_t achr = a->ppu.ppu_read(0x0000U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    a->mapper->save_state(writer);

    auto b = assemble_nes(make_vrc7());
    CHECK(b->bus.read8(0x8000U) != a8000); // power-on differs
    mnemos::chips::state_reader reader(blob);
    b->mapper->load_state(reader);
    CHECK(reader.ok());
    CHECK(b->bus.read8(0x8000U) == a8000);
    CHECK(b->ppu.ppu_read(0x0000U) == achr);
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
