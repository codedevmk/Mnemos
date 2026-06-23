#include "nes_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

    using namespace mnemos::manifests::nes;

    // A 128 KiB-PRG (16 eight-KiB banks) / 32 KiB CHR-ROM (8 four-KiB banks)
    // MMC2/4 cart. PRG 8 KiB bank N byte 0 = $A0+N; CHR 4 KiB bank N byte 0 =
    // $E0+N. `mapper` = 9 (MMC2) or 10 (MMC4).
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
            rom[16U + b * 0x2000U] = static_cast<std::uint8_t>(0xA0U + b);
        }
        const std::size_t chr = 16U + 16U * 0x2000U;
        for (std::size_t b = 0; b < 8U; ++b) {
            rom[chr + b * 0x1000U] = static_cast<std::uint8_t>(0xE0U + b);
        }
        return rom;
    }

} // namespace

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

    // CHR: $B000 = $0xxx bank when latch is $FD, $C000 = when latch is $FE. The
    // latch powers on at $FE, so the $0xxx window shows the $C000 bank.
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
