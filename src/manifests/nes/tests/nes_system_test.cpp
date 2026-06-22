// NES manifest tests: the iNES (.nes) parser, and a synthetic NROM that proves
// the assembled machine maps PRG/WRAM, honours the reset vector, runs 6502 code,
// and routes CPU writes to the PPU through the $2000-$3FFF MMIO window. No ROM
// needed -- the program is hand-assembled; real-game rendering is a data-gated
// golden / a manual player render.

#include "nes_system.hpp"

#include "fds.hpp"

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

    // A 64 KiB-PRG (eight 8 KiB banks) / 8 KiB CHR-ROM RAMBO-1 (mapper 64). PRG
    // bank N byte 0 = $A0+N; CHR 1 KiB bank N byte 0 = $D0+N; the reset vector
    // lives in the fixed last PRG bank ($E000).
    std::vector<std::uint8_t> make_rambo1() {
        std::vector<std::uint8_t> rom(16U + 8U * 0x2000U + 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 64 KiB PRG (eight 8 KiB banks)
        rom[5] = 1U;    // 8 KiB CHR-ROM (eight 1 KiB banks)
        rom[6] = 0x00U; // flags6: mapper low nibble = 0
        rom[7] = 0x40U; // flags7: mapper high nibble = 4 -> mapper 64
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

    // A 64 KiB-PRG (four 16 KiB banks) / 8 KiB CHR-ROM Bandai FCG (mapper 16). PRG
    // 16 KiB bank N byte 0 = $A0+N; CHR 1 KiB bank N byte 0 = $D0+N; the reset
    // vector lives in the fixed last 16 KiB PRG bank ($C000).
    std::vector<std::uint8_t> make_bandai_fcg() {
        std::vector<std::uint8_t> rom(16U + 4U * 0x4000U + 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 4 x 16 KiB = 64 KiB PRG
        rom[5] = 1U;    // 8 KiB CHR-ROM (eight 1 KiB banks)
        rom[6] = 0x00U; // flags6: mapper low nibble = 0
        rom[7] = 0x10U; // flags7: mapper high nibble = 1 -> mapper 16
        for (std::size_t bank = 0; bank < 4U; ++bank) {
            rom[16U + bank * 0x4000U] = static_cast<std::uint8_t>(0xA0U + bank);
        }
        const std::size_t chr = 16U + 4U * 0x4000U;
        for (std::size_t b = 0; b < 8U; ++b) {
            rom[chr + b * 0x0400U] = static_cast<std::uint8_t>(0xD0U + b); // 1 KiB bank b
        }
        rom[16U + 3U * 0x4000U + 0x3FFCU] = 0x00U; // reset vector (last 16 KiB bank) -> $C000
        rom[16U + 3U * 0x4000U + 0x3FFDU] = 0xC0U;
        return rom;
    }

    // I2C master that bit-bangs the Bandai FCG's serial EEPROM through the mapper:
    // $800D drives it (bit 5 = SCL, bit 6 = SDA, bit 7 = read-enable / release SDA)
    // and a $6000 read returns the device's SDA on D0.
    struct fcg_i2c {
        nes_system& sys;
        void set(bool scl, bool sda, bool read_enable) {
            std::uint8_t v = 0U;
            if (read_enable) {
                v |= 0x80U;
            }
            if (sda) {
                v |= 0x40U;
            }
            if (scl) {
                v |= 0x20U;
            }
            sys.bus.write8(0x800DU, v);
        }
        [[nodiscard]] bool dev_sda() { return (sys.bus.read8(0x6000U) & 0x01U) != 0U; }
        void start() {
            set(true, true, false);
            set(true, false, false); // SDA high->low while SCL high
            set(false, false, false);
        }
        void stop() {
            set(false, false, false);
            set(true, false, false);
            set(true, true, false); // SDA low->high while SCL high
        }
        bool write_byte(std::uint8_t b) {
            for (int i = 7; i >= 0; --i) {
                const bool bit = ((b >> i) & 1U) != 0U;
                set(false, bit, false);
                set(true, bit, false);
            }
            set(false, true, true); // release SDA for the ACK bit
            set(true, true, true);
            const bool ack = !dev_sda();
            set(false, true, true);
            return ack;
        }
        std::uint8_t read_byte(bool ack) {
            std::uint8_t v = 0U;
            for (int i = 7; i >= 0; --i) {
                set(false, true, true); // release SDA so the device drives it
                set(true, true, true);
                v = static_cast<std::uint8_t>((v << 1) | (dev_sda() ? 1U : 0U));
            }
            set(false, !ack, false); // master ACK (continue) / NAK (end)
            set(true, !ack, false);
            set(false, !ack, false);
            return v;
        }
    };

    // A 64 KiB-PRG (four 16 KiB banks) / 16 KiB CHR-ROM (eight 2 KiB banks)
    // Sunsoft-3 (mapper 67). PRG 16 KiB bank N byte 0 = $A0+N; CHR 2 KiB bank N
    // byte 0 = $D0+N; the reset vector lives in the fixed last 16 KiB PRG bank.
    std::vector<std::uint8_t> make_sunsoft3() {
        std::vector<std::uint8_t> rom(16U + 4U * 0x4000U + 0x4000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 4 x 16 KiB = 64 KiB PRG
        rom[5] = 2U;    // 2 x 8 KiB = 16 KiB CHR-ROM (eight 2 KiB banks)
        rom[6] = 0x30U; // flags6: mapper low nibble = 3
        rom[7] = 0x40U; // flags7: mapper high nibble = 4 -> mapper 67
        for (std::size_t bank = 0; bank < 4U; ++bank) {
            rom[16U + bank * 0x4000U] = static_cast<std::uint8_t>(0xA0U + bank);
        }
        const std::size_t chr = 16U + 4U * 0x4000U;
        for (std::size_t b = 0; b < 8U; ++b) {
            rom[chr + b * 0x0800U] = static_cast<std::uint8_t>(0xD0U + b); // 2 KiB bank b
        }
        rom[16U + 3U * 0x4000U + 0x3FFCU] = 0x00U; // reset vector (last 16 KiB bank) -> $C000
        rom[16U + 3U * 0x4000U + 0x3FFDU] = 0xC0U;
        return rom;
    }

    // A 256 KiB-PRG (32 x 8 KiB banks) / 32 KiB CHR-ROM (32 x 1 KiB banks) Jaleco
    // SS88006 (mapper 18). PRG 8 KiB bank N byte 0 = $A0+N; CHR 1 KiB bank N byte
    // 0 = $D0+N; the reset vector lives in the fixed last 8 KiB PRG bank ($E000).
    std::vector<std::uint8_t> make_jaleco_ss88006() {
        std::vector<std::uint8_t> rom(16U + 16U * 0x4000U + 4U * 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 16U;   // 16 x 16 KiB = 256 KiB PRG (= 32 x 8 KiB banks)
        rom[5] = 4U;    // 4 x 8 KiB = 32 KiB CHR-ROM (= 32 x 1 KiB banks)
        rom[6] = 0x20U; // flags6: mapper low nibble = 2
        rom[7] = 0x10U; // flags7: mapper high nibble = 1 -> mapper 18
        for (std::size_t bank = 0; bank < 32U; ++bank) {
            rom[16U + bank * 0x2000U] = static_cast<std::uint8_t>(0xA0U + bank); // 8 KiB bank
        }
        const std::size_t chr = 16U + 16U * 0x4000U;
        for (std::size_t b = 0; b < 32U; ++b) {
            rom[chr + b * 0x0400U] = static_cast<std::uint8_t>(0xD0U + b); // 1 KiB bank
        }
        rom[16U + 31U * 0x2000U + 0x1FFCU] = 0x00U; // reset vector (last 8 KiB bank) -> $E000
        rom[16U + 31U * 0x2000U + 0x1FFDU] = 0xE0U;
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

    // A 64 KiB-PRG (eight 8 KiB banks) / 8 KiB CHR-ROM Konami VRC6a (mapper 24).
    // PRG 8 KiB bank N byte 0 = $A0+N; CHR 1 KiB bank N byte 0 = $D0+N; the reset
    // vector lives in the fixed last 8 KiB PRG bank ($E000).
    std::vector<std::uint8_t> make_vrc6() {
        std::vector<std::uint8_t> rom(16U + 8U * 0x2000U + 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 64 KiB PRG (eight 8 KiB banks)
        rom[5] = 1U;    // 8 KiB CHR-ROM (eight 1 KiB banks)
        rom[6] = 0x80U; // flags6: mapper low nibble = 8
        rom[7] = 0x10U; // flags7: mapper high nibble = 1 -> mapper 24
        for (std::size_t bank = 0; bank < 8U; ++bank) {
            rom[16U + bank * 0x2000U] = static_cast<std::uint8_t>(0xA0U + bank);
        }
        const std::size_t chr = 16U + 8U * 0x2000U;
        for (std::size_t b = 0; b < 8U; ++b) {
            rom[chr + b * 0x0400U] = static_cast<std::uint8_t>(0xD0U + b);
        }
        rom[16U + 7U * 0x2000U + 0x1FFCU] = 0x00U; // reset vector (last 8 KiB bank) -> $E000
        rom[16U + 7U * 0x2000U + 0x1FFDU] = 0xE0U;
        return rom;
    }

    // A 64 KiB-PRG (eight 8 KiB banks) / 8 KiB CHR-ROM Namco 163 (mapper 19). PRG
    // 8 KiB bank N byte 0 = $A0+N; CHR 1 KiB bank N byte 0 = $D0+N; the reset vector
    // lives in the fixed last 8 KiB PRG bank ($E000).
    std::vector<std::uint8_t> make_namco163() {
        std::vector<std::uint8_t> rom(16U + 8U * 0x2000U + 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 64 KiB PRG (eight 8 KiB banks)
        rom[5] = 1U;    // 8 KiB CHR-ROM (eight 1 KiB banks)
        rom[6] = 0x30U; // flags6: mapper low nibble = 3
        rom[7] = 0x10U; // flags7: mapper high nibble = 1 -> mapper 19
        for (std::size_t bank = 0; bank < 8U; ++bank) {
            rom[16U + bank * 0x2000U] = static_cast<std::uint8_t>(0xA0U + bank);
        }
        const std::size_t chr = 16U + 8U * 0x2000U;
        for (std::size_t b = 0; b < 8U; ++b) {
            rom[chr + b * 0x0400U] = static_cast<std::uint8_t>(0xD0U + b);
        }
        rom[16U + 7U * 0x2000U + 0x1FFCU] = 0x00U; // reset vector (last 8 KiB bank) -> $E000
        rom[16U + 7U * 0x2000U + 0x1FFDU] = 0xE0U;
        return rom;
    }

    // A 64 KiB-PRG (eight 8 KiB banks) / 32 KiB CHR-ROM Konami VRC4e (mapper 23). PRG
    // 8 KiB bank N byte 0 = $A0+N; CHR 1 KiB bank N byte 0 = N (0..31, so the 9-bit
    // low+high register pair is observable); reset vector in the fixed last bank.
    std::vector<std::uint8_t> make_vrc4() {
        std::vector<std::uint8_t> rom(16U + 8U * 0x2000U + 32U * 0x0400U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 64 KiB PRG (eight 8 KiB banks)
        rom[5] = 4U;    // 32 KiB CHR-ROM (32 one-KiB banks)
        rom[6] = 0x70U; // flags6: mapper low nibble = 7
        rom[7] = 0x10U; // flags7: mapper high nibble = 1 -> mapper 23
        for (std::size_t bank = 0; bank < 8U; ++bank) {
            rom[16U + bank * 0x2000U] = static_cast<std::uint8_t>(0xA0U + bank);
        }
        const std::size_t chr = 16U + 8U * 0x2000U;
        for (std::size_t b = 0; b < 32U; ++b) {
            rom[chr + b * 0x0400U] = static_cast<std::uint8_t>(b);
        }
        rom[16U + 7U * 0x2000U + 0x1FFCU] = 0x00U; // reset vector (last 8 KiB bank) -> $E000
        rom[16U + 7U * 0x2000U + 0x1FFDU] = 0xE0U;
        return rom;
    }

    // A 128 KiB-PRG (four 32 KiB banks) / 32 KiB CHR-ROM (four 8 KiB banks) cart.
    // 32 KiB PRG bank N byte 0 = $A0+N; 8 KiB CHR bank N byte 0 = $C0+N. `mapper` is
    // written into the iNES header (66 = GxROM, 11 = Color Dreams).
    std::vector<std::uint8_t> make_bankswitch_32k(int mapper) {
        std::vector<std::uint8_t> rom(16U + 4U * 0x8000U + 4U * 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 8U; // 128 KiB PRG (eight 16 KiB units = four 32 KiB banks)
        rom[5] = 4U; // 32 KiB CHR (four 8 KiB banks)
        rom[6] = static_cast<std::uint8_t>((mapper & 0x0F) << 4U);
        rom[7] = static_cast<std::uint8_t>(mapper & 0xF0);
        for (std::size_t b = 0; b < 4U; ++b) {
            rom[16U + b * 0x8000U] = static_cast<std::uint8_t>(0xA0U + b); // PRG 32 KiB bank
        }
        const std::size_t chr = 16U + 4U * 0x8000U;
        for (std::size_t b = 0; b < 4U; ++b) {
            rom[chr + b * 0x2000U] = static_cast<std::uint8_t>(0xC0U + b); // CHR 8 KiB bank
        }
        return rom;
    }

    // A 64 KiB-PRG (four 16 KiB banks) / 8 KiB CHR-RAM Camerica cart (mapper 71). PRG
    // 16 KiB bank N byte 0 = $A0+N.
    std::vector<std::uint8_t> make_camerica() {
        std::vector<std::uint8_t> rom(16U + 4U * 0x4000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 64 KiB PRG (four 16 KiB banks)
        rom[5] = 0U;    // CHR-RAM
        rom[6] = 0x70U; // mapper low nibble 7
        rom[7] = 0x40U; // mapper high nibble 4 -> mapper 71
        for (std::size_t b = 0; b < 4U; ++b) {
            rom[16U + b * 0x4000U] = static_cast<std::uint8_t>(0xA0U + b);
        }
        return rom;
    }

    // A 128 KiB-PRG (16 eight-KiB banks) / 32 KiB CHR-ROM (8 four-KiB banks) MMC2/4
    // cart. PRG 8 KiB bank N byte 0 = $A0+N; CHR 4 KiB bank N byte 0 = $E0+N. `mapper`
    // = 9 (MMC2) or 10 (MMC4).
    std::vector<std::uint8_t> make_mmc2(int mapper) {
        std::vector<std::uint8_t> rom(16U + 16U * 0x2000U + 8U * 0x1000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 8U; // 128 KiB PRG (eight 16 KiB units = sixteen 8 KiB banks)
        rom[5] = 4U; // 32 KiB CHR (eight 4 KiB banks)
        rom[6] = static_cast<std::uint8_t>((mapper & 0x0F) << 4U);
        rom[7] = static_cast<std::uint8_t>(mapper & 0xF0);
        for (std::size_t b = 0; b < 16U; ++b) {
            rom[16U + b * 0x2000U] = static_cast<std::uint8_t>(0xA0U + b); // PRG 8 KiB bank
        }
        const std::size_t chr = 16U + 16U * 0x2000U;
        for (std::size_t b = 0; b < 8U; ++b) {
            rom[chr + b * 0x1000U] = static_cast<std::uint8_t>(0xE0U + b); // CHR 4 KiB bank
        }
        return rom;
    }

    // A 128 KiB-PRG (four 32 KiB banks) mapper-34 cart. BNROM when `chr_units` is 0
    // (8 KiB CHR-RAM); NINA-001 when `chr_units` > 0 (CHR-ROM, four KiB banks marked
    // $C0+N). PRG 32 KiB bank N byte 0 = $A0+N.
    std::vector<std::uint8_t> make_mapper34(std::uint8_t chr_units) {
        const std::size_t chr_bytes = static_cast<std::size_t>(chr_units) * 0x2000U;
        std::vector<std::uint8_t> rom(16U + 4U * 0x8000U + chr_bytes, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 8U;        // 128 KiB PRG (four 32 KiB banks)
        rom[5] = chr_units; // 0 = CHR-RAM (BNROM); >0 = CHR-ROM (NINA-001)
        rom[6] = 0x20U;     // mapper low nibble 2
        rom[7] = 0x20U;     // mapper high nibble 2 -> mapper 34
        for (std::size_t b = 0; b < 4U; ++b) {
            rom[16U + b * 0x8000U] = static_cast<std::uint8_t>(0xA0U + b);
        }
        const std::size_t chr = 16U + 4U * 0x8000U;
        for (std::size_t b = 0; b < chr_bytes / 0x1000U; ++b) {
            rom[chr + b * 0x1000U] = static_cast<std::uint8_t>(0xC0U + b); // CHR 4 KiB bank
        }
        return rom;
    }

    // A 64 KiB-PRG (eight 8 KiB banks) / 128 KiB CHR-ROM (32 four-KiB banks) Konami
    // VRC1 (mapper 75). PRG 8 KiB bank N byte 0 = $A0+N; CHR 4 KiB bank N byte 0 = N.
    std::vector<std::uint8_t> make_vrc1() {
        std::vector<std::uint8_t> rom(16U + 8U * 0x2000U + 32U * 0x1000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 64 KiB PRG (eight 8 KiB banks)
        rom[5] = 16U;   // 128 KiB CHR (32 four-KiB banks)
        rom[6] = 0xB0U; // mapper low nibble B
        rom[7] = 0x40U; // -> mapper 75
        for (std::size_t b = 0; b < 8U; ++b) {
            rom[16U + b * 0x2000U] = static_cast<std::uint8_t>(0xA0U + b);
        }
        const std::size_t chr = 16U + 8U * 0x2000U;
        for (std::size_t b = 0; b < 32U; ++b) {
            rom[chr + b * 0x1000U] = static_cast<std::uint8_t>(b);
        }
        return rom;
    }

    // A 128 KiB-PRG (eight 16 KiB banks) / 8 KiB CHR-RAM Konami VRC3 (mapper 73,
    // Salamander). PRG 16 KiB bank N byte 0 = $A0+N.
    std::vector<std::uint8_t> make_vrc3() {
        std::vector<std::uint8_t> rom(16U + 8U * 0x4000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 8U;    // 128 KiB PRG (eight 16 KiB banks)
        rom[5] = 0U;    // CHR-RAM
        rom[6] = 0x90U; // mapper low nibble 9
        rom[7] = 0x40U; // -> mapper 73
        for (std::size_t b = 0; b < 8U; ++b) {
            rom[16U + b * 0x4000U] = static_cast<std::uint8_t>(0xA0U + b);
        }
        return rom;
    }

    // A 64 KiB-PRG (four 16 KiB banks) / 32 KiB CHR-ROM (sixteen 2 KiB banks) Sunsoft-4
    // (mapper 68). PRG 16 KiB bank N byte 0 = $A0+N; CHR 2 KiB bank N byte 0 = $D0+N;
    // the reset vector lives in the fixed last 16 KiB PRG bank ($C000).
    std::vector<std::uint8_t> make_sunsoft4() {
        std::vector<std::uint8_t> rom(16U + 4U * 0x4000U + 16U * 0x0800U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 4U;    // 64 KiB PRG (four 16 KiB banks)
        rom[5] = 4U;    // 32 KiB CHR-ROM (sixteen 2 KiB banks)
        rom[6] = 0x40U; // flags6: mapper low nibble = 4
        rom[7] = 0x40U; // flags7: mapper high nibble = 4 -> mapper 68
        for (std::size_t b = 0; b < 4U; ++b) {
            rom[16U + b * 0x4000U] = static_cast<std::uint8_t>(0xA0U + b);
        }
        const std::size_t chr = 16U + 4U * 0x4000U;
        for (std::size_t b = 0; b < 16U; ++b) {
            rom[chr + b * 0x0800U] = static_cast<std::uint8_t>(0xD0U + b); // 2 KiB bank b
        }
        rom[16U + 3U * 0x4000U + 0x3FFCU] = 0x00U; // reset vector (fixed last bank) -> $C000
        rom[16U + 3U * 0x4000U + 0x3FFDU] = 0xC0U;
        return rom;
    }

    // A minimal one-side Famicom Disk System image (.fds): the 16-byte header, then a
    // disk-info block ($01 + "*NINTENDO-HVC*") and a file-amount block ($02, 0 files),
    // padded to the 65500-byte side. Enough to exercise the parser + disk drive.
    std::vector<std::uint8_t> make_synthetic_fds() {
        std::vector<std::uint8_t> rom(16U + 65500U, 0x00U);
        rom[0] = 'F';
        rom[1] = 'D';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 1U; // one disk side
        const std::size_t s = 16U;
        rom[s + 0U] = 0x01U; // disk-info block code
        const char* hvc = "*NINTENDO-HVC*";
        for (std::size_t i = 0; i < 14U; ++i) {
            rom[s + 1U + i] = static_cast<std::uint8_t>(hvc[i]);
        }
        rom[s + 56U] = 0x02U; // file-amount block code (block 1 is 56 bytes)
        rom[s + 57U] = 0x00U; // zero files
        return rom;
    }

    // A two-side FDS image: each side is a disk-info block ($01 + "*NINTENDO-HVC*")
    // whose game-name byte (offset $10) is 'A' on side 0 and 'B' on side 1, so a test
    // can tell which side the drive is serving, plus a file-amount block (0 files).
    std::vector<std::uint8_t> make_synthetic_fds_2side() {
        std::vector<std::uint8_t> rom(16U + 2U * 65500U, 0x00U);
        rom[0] = 'F';
        rom[1] = 'D';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 2U; // two sides
        const char* hvc = "*NINTENDO-HVC*";
        for (std::size_t side = 0; side < 2U; ++side) {
            const std::size_t s = 16U + side * 65500U;
            rom[s + 0U] = 0x01U;
            for (std::size_t i = 0; i < 14U; ++i) {
                rom[s + 1U + i] = static_cast<std::uint8_t>(hvc[i]);
            }
            rom[s + 0x10U] = static_cast<std::uint8_t>('A' + side); // per-side marker
            rom[s + 56U] = 0x02U;
            rom[s + 57U] = 0x00U;
        }
        return rom;
    }

    // A stand-in 8 KiB FDS BIOS: just enough to assemble (a reset vector to $E000 and
    // a self-loop). The real DISKSYS.ROM is supplied via MNEMOS_FDS_BIOS for golden runs.
    std::vector<std::uint8_t> make_dummy_fds_bios() {
        std::vector<std::uint8_t> bios(0x2000U, 0x00U);
        bios[0x0000U] = 0x4CU; // JMP $E000
        bios[0x0001U] = 0x00U;
        bios[0x0002U] = 0xE0U;
        bios[0x1FFCU] = 0x00U; // reset vector -> $E000
        bios[0x1FFDU] = 0xE0U;
        return bios;
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

TEST_CASE("MMC5 (mapper 5) routes the expansion audio and produces sound", "[manifests][nes]") {
    auto sys = assemble_nes(make_mmc5());
    auto* ea = sys->mapper->expansion_audio();
    REQUIRE(ea != nullptr);

    // Pulse 1 via $5000-$5003 + enable at $5015.
    sys->bus.write8(0x5015U, 0x01U);                // enable pulse 1
    sys->bus.write8(0x5000U, 0xBFU);                // duty 50%, constant volume 15
    sys->bus.write8(0x5002U, 0x40U);                // timer low
    sys->bus.write8(0x5003U, 0x08U);                // timer high -> $040 + length load
    CHECK((sys->bus.read8(0x5015U) & 0x01U) != 0U); // length status set

    ea->tick(40000);
    const std::size_t avail = sys->mapper->expansion_audio_pending();
    REQUIRE(avail > 0U);
    std::vector<std::int16_t> buf(avail * 2U, 0);
    const std::size_t got = sys->mapper->drain_expansion_audio(buf.data(), avail);
    REQUIRE(got > 0U);
    std::int16_t peak = 0;
    for (const std::int16_t s : buf) {
        peak = std::max(peak, static_cast<std::int16_t>(std::abs(s)));
    }
    CHECK(peak > 0);
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

TEST_CASE("RAMBO-1 (mapper 64) banks three switchable PRG banks with the P swap",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_rambo1());

    // Power-on: every register 0, P=0 -> $8000=R6, $A000=R7, $C000=RF (all bank 0),
    // $E000 = last bank (7, always fixed). The third switchable bank (RF at $C000)
    // is what distinguishes RAMBO-1 from MMC3's fixed second-last.
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xA000U) == 0xA0U);
    CHECK(sys->bus.read8(0xC000U) == 0xA0U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U);

    // R6=1, R7=2, RF=5 (RF is register index 0x0F).
    sys->bus.write8(0x8000U, 0x06U);
    sys->bus.write8(0x8001U, 0x01U);
    sys->bus.write8(0x8000U, 0x07U);
    sys->bus.write8(0x8001U, 0x02U);
    sys->bus.write8(0x8000U, 0x0FU);
    sys->bus.write8(0x8001U, 0x05U);
    CHECK(sys->bus.read8(0x8000U) == 0xA1U); // R6
    CHECK(sys->bus.read8(0xA000U) == 0xA2U); // R7
    CHECK(sys->bus.read8(0xC000U) == 0xA5U); // RF
    CHECK(sys->bus.read8(0xE000U) == 0xA7U); // fixed last

    // P=1 swaps R6 and RF between $8000 and $C000 (regs unchanged).
    sys->bus.write8(0x8000U, 0x40U);
    CHECK(sys->bus.read8(0x8000U) == 0xA5U); // RF now at $8000
    CHECK(sys->bus.read8(0xA000U) == 0xA2U); // R7 unchanged
    CHECK(sys->bus.read8(0xC000U) == 0xA1U); // R6 now at $C000
    CHECK(sys->bus.read8(0xE000U) == 0xA7U);
}

TEST_CASE("RAMBO-1 (mapper 64) CHR honours the K 1 KiB mode and C inversion", "[manifests][nes]") {
    auto sys = assemble_nes(make_rambo1());

    // K=0 (2 KiB mode), C=0: R0 is the 2 KiB region at $0000 (low bit ignored), so
    // R0=2 maps banks 2,3 to $0000,$0400; R2 is the 1 KiB bank at $1000.
    sys->bus.write8(0x8000U, 0x00U);
    sys->bus.write8(0x8001U, 0x02U); // R0 = 2
    sys->bus.write8(0x8000U, 0x02U);
    sys->bus.write8(0x8001U, 0x05U); // R2 = 5
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xD2U);
    CHECK(sys->ppu.ppu_read(0x0400U) == 0xD3U); // 2 KiB pair (low bit set)
    CHECK(sys->ppu.ppu_read(0x1000U) == 0xD5U);

    // K=1 (1 KiB mode): R0 -> $0000, R8 -> $0400 as independent 1 KiB banks.
    sys->bus.write8(0x8000U, 0x20U);
    sys->bus.write8(0x8001U, 0x04U); // R0 = 4
    sys->bus.write8(0x8000U, 0x28U);
    sys->bus.write8(0x8001U, 0x06U);            // R8 = 6
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xD4U); // R0
    CHECK(sys->ppu.ppu_read(0x0400U) == 0xD6U); // R8 (independent, not R0+1)

    // C=1 inversion: the four-1 KiB region and the (K=1) split region swap halves,
    // so R2 lands at $0000 and R0 at $1000.
    sys->bus.write8(0x8000U, 0xA0U);            // C=1 (0x80) + K=1 (0x20), index R0
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xD5U); // R2 (=5) now at $0000
    CHECK(sys->ppu.ppu_read(0x1000U) == 0xD4U); // R0 (=4) now at $1000
}

TEST_CASE("RAMBO-1 (mapper 64) scanline IRQ reloads with the OR-1 quirk and acknowledges",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_rambo1());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });

    sys->bus.write8(0xC000U, 0x02U); // latch = 2
    sys->bus.write8(0xC001U, 0x00U); // scanline mode (bit 0 = 0), reload on next clock
    sys->bus.write8(0xE001U, 0x00U); // enable

    // The $C001 reload ORs the non-zero latch with 1, so the counter loads 3 (not
    // 2): it then takes three more clocks to underflow.
    sys->mapper->clock_scanline(0U); // reload -> 3
    CHECK_FALSE(irq);
    sys->mapper->clock_scanline(1U); // 3 -> 2
    sys->mapper->clock_scanline(2U); // 2 -> 1
    CHECK_FALSE(irq);
    sys->mapper->clock_scanline(3U); // 1 -> 0 -> assert
    CHECK(irq);

    sys->bus.write8(0xE000U, 0x00U); // disable acknowledges the line
    CHECK_FALSE(irq);
}

TEST_CASE("RAMBO-1 (mapper 64) CPU-cycle IRQ clocks every four cycles and ignores scanlines",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_rambo1());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });

    sys->bus.write8(0xC000U, 0x01U); // latch = 1
    sys->bus.write8(0xC001U, 0x01U); // CPU-cycle mode (bit 0 = 1), reload, prescaler reset
    sys->bus.write8(0xE001U, 0x00U); // enable

    // In cycle mode the scanline clock must be inert.
    sys->mapper->clock_scanline(0U);
    sys->mapper->clock_scanline(1U);
    CHECK_FALSE(irq);

    // The counter advances once per four CPU cycles: the first group reloads it to
    // 1, the second underflows it to 0.
    sys->mapper->clock_cpu_timer(4U); // 1 clock: reload -> 1
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(4U); // 1 clock: 1 -> 0 -> assert
    CHECK(irq);
}

TEST_CASE("RAMBO-1 (mapper 64) save_state/load_state round-trips banking and IRQ mode",
          "[manifests][nes]") {
    auto a = assemble_nes(make_rambo1());
    a->bus.write8(0x8000U, 0x06U);
    a->bus.write8(0x8001U, 0x03U); // R6 = 3
    a->bus.write8(0x8000U, 0x0FU);
    a->bus.write8(0x8001U, 0x05U); // RF = 5
    a->bus.write8(0x8000U, 0x40U); // P=1 -> $8000=RF, $C000=R6
    a->bus.write8(0xC001U, 0x01U); // cycle mode
    const std::uint8_t a8000 = a->bus.read8(0x8000U);
    const std::uint8_t aC000 = a->bus.read8(0xC000U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    a->mapper->save_state(writer);

    auto b = assemble_nes(make_rambo1());
    CHECK(b->bus.read8(0x8000U) != a8000); // power-on differs
    mnemos::chips::state_reader reader(blob);
    b->mapper->load_state(reader);
    CHECK(reader.ok());
    CHECK(b->bus.read8(0x8000U) == a8000);
    CHECK(b->bus.read8(0xC000U) == aC000);
}

TEST_CASE("Bandai FCG (mapper 16) banks CHR/PRG and selects mirroring", "[manifests][nes]") {
    auto sys = assemble_nes(make_bandai_fcg());

    // Power-on: PRG bank 0 at $8000, the last 16 KiB bank fixed at $C000.
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xC000U) == 0xA3U);

    // $x8 selects the 16 KiB bank at $8000; $C000 stays fixed.
    sys->bus.write8(0x8008U, 0x02U);
    CHECK(sys->bus.read8(0x8000U) == 0xA2U);
    CHECK(sys->bus.read8(0xC000U) == 0xA3U);

    // $x0-$x7 select the eight 1 KiB CHR banks.
    sys->bus.write8(0x8000U, 0x05U); // CHR reg 0 -> $0000 = bank 5
    sys->bus.write8(0x8007U, 0x03U); // CHR reg 7 -> $1C00 = bank 3
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xD5U);
    CHECK(sys->ppu.ppu_read(0x1C00U) == 0xD3U);

    // Mirroring $x9: vertical (0) aliases $2000<->$2800; horizontal (1) aliases
    // $2000<->$2400.
    sys->bus.write8(0x8009U, 0x00U);
    sys->ppu.ppu_write(0x2000U, 0xAAU);
    CHECK(sys->ppu.ppu_read(0x2800U) == 0xAAU);
    sys->bus.write8(0x8009U, 0x01U);
    sys->ppu.ppu_write(0x2000U, 0xBBU);
    CHECK(sys->ppu.ppu_read(0x2400U) == 0xBBU);
}

TEST_CASE("Bandai FCG (mapper 16) 16-bit cycle IRQ counts down and acknowledges",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_bandai_fcg());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });

    // Latch = 200 (little-endian low/high); $xA enable copies the latch to the
    // counter and starts it.
    sys->bus.write8(0x800BU, 0xC8U); // latch low = 200
    sys->bus.write8(0x800CU, 0x00U); // latch high = 0
    sys->bus.write8(0x800AU, 0x01U); // enable -> counter = 200
    CHECK_FALSE(irq);

    sys->mapper->clock_cpu_timer(114U); // 200 -> 86
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(114U); // passes through zero -> assert
    CHECK(irq);

    sys->bus.write8(0x800AU, 0x00U); // disable acknowledges the line
    CHECK_FALSE(irq);

    // Enabling while the counter holds zero asserts immediately.
    sys->bus.write8(0x800BU, 0x00U);
    sys->bus.write8(0x800CU, 0x00U);
    sys->bus.write8(0x800AU, 0x01U);
    CHECK(irq);
}

TEST_CASE("Bandai FCG (mapper 16) stores and reads back a byte over the I2C EEPROM",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_bandai_fcg());
    fcg_i2c h{*sys};

    // Sequential write: control (write) -> word address $05 -> data $42.
    h.start();
    CHECK(h.write_byte(0xA0U)); // 1010 000 + R/W=0 (write); device ACKs
    CHECK(h.write_byte(0x05U)); // word address
    CHECK(h.write_byte(0x42U)); // data
    h.stop();

    // Random read of $05: set the pointer, repeated-start, control (read), one byte.
    h.start();
    CHECK(h.write_byte(0xA0U)); // write to set the address pointer
    CHECK(h.write_byte(0x05U));
    h.start();                                   // repeated start
    CHECK(h.write_byte(0xA1U));                  // 1010 000 + R/W=1 (read)
    const std::uint8_t got = h.read_byte(false); // single byte -> master NAKs
    h.stop();
    CHECK(got == 0x42U);
}

TEST_CASE("Bandai FCG (mapper 16) save/load round-trips banking and the EEPROM",
          "[manifests][nes]") {
    auto a = assemble_nes(make_bandai_fcg());
    a->bus.write8(0x8008U, 0x02U); // PRG bank 2
    a->bus.write8(0x8000U, 0x05U); // CHR reg 0 -> bank 5
    fcg_i2c ha{*a};                // store $7E at EEPROM $10
    ha.start();
    ha.write_byte(0xA0U);
    ha.write_byte(0x10U);
    ha.write_byte(0x7EU);
    ha.stop();
    const std::uint8_t a8000 = a->bus.read8(0x8000U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    a->mapper->save_state(writer);

    auto b = assemble_nes(make_bandai_fcg());
    CHECK(b->bus.read8(0x8000U) != a8000); // power-on differs
    mnemos::chips::state_reader reader(blob);
    b->mapper->load_state(reader);
    CHECK(reader.ok());
    CHECK(b->bus.read8(0x8000U) == a8000);    // PRG bank restored
    CHECK(b->ppu.ppu_read(0x0000U) == 0xD5U); // CHR bank restored

    fcg_i2c hb{*b}; // the stored EEPROM byte survived the round-trip
    hb.start();
    hb.write_byte(0xA0U);
    hb.write_byte(0x10U);
    hb.start();
    hb.write_byte(0xA1U);
    const std::uint8_t got = hb.read_byte(false);
    hb.stop();
    CHECK(got == 0x7EU);
}

TEST_CASE("Sunsoft-3 (mapper 67) banks 2 KiB CHR, 16 KiB PRG and selects mirroring",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_sunsoft3());

    // Power-on: PRG bank 0 at $8000, last 16 KiB bank fixed at $C000.
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xC000U) == 0xA3U);

    // $F800 selects the 16 KiB bank at $8000; $C000 stays fixed.
    sys->bus.write8(0xF800U, 0x02U);
    CHECK(sys->bus.read8(0x8000U) == 0xA2U);
    CHECK(sys->bus.read8(0xC000U) == 0xA3U);

    // CHR 2 KiB banks: $8800 -> $0000-$07FF, $B800 -> $1800-$1FFF.
    sys->bus.write8(0x8800U, 0x05U); // CHR0 = 2 KiB bank 5
    sys->bus.write8(0xB800U, 0x02U); // CHR3 = 2 KiB bank 2
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xD5U);
    CHECK(sys->ppu.ppu_read(0x1800U) == 0xD2U);

    // Mirroring $E800: vertical (0) aliases $2000<->$2800; horizontal (1) aliases
    // $2000<->$2400.
    sys->bus.write8(0xE800U, 0x00U);
    sys->ppu.ppu_write(0x2000U, 0xAAU);
    CHECK(sys->ppu.ppu_read(0x2800U) == 0xAAU);
    sys->bus.write8(0xE800U, 0x01U);
    sys->ppu.ppu_write(0x2000U, 0xBBU);
    CHECK(sys->ppu.ppu_read(0x2400U) == 0xBBU);
}

TEST_CASE("Sunsoft-3 (mapper 67) 16-bit IRQ loads high/low, fires on underflow, and pauses",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_sunsoft3());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });

    // Load the counter = 100 ($0064) high byte then low byte through $C800.
    sys->bus.write8(0xC800U, 0x00U); // high
    sys->bus.write8(0xC800U, 0x64U); // low -> counter = 100
    sys->bus.write8(0xD800U, 0x10U); // bit 4 enables counting
    CHECK_FALSE(irq);

    sys->mapper->clock_cpu_timer(50U); // 100 -> 50
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(60U); // 50 wraps past zero -> assert + pause
    CHECK(irq);

    sys->bus.write8(0x8000U, 0x00U); // $8000 acknowledges
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(1000U); // paused after the IRQ -> does not re-fire
    CHECK_FALSE(irq);

    // Re-arm: $D800 resets the high/low write toggle, so $C800 takes the high byte
    // first again -- loading $0005 (not $0500) and firing after ~5 cycles proves it.
    sys->bus.write8(0xD800U, 0x00U);   // disable + reset the write toggle
    sys->bus.write8(0xC800U, 0x00U);   // high
    sys->bus.write8(0xC800U, 0x05U);   // low -> counter = 5
    sys->bus.write8(0xD800U, 0x10U);   // enable
    sys->mapper->clock_cpu_timer(10U); // 5 wraps past zero -> assert
    CHECK(irq);
}

TEST_CASE("Sunsoft-3 (mapper 67) save/load round-trips banking", "[manifests][nes]") {
    auto a = assemble_nes(make_sunsoft3());
    a->bus.write8(0xF800U, 0x02U); // PRG bank 2
    a->bus.write8(0x8800U, 0x05U); // CHR0 = bank 5
    a->bus.write8(0xC800U, 0x12U); // counter high
    a->bus.write8(0xC800U, 0x34U); // counter low -> $1234
    a->bus.write8(0xD800U, 0x10U); // counting
    const std::uint8_t a8000 = a->bus.read8(0x8000U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    a->mapper->save_state(writer);

    auto b = assemble_nes(make_sunsoft3());
    CHECK(b->bus.read8(0x8000U) != a8000); // power-on differs
    mnemos::chips::state_reader reader(blob);
    b->mapper->load_state(reader);
    CHECK(reader.ok());
    CHECK(b->bus.read8(0x8000U) == a8000);    // PRG bank restored
    CHECK(b->ppu.ppu_read(0x0000U) == 0xD5U); // CHR bank restored
}

TEST_CASE("Jaleco SS88006 (mapper 18) banks PRG/CHR through nibble-pair registers",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_jaleco_ss88006());

    // Power-on: banks 0 at $8000/$A000/$C000, the last bank (31) fixed at $E000.
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xA000U) == 0xA0U);
    CHECK(sys->bus.read8(0xC000U) == 0xA0U);
    CHECK(sys->bus.read8(0xE000U) == static_cast<std::uint8_t>(0xA0U + 31U));

    // PRG0 = bank $12 (18): low nibble $2 -> $8000, high nibble $1 -> $8001.
    sys->bus.write8(0x8000U, 0x02U);
    sys->bus.write8(0x8001U, 0x01U);
    CHECK(sys->bus.read8(0x8000U) == static_cast<std::uint8_t>(0xA0U + 0x12U));
    // PRG1 = bank 5 ($A000), PRG2 = bank 7 ($C000); $E000 stays fixed.
    sys->bus.write8(0x8002U, 0x05U);
    sys->bus.write8(0x8003U, 0x00U);
    sys->bus.write8(0x9000U, 0x07U);
    sys->bus.write8(0x9001U, 0x00U);
    CHECK(sys->bus.read8(0xA000U) == static_cast<std::uint8_t>(0xA0U + 5U));
    CHECK(sys->bus.read8(0xC000U) == static_cast<std::uint8_t>(0xA0U + 7U));
    CHECK(sys->bus.read8(0xE000U) == static_cast<std::uint8_t>(0xA0U + 31U));

    // CHR0 = bank $12 via $A000/$A001; CHR4 = bank 9 via $C000/$C001 ($1000);
    // CHR7 = bank 3 via $D002/$D003 ($1C00).
    sys->bus.write8(0xA000U, 0x02U);
    sys->bus.write8(0xA001U, 0x01U);
    sys->bus.write8(0xC000U, 0x09U);
    sys->bus.write8(0xC001U, 0x00U);
    sys->bus.write8(0xD002U, 0x03U);
    sys->bus.write8(0xD003U, 0x00U);
    CHECK(sys->ppu.ppu_read(0x0000U) == static_cast<std::uint8_t>(0xD0U + 0x12U));
    CHECK(sys->ppu.ppu_read(0x1000U) == static_cast<std::uint8_t>(0xD0U + 9U));
    CHECK(sys->ppu.ppu_read(0x1C00U) == static_cast<std::uint8_t>(0xD0U + 3U));
}

TEST_CASE("Jaleco SS88006 (mapper 18) selects mirroring (0=H, 1=V)", "[manifests][nes]") {
    auto sys = assemble_nes(make_jaleco_ss88006());

    // $F002 = 0 -> horizontal: $2000 aliases $2400.
    sys->bus.write8(0xF002U, 0x00U);
    sys->ppu.ppu_write(0x2000U, 0xAAU);
    CHECK(sys->ppu.ppu_read(0x2400U) == 0xAAU);
    // $F002 = 1 -> vertical: $2000 aliases $2800.
    sys->bus.write8(0xF002U, 0x01U);
    sys->ppu.ppu_write(0x2000U, 0xBBU);
    CHECK(sys->ppu.ppu_read(0x2800U) == 0xBBU);
}

TEST_CASE("Jaleco SS88006 (mapper 18) IRQ counts down, honours the width select, and acks",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_jaleco_ss88006());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });

    // 16-bit mode: latch = $0064 (100) via nibbles ($E000=4, $E001=6).
    sys->bus.write8(0xE000U, 0x04U);
    sys->bus.write8(0xE001U, 0x06U);
    sys->bus.write8(0xE002U, 0x00U);
    sys->bus.write8(0xE003U, 0x00U);
    sys->bus.write8(0xF000U, 0x00U);   // reload counter from latch (= 100)
    sys->bus.write8(0xF001U, 0x01U);   // enable, 16-bit width
    sys->mapper->clock_cpu_timer(50U); // 100 -> 50
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(60U); // 50 wraps past zero -> assert
    CHECK(irq);
    // Real games acknowledge by writing $F001 (disabling the counter), NOT $F000 --
    // so $F001 must clear the line, or the IRQ re-fires forever.
    sys->bus.write8(0xF001U, 0x00U);
    CHECK_FALSE(irq);

    // 4-bit width: only the low nibble counts, so a full latch of $00F5 fires after
    // just ~5 cycles -- proving the width mask ($F001 bit 3) ignores the high bits.
    sys->bus.write8(0xE000U, 0x05U);  // bits 0-3 = 5
    sys->bus.write8(0xE001U, 0x0FU);  // bits 4-7 = F (frozen in 4-bit mode)
    sys->bus.write8(0xF000U, 0x00U);  // counter = $00F5
    sys->bus.write8(0xF001U, 0x09U);  // enable + 4-bit width (bit 3)
    sys->mapper->clock_cpu_timer(4U); // low nibble 5 -> 1
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(4U); // 1 wraps past zero -> assert
    CHECK(irq);
}

TEST_CASE("Jaleco SS88006 (mapper 18) save/load round-trips banking", "[manifests][nes]") {
    auto a = assemble_nes(make_jaleco_ss88006());
    a->bus.write8(0x8000U, 0x03U); // PRG0 = bank 3
    a->bus.write8(0x8001U, 0x00U);
    a->bus.write8(0xA000U, 0x07U); // CHR0 = bank 7
    a->bus.write8(0xA001U, 0x00U);
    a->bus.write8(0xF002U, 0x01U); // mirroring vertical
    const std::uint8_t a8000 = a->bus.read8(0x8000U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    a->mapper->save_state(writer);

    auto b = assemble_nes(make_jaleco_ss88006());
    CHECK(b->bus.read8(0x8000U) != a8000); // power-on differs
    mnemos::chips::state_reader reader(blob);
    b->mapper->load_state(reader);
    CHECK(reader.ok());
    CHECK(b->bus.read8(0x8000U) == a8000);                                    // PRG restored
    CHECK(b->ppu.ppu_read(0x0000U) == static_cast<std::uint8_t>(0xD0U + 7U)); // CHR restored
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

TEST_CASE("VRC6 (mapper 24) banks PRG + CHR", "[manifests][nes]") {
    auto sys = assemble_nes(make_vrc6());

    // $8000 = 16 KiB bank 0 (8 KiB bank 0), $E000 = the fixed last 8 KiB bank.
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U);

    sys->bus.write8(0x8000U, 0x01U); // 16 KiB bank 1 -> 8 KiB bank 2 at $8000
    CHECK(sys->bus.read8(0x8000U) == 0xA2U);
    sys->bus.write8(0xC000U, 0x03U); // 8 KiB bank 3 at $C000
    CHECK(sys->bus.read8(0xC000U) == 0xA3U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U); // last bank stays fixed

    // CHR: $D000 -> slot 0 ($0000), $E000 -> slot 4 ($1000). (Reads at $E000 hit the
    // fixed PRG bank; writes there decode to the CHR register.)
    sys->bus.write8(0xD000U, 0x02U);
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xD2U);
    sys->bus.write8(0xE000U, 0x05U);
    CHECK(sys->ppu.ppu_read(0x1000U) == 0xD5U);
}

TEST_CASE("VRC6 (mapper 24) routes the pulse/saw audio and produces sound", "[manifests][nes]") {
    auto sys = assemble_nes(make_vrc6());
    auto* ea = sys->mapper->expansion_audio();
    REQUIRE(ea != nullptr);

    sys->bus.write8(0x9000U, 0x7FU); // pulse 1: duty 7 (50%), volume 15
    sys->bus.write8(0x9001U, 0x40U); // period low
    sys->bus.write8(0x9002U, 0x81U); // enable + period high

    ea->tick(40000);
    const std::size_t avail = sys->mapper->expansion_audio_pending();
    REQUIRE(avail > 0U);
    std::vector<std::int16_t> buf(avail * 2U, 0);
    const std::size_t got = sys->mapper->drain_expansion_audio(buf.data(), avail);
    REQUIRE(got > 0U);
    std::int16_t peak = 0;
    for (const std::int16_t s : buf) {
        peak = std::max(peak, static_cast<std::int16_t>(std::abs(s)));
    }
    CHECK(peak > 0);
}

TEST_CASE("VRC6 (mapper 24) Konami VRC IRQ fires in cycle mode and acknowledges",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_vrc6());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });

    sys->bus.write8(0xF000U, 0xFEU); // IRQ latch = 0xFE
    sys->bus.write8(0xF001U, 0x06U); // enable (bit 1) + cycle mode (bit 2) -> counter 0xFE
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(1U); // 0xFE -> 0xFF
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(1U); // 0xFF -> overflow -> assert
    CHECK(irq);
    sys->bus.write8(0xF002U, 0x00U); // acknowledge
    CHECK_FALSE(irq);
}

TEST_CASE("VRC4 (mapper 23) banks PRG with swap mode + 9-bit CHR", "[manifests][nes]") {
    auto sys = assemble_nes(make_vrc4());

    // Power-on: $8000 = PRG0 (bank 0), $A000 = PRG1 (bank 0), $C000 = second-last
    // (bank 6), $E000 = last (bank 7).
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xA000U) == 0xA0U);
    CHECK(sys->bus.read8(0xC000U) == 0xA6U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U);

    sys->bus.write8(0x8000U, 0x03U); // PRG0 = bank 3
    CHECK(sys->bus.read8(0x8000U) == 0xA3U);
    sys->bus.write8(0xA000U, 0x05U); // PRG1 = bank 5
    CHECK(sys->bus.read8(0xA000U) == 0xA5U);

    // Swap mode ($9002 decodes to sub-index 2 on mapper 23): PRG0 moves to $C000 and
    // $8000 becomes the fixed second-last bank.
    sys->bus.write8(0x9002U, 0x02U);
    CHECK(sys->bus.read8(0x8000U) == 0xA6U); // second-last
    CHECK(sys->bus.read8(0xC000U) == 0xA3U); // PRG0
    CHECK(sys->bus.read8(0xE000U) == 0xA7U); // last stays fixed

    // CHR bank 0: low nibble $B000 = 5 -> bank 5, then high nibble $B001 = 1 ->
    // bank (1<<4)|5 = 21.
    sys->bus.write8(0xB000U, 0x05U);
    CHECK(sys->ppu.ppu_read(0x0000U) == 5U);
    sys->bus.write8(0xB001U, 0x01U);
    CHECK(sys->ppu.ppu_read(0x0000U) == 21U);
}

TEST_CASE("VRC4 (mapper 23) Konami VRC IRQ fires and acknowledges", "[manifests][nes]") {
    auto sys = assemble_nes(make_vrc4());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });

    sys->bus.write8(0xF000U, 0x0EU); // latch low nibble  -> $.E
    sys->bus.write8(0xF001U, 0x0FU); // latch high nibble -> $FE
    sys->bus.write8(0xF002U, 0x06U); // enable (bit 1) + cycle mode (bit 2) -> counter $FE
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(1U); // $FE -> $FF
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(1U); // $FF -> overflow -> assert
    CHECK(irq);
    sys->bus.write8(0xF003U, 0x00U); // acknowledge
    CHECK_FALSE(irq);
}

TEST_CASE("VRC2a (mapper 22) drops the low CHR bit and has no IRQ", "[manifests][nes]") {
    auto rom = make_vrc4();
    rom[6] = 0x60U; // mapper low nibble 6 -> mapper 22 (VRC2a)
    auto sys = assemble_nes(rom);

    // VRC2a uses the written CHR value >> 1: writing 10 to the low register selects
    // bank 5 (10 >> 1).
    sys->bus.write8(0xB000U, 0x0AU);
    CHECK(sys->ppu.ppu_read(0x0000U) == 5U);

    // No IRQ device: $F002 writes do nothing.
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });
    sys->bus.write8(0xF000U, 0x0FU);
    sys->bus.write8(0xF001U, 0x0FU);
    sys->bus.write8(0xF002U, 0x06U);
    sys->mapper->clock_cpu_timer(1000U);
    CHECK_FALSE(irq);
}

TEST_CASE("GxROM (mapper 66) switches 32 KiB PRG + 8 KiB CHR", "[manifests][nes]") {
    auto sys = assemble_nes(make_bankswitch_32k(66));

    CHECK(sys->bus.read8(0x8000U) == 0xA0U);    // PRG bank 0
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xC0U); // CHR bank 0

    // bits 5-4 = PRG bank 2, bits 1-0 = CHR bank 1.
    sys->bus.write8(0x8000U, 0x21U);
    CHECK(sys->bus.read8(0x8000U) == 0xA2U);
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xC1U);
}

TEST_CASE("Color Dreams (mapper 11) switches 32 KiB PRG + 8 KiB CHR", "[manifests][nes]") {
    auto sys = assemble_nes(make_bankswitch_32k(11));

    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xC0U);

    // bits 1-0 = PRG bank 2, bits 7-4 = CHR bank 1.
    sys->bus.write8(0x8000U, 0x12U);
    CHECK(sys->bus.read8(0x8000U) == 0xA2U);
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xC1U);
}

TEST_CASE("Camerica (mapper 71) switches the 16 KiB bank at $8000", "[manifests][nes]") {
    auto sys = assemble_nes(make_camerica());

    CHECK(sys->bus.read8(0x8000U) == 0xA0U); // bank 0
    CHECK(sys->bus.read8(0xC000U) == 0xA3U); // fixed last bank

    sys->bus.write8(0xC000U, 0x02U); // bank reg responds across $C000-$FFFF
    CHECK(sys->bus.read8(0x8000U) == 0xA2U);
    CHECK(sys->bus.read8(0xC000U) == 0xA3U); // last stays fixed
}

TEST_CASE("MMC2 (mapper 9) banks 8 KiB PRG + selects CHR per latch", "[manifests][nes]") {
    auto sys = assemble_nes(make_mmc2(9));

    // Power-on PRG: $8000 = switchable bank 0; $A000/$C000/$E000 = the fixed last
    // three 8 KiB banks (13/14/15 of 16).
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xA000U) == 0xADU);
    CHECK(sys->bus.read8(0xC000U) == 0xAEU);
    CHECK(sys->bus.read8(0xE000U) == 0xAFU);

    sys->bus.write8(0xA000U, 0x05U); // PRG bank 5 at $8000
    CHECK(sys->bus.read8(0x8000U) == 0xA5U);

    // CHR: $B000 = $0xxx bank when latch is $FD, $C000 = when latch is $FE. The latch
    // powers on at $FE, so the $0xxx window shows the $C000 bank.
    sys->bus.write8(0xB000U, 0x02U);            // FD -> CHR 4 KiB bank 2 ($E2)
    sys->bus.write8(0xC000U, 0x03U);            // FE -> CHR 4 KiB bank 3 ($E3)
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xE3U); // default latch $FE
    // $1xxx half uses $D000 ($FD) / $E000 ($FE).
    sys->bus.write8(0xD000U, 0x04U);
    sys->bus.write8(0xE000U, 0x05U);
    CHECK(sys->ppu.ppu_read(0x1000U) == 0xE5U); // default latch $FE

    // The latch flip itself is driven by the PPU fetching tile $FD/$FE during
    // rendering -- verified end-to-end on the real Punch-Out!! ROM.
}

TEST_CASE("MMC4 (mapper 10) banks 16 KiB PRG over the fixed last bank", "[manifests][nes]") {
    auto sys = assemble_nes(make_mmc2(10));

    // 16 KiB switchable at $8000 (bank 0 byte 0 = $A0) over the fixed last 16 KiB
    // (8 KiB bank 14 = $AE).
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xC000U) == 0xAEU);

    sys->bus.write8(0xA000U, 0x02U); // 16 KiB bank 2 -> 8 KiB bank 4 = $A4
    CHECK(sys->bus.read8(0x8000U) == 0xA4U);
    CHECK(sys->bus.read8(0xC000U) == 0xAEU); // last stays fixed
}

TEST_CASE("BNROM (mapper 34) switches the 32 KiB PRG bank", "[manifests][nes]") {
    auto sys = assemble_nes(make_mapper34(0)); // CHR-RAM -> BNROM

    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    sys->bus.write8(0x8000U, 0x02U); // any $8000-$FFFF write sets the 32 KiB bank
    CHECK(sys->bus.read8(0x8000U) == 0xA2U);
}

TEST_CASE("NINA-001 (mapper 34) banks PRG + two 4 KiB CHR via $7FFD-$7FFF", "[manifests][nes]") {
    auto sys = assemble_nes(make_mapper34(4)); // CHR-ROM -> NINA-001

    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xC0U); // $0xxx = CHR bank 0
    CHECK(sys->ppu.ppu_read(0x1000U) == 0xC1U); // $1xxx = CHR bank 1 (power-on)

    sys->bus.write8(0x7FFDU, 0x02U); // 32 KiB PRG bank 2
    CHECK(sys->bus.read8(0x8000U) == 0xA2U);
    sys->bus.write8(0x7FFEU, 0x03U); // $0xxx CHR bank 3
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xC3U);
    sys->bus.write8(0x7FFFU, 0x05U); // $1xxx CHR bank 5
    CHECK(sys->ppu.ppu_read(0x1000U) == 0xC5U);
}

TEST_CASE("VRC1 (mapper 75) banks PRG + 4 KiB CHR with the $9000 high bit", "[manifests][nes]") {
    auto sys = assemble_nes(make_vrc1());

    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U); // fixed last 8 KiB bank

    sys->bus.write8(0x8000U, 0x03U); // $8000 PRG bank 3
    CHECK(sys->bus.read8(0x8000U) == 0xA3U);
    sys->bus.write8(0xA000U, 0x05U); // $A000 PRG bank 5
    CHECK(sys->bus.read8(0xA000U) == 0xA5U);
    sys->bus.write8(0xC000U, 0x02U); // $C000 PRG bank 2
    CHECK(sys->bus.read8(0xC000U) == 0xA2U);

    // CHR bank 0: low nibble $E000 = 2 -> bank 2; $9000 bit 1 adds bit 4 -> bank 18.
    sys->bus.write8(0xE000U, 0x02U);
    CHECK(sys->ppu.ppu_read(0x0000U) == 2U);
    sys->bus.write8(0x9000U, 0x02U); // bit 1 -> CHR0 high bit
    CHECK(sys->ppu.ppu_read(0x0000U) == 18U);
}

TEST_CASE("VRC3 (mapper 73) banks 16 KiB PRG + a 16-bit cycle IRQ", "[manifests][nes]") {
    auto sys = assemble_nes(make_vrc3());

    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xC000U) == 0xA7U); // fixed last 16 KiB bank
    sys->bus.write8(0xF000U, 0x02U);         // 16 KiB bank 2 at $8000
    CHECK(sys->bus.read8(0x8000U) == 0xA2U);

    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });
    // Latch = $FFFE via the four nibble registers; 16-bit mode + enable.
    sys->bus.write8(0x8000U, 0x0EU); // bits 0-3
    sys->bus.write8(0x9000U, 0x0FU); // bits 4-7
    sys->bus.write8(0xA000U, 0x0FU); // bits 8-11
    sys->bus.write8(0xB000U, 0x0FU); // bits 12-15 -> latch $FFFE
    sys->bus.write8(0xC000U, 0x02U); // enable (bit 1), 16-bit mode -> counter = $FFFE
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(1U); // $FFFE -> $FFFF
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(1U); // $FFFF -> overflow -> assert
    CHECK(irq);
    sys->bus.write8(0xD000U, 0x00U); // acknowledge
    CHECK_FALSE(irq);
}

TEST_CASE("Namco 163 (mapper 19) banks PRG + CHR", "[manifests][nes]") {
    auto sys = assemble_nes(make_namco163());

    // Power-on: every switchable PRG window is bank 0; $E000 is the fixed last bank.

    // Power-on: every switchable PRG window is bank 0; $E000 is the fixed last bank.
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xA000U) == 0xA0U);
    CHECK(sys->bus.read8(0xC000U) == 0xA0U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U);

    // $E000 -> $8000 bank, $E800 -> $A000 bank, $F000 -> $C000 bank (writes decode;
    // reads at those addresses hit the fixed PRG bank).
    sys->bus.write8(0xE000U, 0x03U);
    CHECK(sys->bus.read8(0x8000U) == 0xA3U);
    sys->bus.write8(0xE800U, 0x05U);
    CHECK(sys->bus.read8(0xA000U) == 0xA5U);
    sys->bus.write8(0xF000U, 0x02U);
    CHECK(sys->bus.read8(0xC000U) == 0xA2U);
    CHECK(sys->bus.read8(0xE000U) == 0xA7U); // last bank stays fixed

    // CHR: $8000 -> slot 0 ($0000), $B800 -> slot 7 ($1C00).
    sys->bus.write8(0x8000U, 0x02U);
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xD2U);
    sys->bus.write8(0xB800U, 0x05U);
    CHECK(sys->ppu.ppu_read(0x1C00U) == 0xD5U);
}

TEST_CASE("Namco 163 (mapper 19) routes the wavetable audio and produces sound",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_namco163());
    auto* ea = sys->mapper->expansion_audio();
    REQUIRE(ea != nullptr);

    // Channel 8 ($78-$7F): a 4-sample square wave, full volume, one active channel.
    sys->bus.write8(0xF800U, 0xF8U); // sound address $78, auto-increment
    sys->bus.write8(0x4800U, 0x00U); // $78 freq low
    sys->bus.write8(0x4800U, 0x00U); // $79 phase low
    sys->bus.write8(0x4800U, 0x40U); // $7A freq mid -> freq $4000
    sys->bus.write8(0x4800U, 0x00U); // $7B phase mid
    sys->bus.write8(0x4800U, 0xFCU); // $7C freq high 0 + length -> 4 samples
    sys->bus.write8(0x4800U, 0x00U); // $7D phase high
    sys->bus.write8(0x4800U, 0x00U); // $7E wave address
    sys->bus.write8(0x4800U, 0x0FU); // $7F volume 15, 1 channel
    sys->bus.write8(0xF800U, 0x80U); // sound address $00, auto-increment
    sys->bus.write8(0x4800U, 0x0FU); // samples 0,1 = 15,0
    sys->bus.write8(0x4800U, 0x0FU); // samples 2,3 = 15,0

    // The data port reads back through the same window.
    sys->bus.write8(0xF800U, 0x00U); // address $00, no auto-increment
    CHECK(sys->bus.read8(0x4800U) == 0x0FU);

    ea->tick(40000);
    const std::size_t avail = sys->mapper->expansion_audio_pending();
    REQUIRE(avail > 0U);
    std::vector<std::int16_t> buf(avail * 2U, 0);
    const std::size_t got = sys->mapper->drain_expansion_audio(buf.data(), avail);
    REQUIRE(got > 0U);
    std::int16_t peak = 0;
    for (const std::int16_t s : buf) {
        peak = std::max(peak, static_cast<std::int16_t>(std::abs(s)));
    }
    CHECK(peak > 0);
}

TEST_CASE("Namco 163 (mapper 19) 15-bit IRQ counter fires at $7FFF and acknowledges",
          "[manifests][nes]") {
    auto sys = assemble_nes(make_namco163());
    bool irq = false;
    sys->mapper->set_irq_callback([&irq](bool asserted) { irq = asserted; });

    sys->bus.write8(0x5800U, 0xFFU); // counter high = $7F, enable (bit 7)
    sys->bus.write8(0x5000U, 0xFDU); // counter low = $FD -> counter = $7FFD
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(1U); // $7FFD -> $7FFE
    CHECK_FALSE(irq);
    sys->mapper->clock_cpu_timer(1U); // $7FFE -> $7FFF -> assert + stop
    CHECK(irq);

    sys->bus.write8(0x5000U, 0x00U); // rewriting the counter acknowledges
    CHECK_FALSE(irq);
}

TEST_CASE("Namco 163 (mapper 19) save_state/load_state round-trips banking", "[manifests][nes]") {
    auto a = assemble_nes(make_namco163());
    a->bus.write8(0xE000U, 0x03U); // $8000 = PRG bank 3
    a->bus.write8(0x8000U, 0x02U); // CHR slot 0 = bank 2
    const std::uint8_t a8000 = a->bus.read8(0x8000U);
    const std::uint8_t achr = a->ppu.ppu_read(0x0000U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    a->mapper->save_state(writer);

    auto b = assemble_nes(make_namco163());
    CHECK(b->bus.read8(0x8000U) != a8000); // power-on differs
    mnemos::chips::state_reader reader(blob);
    b->mapper->load_state(reader);
    CHECK(reader.ok());
    CHECK(b->bus.read8(0x8000U) == a8000);
    CHECK(b->ppu.ppu_read(0x0000U) == achr);
}

TEST_CASE("Sunsoft-4 (mapper 68) banks 16 KiB PRG over the fixed last bank", "[manifests][nes]") {
    auto sys = assemble_nes(make_sunsoft4());

    // Power-on: $8000 = switchable bank 0; $C000 = the fixed last 16 KiB bank (3).
    CHECK(sys->bus.read8(0x8000U) == 0xA0U);
    CHECK(sys->bus.read8(0xC000U) == 0xA3U);

    // $F000 low 4 bits select the $8000 16 KiB bank.
    sys->bus.write8(0xF000U, 0x02U);
    CHECK(sys->bus.read8(0x8000U) == 0xA2U);
    CHECK(sys->bus.read8(0xC000U) == 0xA3U); // the last bank never moves
}

TEST_CASE("Sunsoft-4 (mapper 68) composes the four 2 KiB CHR banks", "[manifests][nes]") {
    auto sys = assemble_nes(make_sunsoft4());

    // Power-on: the four 2 KiB windows all select bank 0.
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xD0U);

    // $8000/$9000/$A000/$B000 each set the 2 KiB bank at $0000/$0800/$1000/$1800.
    sys->bus.write8(0x8000U, 0x04U); // $0000 <- bank 4
    sys->bus.write8(0x9000U, 0x06U); // $0800 <- bank 6
    sys->bus.write8(0xA000U, 0x08U); // $1000 <- bank 8
    sys->bus.write8(0xB000U, 0x0AU); // $1800 <- bank 10
    CHECK(sys->ppu.ppu_read(0x0000U) == 0xD4U);
    CHECK(sys->ppu.ppu_read(0x0800U) == 0xD6U);
    CHECK(sys->ppu.ppu_read(0x1000U) == 0xD8U);
    CHECK(sys->ppu.ppu_read(0x1800U) == 0xDAU);
}

TEST_CASE("Sunsoft-4 (mapper 68) drives mirroring from the $E000 low 2 bits", "[manifests][nes]") {
    auto sys = assemble_nes(make_sunsoft4());

    // Distinct bytes in the two physical CIRAM pages so the arrangement is
    // observable: write $11 at $2000 (page 0) and $22 at $2400 (the second physical
    // page under vertical mirroring).
    sys->bus.write8(0xE000U, 0x00U); // vertical
    sys->bus.write8(0x2006U, 0x20U);
    sys->bus.write8(0x2006U, 0x00U);
    sys->bus.write8(0x2007U, 0x11U); // $2000
    sys->bus.write8(0x2006U, 0x24U);
    sys->bus.write8(0x2006U, 0x00U);
    sys->bus.write8(0x2007U, 0x22U); // $2400
    CHECK(sys->ppu.ppu_read(0x2000U) == 0x11U);
    CHECK(sys->ppu.ppu_read(0x2400U) == 0x22U); // vertical: $2400 is its own page

    // Single-screen A ($E000 = 2): every nametable region maps to page 0, so $2400
    // now reads page 0's byte.
    sys->bus.write8(0xE000U, 0x02U);
    CHECK(sys->ppu.ppu_read(0x2400U) == 0x11U);
}

TEST_CASE("Sunsoft-4 (mapper 68) save_state/load_state round-trips banking", "[manifests][nes]") {
    auto a = assemble_nes(make_sunsoft4());
    a->bus.write8(0xF000U, 0x02U); // $8000 = PRG bank 2
    a->bus.write8(0x8000U, 0x04U); // $0000 CHR = 2 KiB bank 4
    a->bus.write8(0xC000U, 0x07U); // a nametable page register (stored only)
    const std::uint8_t a8000 = a->bus.read8(0x8000U);
    const std::uint8_t achr = a->ppu.ppu_read(0x0000U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    a->mapper->save_state(writer);

    auto b = assemble_nes(make_sunsoft4());
    CHECK(b->bus.read8(0x8000U) != a8000); // power-on differs
    mnemos::chips::state_reader reader(blob);
    b->mapper->load_state(reader);
    CHECK(reader.ok());
    CHECK(b->bus.read8(0x8000U) == a8000);
    CHECK(b->ppu.ppu_read(0x0000U) == achr);
}

TEST_CASE("Zapper on port 2 reports the trigger and off-screen light sense", "[manifests][nes]") {
    auto sys = assemble_nes(make_synthetic_nrom(), nes_config{.zapper = true});

    // Trigger pulled, aimed off-screen (no frame rendered -> no light): $4017 bit 4
    // set (trigger), bit 3 set (no light).
    sys->set_zapper(-1, -1, true);
    std::uint8_t v = sys->bus.read8(0x4017U);
    CHECK((v & 0x10U) != 0U); // trigger
    CHECK((v & 0x08U) != 0U); // no light off-screen

    // Trigger released.
    sys->set_zapper(-1, -1, false);
    v = sys->bus.read8(0x4017U);
    CHECK((v & 0x10U) == 0U);

    // Port 1 ($4016) is unaffected -- it is still a standard pad.
    sys->set_pad(0, nes_system::btn_a);
    sys->bus.write8(0x4016U, 0x01U);
    sys->bus.write8(0x4016U, 0x00U);
    CHECK((sys->bus.read8(0x4016U) & 0x01U) == 0x01U); // A shifts out first
}

TEST_CASE("Four Score clocks four pads + the adapter signature", "[manifests][nes]") {
    auto sys = assemble_nes(make_synthetic_nrom(), nes_config{.four_score = true});

    // Four distinct single-button pads so each shows up at a unique bit position.
    sys->set_pad(0, nes_system::btn_a);      // bit 0
    sys->set_pad(1, nes_system::btn_b);      // bit 1
    sys->set_pad(2, nes_system::btn_select); // bit 2
    sys->set_pad(3, nes_system::btn_start);  // bit 3

    sys->bus.write8(0x4016U, 0x01U); // strobe
    sys->bus.write8(0x4016U, 0x00U);

    std::array<std::uint8_t, 24> p0{};
    std::array<std::uint8_t, 24> p1{};
    for (std::size_t i = 0; i < 24U; ++i) {
        p0[i] = static_cast<std::uint8_t>(sys->bus.read8(0x4016U) & 0x01U);
        p1[i] = static_cast<std::uint8_t>(sys->bus.read8(0x4017U) & 0x01U);
    }

    // $4016: controller 1 (A at bit 0) then controller 3 (Select at bit 8+2=10) then
    // the signature 1 at bit 19.
    CHECK(p0[0] == 1U);
    CHECK(p0[10] == 1U);
    CHECK(p0[19] == 1U);
    // $4017: controller 2 (B at bit 1) then controller 4 (Start at bit 8+3=11) then
    // the signature 1 at bit 18.
    CHECK(p1[1] == 1U);
    CHECK(p1[11] == 1U);
    CHECK(p1[18] == 1U);

    // Every other bit in the controller+signature window is 0.
    int ones0 = 0;
    int ones1 = 0;
    for (std::size_t i = 0; i < 24U; ++i) {
        ones0 += p0[i];
        ones1 += p1[i];
    }
    CHECK(ones0 == 3); // A + Select + signature
    CHECK(ones1 == 3); // B + Start + signature

    // Reads past the 24th return 1.
    CHECK((sys->bus.read8(0x4016U) & 0x01U) == 1U);
    CHECK((sys->bus.read8(0x4017U) & 0x01U) == 1U);
}

TEST_CASE("looks_like_fds distinguishes FDS disks from iNES carts", "[manifests][nes][fds]") {
    CHECK(looks_like_fds(make_synthetic_fds())); // headered "FDS\x1A"

    std::vector<std::uint8_t> headerless(k_fds_side_size, 0x00U);
    headerless[0] = 0x01U;
    CHECK(looks_like_fds(headerless)); // headerless single side

    CHECK_FALSE(looks_like_fds(make_synthetic_nrom()));               // an iNES cart
    CHECK_FALSE(looks_like_fds(std::vector<std::uint8_t>(100U, 0U))); // neither
}

TEST_CASE("parse_fds_sides strips the header and returns whole sides", "[manifests][nes][fds]") {
    const auto sides = parse_fds_sides(make_synthetic_fds());
    REQUIRE(sides.size() == k_fds_side_size); // one side, 16-byte header removed
    CHECK(sides[0] == 0x01U);                 // disk-info block is first
    CHECK(sides[1] == static_cast<std::uint8_t>('*'));
}

TEST_CASE("FDS image assembles the RP2C33 RAM adapter (RAM + BIOS + drive)",
          "[manifests][nes][fds]") {
    nes_config cfg;
    cfg.fds_bios = make_dummy_fds_bios();
    auto sys = assemble_nes(make_synthetic_fds(), cfg);
    REQUIRE(sys->is_fds);

    // $E000-$FFFF is the BIOS (its reset vector resolves); $6000-$DFFF is 32 KiB RAM.
    CHECK(sys->bus.read8(0xFFFCU) == 0x00U);
    CHECK(sys->bus.read8(0xFFFDU) == 0xE0U);
    sys->bus.write8(0x6000U, 0x5AU);
    sys->bus.write8(0xDFFFU, 0xA5U);
    CHECK(sys->bus.read8(0x6000U) == 0x5AU);
    CHECK(sys->bus.read8(0xDFFFU) == 0xA5U);

    // $4032 reports the disk inserted + ready (bits 0/1 clear).
    CHECK((sys->bus.read8(0x4032U) & 0x03U) == 0x00U);
}

TEST_CASE("FDS disk drive streams block bytes with synthesized CRC gaps", "[manifests][nes][fds]") {
    nes_config cfg;
    cfg.fds_bios = make_dummy_fds_bios();
    auto sys = assemble_nes(make_synthetic_fds(), cfg);

    sys->bus.write8(0x4023U, 0x83U); // enable the disk registers
    sys->bus.write8(0x4025U, 0x2DU); // motor on, transfer running, read mode

    // Pull bytes via the byte-ready ($4030 bit 7) / read-data ($4031) handshake,
    // clocking the drive (the board normally does this once per scanline).
    const auto next_byte = [&]() -> int {
        for (int i = 0; i < 20000; ++i) {
            if ((sys->bus.read8(0x4030U) & 0x80U) != 0U) {
                return sys->bus.read8(0x4031U);
            }
            sys->mapper->clock_cpu_timer(114U);
        }
        return -1;
    };

    std::array<int, 60> b{};
    for (int& v : b) {
        v = next_byte();
    }
    CHECK(b[0] == 0x01);                  // block 1 (disk info) ID
    CHECK(b[1] == static_cast<int>('*')); // "*NINTENDO-HVC*"
    CHECK(b[56] == 0x00);                 // synthesized CRC byte after block 1's 56 bytes
    CHECK(b[57] == 0x00);                 // (the raw .fds has no CRC; the drive supplies it)
    CHECK(b[58] == 0x02);                 // block 2 (file amount) ID, correctly aligned
    CHECK(b[59] == 0x00);                 // zero files
}

TEST_CASE("FDS multi-side disk swapping flips the served side", "[manifests][nes][fds]") {
    nes_config cfg;
    cfg.fds_bios = make_dummy_fds_bios();
    auto sys = assemble_nes(make_synthetic_fds_2side(), cfg);
    REQUIRE(sys->is_fds);
    REQUIRE(sys->mapper->disk_side_count() == 2U);
    CHECK(sys->mapper->current_disk_side() == 0U);

    // Drive the disk from its start and return the disk-info block's game-name byte
    // (offset $10 = the 17th byte), which differs per side.
    const auto read_side_marker = [&]() -> int {
        sys->bus.write8(0x4025U, 0x2EU); // hold/rewind to the side's start
        sys->bus.write8(0x4023U, 0x83U); // enable the disk registers
        sys->bus.write8(0x4025U, 0x2DU); // motor on, transfer running, read
        int byte = -1;
        for (int b = 0; b < 17; ++b) {
            byte = -1;
            for (int i = 0; i < 40000 && byte < 0; ++i) {
                if ((sys->bus.read8(0x4030U) & 0x80U) != 0U) {
                    byte = sys->bus.read8(0x4031U);
                } else {
                    sys->mapper->clock_cpu_timer(114U);
                }
            }
        }
        return byte; // the game-name byte (block offset $10)
    };

    CHECK(read_side_marker() == static_cast<int>('A')); // side 0

    sys->mapper->insert_disk_side(1U);
    CHECK(sys->mapper->current_disk_side() == 1U);
    CHECK((sys->bus.read8(0x4032U) & 0x01U) != 0U);     // mid-swap: the disk reports removed
    CHECK(read_side_marker() == static_cast<int>('B')); // side 1 after the flip
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
