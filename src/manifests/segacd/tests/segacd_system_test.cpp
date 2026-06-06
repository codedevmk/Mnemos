// segacd_system (phase B1): the sub-CPU boots from PRG-RAM and runs against its
// bus; PCM + word RAM are reachable through the sub-bus; reset gating + BIOS
// read-overlay behave.

#include "segacd_system.hpp"

#include "disc_image.hpp"
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

    // An IRQ test program: main lowers IPM (MOVE.W #$2000,SR) and spins; the
    // level-2 autovector ($68) points at a handler ($80) that writes a $BEEF
    // marker to word RAM and RTEs.
    void load_irq_program(segacd_system& sys) {
        sys.prg_ram.fill(0);
        const auto put = [&](std::size_t at, std::initializer_list<std::uint8_t> bytes) {
            std::size_t i = at;
            for (const std::uint8_t b : bytes) {
                sys.prg_ram[i++] = b;
            }
        };
        put(0x00, {0x00, 0x08, 0x00, 0x00});             // SSP = $00080000
        put(0x04, {0x00, 0x00, 0x00, 0x40});             // PC  = $40 (main)
        put(0x40, {0x46, 0xFC, 0x20, 0x00, 0x60, 0xFE}); // MOVE.W #$2000,SR ; BRA *
        put(0x68, {0x00, 0x00, 0x00, 0x80});             // autovector L2 -> handler $80
        put(0x80, {0x33, 0xFC, 0xBE, 0xEF, 0x00, 0x08, 0x02, 0x00, 0x4E,
                   0x73}); // MOVE.W #$BEEF,($080200).L ; RTE
    }

    // A minimal multi-sector raw Mode-1 BIN (sync + Mode-1 byte per sector).
    std::vector<std::uint8_t> make_data_bin(int sectors) {
        std::vector<std::uint8_t> d(static_cast<std::size_t>(sectors) * 2352U, 0);
        for (int lba = 0; lba < sectors; ++lba) {
            std::uint8_t* s = &d[static_cast<std::size_t>(lba) * 2352U];
            s[0] = 0x00;
            for (std::size_t i = 1; i <= 10; ++i) {
                s[i] = 0xFF;
            }
            s[11] = 0x00;
            s[15] = 0x01; // Mode 1
            for (std::size_t k = 0; k < 2048; ++k) {
                s[16U + k] = static_cast<std::uint8_t>(lba * 13 + static_cast<int>(k));
            }
        }
        return d;
    }

    // Set a CDC indirect register via the gate array (AR pointer at $05, data at $07).
    void cdc_set_reg(segacd_system& sys, std::uint8_t idx, std::uint8_t val) {
        sys.gate_write_main(0x05, idx);
        sys.gate_write_main(0x07, val);
    }

    // A tiny spin program (reset vectors + BRA *) so run_cycles can pump CDC DMA
    // without the sub-CPU doing anything observable.
    void load_spin(segacd_system& sys) {
        sys.prg_ram.fill(0);
        sys.prg_ram[1] = 0x08; // SSP = $00080000
        sys.prg_ram[7] = 0x08; // PC  = $00000008
        sys.prg_ram[8] = 0x60; // BRA *
        sys.prg_ram[9] = 0xFE;
    }

    // Issue a CDD Play command for the absolute MSF m:s:f.
    void issue_play(segacd_system& sys, int m, int s, int f) {
        sys.gate_write_main(0x42, 0x30); // Play (command 3)
        sys.gate_write_main(0x44, static_cast<std::uint8_t>(m / 10));
        sys.gate_write_main(0x45, static_cast<std::uint8_t>(m % 10));
        sys.gate_write_main(0x46, static_cast<std::uint8_t>(s / 10));
        sys.gate_write_main(0x47, static_cast<std::uint8_t>(s % 10));
        sys.gate_write_main(0x48, static_cast<std::uint8_t>(f / 10));
        sys.gate_write_main(0x49, static_cast<std::uint8_t>(f % 10));
        sys.gate_write_main(0x4B, 0x00); // commit
    }

    // A 2-sector BIN: sector 0 is a valid Mode-1 sector (so open_bin accepts the
    // image); sector 1 holds known little-endian stereo PCM (sample n: L=n,
    // R=n+0x100) for the CD-DA pump to stream.
    std::vector<std::uint8_t> make_audio_bin() {
        std::vector<std::uint8_t> d(2U * 2352U, 0);
        d[0] = 0x00;
        for (std::size_t i = 1; i <= 10; ++i) {
            d[i] = 0xFF;
        }
        d[11] = 0x00;
        d[15] = 0x01;
        std::uint8_t* a = &d[2352];
        for (std::uint16_t n = 0; n < 588; ++n) {
            const std::uint16_t l = n;
            const auto r = static_cast<std::uint16_t>(n + 0x100);
            a[n * 4 + 0] = static_cast<std::uint8_t>(l);
            a[n * 4 + 1] = static_cast<std::uint8_t>(l >> 8);
            a[n * 4 + 2] = static_cast<std::uint8_t>(r);
            a[n * 4 + 3] = static_cast<std::uint8_t>(r >> 8);
        }
        return d;
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

TEST_CASE("segacd sub-CPU $0 is PRG-RAM (no BIOS overlay)", "[segacd][bios]") {
    // The sub bus must NOT overlay the BIOS at $0: the sub-CPU's reset vectors
    // ($0 SSP / $4 PC) come from PRG-RAM, where the main BIOS writes the Sub-CPU
    // BIOS before releasing the sub. An overlay here (as the Emu reference has)
    // shadows those vectors with the MAIN entry -- whose stack is in main work
    // RAM, unmapped on the sub bus -- and crashes the sub, so the real BIOS would
    // never reach its boot screen.
    std::vector<std::uint8_t> bios(64, 0x11); // non-zero BIOS image
    auto sys = assemble_segacd(std::move(bios));
    // Sub $0 reads PRG-RAM (zero at power-on), NOT the BIOS.
    REQUIRE(sys->sub_bus.read8(0x000000U) == 0x00);
    // Writes land in PRG-RAM and read straight back (no read-only shadow).
    sys->sub_bus.write8(0x000000U, 0xFFU);
    REQUIRE(sys->prg_ram[0] == 0xFF);
    REQUIRE(sys->sub_bus.read8(0x000000U) == 0xFF);
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

TEST_CASE("segacd sub-CPU IRQ priority, masking, and acknowledge", "[segacd][irq]") {
    auto sys = assemble_segacd();
    sys->gate_write_main(0x33, 0x7E); // enable all six levels (bits 1-6)
    REQUIRE(sys->pending_irq_level() == 0);
    sys->raise_sub_irq(segacd_system::irq_ifl2); // level 2
    REQUIRE(sys->pending_irq_level() == 2);
    sys->raise_sub_irq(segacd_system::irq_cdd); // level 4 (higher wins)
    REQUIRE(sys->pending_irq_level() == 4);
    sys->raise_sub_irq(segacd_system::irq_subcode); // level 6 (highest)
    REQUIRE(sys->pending_irq_level() == 6);
    // Masking the top source falls back to the next pending one.
    sys->gate_write_sub(0x33, static_cast<std::uint8_t>(0x7EU & ~segacd_system::irq_subcode));
    REQUIRE(sys->pending_irq_level() == 4);
    // $36 bit 0 acknowledges (clears) all pending.
    sys->gate_write_sub(0x36, 0x01);
    REQUIRE(sys->pending_irq_level() == 0);
}

TEST_CASE("segacd sub-CPU takes a level-2 IFL2 interrupt", "[segacd][irq]") {
    auto sys = assemble_segacd();
    load_irq_program(*sys);
    sys->gate_write_main(0x33, segacd_system::irq_ifl2); // enable level 2
    sys->release_sub_reset();
    sys->run_cycles(80);              // main lowers IPM then spins
    sys->gate_write_main(0x00, 0x01); // IFL2 pulse -> raise level 2
    sys->run_cycles(300);             // the sub-CPU accepts -> handler runs
    REQUIRE(sys->word_ram[0x200] == 0xBE);
    REQUIRE(sys->word_ram[0x201] == 0xEF);
}

TEST_CASE("segacd masked sub-CPU IRQ is not taken", "[segacd][irq]") {
    auto sys = assemble_segacd();
    load_irq_program(*sys);
    sys->gate_write_main(0x33, 0x00); // all masked
    sys->release_sub_reset();
    sys->run_cycles(80);
    sys->gate_write_main(0x00, 0x01); // IFL2 raised but masked off
    sys->run_cycles(300);
    REQUIRE(sys->word_ram[0x200] == 0x00); // handler did not run
}

TEST_CASE("segacd CDD plays a data disc and advances the read head", "[segacd][cdd]") {
    const auto bin = make_data_bin(10);
    auto disc = mnemos::disc::disc_image::open_bin(bin);
    REQUIRE(disc.has_value());
    auto sys = assemble_segacd();
    sys->attach_disc(&*disc);
    REQUIRE(sys->cdd_drive_status == segacd_system::cdd_toc);

    issue_play(*sys, 0, 2, 0); // absolute MSF 00:02:00 -> LBA 0
    REQUIRE(sys->cdd_drive_status == segacd_system::cdd_seek);
    REQUIRE(sys->cdd_lba == 0);

    for (int i = 0; i < 3; ++i) {
        sys->cdd_update(); // seek latency resolves to PLAY
    }
    REQUIRE(sys->cdd_drive_status == segacd_system::cdd_play);

    const std::uint64_t before = sys->cdc_sectors_decoded;
    sys->cdd_update();
    REQUIRE(sys->cdd_lba == 1);
    REQUIRE(sys->cdc_sectors_decoded > before); // a sector was fed to the decoder
    REQUIRE(sys->cdda_active == false);         // data track -> no CD-DA
    REQUIRE(sys->gate_read(0x38) == (segacd_system::cdd_play & 0x0F)); // status mirrored
}

TEST_CASE("segacd CDD stop returns to the TOC state", "[segacd][cdd]") {
    const auto bin = make_data_bin(4);
    auto disc = mnemos::disc::disc_image::open_bin(bin);
    auto sys = assemble_segacd();
    sys->attach_disc(&*disc);
    issue_play(*sys, 0, 2, 0);
    sys->gate_write_main(0x42, 0x10); // Stop (command 1)
    sys->gate_write_main(0x4B, 0x00); // commit
    REQUIRE(sys->cdd_drive_status == segacd_system::cdd_toc);
    REQUIRE(sys->gate_read(0x38) == segacd_system::cdd_stop); // STOP status frame
}

TEST_CASE("segacd CDD reports first/last track numbers", "[segacd][cdd]") {
    const auto bin = make_data_bin(4);
    auto disc = mnemos::disc::disc_image::open_bin(bin);
    auto sys = assemble_segacd();
    sys->attach_disc(&*disc);
    sys->gate_write_main(0x42, 0x20);      // Report TOC (command 2)
    sys->gate_write_main(0x44, 0x04);      // sub-command: first/last track
    sys->gate_write_main(0x4B, 0x00);      // commit
    REQUIRE(sys->gate_read(0x3A) == 0x01); // RS2 = first track 1 (BCD)
    REQUIRE(sys->gate_read(0x3B) == 0x01); // RS3 = last track 1 (BCD)
}

TEST_CASE("segacd CDD with no disc reports no-disc", "[segacd][cdd]") {
    auto sys = assemble_segacd();
    REQUIRE(sys->cdd_drive_status == segacd_system::cdd_nodisc);
    sys->attach_disc(nullptr);
    REQUIRE(sys->cdd_drive_status == segacd_system::cdd_nodisc);
}

TEST_CASE("segacd CDC decodes a sector and raises the level-5 IRQ", "[segacd][cdc]") {
    const auto bin = make_data_bin(4);
    auto disc = mnemos::disc::disc_image::open_bin(bin);
    auto sys = assemble_segacd();
    sys->attach_disc(&*disc);
    sys->gate_write_main(0x33, segacd_system::irq_cdc); // enable CDC level-5
    cdc_set_reg(*sys, 0x0A, 0x84);                      // CTRL0: DECEN | WRRQ
    cdc_set_reg(*sys, 0x01, 0x20);                      // IFCTRL: DECIEN

    issue_play(*sys, 0, 2, 0);
    for (int i = 0; i < 4; ++i) {
        sys->cdd_update(); // seek then decode sector 0
    }
    REQUIRE(sys->cdc_sectors_decoded > 0U);
    REQUIRE((sys->sub_irq_pending & segacd_system::irq_cdc) != 0U); // level-5 latched

    // cdc_reg_r over the block-header registers returns the latched header; the
    // mode byte (HEAD3) is 0x01 for a Mode-1 sector.
    sys->gate_write_main(0x05, 0x07); // AR = HEAD3
    REQUIRE(sys->gate_read(0x07) == 0x01);
}

TEST_CASE("segacd CDC DMAs decoded user data to PRG-RAM", "[segacd][cdc]") {
    const auto bin = make_data_bin(4);
    auto disc = mnemos::disc::disc_image::open_bin(bin);
    auto sys = assemble_segacd();
    sys->attach_disc(&*disc);
    cdc_set_reg(*sys, 0x0A, 0x84); // CTRL0: DECEN | WRRQ
    issue_play(*sys, 0, 2, 0);
    for (int i = 0; i < 4; ++i) {
        sys->cdd_update(); // decode sector 0 into the ring
    }

    // DMA 8 user-data bytes (skip the 4-byte header) from the ring to PRG-RAM.
    const auto src = static_cast<std::uint16_t>((sys->cdc_pt & 0x3FFFU) + 4U);
    cdc_set_reg(*sys, 0x04, static_cast<std::uint8_t>(src));       // DAC low
    cdc_set_reg(*sys, 0x05, static_cast<std::uint8_t>(src >> 8U)); // DAC high
    cdc_set_reg(*sys, 0x02, 0x07);                                 // DBC low = 7 -> 8 bytes
    cdc_set_reg(*sys, 0x03, 0x00);                                 // DBC high
    cdc_set_reg(*sys, 0x01, 0x02);                                 // IFCTRL: DOUTEN (arm transfers)
    sys->gate_write_main(0x04, 0x05); // gate $04: transfer dest = 5 (PRG-RAM)
    sys->gate_write_main(0x0A, 0x00); // dst word addr high
    sys->gate_write_main(0x0B, 0x10); // dst word addr low -> byte (0x10<<3)=0x80
    cdc_set_reg(*sys, 0x06, 0x00);    // DTRG -> arm the DMA

    load_spin(*sys);
    sys->release_sub_reset();
    sys->run_cycles(50); // run_cycles services the armed CDC DMA

    // LBA 0 user data is byte k = k; 8 bytes land at PRG-RAM $80.
    for (std::size_t k = 0; k < 8; ++k) {
        REQUIRE(sys->prg_ram[0x80 + k] == static_cast<std::uint8_t>(k));
    }
}

TEST_CASE("segacd CD-DA streams stereo samples from a raw sector", "[segacd][cdda]") {
    const auto bin = make_audio_bin();
    auto disc = mnemos::disc::disc_image::open_bin(bin);
    REQUIRE(disc.has_value());
    auto sys = assemble_segacd();
    sys->attach_disc(&*disc);
    // Drive the CD-DA pump over sector 1 (the PCM sector) directly.
    sys->cdda_active = true;
    sys->cdda_start_lba = 1;
    sys->cdda_end_lba = 1;
    sys->cdda_current_lba = 1;
    sys->cdda_sample_in_sector = 0;

    std::int16_t l = 0;
    std::int16_t r = 0;
    REQUIRE(sys->cdda_next_sample(l, r));
    REQUIRE(l == 0);
    REQUIRE(r == 0x100);
    REQUIRE(sys->cdda_next_sample(l, r));
    REQUIRE(l == 1);
    REQUIRE(r == 0x101);
}

TEST_CASE("segacd CD-DA stops at the end of the range", "[segacd][cdda]") {
    const auto bin = make_audio_bin();
    auto disc = mnemos::disc::disc_image::open_bin(bin);
    auto sys = assemble_segacd();
    sys->attach_disc(&*disc);
    sys->cdda_active = true;
    sys->cdda_start_lba = 1;
    sys->cdda_end_lba = 1;
    sys->cdda_current_lba = 1;
    sys->cdda_sample_in_sector = 0;

    std::int16_t l = 0;
    std::int16_t r = 0;
    for (int i = 0; i < 588; ++i) { // one whole sector of samples
        REQUIRE(sys->cdda_next_sample(l, r));
    }
    REQUIRE(sys->cdda_active == false);         // ran past end -> stopped
    REQUIRE_FALSE(sys->cdda_next_sample(l, r)); // nothing left to pull
}

TEST_CASE("segacd Play on a data track stops CD-DA", "[segacd][cdda]") {
    const auto bin = make_data_bin(4);
    auto disc = mnemos::disc::disc_image::open_bin(bin);
    auto sys = assemble_segacd();
    sys->attach_disc(&*disc);
    sys->cdda_active = true;            // pretend audio was playing
    issue_play(*sys, 0, 2, 0);          // LBA 0 is a data track
    REQUIRE(sys->cdda_active == false); // Play on data stops CD-DA
}

TEST_CASE("segacd sub-CPU timer raises the level-3 IRQ at its period", "[segacd][timer]") {
    auto sys = assemble_segacd();
    sys->gate_write_main(0x33, segacd_system::irq_timer); // enable level-3 in the mask
    sys->gate_write_main(0x31, 0x01);                     // timer_word=1 -> period 2*385 = 770
    sys->release_sub_reset();                             // run_cycles now ticks + accumulates

    sys->run_cycles(700); // under one period
    REQUIRE((sys->sub_irq_pending & segacd_system::irq_timer) == 0U);
    sys->run_cycles(200); // total 900 >= 770 -> the timer fires
    REQUIRE((sys->sub_irq_pending & segacd_system::irq_timer) != 0U);
}

TEST_CASE("segacd sub-CPU timer is disabled when timer_word is 0", "[segacd][timer]") {
    auto sys = assemble_segacd();
    sys->gate_write_main(0x33, segacd_system::irq_timer);
    sys->gate_write_main(0x31, 0x00); // 0 disables the timer
    sys->release_sub_reset();
    sys->run_cycles(100000);
    REQUIRE((sys->sub_irq_pending & segacd_system::irq_timer) == 0U);
}

TEST_CASE("segacd stamp ASIC rotates word RAM into the image buffer + raises L1",
          "[segacd][stamp]") {
    auto sys = assemble_segacd();
    // Source stamp row at word RAM 0..3.
    sys->word_ram[0] = 0xAA;
    sys->word_ram[1] = 0xBB;
    sys->word_ram[2] = 0xCC;
    sys->word_ram[3] = 0xDD;
    // Trace-vector table at word RAM 0x1000: src_x=0, src_y=0, dx=0x0010 (one
    // source column per output pixel), dy=0.
    sys->word_ram[0x1004] = 0x00;
    sys->word_ram[0x1005] = 0x10; // dx = 16 (Q12.4 -> +1 column)
    // Config: image-buffer width=4, height=1, trace vectors at 0x1000 (>>2),
    // image buffer at word RAM 0x2000 (>>2).
    sys->gate_write_main(0x64, 0x00);
    sys->gate_write_main(0x65, 0x04); // IBW = 4
    sys->gate_write_main(0x66, 0x00);
    sys->gate_write_main(0x67, 0x01); // IBH = 1
    sys->gate_write_main(0x6A, 0x04);
    sys->gate_write_main(0x6B, 0x00); // TVADDR = 0x0400 (<<2 = 0x1000)
    sys->gate_write_main(0x68, 0x08);
    sys->gate_write_main(0x69, 0x00); // IBO = 0x0800 (<<2 = 0x2000)

    // Trigger ROT ($59 bit 0).
    sys->gate_write_main(0x58, 0x00);
    sys->gate_write_main(0x59, 0x01);

    // The rotated row landed in the image buffer at word RAM 0x2000.
    REQUIRE(sys->word_ram[0x2000] == 0xAA);
    REQUIRE(sys->word_ram[0x2001] == 0xBB);
    REQUIRE(sys->word_ram[0x2002] == 0xCC);
    REQUIRE(sys->word_ram[0x2003] == 0xDD);
    // graphics-complete raised the sub-CPU level-1 IRQ.
    REQUIRE((sys->sub_irq_pending & segacd_system::irq_graphics) != 0U);
}
