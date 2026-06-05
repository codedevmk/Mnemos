// Phase D1: the Genesis main 68000 reaches the Sega CD sub side through its own
// bus -- gate array ($A12000), word RAM ($200000), and the banked PRG window
// ($020000). Built additively on assemble_genesis (plain Genesis untouched).

#include "segacd_machine.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using mnemos::manifests::segacd::assemble_segacd_machine;

    // A minimal 128 KB "BIOS" with valid 68000 reset vectors so assemble_genesis
    // can boot it as the cartridge.
    std::vector<std::uint8_t> make_bios() {
        std::vector<std::uint8_t> bios(0x20000, 0);
        bios[0] = 0x00;
        bios[1] = 0xFF;
        bios[2] = 0x00;
        bios[3] = 0x00; // SSP = $00FF0000
        bios[4] = 0x00;
        bios[5] = 0x00;
        bios[6] = 0x02;
        bios[7] = 0x00; // PC  = $00000200
        return bios;
    }

} // namespace

TEST_CASE("segacd_machine bridges the Genesis main bus to the sub side", "[segacd][machine]") {
    auto m = assemble_segacd_machine(make_bios());
    REQUIRE(m->genesis != nullptr);
    REQUIRE(m->sub != nullptr);
    auto& bus = m->genesis->bus;

    // BIOS visible at $000000 (the Genesis cartridge) -- reset vectors readable.
    REQUIRE(bus.read8(0x000001U) == 0xFF);
    REQUIRE(bus.read8(0x000006U) == 0x02);

    // $A12000 gate array: a main-side comm write reaches the sub side.
    bus.write8(0xA12010U, 0x99U);
    REQUIRE(m->sub->gate_read(0x10) == 0x99);
    REQUIRE(bus.read8(0xA12010U) == 0x99);

    // $200000 word RAM round-trips through the main bus.
    bus.write8(0x200040U, 0xC3U);
    REQUIRE(m->sub->word_ram[0x40] == 0xC3);
    REQUIRE(bus.read8(0x200040U) == 0xC3);

    // $020000 PRG window, bank 0 (gate $03 = 0x01 at power-on).
    bus.write8(0x020080U, 0x5AU);
    REQUIRE(m->sub->prg_ram[0x80] == 0x5A);
    REQUIRE(bus.read8(0x020080U) == 0x5A);
}

TEST_CASE("segacd_machine PRG window follows the bank register", "[segacd][machine]") {
    auto m = assemble_segacd_machine(make_bios());
    auto& bus = m->genesis->bus;
    // Select PRG bank 1 (gate $03 bits 6-7 = 01) from the main side.
    bus.write8(0xA12003U, 0x40U);
    REQUIRE(((m->sub->gate_read(0x03) >> 6) & 0x03U) == 0x01U);
    bus.write8(0x020000U, 0x7EU);
    REQUIRE(m->sub->prg_ram[0x20000] == 0x7E); // bank 1 base = 0x20000

    // Switch back to bank 0 -- the same window now hits a different page.
    bus.write8(0xA12003U, 0x00U);
    bus.write8(0x020000U, 0x11U);
    REQUIRE(m->sub->prg_ram[0x00000] == 0x11);
    REQUIRE(m->sub->prg_ram[0x20000] == 0x7E); // bank 1 page untouched
}
