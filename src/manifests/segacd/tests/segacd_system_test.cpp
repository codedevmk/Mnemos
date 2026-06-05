// segacd_system (phase B1): the sub-CPU boots from PRG-RAM and runs against its
// bus; PCM + word RAM are reachable through the sub-bus; reset gating + BIOS
// read-overlay behave.

#include "segacd_system.hpp"

#include "rf5c68.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::manifests::segacd::assemble_segacd;
    using mnemos::manifests::segacd::segacd_system;

    // Load a tiny program at PRG-RAM $0: reset vectors (SSP=$00080000, PC=$08),
    // then `MOVE.W #$1234,($00080100).L` followed by `BRA *` (spin).
    void load_test_program(segacd_system& sys) {
        const std::array<std::uint8_t, 18> prog = {
            0x00, 0x08, 0x00, 0x00, // $00: initial SSP = 0x00080000
            0x00, 0x00, 0x00, 0x08, // $04: initial PC  = 0x00000008
            0x33, 0xFC, 0x12, 0x34, // $08: MOVE.W #$1234,
            0x00, 0x08, 0x01, 0x00, //      ($00080100).L
            0x60, 0xFE,             // $10: BRA *  (spin forever)
        };
        for (std::size_t i = 0; i < prog.size(); ++i) {
            sys.prg_ram[i] = prog[i];
        }
    }

} // namespace

TEST_CASE("segacd sub-CPU boots from PRG-RAM and writes word RAM", "[segacd][subcpu]") {
    auto sys = assemble_segacd();
    load_test_program(*sys);
    sys->release_sub_reset();
    sys->run_cycles(200);

    // MOVE.W #$1234 to $00080100 -> word RAM offset 0x100 (big-endian).
    REQUIRE(sys->word_ram[0x100] == 0x12);
    REQUIRE(sys->word_ram[0x101] == 0x34);
    // The sub-CPU actually executed (it is now spinning on BRA).
    REQUIRE(sys->sub_cpu.elapsed_cycles() > 0U);
}

TEST_CASE("segacd sub-CPU stays halted until reset is released", "[segacd][subcpu]") {
    auto sys = assemble_segacd();
    load_test_program(*sys);
    sys->run_cycles(200); // still asserted -> no-op
    REQUIRE(sys->sub_cpu.elapsed_cycles() == 0U);
    REQUIRE(sys->word_ram[0x100] == 0x00);
}

TEST_CASE("segacd sub-bus maps PCM registers and wave RAM", "[segacd][bus][pcm]") {
    auto sys = assemble_segacd();
    // CTRL ($FF0007): enable + voice select reaches the PCM chip.
    sys->sub_bus.write8(0xFF0007U, 0x80U);
    REQUIRE(sys->pcm.read_reg(mnemos::chips::audio::rf5c68::reg_ctrl) == 0x80U);
    REQUIRE(sys->sub_bus.read8(0xFF0007U) == 0x80U);
    // Wave-RAM window ($FF1000, bank 0) writes into the chip's wave RAM.
    sys->sub_bus.write8(0xFF1000U, 0xABU);
    REQUIRE(sys->pcm.waveram()[0] == 0xAB);
    REQUIRE(sys->sub_bus.read8(0xFF1000U) == 0xAB);
}

TEST_CASE("segacd sub-bus maps PRG and word RAM directly", "[segacd][bus]") {
    auto sys = assemble_segacd();
    sys->sub_bus.write8(0x000010U, 0x5AU); // PRG-RAM
    REQUIRE(sys->prg_ram[0x10] == 0x5A);
    sys->sub_bus.write8(0x080040U, 0xC3U); // word RAM (2M mode)
    REQUIRE(sys->word_ram[0x40] == 0xC3);
    REQUIRE(sys->sub_bus.read8(0x080040U) == 0xC3);
}

TEST_CASE("segacd BIOS read-overlay sits over PRG-RAM", "[segacd][bios]") {
    std::vector<std::uint8_t> bios(64, 0x00);
    bios[0] = 0x11;
    bios[1] = 0x22;
    auto sys = assemble_segacd(std::move(bios));
    // Reads in the BIOS range return the boot ROM...
    REQUIRE(sys->sub_bus.read8(0x000000U) == 0x11);
    REQUIRE(sys->sub_bus.read8(0x000001U) == 0x22);
    // ...writes fall through to PRG-RAM underneath (BIOS unchanged on read).
    sys->sub_bus.write8(0x000000U, 0xFFU);
    REQUIRE(sys->prg_ram[0] == 0xFF);
    REQUIRE(sys->sub_bus.read8(0x000000U) == 0x11); // still the BIOS byte
    // Past the BIOS extent it is plain PRG-RAM.
    sys->sub_bus.write8(0x000100U, 0x7EU);
    REQUIRE(sys->sub_bus.read8(0x000100U) == 0x7E);
}
