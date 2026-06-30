#include "sh3.hpp"

#include "bus.hpp"
#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::cpu::sh3;

    struct machine final {
        std::array<std::uint8_t, 0x10000> ram{};
        mnemos::topology::bus bus{32U, mnemos::topology::endianness::big};
        sh3 cpu;

        machine() {
            bus.map_ram(0x0000U, std::span<std::uint8_t>(ram), 0);
            cpu.attach_bus(bus);
        }

        void w16(std::uint32_t address, std::uint16_t value) {
            ram[address] = static_cast<std::uint8_t>(value >> 8U);
            ram[address + 1U] = static_cast<std::uint8_t>(value);
        }

        void w32(std::uint32_t address, std::uint32_t value) {
            w16(address, static_cast<std::uint16_t>(value >> 16U));
            w16(address + 2U, static_cast<std::uint16_t>(value));
        }

        void set_pc(std::uint32_t pc) {
            auto regs = cpu.cpu_registers();
            regs.core.pc = pc;
            cpu.set_registers(regs);
        }
    };
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::icpu, sh3>);

TEST_CASE("sh3 reports SH7708 identity and factory ids") {
    const sh3 cpu;
    const auto md = cpu.metadata();
    CHECK(md.manufacturer == "Hitachi");
    CHECK(md.part_number == "HD6417708S");
    CHECK(md.family == "SH-3");
    CHECK(md.klass == mnemos::chips::chip_class::cpu);

    auto chip = mnemos::chips::create_chip("hitachi.hd6417708s");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == std::string("HD6417708S"));

    auto generic = mnemos::chips::create_chip("hitachi.sh3");
    REQUIRE(generic != nullptr);
    CHECK(generic->metadata().family == std::string("SH-3"));
}

TEST_CASE("sh3 reset and base instruction execution use the big-endian SH bus") {
    machine m;
    m.w32(0x0000U, 0x00001000U);
    m.w32(0x0004U, 0x0000FFF0U);
    m.w16(0x1000U, 0xE37FU); // MOV #$7f,R3
    m.cpu.reset(mnemos::chips::reset_kind::power_on);

    CHECK(m.cpu.cpu_registers().core.pc == 0x1000U);
    CHECK(m.cpu.cpu_registers().core.r[15] == 0x0000FFF0U);
    CHECK(m.cpu.step_instruction() == 1);
    CHECK(m.cpu.cpu_registers().core.r[3] == 0x7FU);
}

TEST_CASE("sh3 local control registers round-trip through accessors and state") {
    sh3 cpu;
    cpu.write_onchip_register(0xFFFFFF84U, 0x00000001U);
    cpu.write_onchip_register(0xFFFFFFECU, 0x0000000FU);
    cpu.write_onchip_register(0xFFFFFF60U, 0x1234U);
    CHECK(cpu.read_onchip_register(0xFFFFFF84U) == 0x00000001U);
    CHECK(cpu.read_onchip_register(0xFFFFFFECU) == 0x0000000FU);
    CHECK(cpu.read_onchip_register(0xFFFFFF60U) == 0x1234U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    cpu.save_state(writer);

    sh3 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.read_onchip_register(0xFFFFFF84U) == 0x00000001U);
    CHECK(restored.read_onchip_register(0xFFFFFFECU) == 0x0000000FU);
    CHECK(restored.read_onchip_register(0xFFFFFF60U) == 0x1234U);
}

TEST_CASE("sh3 SH7708 local registers are reachable through executed SH bus ops") {
    machine m;
    m.w16(0x0100U, 0x2121U); // MOV.W R2,@R1
    m.w16(0x0102U, 0x2342U); // MOV.L R4,@R3
    m.w16(0x0104U, 0x6611U); // MOV.W @R1,R6
    m.w16(0x0106U, 0x6732U); // MOV.L @R3,R7
    m.w16(0x0108U, 0x0009U); // NOP

    auto regs = m.cpu.cpu_registers();
    regs.core.pc = 0x0100U;
    regs.core.r[1] = 0xFFFFFF60U;
    regs.core.r[2] = 0x00001234U;
    regs.core.r[3] = 0xFFFFFF84U;
    regs.core.r[4] = 0xA5A55A5AU;
    m.cpu.set_registers(regs);

    CHECK(m.cpu.step_instruction() > 0);
    CHECK(m.cpu.step_instruction() > 0);
    CHECK(m.cpu.step_instruction() > 0);
    CHECK(m.cpu.step_instruction() > 0);

    const auto after = m.cpu.cpu_registers();
    CHECK(m.cpu.read_onchip_register(0xFFFFFF60U) == 0x1234U);
    CHECK(m.cpu.read_onchip_register(0xFFFFFF84U) == 0xA5A55A5AU);
    CHECK(after.core.r[6] == 0x1234U);
    CHECK(after.core.r[7] == 0xA5A55A5AU);
}
