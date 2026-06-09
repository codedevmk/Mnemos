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
    REQUIRE(m->thirtytwox != nullptr);
    auto& bus = m->genesis->bus;

    // The cartridge is visible at $000000 on the Genesis bus (reset vectors).
    REQUIRE(bus.read8(0x000001U) == 0xFF);
    REQUIRE(bus.read8(0x000006U) == 0x02);

    // The SH-2s power on held in reset and have executed nothing.
    REQUIRE(m->thirtytwox->sh2_reset_asserted);
    REQUIRE(m->thirtytwox->master_cpu.elapsed_cycles() == 0U);
    REQUIRE(m->thirtytwox->slave_cpu.elapsed_cycles() == 0U);

    // While held, the scheduler does not advance them even as the 68000 runs.
    m->begin_slice();
    m->genesis->cpu.tick(2000U);
    m->catch_up_sh2();
    CHECK(m->thirtytwox->master_cpu.elapsed_cycles() == 0U);
}

TEST_CASE("sega32x_machine $A15100 ADEN+RES release starts and parks the SH-2s",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;

    // Low byte of the adapter-control word: ADEN (bit 1) + RES-release (bit 0).
    bus.write8(0xA15101U, 0x03U);
    CHECK_FALSE(m->thirtytwox->sh2_reset_asserted); // released
    // Read-back: RES (bit 0) reflects the live /RES line -- 1 while running.
    CHECK(bus.read8(0xA15101U) == 0x03U);

    // Clearing RES (bit 0 = 0) parks the SH-2s again regardless of ADEN.
    bus.write8(0xA15101U, 0x00U);
    CHECK(m->thirtytwox->sh2_reset_asserted);
}

TEST_CASE("sega32x_machine runs the SH-2s at 3x the 68000 after release", "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    bus.write8(0xA15101U, 0x03U); // ADEN + release RES

    m->begin_slice();
    m->genesis->cpu.tick(3000U);
    const std::uint64_t main_delta = m->genesis->cpu.elapsed_cycles();
    m->catch_up_sh2();

    const std::uint64_t sh2 = m->thirtytwox->master_cpu.elapsed_cycles();
    // The SH-2s tick at 3x the 68000; instruction-atomic stepping overshoots the
    // target by at most one instruction, never undershoots.
    CHECK(sh2 >= main_delta * 3U);
    CHECK(sh2 < main_delta * 3U + 64U);
}

TEST_CASE("sega32x_machine INTM/INTS carry only the CMD bit and edge-assert CMD",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    auto& tx = *m->thirtytwox;
    bus.write8(0xA15101U, 0x03U); // ADEN + release so IRQ delivery is live

    // $A15102 (INTM): the 0->1 CMD-enable transition asserts CMD on the master.
    bus.write8(0xA15103U, 0x04U);
    CHECK(tx.master_irq_mask == 0x04U);
    CHECK(m->thirtytwox->master_cpu.pending_irq_level() == 8);
    CHECK(m->thirtytwox->master_cpu.pending_irq_vector() == 0x48U);
    CHECK(m->thirtytwox->slave_cpu.pending_irq_level() == 0); // targeted, not broadcast
    CHECK(bus.read8(0xA15103U) == 0x04U);

    // A 68000 write cannot set the SH-2-private VINT/HINT/PWM enables.
    bus.write8(0xA15103U, 0xFFU);
    CHECK(tx.master_irq_mask == 0x04U);

    // $A15104 (INTS): same contract for the slave -- a bank-select style write
    // without the CMD bit must not enable anything.
    bus.write8(0xA15105U, 0x03U);
    CHECK(tx.slave_irq_mask == 0x00U);
    CHECK(m->thirtytwox->slave_cpu.pending_irq_level() == 0);
    bus.write8(0xA15105U, 0x04U);
    CHECK(tx.slave_irq_mask == 0x04U);
    CHECK(m->thirtytwox->slave_cpu.pending_irq_level() == 8);
}

TEST_CASE("sega32x_machine shares the COMM bank between the 68000 and the SH-2s",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    auto& tx = *m->thirtytwox;

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
    auto& tx = *m->thirtytwox;
    bus.write8(0xA15101U, 0x03U); // ADEN + release RES

    // Only the master program enables its VINT (slave stays masked).
    tx.set_master_irq_mask(sega32x_system::irq_vint);

    // The VDP resets onto the VBL-entry line, so the first ticked line flushes
    // a boot-time V-blank edge through the wrapper. Consume it, then prove the
    // per-frame edge delivers on its own.
    auto& vdp = m->genesis->vdp;
    vdp.tick(3420U);
    CHECK(m->thirtytwox->master_cpu.pending_irq_level() == 12); // boot edge seen
    m->thirtytwox->master_cpu.clear_irq();
    tx.master_irq_latch = 0U;
    tx.slave_irq_latch = 0U;

    // One full NTSC frame, line by line (edges within a single tick coalesce
    // into the trailing state check, so big ticks can miss them).
    for (int line = 0; line < 262; ++line) {
        vdp.tick(3420U);
    }

    CHECK(m->thirtytwox->master_cpu.pending_irq_level() == 12);
    CHECK(m->thirtytwox->master_cpu.pending_irq_vector() == 0x44U);
    CHECK(m->thirtytwox->slave_cpu.pending_irq_level() == 0);
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
    auto& tx = *m->thirtytwox;
    bus.write8(0xA15101U, 0x03U); // ADEN + release RES
    tx.set_master_irq_mask(sega32x_system::irq_hint);

    // Program the VDP through the 68000 control port: reg 10 (H-int counter)
    // = 0 fires every line, reg 0 bit 4 (IE1) enables the H-interrupt.
    bus.write8(0xC00004U, 0x8AU);
    bus.write8(0xC00005U, 0x00U);
    bus.write8(0xC00004U, 0x80U);
    bus.write8(0xC00005U, 0x14U); // IE1 + the always-set bit 2

    m->genesis->vdp.tick(3420ULL * 2ULL); // a couple of scanlines

    CHECK(m->thirtytwox->master_cpu.pending_irq_level() == 10);
    CHECK(m->thirtytwox->master_cpu.pending_irq_vector() == 0x46U);
    CHECK(m->thirtytwox->slave_cpu.pending_irq_level() == 0);
}

TEST_CASE("sega32x_machine latches PWM CNTL/CYCLE and stubs DREQ/FIFO offsets",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    auto& tx = *m->thirtytwox;

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
