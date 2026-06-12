#include "sega32x_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace {
    using mnemos::manifests::sega32x::assemble_sega32x;
    using mnemos::manifests::sega32x::sega32x_system;

    void write_be32(std::span<std::uint8_t> mem, std::size_t off, std::uint32_t value) {
        mem[off] = static_cast<std::uint8_t>(value >> 24U);
        mem[off + 1U] = static_cast<std::uint8_t>(value >> 16U);
        mem[off + 2U] = static_cast<std::uint8_t>(value >> 8U);
        mem[off + 3U] = static_cast<std::uint8_t>(value);
    }
} // namespace

TEST_CASE("sega32x_system boots the master and slave from distinct BIOS vectors") {
    auto sys = assemble_sega32x();
    write_be32(sys->m_bios, 0, 0x06000100U); // master PC
    write_be32(sys->m_bios, 4, 0x0603FFFCU); // master SP
    write_be32(sys->s_bios, 0, 0x06000200U); // slave PC
    write_be32(sys->s_bios, 4, 0x0603FFFCU); // slave SP
    sys->reset();
    CHECK(sys->master_cpu.cpu_registers().pc == 0x06000100U);
    CHECK(sys->slave_cpu.cpu_registers().pc == 0x06000200U);
    CHECK(sys->master_cpu.cpu_registers().r[15] == 0x0603FFFCU);
}

TEST_CASE("sega32x_system shares SDRAM and the COMM bank across both buses") {
    auto sys = assemble_sega32x();
    // SDRAM written via the master bus is visible on the slave bus.
    sys->master_bus.write8(0x06000010U, 0xABU);
    CHECK(sys->slave_bus.read8(0x06000010U) == 0xABU);
    // COMM word 0 (big-endian) in the system-register window, written via the
    // master and read back via the slave.
    sys->master_bus.write8(0x00004020U, 0x12U);
    sys->master_bus.write8(0x00004021U, 0x34U);
    CHECK(sys->slave_bus.read8(0x00004020U) == 0x12U);
    CHECK(sys->slave_bus.read8(0x00004021U) == 0x34U);
    CHECK(sys->comm[0] == 0x1234U);
    // The $20004000 cache-through mirror reaches the same COMM bank.
    CHECK(sys->master_bus.read8(0x20004020U) == 0x12U);
}

TEST_CASE("sega32x_system charges TAS bus-lock waits on shared 32X ranges") {
    auto sys = assemble_sega32x();

    auto r = sys->master_cpu.cpu_registers();
    r.r[1] = 0x06000010U;
    r.pc = 0x06000100U;
    sys->master_cpu.set_registers(r);
    sys->master_bus.write8(0x06000010U, 0x00U);
    sys->master_bus.write8(0x06000100U, 0x41U); // TAS.B @R1
    sys->master_bus.write8(0x06000101U, 0x1BU);

    CHECK(sys->master_cpu.step_instruction() ==
          4 + mnemos::manifests::sega32x::shared_tas_bus_lock_wait_cycles);
    CHECK(sys->master_bus.read8(0x06000010U) == 0x80U);

    r = sys->master_cpu.cpu_registers();
    r.r[1] = 0xC0000000U; // per-CPU cache-data scratch, not shared external bus
    r.pc = 0x06000102U;
    sys->master_cpu.set_registers(r);
    sys->master_bus.write8(0x06000102U, 0x41U); // TAS.B @R1
    sys->master_bus.write8(0x06000103U, 0x1BU);
    sys->master_bus.write8(0xC0000000U, 0x00U);

    CHECK(sys->master_cpu.step_instruction() == 4);
    CHECK(sys->master_bus.read8(0xC0000000U) == 0x80U);
}

TEST_CASE("sega32x_system arbitrates simultaneous SH-2 TAS bus locks") {
    auto sys = assemble_sega32x();
    sys->set_sh2_reset(false);

    auto master = sys->master_cpu.cpu_registers();
    master.r[1] = 0x06000010U;
    master.pc = 0x06000100U;
    sys->master_cpu.set_registers(master);

    auto slave = sys->slave_cpu.cpu_registers();
    slave.r[1] = 0x06000020U;
    slave.pc = 0x06000200U;
    sys->slave_cpu.set_registers(slave);

    sys->master_bus.write8(0x06000100U, 0x41U); // TAS.B @R1
    sys->master_bus.write8(0x06000101U, 0x1BU);
    sys->slave_bus.write8(0x06000200U, 0x41U); // TAS.B @R1
    sys->slave_bus.write8(0x06000201U, 0x1BU);
    sys->master_bus.write8(0x06000010U, 0x00U);
    sys->slave_bus.write8(0x06000020U, 0x00U);

    sys->run_cycles(1U);

    CHECK(sys->master_cpu.elapsed_cycles() ==
          4U + mnemos::manifests::sega32x::shared_tas_bus_lock_wait_cycles);
    CHECK(sys->slave_cpu.elapsed_cycles() ==
          4U + 2U * mnemos::manifests::sega32x::shared_tas_bus_lock_wait_cycles);
    CHECK(sys->master_bus.read8(0x06000010U) == 0x80U);
    CHECK(sys->slave_bus.read8(0x06000020U) == 0x80U);
}

TEST_CASE("sega32x_system absorbs a master WDT internal reset without desyncing pacing") {
    auto sys = assemble_sega32x();
    sys->set_sh2_reset(false);

    // Both cores loop on a self-branch in shared SDRAM so the batch consumes
    // real cycles deterministically.
    sys->master_bus.write8(0x06000000U, 0xAFU); // BRA self
    sys->master_bus.write8(0x06000001U, 0xFEU);
    sys->master_bus.write8(0x06000002U, 0x00U); // (delay) NOP
    sys->master_bus.write8(0x06000003U, 0x09U);
    auto master = sys->master_cpu.cpu_registers();
    master.pc = 0x06000000U;
    sys->master_cpu.set_registers(master);
    auto slave = sys->slave_cpu.cpu_registers();
    slave.pc = 0x06000000U;
    sys->slave_cpu.set_registers(slave);

    sys->run_cycles(20U);
    const std::uint64_t pos_before = sys->sh2_position();
    CHECK(pos_before > 0U);

    // Arm the master's watchdog to drive an internal power-on reset on overflow.
    auto& p = sys->master_cpu.peripherals();
    p.write8(0xFFFFFE82U, 0x5AU);
    p.write8(0xFFFFFE83U, 0x40U); // RSTCSR: RSTE=1, RSTS=0 -> power-on reset
    p.write8(0xFFFFFE80U, 0x5AU);
    p.write8(0xFFFFFE81U, 0xFFU); // WTCNT = 0xFF (one tick from overflow)
    p.write8(0xFFFFFE80U, 0xA5U);
    p.write8(0xFFFFFE81U, 0x60U); // WTCSR: watchdog mode, TME, prescale 2

    // The watchdog overflows mid-batch and self-resets the master. Pre-fix this
    // zeroed master_cpu.elapsed_cycles() without rebasing sh2_elapsed_base, so
    // sh2_position() jumped backwards and run_cycles()'s PWM delta underflowed.
    sys->run_cycles(64U);

    CHECK(sys->sh2_position() >= pos_before);                       // pacing stays monotone
    CHECK((p.read8(0xFFFFFE82U) & 0xC0U) == 0xC0U);                 // WOVF | RSTE preserved
    CHECK(sys->master_cpu.cpu_registers().pc == 0x00000000U);      // re-fetched the reset vector
}

TEST_CASE("sega32x_system drives each CPU's IRQ mask from its own register window") {
    auto sys = assemble_sega32x();
    sys->set_sh2_reset(false);
    // The interrupt-enable mask is the low (odd) byte of the $4000 system word,
    // self-referential, hardware bit order: V=8 / H=4 / CMD=2 / PWM=1.
    sys->master_bus.write8(0x00004001U, 0x08U); // VINT enable
    CHECK(sys->master_irq_mask == sega32x_system::irq_vint);
    CHECK(sys->slave_irq_mask == 0U); // unaffected
    CHECK(sys->master_bus.read8(0x00004001U) == 0x08U);
    // The $40000004 block reaches the same mask (slave enables PWM there).
    sys->slave_bus.write8(0x40000005U, 0x01U);
    CHECK(sys->slave_irq_mask == sega32x_system::irq_pwm);
    CHECK(sys->slave_bus.read8(0x40000005U) == 0x01U);
    // With VINT now enabled, a raised VINT is delivered to the master.
    sys->raise_vint();
    CHECK(sys->master_cpu.pending_irq_level() == 12);
    CHECK(sys->slave_cpu.pending_irq_level() == 0); // slave's VINT still masked
}

TEST_CASE("sega32x_system adapter control round-trips through the register window") {
    auto sys = assemble_sega32x();
    // Even byte = the adapter flag byte (the boot ROMs check ADEN bit 1 here);
    // the odd byte is the interrupt-enable mask, not adapter state.
    sys->master_bus.write8(0x00004000U, 0x82U); // flag byte (FM-ish bits latch)
    CHECK((sys->adapter_ctrl & 0x00FFU) == 0x82U);
    // ADEN (bit 1) and the CART pin (bit 0, 1 = no cartridge) are read-only
    // inputs on the SH-2 view -- written bits do not echo; they reflect the
    // adapter's real state. The bare system has no cart attached.
    CHECK(sys->master_bus.read8(0x00004000U) == 0x81U);
    sys->adapter_enabled = true;
    CHECK(sys->master_bus.read8(0x00004000U) == 0x83U);
    CHECK(sys->slave_bus.read8(0x00004000U) == 0x83U); // same flags on both CPUs
    // The H-count register at +$02 is plain storage.
    sys->master_bus.write8(0x00004002U, 0x00U);
    sys->master_bus.write8(0x00004003U, 0x42U);
    CHECK(sys->hcount == 0x0042U);
    CHECK(sys->slave_bus.read8(0x00004003U) == 0x42U);
}

TEST_CASE("sega32x_system holds the SH-2s in reset until released") {
    auto sys = assemble_sega32x();
    write_be32(sys->m_bios, 0, 0x06000000U); // boot at SDRAM $06000000
    write_be32(sys->m_bios, 4, 0x0603FFFCU);
    write_be32(sys->s_bios, 0, 0x06000000U);
    write_be32(sys->s_bios, 4, 0x0603FFFCU);
    sys->master_bus.write8(0x06000000U, 0xE1U); // MOV #5,R1 (0xE105) in shared SDRAM
    sys->master_bus.write8(0x06000001U, 0x05U);
    sys->reset();
    sys->run_cycles(100); // held in reset: nothing executes
    CHECK(sys->master_cpu.cpu_registers().r[1] == 0U);
    sys->set_sh2_reset(false);
    sys->run_cycles(1); // released: the master runs one boot instruction
    CHECK(sys->master_cpu.cpu_registers().r[1] == 5U);
}

TEST_CASE("sega32x_system latches a masked IRQ and redelivers it on a mask write") {
    auto sys = assemble_sega32x();
    sys->set_sh2_reset(false);
    sys->set_master_irq_mask(0U); // VINT disabled
    sys->raise_vint();
    CHECK(sys->master_cpu.pending_irq_level() == 0); // latched, not delivered
    CHECK((sys->master_irq_latch & sega32x_system::irq_vint) != 0U);
    sys->set_master_irq_mask(sega32x_system::irq_vint); // enable -> redeliver the latched edge
    CHECK(sys->master_cpu.pending_irq_level() == 12);
    CHECK(sys->master_cpu.pending_irq_vector() == 0x46U);
}

TEST_CASE("sega32x_system presents the highest-priority latched IRQ first") {
    auto sys = assemble_sega32x();
    sys->set_sh2_reset(false);
    sys->set_master_irq_mask(0U); // all masked -> both edges latch
    sys->raise_pwm();             // level 6
    sys->raise_vint();            // level 12
    CHECK(sys->master_cpu.pending_irq_level() == 0);
    sys->set_master_irq_mask(0x0FU);                                // enable all
    CHECK(sys->master_cpu.pending_irq_level() == 12);               // VINT (highest) wins
    CHECK((sys->master_irq_latch & sega32x_system::irq_pwm) != 0U); // PWM still latched (lower)
}

TEST_CASE("sega32x_system masks IRQs per CPU independently") {
    auto sys = assemble_sega32x();
    sys->set_sh2_reset(false);
    sys->set_master_irq_mask(0U);                     // master: CMD disabled
    sys->set_slave_irq_mask(sega32x_system::irq_cmd); // slave: CMD enabled
    sys->raise_cmd();
    CHECK(sys->master_cpu.pending_irq_level() == 0); // master masked -> held
    CHECK(sys->slave_cpu.pending_irq_level() == 8);  // slave received CMD
    CHECK((sys->master_irq_latch & sega32x_system::irq_cmd) != 0U);
}

TEST_CASE("sega32x_system redelivers a latched IRQ after the CPU accepts a higher one") {
    auto sys = assemble_sega32x();
    write_be32(sys->m_bios, 0, 0x06000000U);          // reset PC
    write_be32(sys->m_bios, 4, 0x0603FFFCU);          // SP
    write_be32(sys->m_bios, 0x46U * 4U, 0x06000100U); // VINT auto-vector (0x46) -> handler
    write_be32(sys->s_bios, 0, 0x06000000U);
    write_be32(sys->s_bios, 4, 0x0603FFFCU);
    sys->master_bus.write8(0x06000000U, 0x00U); // NOP at the boot address
    sys->master_bus.write8(0x06000001U, 0x09U);
    sys->master_bus.write8(0x06000100U, 0x00U); // NOP at the VINT handler
    sys->master_bus.write8(0x06000101U, 0x09U);
    sys->reset();
    sys->set_sh2_reset(false);
    // Lower the master's SR.IMASK (15 out of reset) so it accepts the IRQ.
    auto mr = sys->master_cpu.cpu_registers();
    mr.sr = 0U;
    sys->master_cpu.set_registers(mr);
    sys->set_master_irq_mask(0x0FU);
    sys->raise_vint(); // delivered (master pending = 12)
    sys->raise_hint(); // latched (level 10 < pending 12)
    REQUIRE(sys->master_cpu.pending_irq_level() == 12);
    sys->run_cycles(1); // master accepts VINT -> accept callback redelivers HINT
    CHECK(sys->master_cpu.pending_irq_level() == 10);
    CHECK(sys->master_cpu.pending_irq_vector() == 0x45U);
}

TEST_CASE("sega32x_system PWM FIFOs report status and reject pushes when full") {
    auto sys = assemble_sega32x();

    // LCH status (even byte = status high half): EMPTY (bit 14) out of reset.
    CHECK(sys->master_bus.read8(0x00004034U) == 0x40U);

    // Three byte-pair pushes fill the FIFO (bit 15 = FULL); a fourth drops.
    for (int i = 0; i < 3; ++i) {
        sys->master_bus.write8(0x00004034U, 0x00U); // duty high latch
        sys->master_bus.write8(0x00004035U, static_cast<std::uint8_t>(0x0AU + i));
    }
    CHECK(sys->pwm_fifo_l_count == 3U);
    CHECK(sys->master_bus.read8(0x00004034U) == 0x80U);
    sys->master_bus.write8(0x00004034U, 0x00U);
    sys->master_bus.write8(0x00004035U, 0x7FU); // full: rejected
    CHECK(sys->pwm_fifo_l_count == 3U);
    CHECK(sys->pwm_fifo_l[0] == 0x0AU);

    // MONO pushes both channels; the MONO status is the OR of the two.
    sys->master_bus.write8(0x00004038U, 0x00U);
    sys->master_bus.write8(0x00004039U, 0x21U);
    CHECK(sys->pwm_fifo_r_count == 1U);
    CHECK(sys->master_bus.read8(0x00004038U) == 0x80U); // L still full
}

TEST_CASE("sega32x_system PWM steps at the CYCLE rate, holds on empty, raises TM IRQ") {
    auto sys = assemble_sega32x();
    sys->set_sh2_reset(false);

    // CNTL TM = 1 (interrupt every step), CYCLE = 16 SH-2 cycles per step.
    sys->master_bus.write8(0x00004030U, 0x01U);
    sys->master_bus.write8(0x00004031U, 0x00U);
    sys->master_bus.write8(0x00004032U, 0x00U);
    sys->master_bus.write8(0x00004033U, 0x10U);

    // Push duty 12 (above CYCLE/2 = 8) on the left channel only.
    sys->master_bus.write8(0x00004034U, 0x00U);
    sys->master_bus.write8(0x00004035U, 0x0CU);
    sys->set_master_irq_mask(sega32x_system::irq_pwm);

    sys->run_cycles(64U); // >= 4 steps: FIFO drains, then the DAC holds

    // duty 12, half 8: PCM = (12-8)*32767/8 = 16383, held after the drain.
    CHECK(sys->pwm_output_l() == 16383);
    // The right FIFO never had data: its DAC stays at the reset duty 0,
    // which converts to the negative rail ((0-8)*32767/8).
    CHECK(sys->pwm_output_r() == -32767);
    // The TM counter latched + delivered the PWM interrupt (level 6, vec 0x43).
    CHECK(sys->master_cpu.pending_irq_level() == 6);
    CHECK(sys->master_cpu.pending_irq_vector() == 0x43U);
    CHECK((sys->slave_irq_latch & sega32x_system::irq_pwm) != 0U); // slave masked: latched
}
