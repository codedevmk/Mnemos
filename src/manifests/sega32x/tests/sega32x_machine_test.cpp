// Phase B8: the Genesis 68000 reaches the 32X board through its own bus -- the
// adapter-control register at $A15100 drives the SH-2 /RES line, and the
// machine's scheduler runs the two SH-2s at 3x the 68000 clock. Built additively
// on assemble_genesis (a plain Genesis never maps any of this).

#include "sega32x_machine.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using mnemos::manifests::sega32x::assemble_sega32x_machine;
    using mnemos::manifests::sega32x::sega32x_bios;
    using mnemos::manifests::sega32x::sega32x_system;

    // A minimal Genesis cartridge with valid 68000 reset vectors and a self-branch
    // loop at the entry point, so the 68000 advances deterministically when ticked.
    std::vector<std::uint8_t> make_cart() {
        std::vector<std::uint8_t> cart(0x10000, 0);
        cart[0] = 0x00;
        cart[1] = 0xFF;
        cart[2] = 0x00;
        cart[3] = 0x00; // SSP = $00FF0000
        cart[4] = 0x00;
        cart[5] = 0x00;
        cart[6] = 0x02;
        cart[7] = 0x00;     // PC  = $00000200
        cart[0x200] = 0x60; // BRA.B *  (0x60FE: branch to self -- a tight idle loop)
        cart[0x201] = 0xFE;
        return cart;
    }

} // namespace

TEST_CASE("sega32x_machine boots the cartridge and holds the SH-2s in reset",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    REQUIRE(m->genesis != nullptr);
    REQUIRE(m->sega32x != nullptr);
    auto& bus = m->genesis->bus;

    // The cartridge is visible at $000000 on the Genesis bus (reset vectors).
    REQUIRE(bus.read8(0x000001U) == 0xFF);
    REQUIRE(bus.read8(0x000006U) == 0x02);

    // The SH-2s power on held in reset and have executed nothing.
    REQUIRE(m->sega32x->sh2_reset_asserted);
    REQUIRE(m->sega32x->master_cpu.elapsed_cycles() == 0U);
    REQUIRE(m->sega32x->slave_cpu.elapsed_cycles() == 0U);

    // While held, the scheduler does not advance them even as the 68000 runs.
    m->genesis->cpu.tick(2000U);
    m->catch_up_sh2();
    CHECK(m->sega32x->master_cpu.elapsed_cycles() == 0U);
}

TEST_CASE("sega32x_machine $A15100 ADEN+nRES release starts and parks the SH-2s",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;

    // Low byte of the adapter-control word: ADEN (bit 0) + nRES (bit 1).
    bus.write8(0xA15101U, 0x03U);
    CHECK_FALSE(m->sega32x->sh2_reset_asserted); // released
    // Read-back: nRES (bit 1) reflects the live /RES line -- 1 while running.
    CHECK(bus.read8(0xA15101U) == 0x03U);

    // Clearing nRES (bit 1 = 0) parks the SH-2s again; ADEN stays latched.
    bus.write8(0xA15101U, 0x00U);
    CHECK(m->sega32x->sh2_reset_asserted);
    CHECK(bus.read8(0xA15101U) == 0x01U);
}

TEST_CASE("sega32x_machine serves the MARS adapter identity at $A130EC", "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;

    // The security block probes this before ADEN is set.
    CHECK(bus.read8(0xA130ECU) == 'M');
    CHECK(bus.read8(0xA130EDU) == 'A');
    CHECK(bus.read8(0xA130EEU) == 'R');
    CHECK(bus.read8(0xA130EFU) == 'S');
}

TEST_CASE("sega32x_machine runs the SH-2s at 3x the 68000 after release", "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    bus.write8(0xA15101U, 0x03U); // ADEN + release RES

    m->genesis->cpu.tick(3000U);
    const std::uint64_t main_delta = m->genesis->cpu.elapsed_cycles();
    m->catch_up_sh2();

    const std::uint64_t sh2 = m->sega32x->master_cpu.elapsed_cycles();
    // The SH-2s tick at 3x the 68000; instruction-atomic stepping overshoots the
    // target by at most one instruction, never undershoots.
    CHECK(sh2 >= main_delta * 3U);
    CHECK(sh2 < main_delta * 3U + 64U);
}

TEST_CASE("sega32x_machine $A15102 requests CMD interrupts held until the SH-2 clears",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    auto& tx = *m->sega32x;
    bus.write8(0xA15101U, 0x03U); // ADEN + release so IRQ delivery is live

    // INTM (bit 0): delivery is gated by the master's own CMD enable; the
    // request bit reads back at $A15102 either way.
    tx.set_master_irq_mask(sega32x_system::irq_cmd);
    bus.write8(0xA15103U, 0x01U);
    CHECK(m->sega32x->master_cpu.pending_irq_level() == 8);
    CHECK(m->sega32x->master_cpu.pending_irq_vector() == 0x44U);
    CHECK(m->sega32x->slave_cpu.pending_irq_level() == 0); // targeted, not broadcast
    CHECK(bus.read8(0xA15103U) == 0x01U);

    // A 68000 zero-write does NOT clear the request; the master SH-2's
    // CMD-interrupt-clear register ($401A) does.
    bus.write8(0xA15103U, 0x00U);
    CHECK(bus.read8(0xA15103U) == 0x01U);
    tx.master_bus.write8(0x0000401AU, 0x00U);
    CHECK(bus.read8(0xA15103U) == 0x00U);

    // INTS (bit 1) targets the slave: with its CMD enable off the edge stays
    // latched in the 32X latch (delivered on a later unmask) and the request
    // bit still reads 1.
    bus.write8(0xA15103U, 0x02U);
    CHECK(m->sega32x->slave_cpu.pending_irq_level() == 0);
    CHECK((tx.slave_irq_latch & sega32x_system::irq_cmd) != 0U);
    CHECK(bus.read8(0xA15103U) == 0x02U);
    tx.slave_bus.write8(0x0000401BU, 0x00U); // odd byte clears too (word register)
    CHECK(bus.read8(0xA15103U) == 0x00U);
}

TEST_CASE("sega32x_machine $A15104 swings the $900000 window across cart banks",
          "[sega32x][machine]") {
    auto cart = make_cart();
    cart.resize(0x200000U, 0x00U); // two full 1 MiB banks
    cart[0x000123U] = 0x11U;
    cart[0x100123U] = 0x22U;
    auto m = assemble_sega32x_machine(std::move(cart));
    auto& bus = m->genesis->bus;

    CHECK(bus.read8(0x900123U) == 0x11U); // power-on bank 0
    bus.write8(0xA15105U, 0x01U);
    CHECK(bus.read8(0xA15105U) == 0x01U);
    CHECK(bus.read8(0x900123U) == 0x22U); // bank 1 now visible
    bus.write8(0xA15105U, 0x03U);         // past the cart end: window unchanged
    CHECK(bus.read8(0x900123U) == 0x22U);
    bus.write8(0xA15105U, 0x00U);
    CHECK(bus.read8(0x900123U) == 0x11U);
}

TEST_CASE("sega32x_machine shares the COMM bank between the 68000 and the SH-2s",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    auto& tx = *m->sega32x;

    // 68000 writes COMM word 0 byte-wise (big-endian lanes), the SH-2 buses see
    // the same word through their $00004020 system-register window.
    bus.write8(0xA15120U, 0x4DU); // "M"
    bus.write8(0xA15121U, 0x5FU); // "_"
    CHECK(tx.comm[0] == 0x4D5FU);
    CHECK(tx.master_bus.read8(0x00004020U) == 0x4DU);
    CHECK(tx.slave_bus.read8(0x00004021U) == 0x5FU);

    // SH-2 writes are visible to the 68000 (word 7, both lanes).
    tx.slave_bus.write8(0x0000402EU, 0x12U);
    tx.slave_bus.write8(0x0000402FU, 0x34U);
    CHECK(bus.read8(0xA1512EU) == 0x12U);
    CHECK(bus.read8(0xA1512FU) == 0x34U);
}

TEST_CASE("sega32x_machine sources VINT from the Genesis VDP V-blank edge", "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    auto& tx = *m->sega32x;
    bus.write8(0xA15101U, 0x03U); // ADEN + release RES

    // Only the master program enables its VINT (slave stays masked).
    tx.set_master_irq_mask(sega32x_system::irq_vint);

    // The VDP resets onto the VBL-entry line, so the first ticked line flushes
    // a boot-time V-blank edge through the wrapper. Consume it, then prove the
    // per-frame edge delivers on its own.
    auto& vdp = m->genesis->vdp;
    vdp.tick(3420U);
    CHECK(m->sega32x->master_cpu.pending_irq_level() == 12); // boot edge seen
    m->sega32x->master_cpu.clear_irq();
    tx.master_irq_latch = 0U;
    tx.slave_irq_latch = 0U;

    // One full NTSC frame, line by line (edges within a single tick coalesce
    // into the trailing state check, so big ticks can miss them).
    for (int line = 0; line < 262; ++line) {
        vdp.tick(3420U);
    }

    CHECK(m->sega32x->master_cpu.pending_irq_level() == 12);
    CHECK(m->sega32x->master_cpu.pending_irq_vector() == 0x46U);
    CHECK(m->sega32x->slave_cpu.pending_irq_level() == 0);
    CHECK((tx.slave_irq_latch & sega32x_system::irq_vint) != 0U); // latched for later
    // V-blank is mirrored into adapter-control bit 7 (a poll-based frame sync).
    CHECK((tx.adapter_ctrl & 0x0080U) != 0U);
    // The stock Genesis vblank behaviour still runs through the wrapper: the
    // boot edge plus the per-frame edge both counted.
    CHECK(m->genesis->frame_index == 2U);
}

TEST_CASE("sega32x_machine sources HINT from the VDP line-counter latch", "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    auto& tx = *m->sega32x;
    bus.write8(0xA15101U, 0x03U); // ADEN + release RES
    tx.set_master_irq_mask(sega32x_system::irq_hint);

    // Program the VDP through the 68000 control port: reg 10 (H-int counter)
    // = 0 fires every line, reg 0 bit 4 (IE1) enables the H-interrupt.
    bus.write8(0xC00004U, 0x8AU);
    bus.write8(0xC00005U, 0x00U);
    bus.write8(0xC00004U, 0x80U);
    bus.write8(0xC00005U, 0x14U); // IE1 + the always-set bit 2

    m->genesis->vdp.tick(3420ULL * 2ULL); // a couple of scanlines

    CHECK(m->sega32x->master_cpu.pending_irq_level() == 10);
    CHECK(m->sega32x->master_cpu.pending_irq_vector() == 0x45U);
    CHECK(m->sega32x->slave_cpu.pending_irq_level() == 0);
}

TEST_CASE("sega32x_machine maps the cart into the SH-2 partition windows", "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& tx = *m->sega32x;

    // Partition 0: cart at $02000000 (plus the $82000000 cacheable alias).
    CHECK(tx.master_bus.read8(0x02000001U) == 0xFFU); // cart[1] (SSP byte)
    CHECK(tx.master_bus.read8(0x02000006U) == 0x02U); // cart[6] (PC byte)
    CHECK(tx.slave_bus.read8(0x82000006U) == 0x02U);

    // Cache-through views: cart at $20000000 / $22000000 (the header-check
    // alias the boot ROM uses) -- but the system registers stay on top of the
    // $20004000 window.
    CHECK(tx.master_bus.read8(0x20000006U) == 0x02U);
    CHECK(tx.master_bus.read8(0x22000006U) == 0x02U);
    tx.comm[0] = 0x4D5FU;
    CHECK(tx.master_bus.read8(0x20004020U) == 0x4DU); // COMM, not cart bytes

    // Cache-through aliases of the frame buffer and SDRAM.
    tx.master_bus.write8(0x24000000U, 0xAAU);
    CHECK(tx.framebuffer[0] == 0xAAU);
    CHECK(tx.slave_bus.read8(0x04000000U) == 0xAAU);
    tx.slave_bus.write8(0x26000010U, 0xBBU);
    CHECK(tx.sdram[0x10] == 0xBBU);
    CHECK(tx.master_bus.read8(0x86000010U) == 0xBBU); // partition-4 alias
}

TEST_CASE("sega32x_machine release edge restarts the SH-2s from their reset vectors",
          "[sega32x][machine]") {
    sega32x_bios bios;
    bios.m_bios.assign(16, 0);
    bios.m_bios[3] = 0x10U; // master PC = $00000010
    bios.m_bios[6] = 0x40U; // master SP = $00004000... (big-endian @4)
    bios.s_bios.assign(16, 0);
    bios.s_bios[3] = 0x20U; // slave PC = $00000020
    auto m = assemble_sega32x_machine(make_cart(), bios);
    auto& bus = m->genesis->bus;
    auto& tx = *m->sega32x;

    bus.write8(0xA15101U, 0x03U); // ADEN + release
    CHECK(tx.master_cpu.cpu_registers().pc == 0x10U);
    CHECK(tx.slave_cpu.cpu_registers().pc == 0x20U);

    // Park, scribble PC by running, then re-release: vectors are re-fetched.
    bus.write8(0xA15101U, 0x00U);
    bus.write8(0xA15101U, 0x03U);
    CHECK(tx.master_cpu.cpu_registers().pc == 0x10U);
}

TEST_CASE("sega32x_machine overlays the G BIOS vectors on the ADEN edge and maps the 68K cart "
          "windows",
          "[sega32x][machine]") {
    sega32x_bios bios;
    bios.g_bios.assign(256, 0x5AU);
    auto m = assemble_sega32x_machine(make_cart(), bios);
    auto& bus = m->genesis->bus;

    // Power-on is plain-Genesis mode: the cart's own vectors at $000000 (the
    // security block boots from them before it enables the adapter).
    CHECK(bus.read8(0x000001U) == 0xFFU); // cart SSP byte
    CHECK(bus.read8(0x000006U) == 0x02U); // cart PC byte

    // The security block sets ADEN alone (bit 0): the overlay appears, the
    // SH-2s stay parked.
    bus.write8(0xA15101U, 0x01U);
    CHECK(m->sega32x->sh2_reset_asserted);
    CHECK(bus.read8(0x000000U) == 0x5AU);
    CHECK(bus.read8(0x0000FFU) == 0x5AU);
    CHECK(bus.read8(0x000200U) == 0x60U); // cart entry loop opcode past the overlay

    // $880000 views the raw cart (no overlay): cart[0] = $00, cart[6] = $02.
    CHECK(bus.read8(0x880000U) == 0x00U);
    CHECK(bus.read8(0x880006U) == 0x02U);
    // $900000 banked window, power-on bank 0.
    CHECK(bus.read8(0x900200U) == 0x60U);
    // Reads past the 64 KiB test cart fall to open bus.
    CHECK(bus.read8(0x890000U) == 0xFFU);
}

TEST_CASE("sega32x_machine without a G BIOS leaves the cart vectors at $000000",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    CHECK(m->genesis->bus.read8(0x000001U) == 0xFFU); // cart SSP byte
    CHECK(m->genesis->bus.read8(0x000006U) == 0x02U); // cart PC byte
}

TEST_CASE("sega32x_machine exposes the 32X VDP to both SH-2s and the 68000", "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    auto& tx = *m->sega32x;
    using vdp_chip = mnemos::chips::video::sega32x_vdp;

    // Master SH-2 writes the bitmap-mode register at $4100 (byte lanes, even =
    // high); the slave sees it through the cache-through mirror, the 68000
    // through $A15180.
    tx.master_bus.write8(0x00004101U, vdp_chip::mode_packed);
    CHECK(tx.vdp.mode() == vdp_chip::mode_packed);
    CHECK(tx.slave_bus.read8(0x20004101U) == vdp_chip::mode_packed);
    CHECK(bus.read8(0xA15181U) == vdp_chip::mode_packed);

    // Palette CRAM at $4200 (and its P1 mirror), shared storage.
    tx.master_bus.write8(0x00004202U, 0x7CU);
    tx.master_bus.write8(0x00004203U, 0x1FU);
    CHECK(tx.vdp.palette(1) == 0x7C1FU);
    CHECK(tx.slave_bus.read8(0x20004202U) == 0x7CU);

    // An autofill programmed through the register window fills the shared
    // frame buffer when the DATA low byte completes the word.
    tx.master_bus.write8(0x00004105U, 0x01U); // length = 1 -> 2 words
    tx.master_bus.write8(0x00004106U, 0x00U); // addr high
    tx.master_bus.write8(0x00004107U, 0x10U); // addr low -> word $0010
    tx.master_bus.write8(0x00004108U, 0xDEU); // data high (latch only)
    CHECK(tx.framebuffer[0x10U * 2U] == 0x00U);
    tx.master_bus.write8(0x00004109U, 0xADU); // data low -> fill fires
    CHECK(tx.framebuffer[0x10U * 2U] == 0xDEU);
    CHECK(tx.framebuffer[0x10U * 2U + 1U] == 0xADU);
    CHECK(tx.framebuffer[0x11U * 2U] == 0xDEU);
    CHECK(tx.framebuffer[0x12U * 2U] == 0x00U);
}

TEST_CASE("sega32x_machine commits the VDP frame-select on the V-blank edge",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& tx = *m->sega32x;
    auto& vdp = m->genesis->vdp;

    // Flush the boot V-blank edge (the Genesis VDP resets onto the VBL line),
    // then run to active display so the next rising edge is a clean one.
    vdp.tick(3420U);
    for (int line = 0; line < 100; ++line) {
        vdp.tick(3420U);
    }
    CHECK((tx.vdp.fb_control() & 0x8000U) == 0U); // VBLK low in active display

    // The SH-2 writes FS=1; the banks hold until V-blank rises, then both the
    // access bank and the $04000000 window flip together.
    tx.master_bus.write8(0x0000410BU, 0x01U); // FBCR low byte
    CHECK(tx.vdp.access_bank() == 0);
    CHECK(tx.fb_access_bank == 0);
    for (int line = 0; line < 262; ++line) {
        vdp.tick(3420U);
    }
    CHECK(tx.vdp.access_bank() == 1);
    CHECK(tx.fb_access_bank == 1);
    // The window now reaches bank 1: a write through $04000000 lands at
    // byte $20000 of the underlying store.
    tx.master_bus.write8(0x04000000U, 0x5AU);
    CHECK(tx.framebuffer[0x20000] == 0x5AU);
    CHECK(tx.framebuffer[0] != 0x5AU);
}

TEST_CASE("sega32x_machine exposes the 68K frame-buffer and overwrite windows",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    auto& tx = *m->sega32x;

    // $840000 reads and writes the live access bank (bank 0 at power-on).
    bus.write8(0x840010U, 0x5AU);
    CHECK(tx.framebuffer[0x10] == 0x5AU);
    CHECK(bus.read8(0x840010U) == 0x5AU);

    // $860000 is the overwrite image: a zero byte is skipped, non-zero lands.
    bus.write8(0x860010U, 0x00U);
    CHECK(tx.framebuffer[0x10] == 0x5AU); // zero write skipped
    bus.write8(0x860010U, 0xA5U);
    CHECK(tx.framebuffer[0x10] == 0xA5U);

    // Both windows follow the access-bank flip.
    tx.set_fb_access_bank(1);
    bus.write8(0x840010U, 0x77U);
    CHECK(tx.framebuffer[0x20000U + 0x10U] == 0x77U);
    CHECK(tx.framebuffer[0x10] == 0xA5U); // bank 0 untouched

    // The SH-2-side overwrite image at FB+$20000 has the same semantics.
    tx.set_fb_access_bank(0);
    tx.master_bus.write8(0x04020020U, 0x00U);
    CHECK(tx.framebuffer[0x20] == 0x00U); // skipped (still the cleared value)
    tx.master_bus.write8(0x04020020U, 0x42U);
    CHECK(tx.framebuffer[0x20] == 0x42U);
    tx.slave_bus.write8(0x24020020U, 0x00U); // cache-through alias, zero skipped
    CHECK(tx.framebuffer[0x20] == 0x42U);
    CHECK(tx.slave_bus.read8(0x04020020U) == 0x42U);
}

TEST_CASE("sega32x_machine latches PWM CNTL/CYCLE and stubs DREQ/FIFO offsets",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    auto& tx = *m->sega32x;

    bus.write8(0xA15130U, 0x01U); // CNTL high byte (TM field)
    bus.write8(0xA15131U, 0x05U); // CNTL low byte
    bus.write8(0xA15132U, 0x02U); // CYCLE high byte
    bus.write8(0xA15133U, 0x07U); // CYCLE low byte
    CHECK(tx.pwm_cntl == 0x0105U);
    CHECK(tx.pwm_cycle == 0x0207U);
    CHECK(bus.read8(0xA15131U) == 0x05U);
    CHECK(bus.read8(0xA15133U) == 0x07U);
    // The SH-2s read the same registers through their own window ($4030/$4032).
    CHECK(tx.master_bus.read8(0x00004031U) == 0x05U);
    CHECK(tx.slave_bus.read8(0x00004032U) == 0x02U);

    // DREQ control and the PWM FIFO offsets read 0 and drop writes for now.
    CHECK(bus.read8(0xA15106U) == 0x00U);
    CHECK(bus.read8(0xA15107U) == 0x00U);
    bus.write8(0xA15135U, 0xAAU); // LCH FIFO -- dropped
    CHECK(bus.read8(0xA15135U) == 0x00U);
}

TEST_CASE("sega32x_machine streams 68000 words through the DREQ FIFO into SH-2 DMA",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    auto& tx = *m->sega32x;
    bus.write8(0xA15101U, 0x03U); // ADEN + release

    // Program the slave DMAC: channel 0 module-request (CHCR.AR=0), source
    // fixed at the FIFO port ($20004012), destination incrementing into SDRAM,
    // word units, 4 transfers, channel + master enable.
    const auto wr32 = [&tx](std::uint32_t a, std::uint32_t v) {
        // The on-chip window is intercepted by the CPU, not bus-mapped.
        auto& p = tx.slave_cpu.peripherals();
        p.write8(a, static_cast<std::uint8_t>(v >> 24U));
        p.write8(a + 1U, static_cast<std::uint8_t>(v >> 16U));
        p.write8(a + 2U, static_cast<std::uint8_t>(v >> 8U));
        p.write8(a + 3U, static_cast<std::uint8_t>(v));
    };
    wr32(0xFFFFFF80U, 0x20004012U); // SAR0 = the FIFO read port
    wr32(0xFFFFFF84U, 0x06000400U); // DAR0 = SDRAM
    wr32(0xFFFFFF88U, 4U);          // TCR0 = 4 units
    wr32(0xFFFFFF8CU, 0x4401U);     // CHCR0: DM=inc, TS=word, AR=0, DE
    wr32(0xFFFFFFB0U, 1U);          // DMAOR.DME

    // 68000: arm 68S, then push four words through the FIFO write port.
    bus.write8(0xA15107U, 0x04U); // DREQ ctrl low byte: 68S
    CHECK((tx.dreq_ctrl & 0x04U) != 0U);
    const std::array<std::uint16_t, 4> words{{0x1234U, 0x5678U, 0x9ABCU, 0xDEF0U}};
    for (const std::uint16_t w : words) {
        bus.write8(0xA15112U, static_cast<std::uint8_t>(w >> 8U));
        bus.write8(0xA15113U, static_cast<std::uint8_t>(w));
    }
    CHECK(tx.dreq_fifo_count == 4U);

    // Run the SH-2s: the slave's DMAC drains the FIFO into SDRAM.
    tx.run_cycles(64U);
    CHECK(tx.dreq_fifo_count == 0U);
    CHECK(tx.sdram[0x400] == 0x12U);
    CHECK(tx.sdram[0x401] == 0x34U);
    CHECK(tx.sdram[0x404] == 0x9AU);
    CHECK(tx.sdram[0x407] == 0xF0U);

    // FULL status: 8 queued words raise bit 7 of the DREQ control read;
    // clearing 68S flushes.
    for (int i = 0; i < 8; ++i) {
        bus.write8(0xA15112U, 0x11U);
        bus.write8(0xA15113U, 0x22U);
    }
    CHECK((bus.read8(0xA15107U) & 0x80U) != 0U);
    bus.write8(0xA15107U, 0x00U);
    CHECK(tx.dreq_fifo_count == 0U);
}
