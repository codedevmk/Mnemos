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

TEST_CASE("segacd gate-array $01 controls sub-CPU reset and bus request", "[segacd][gate]") {
    auto sys = assemble_segacd();
    load_test_program(*sys);
    // RESET bit 0->1 releases + boots the sub-CPU.
    sys->gate_write_main(0x01, 0x01);
    sys->run_cycles(200);
    REQUIRE(sys->sub_cpu.elapsed_cycles() > 0U);
    REQUIRE(sys->word_ram[0x100] == 0x12);
    // BUSREQ (bit 1) halts it: no further execution.
    const std::uint64_t at_busreq = sys->sub_cpu.elapsed_cycles();
    sys->gate_write_main(0x01, 0x03); // release + busreq
    sys->run_cycles(200);
    REQUIRE(sys->sub_cpu.elapsed_cycles() == at_busreq);
    // Clearing busreq lets it run again (no re-boot).
    sys->gate_write_main(0x01, 0x01);
    sys->run_cycles(200);
    REQUIRE(sys->sub_cpu.elapsed_cycles() > at_busreq);
}

TEST_CASE("segacd gate-array $03 memory mode tracks RET/DMNA ownership", "[segacd][gate]") {
    auto sys = assemble_segacd();
    REQUIRE((sys->gate_read(0x03) & 0x01U) == 0x01U); // RET=1 power-on (main owns word RAM)
    // Main sets DMNA -> hands word RAM to the sub-CPU: RET clears, DMNA sets.
    sys->gate_write_main(0x03, 0x02);
    REQUIRE((sys->gate_read(0x03) & 0x03U) == 0x02U);
    // Sub sets RET -> hands word RAM back to main: DMNA clears, RET sets.
    sys->gate_write_sub(0x03, 0x01);
    REQUIRE((sys->gate_read(0x03) & 0x03U) == 0x01U);
    // Main writes the PRG-RAM bank (bits 6-7) + mode (bit 2); RET preserved.
    sys->gate_write_main(0x03, 0xC4);
    REQUIRE((sys->gate_read(0x03) & 0xC4U) == 0xC4U);
    REQUIRE((sys->gate_read(0x03) & 0x01U) == 0x01U);
    // The sub side cannot change the PRG bank (main-only bits preserved).
    sys->gate_write_sub(0x03, 0x00);
    REQUIRE((sys->gate_read(0x03) & 0xC0U) == 0xC0U);
}

TEST_CASE("segacd gate-array comm registers are shared", "[segacd][gate]") {
    auto sys = assemble_segacd();
    sys->gate_write_main(0x10, 0xAB); // main->sub comm word
    REQUIRE(sys->gate_read(0x10) == 0xAB);
    sys->gate_write_sub(0x20, 0xCD); // sub->main comm word
    REQUIRE(sys->gate_read(0x20) == 0xCD);
}

TEST_CASE("segacd backup RAM uses the odd byte lane", "[segacd][bus]") {
    auto sys = assemble_segacd();
    sys->sub_bus.write8(0xFE0001U, 0x99U); // odd -> backup[0]
    REQUIRE(sys->backup_ram[0] == 0x99);
    REQUIRE(sys->sub_bus.read8(0xFE0001U) == 0x99);
    sys->sub_bus.write8(0xFE0000U, 0x55U); // even -> ignored
    REQUIRE(sys->sub_bus.read8(0xFE0000U) == 0x00);
    sys->sub_bus.write8(0xFE0003U, 0x42U); // odd -> backup[1]
    REQUIRE(sys->backup_ram[1] == 0x42);
}

TEST_CASE("segacd gate array is reachable through both sub-bus mirrors", "[segacd][bus][gate]") {
    auto sys = assemble_segacd();
    // $FF8003 routes to the sub-side memory-mode write.
    sys->sub_bus.write8(0xFF8003U, 0x01U);
    REQUIRE((sys->gate_read(0x03) & 0x01U) == 0x01U);
    REQUIRE(sys->sub_bus.read8(0xFF8003U) == sys->gate_read(0x03));
    // $0FF800 is the same register block.
    sys->sub_bus.write8(0x0FF810U, 0x77U);
    REQUIRE(sys->gate_read(0x10) == 0x77);
    REQUIRE(sys->sub_bus.read8(0x0FF810U) == 0x77);
}
