#include "v30.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::ibus;
    using mnemos::chips::cpu::v30;

    // Flat 1 MiB physical memory covering the V30's full 20-bit space.
    // Heap-backed: a megabyte std::array would overflow the default thread
    // stack the moment a test creates the bus as a local.
    class flat_bus final : public ibus {
      public:
        std::vector<std::uint8_t> memory = std::vector<std::uint8_t>(0x100000U, 0U);

        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            return memory[address & 0xFFFFFU];
        }

        void write8(std::uint32_t address, std::uint8_t value) override {
            memory[address & 0xFFFFFU] = value;
        }
    };

    constexpr std::uint32_t linear(std::uint16_t segment, std::uint16_t offset) {
        return ((static_cast<std::uint32_t>(segment) << 4U) + offset) & 0xFFFFFU;
    }

    // Writes `code` at segment:offset and points the CPU there.
    void load_program(flat_bus& bus, v30& cpu, std::uint16_t segment, std::uint16_t offset,
                      const std::vector<std::uint8_t>& code) {
        for (std::size_t i = 0; i < code.size(); ++i) {
            bus.memory[(linear(segment, offset) + i) & 0xFFFFFU] = code[i];
        }
        auto regs = cpu.cpu_registers();
        regs.cs = segment;
        regs.ip = offset;
        cpu.set_registers(regs);
    }

} // namespace

TEST_CASE("v30 registers through the chip registry and reports cpu metadata", "[v30]") {
    auto chip = mnemos::chips::create_chip("nec.v30");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().manufacturer == "NEC");
    CHECK(chip->metadata().part_number == "v30");
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::cpu);
}

TEST_CASE("v30 powers on at FFFF:0000 and takes a far jump", "[v30]") {
    flat_bus bus;
    v30 cpu;
    cpu.attach_bus(bus);

    const auto regs = cpu.cpu_registers();
    CHECK(regs.cs == 0xFFFFU);
    CHECK(regs.ip == 0x0000U);

    // JMP 1000:8000 at the reset vector.
    bus.memory[0xFFFF0U] = 0xEAU;
    bus.memory[0xFFFF1U] = 0x00U;
    bus.memory[0xFFFF2U] = 0x80U;
    bus.memory[0xFFFF3U] = 0x00U;
    bus.memory[0xFFFF4U] = 0x10U;
    cpu.step_instruction();

    const auto after = cpu.cpu_registers();
    CHECK(after.cs == 0x1000U);
    CHECK(after.ip == 0x8000U);
}

TEST_CASE("v30 arithmetic sets the documented flags", "[v30]") {
    flat_bus bus;
    v30 cpu;
    cpu.attach_bus(bus);

    SECTION("signed overflow on ADD") {
        // MOV AL, 7F; ADD AL, 01
        load_program(bus, cpu, 0x0100U, 0x0000U, {0xB0U, 0x7FU, 0x04U, 0x01U});
        cpu.step_instruction();
        cpu.step_instruction();
        const auto regs = cpu.cpu_registers();
        CHECK((regs.ax & 0xFFU) == 0x80U);
        CHECK((regs.flags & v30::flag_o) != 0U); // overflow
        CHECK((regs.flags & v30::flag_s) != 0U); // negative
        CHECK((regs.flags & v30::flag_a) != 0U); // half carry out of bit 3
        CHECK((regs.flags & v30::flag_c) == 0U);
        CHECK((regs.flags & v30::flag_z) == 0U);
    }

    SECTION("borrow on SUB to zero then below") {
        // MOV AX, 0001; SUB AX, 0001; SUB AX, 0001
        load_program(bus, cpu, 0x0100U, 0x0000U,
                     {0xB8U, 0x01U, 0x00U, 0x2DU, 0x01U, 0x00U, 0x2DU, 0x01U, 0x00U});
        cpu.step_instruction();
        cpu.step_instruction();
        auto regs = cpu.cpu_registers();
        CHECK(regs.ax == 0x0000U);
        CHECK((regs.flags & v30::flag_z) != 0U);
        CHECK((regs.flags & v30::flag_c) == 0U);
        cpu.step_instruction();
        regs = cpu.cpu_registers();
        CHECK(regs.ax == 0xFFFFU);
        CHECK((regs.flags & v30::flag_c) != 0U); // borrow
        CHECK((regs.flags & v30::flag_s) != 0U);
    }

    SECTION("logic clears carry and overflow") {
        // STC; MOV AL, F0; AND AL, 0F
        load_program(bus, cpu, 0x0100U, 0x0000U, {0xF9U, 0xB0U, 0xF0U, 0x24U, 0x0FU});
        cpu.step_instruction();
        cpu.step_instruction();
        cpu.step_instruction();
        const auto regs = cpu.cpu_registers();
        CHECK((regs.ax & 0xFFU) == 0x00U);
        CHECK((regs.flags & v30::flag_z) != 0U);
        CHECK((regs.flags & v30::flag_c) == 0U);
        CHECK((regs.flags & v30::flag_o) == 0U);
    }
}

TEST_CASE("v30 resolves modrm effective addresses with segment overrides", "[v30]") {
    flat_bus bus;
    v30 cpu;
    cpu.attach_bus(bus);

    // MOV AL, 5A; ES: MOV [BX+SI+10], AL
    load_program(bus, cpu, 0x0100U, 0x0000U, {0xB0U, 0x5AU, 0x26U, 0x88U, 0x40U, 0x10U});
    auto regs = cpu.cpu_registers();
    regs.bx = 0x0200U;
    regs.si = 0x0030U;
    regs.es = 0x2000U;
    regs.ds = 0x3000U;
    cpu.set_registers(regs);

    cpu.step_instruction();
    cpu.step_instruction();
    CHECK(bus.memory[linear(0x2000U, 0x0240U)] == 0x5AU);
    CHECK(bus.memory[linear(0x3000U, 0x0240U)] == 0x00U); // DS untouched
}

TEST_CASE("v30 call and return round-trip through the stack", "[v30]") {
    flat_bus bus;
    v30 cpu;
    cpu.attach_bus(bus);

    // CALL +3 (to the RET); then INC AX as the return target; RET at offset 6.
    // 0000: E8 03 00   CALL 0006
    // 0003: 40         INC AX
    // 0004: EB FE      (unreached)
    // 0006: C3         RET
    load_program(bus, cpu, 0x0100U, 0x0000U, {0xE8U, 0x03U, 0x00U, 0x40U, 0xEBU, 0xFEU, 0xC3U});
    auto regs = cpu.cpu_registers();
    regs.ss = 0x0900U;
    regs.sp = 0x0100U;
    cpu.set_registers(regs);

    cpu.step_instruction(); // CALL
    auto mid = cpu.cpu_registers();
    CHECK(mid.ip == 0x0006U);
    CHECK(mid.sp == 0x00FEU);
    cpu.step_instruction(); // RET
    mid = cpu.cpu_registers();
    CHECK(mid.ip == 0x0003U);
    CHECK(mid.sp == 0x0100U);
    cpu.step_instruction(); // INC AX
    CHECK(cpu.cpu_registers().ax == 0x0001U);
}

TEST_CASE("v30 REP MOVSB copies a block and consumes CX", "[v30]") {
    flat_bus bus;
    v30 cpu;
    cpu.attach_bus(bus);

    load_program(bus, cpu, 0x0100U, 0x0000U, {0xFCU, 0xF3U, 0xA4U}); // CLD; REP MOVSB
    auto regs = cpu.cpu_registers();
    regs.ds = 0x2000U;
    regs.si = 0x0000U;
    regs.es = 0x3000U;
    regs.di = 0x0010U;
    regs.cx = 5U;
    cpu.set_registers(regs);
    for (std::uint32_t i = 0; i < 5U; ++i) {
        bus.memory[linear(0x2000U, 0x0000U) + i] = static_cast<std::uint8_t>(0xA0U + i);
    }

    cpu.step_instruction(); // CLD
    cpu.step_instruction(); // REP MOVSB
    const auto after = cpu.cpu_registers();
    CHECK(after.cx == 0U);
    CHECK(after.si == 0x0005U);
    CHECK(after.di == 0x0015U);
    for (std::uint32_t i = 0; i < 5U; ++i) {
        CHECK(bus.memory[linear(0x3000U, 0x0010U) + i] == 0xA0U + i);
    }
}

TEST_CASE("v30 multiplies and divides through group 3", "[v30]") {
    flat_bus bus;
    v30 cpu;
    cpu.attach_bus(bus);

    SECTION("MUL widens into DX:AX") {
        // MOV AX, 1234; MOV CX, 0100; MUL CX
        load_program(bus, cpu, 0x0100U, 0x0000U,
                     {0xB8U, 0x34U, 0x12U, 0xB9U, 0x00U, 0x01U, 0xF7U, 0xE1U});
        cpu.step_instruction();
        cpu.step_instruction();
        cpu.step_instruction();
        const auto regs = cpu.cpu_registers();
        CHECK(regs.ax == 0x3400U);
        CHECK(regs.dx == 0x0012U);
        CHECK((regs.flags & v30::flag_c) != 0U); // high half significant
    }

    SECTION("divide by zero vectors through INT 0") {
        // IVT[0] -> 0050:0060
        bus.memory[0x0000U] = 0x60U;
        bus.memory[0x0001U] = 0x00U;
        bus.memory[0x0002U] = 0x50U;
        bus.memory[0x0003U] = 0x00U;
        // MOV AX, 0005; MOV CL, 00; DIV CL
        load_program(bus, cpu, 0x0100U, 0x0000U, {0xB8U, 0x05U, 0x00U, 0xB1U, 0x00U, 0xF6U, 0xF1U});
        auto regs = cpu.cpu_registers();
        regs.ss = 0x0900U;
        regs.sp = 0x0100U;
        cpu.set_registers(regs);
        cpu.step_instruction();
        cpu.step_instruction();
        cpu.step_instruction();
        const auto after = cpu.cpu_registers();
        CHECK(after.cs == 0x0050U);
        CHECK(after.ip == 0x0060U);
        CHECK((after.flags & v30::flag_i) == 0U); // IF cleared on entry
    }
}

TEST_CASE("v30 IN and OUT route through the port callbacks", "[v30]") {
    flat_bus bus;
    v30 cpu;
    cpu.attach_bus(bus);

    std::uint16_t out_port = 0U;
    std::uint8_t out_value = 0U;
    cpu.set_port_in(
        [](std::uint16_t port) -> std::uint8_t { return port == 0x42U ? 0x99U : 0x00U; });
    cpu.set_port_out([&](std::uint16_t port, std::uint8_t value) {
        out_port = port;
        out_value = value;
    });

    // IN AL, 42; OUT 7F, AL
    load_program(bus, cpu, 0x0100U, 0x0000U, {0xE4U, 0x42U, 0xE6U, 0x7FU});
    cpu.step_instruction();
    CHECK((cpu.cpu_registers().ax & 0xFFU) == 0x99U);
    cpu.step_instruction();
    CHECK(out_port == 0x7FU);
    CHECK(out_value == 0x99U);
}

TEST_CASE("v30 halts on HLT and wakes for an acknowledged interrupt", "[v30]") {
    flat_bus bus;
    v30 cpu;
    cpu.attach_bus(bus);

    // IVT[0x20] -> 0040:0008
    bus.memory[0x0080U] = 0x08U;
    bus.memory[0x0081U] = 0x00U;
    bus.memory[0x0082U] = 0x40U;
    bus.memory[0x0083U] = 0x00U;

    // STI; HLT
    load_program(bus, cpu, 0x0100U, 0x0000U, {0xFBU, 0xF4U});
    auto regs = cpu.cpu_registers();
    regs.ss = 0x0900U;
    regs.sp = 0x0100U;
    cpu.set_registers(regs);
    cpu.set_irq_ack([]() -> std::uint8_t { return 0x20U; });

    cpu.step_instruction(); // STI (shadows IRQ for one instruction)
    cpu.step_instruction(); // HLT
    CHECK(cpu.halted());
    cpu.step_instruction(); // halted idle cycle, no interrupt pending
    CHECK(cpu.halted());

    cpu.set_irq_line(true);
    cpu.step_instruction(); // interrupt accepted, halt cleared
    CHECK_FALSE(cpu.halted());
    const auto after = cpu.cpu_registers();
    CHECK(after.cs == 0x0040U);
    CHECK(after.ip == 0x0008U);
    CHECK((after.flags & v30::flag_i) == 0U);
}

TEST_CASE("v30 NMI is edge latched and vectors through INT 2", "[v30]") {
    flat_bus bus;
    v30 cpu;
    cpu.attach_bus(bus);

    // IVT[2] -> 0070:0004
    bus.memory[0x0008U] = 0x04U;
    bus.memory[0x0009U] = 0x00U;
    bus.memory[0x000AU] = 0x70U;
    bus.memory[0x000BU] = 0x00U;

    load_program(bus, cpu, 0x0100U, 0x0000U, {0x90U, 0x90U}); // NOP; NOP
    auto regs = cpu.cpu_registers();
    regs.ss = 0x0900U;
    regs.sp = 0x0100U;
    cpu.set_registers(regs);

    cpu.set_nmi_line(true);
    cpu.set_nmi_line(false); // latched on the edge; deassertion does not cancel
    cpu.step_instruction();
    const auto after = cpu.cpu_registers();
    CHECK(after.cs == 0x0070U);
    CHECK(after.ip == 0x0004U);
}

TEST_CASE("v30 save and load state round-trips mid-program", "[v30]") {
    flat_bus bus;
    v30 cpu;
    cpu.attach_bus(bus);

    // MOV AX, 1111; ADD AX, 2222; ADD AX, 3333
    load_program(bus, cpu, 0x0100U, 0x0000U,
                 {0xB8U, 0x11U, 0x11U, 0x05U, 0x22U, 0x22U, 0x05U, 0x33U, 0x33U});
    cpu.step_instruction();
    cpu.step_instruction();

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    cpu.save_state(writer);

    cpu.step_instruction();
    CHECK(cpu.cpu_registers().ax == 0x6666U);

    mnemos::chips::state_reader reader(snapshot);
    cpu.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(cpu.cpu_registers().ax == 0x3333U);

    cpu.step_instruction(); // replay the third ADD deterministically
    CHECK(cpu.cpu_registers().ax == 0x6666U);
}

TEST_CASE("v30 exposes a trace target and register view through introspection", "[v30]") {
    flat_bus bus;
    v30 cpu;
    cpu.attach_bus(bus);

    load_program(bus, cpu, 0x0100U, 0x0000U, {0x90U, 0x90U}); // NOP; NOP

    auto* trace = cpu.introspection().trace();
    REQUIRE(trace != nullptr);
    std::vector<std::uint32_t> pcs;
    trace->install(
        [&](const mnemos::instrumentation::trace_event& event) { pcs.push_back(event.pc); });
    cpu.step_instruction();
    cpu.step_instruction();
    trace->install({});
    cpu.step_instruction();

    REQUIRE(pcs.size() == 2U);
    CHECK(pcs[0] == linear(0x0100U, 0x0000U));
    CHECK(pcs[1] == linear(0x0100U, 0x0001U));

    auto* registers = cpu.introspection().registers();
    REQUIRE(registers != nullptr);
    const auto snapshot = registers->registers();
    REQUIRE(snapshot.size() == 14U);
    CHECK(snapshot[0].name == "AX");
    CHECK(snapshot[13].name == "FLAGS");
}
