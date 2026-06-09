#include "sega32x_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace {
    using mnemos::manifests::sega32x::assemble_sega32x;

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
