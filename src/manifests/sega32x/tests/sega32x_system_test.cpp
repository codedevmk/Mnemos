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
    // COMM word 0 (big-endian) written via the master, read back via the slave.
    sys->master_bus.write8(0x40000020U, 0x12U);
    sys->master_bus.write8(0x40000021U, 0x34U);
    CHECK(sys->slave_bus.read8(0x40000020U) == 0x12U);
    CHECK(sys->slave_bus.read8(0x40000021U) == 0x34U);
    CHECK(sys->comm[0] == 0x1234U);
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
    CHECK(sys->master_cpu.pending_irq_vector() == 0x44U);
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
    write_be32(sys->m_bios, 0x44U * 4U, 0x06000100U); // VINT vector (0x44) -> handler
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
    CHECK(sys->master_cpu.pending_irq_vector() == 0x46U);
}
