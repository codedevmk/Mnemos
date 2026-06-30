#include "r3000a.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace {
    using mnemos::chips::cpu::r3000a;

    class sparse_bus final : public mnemos::chips::ibus {
      public:
        std::unordered_map<std::uint32_t, std::uint8_t> memory;

        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            const auto it = memory.find(address);
            return it != memory.end() ? it->second : 0U;
        }
        void write8(std::uint32_t address, std::uint8_t value) override {
            memory[address] = value;
        }
    };

    [[nodiscard]] constexpr std::uint32_t r(std::uint8_t rs, std::uint8_t rt, std::uint8_t rd,
                                            std::uint8_t sh, std::uint8_t fn) {
        return (static_cast<std::uint32_t>(rs) << 21U) |
               (static_cast<std::uint32_t>(rt) << 16U) |
               (static_cast<std::uint32_t>(rd) << 11U) |
               (static_cast<std::uint32_t>(sh) << 6U) | fn;
    }

    [[nodiscard]] constexpr std::uint32_t i(std::uint8_t op, std::uint8_t rs, std::uint8_t rt,
                                            std::uint16_t imm) {
        return (static_cast<std::uint32_t>(op) << 26U) |
               (static_cast<std::uint32_t>(rs) << 21U) |
               (static_cast<std::uint32_t>(rt) << 16U) | imm;
    }

    [[nodiscard]] constexpr std::uint32_t j(std::uint8_t op, std::uint32_t target) {
        return (static_cast<std::uint32_t>(op) << 26U) | ((target >> 2U) & 0x03FFFFFFU);
    }

    [[nodiscard]] constexpr std::uint32_t cop(std::uint8_t opcode, std::uint8_t cop_rs,
                                              std::uint8_t rt, std::uint8_t rd,
                                              std::uint32_t function = 0U) {
        return (static_cast<std::uint32_t>(opcode) << 26U) |
               (static_cast<std::uint32_t>(cop_rs) << 21U) |
               (static_cast<std::uint32_t>(rt) << 16U) |
               (static_cast<std::uint32_t>(rd) << 11U) | (function & 0x7FFU);
    }

    void w32(sparse_bus& bus, std::uint32_t address, std::uint32_t value) {
        bus.memory[address + 0U] = static_cast<std::uint8_t>(value);
        bus.memory[address + 1U] = static_cast<std::uint8_t>(value >> 8U);
        bus.memory[address + 2U] = static_cast<std::uint8_t>(value >> 16U);
        bus.memory[address + 3U] = static_cast<std::uint8_t>(value >> 24U);
    }

    void load_program(sparse_bus& bus, r3000a& cpu, std::uint32_t pc,
                      const std::vector<std::uint32_t>& code) {
        for (std::size_t n = 0; n < code.size(); ++n) {
            w32(bus, pc + static_cast<std::uint32_t>(n * 4U), code[n]);
        }
        auto regs = cpu.cpu_registers();
        regs.pc = pc;
        cpu.set_registers(regs);
    }

    void run(r3000a& cpu, int instructions) {
        for (int n = 0; n < instructions; ++n) {
            cpu.step_instruction();
        }
    }

} // namespace

TEST_CASE("r3000a registers through the chip registry and resets to the boot vector",
          "[r3000a]") {
    auto chip = mnemos::chips::create_chip("sony.r3000a");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().manufacturer == "Sony");
    CHECK(chip->metadata().part_number == "R3000A");
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::cpu);

    r3000a cpu;
    const auto regs = cpu.cpu_registers();
    CHECK(regs.pc == r3000a::reset_vector);
    CHECK((regs.cop0[r3000a::cop0_status] & r3000a::status_bev) != 0U);
    CHECK(regs.r[0] == 0U);
}

TEST_CASE("r3000a fetches little-endian instructions and executes integer ALU ops",
          "[r3000a]") {
    sparse_bus bus;
    r3000a cpu;
    cpu.attach_bus(bus);

    load_program(bus, cpu, 0x1000U,
                 {
                     i(0x0FU, 0, 1, 0x1234U),        // LUI AT,1234
                     i(0x0DU, 1, 1, 0x5678U),        // ORI AT,AT,5678
                     i(0x09U, 1, 2, 0x0001U),        // ADDIU V0,AT,1
                     r(2, 1, 3, 0, 0x23U),           // SUBU V1,V0,AT
                     r(1, 2, 4, 0, 0x2BU),           // SLTU A0,AT,V0
                     r(1, 2, 5, 0, 0x18U),           // MULT AT,V0
                     r(0, 0, 6, 0, 0x12U),           // MFLO A2
                 });

    run(cpu, 7);
    const auto regs = cpu.cpu_registers();
    CHECK(regs.r[1] == 0x12345678U);
    CHECK(regs.r[2] == 0x12345679U);
    CHECK(regs.r[3] == 1U);
    CHECK(regs.r[4] == 1U);
    CHECK(regs.r[6] == regs.lo);
    CHECK(cpu.elapsed_cycles() >= 18U); // MULT carries a first-order extra cost.
}

TEST_CASE("r3000a honours the MIPS I load delay and little-endian data stores",
          "[r3000a]") {
    sparse_bus bus;
    r3000a cpu;
    cpu.attach_bus(bus);

    w32(bus, 0x200U, 0x89ABCDEFU);
    load_program(bus, cpu, 0x1000U,
                 {
                     i(0x09U, 0, 1, 0x0200U), // ADDIU AT,R0,0200
                     i(0x23U, 1, 2, 0x0000U), // LW V0,0(AT)
                     r(2, 0, 3, 0, 0x21U),    // ADDU V1,V0,R0 (sees old V0)
                     r(2, 0, 4, 0, 0x21U),    // ADDU A0,V0,R0 (sees loaded V0)
                     i(0x2BU, 1, 4, 0x0004U), // SW A0,4(AT)
                 });

    run(cpu, 5);
    const auto regs = cpu.cpu_registers();
    CHECK(regs.r[2] == 0x89ABCDEFU);
    CHECK(regs.r[3] == 0U);
    CHECK(regs.r[4] == 0x89ABCDEFU);
    CHECK(bus.memory[0x204U] == 0xEFU);
    CHECK(bus.memory[0x205U] == 0xCDU);
    CHECK(bus.memory[0x206U] == 0xABU);
    CHECK(bus.memory[0x207U] == 0x89U);
}

TEST_CASE("r3000a executes branch and jump delay slots", "[r3000a]") {
    sparse_bus bus;
    r3000a cpu;
    cpu.attach_bus(bus);

    load_program(bus, cpu, 0x1000U,
                 {
                     i(0x09U, 0, 1, 0x0001U), // ADDIU AT,0,1
                     i(0x04U, 1, 1, 0x0002U), // BEQ AT,AT,+2 -> 1010
                     i(0x09U, 0, 2, 0x0002U), // delay slot executes
                     i(0x09U, 0, 3, 0x0003U), // skipped
                     j(0x03U, 0x1020U),       // JAL 1020
                     i(0x09U, 0, 4, 0x0004U), // JAL delay slot executes
                     i(0x09U, 0, 5, 0x0005U), // skipped
                     i(0x09U, 0, 6, 0x0006U), // skipped
                     i(0x09U, 0, 7, 0x0007U), // target
                 });

    run(cpu, 6);
    const auto regs = cpu.cpu_registers();
    CHECK(regs.r[2] == 2U);
    CHECK(regs.r[3] == 0U);
    CHECK(regs.r[4] == 4U);
    CHECK(regs.r[5] == 0U);
    CHECK(regs.r[7] == 7U);
    CHECK(regs.r[31] == 0x1018U);
}

TEST_CASE("r3000a exposes CP0 moves and exceptions with EPC and Cause", "[r3000a]") {
    sparse_bus bus;
    r3000a cpu;
    cpu.attach_bus(bus);

    load_program(bus, cpu, 0x1000U,
                 {
                     i(0x0FU, 0, 1, 0x0040U),        // LUI AT,0040 (BEV)
                     (0x10U << 26U) | (4U << 21U) |  // MTC0 AT,Status
                         (1U << 16U) | (r3000a::cop0_status << 11U),
                     (0x10U << 26U) |                // MFC0 V0,Status
                         (2U << 16U) | (r3000a::cop0_status << 11U),
                     r(2, 0, 3, 0, 0x21U), // V1 sees old V0 due to load delay
                     r(2, 0, 4, 0, 0x21U), // A0 sees loaded status
                     0x0000000DU,          // BREAK
                 });

    run(cpu, 6);
    const auto regs = cpu.cpu_registers();
    CHECK(regs.r[3] == 0U);
    CHECK(regs.r[4] == r3000a::status_bev);
    CHECK(regs.pc == r3000a::boot_exception_vector);
    CHECK(regs.cop0[r3000a::cop0_epc] == 0x1014U);
    CHECK(((regs.cop0[r3000a::cop0_cause] & r3000a::cause_exception_code_mask) >> 2U) ==
          static_cast<std::uint32_t>(r3000a::exception_code::breakpoint));
}

TEST_CASE("r3000a latches COP2/GTE register moves and command words", "[r3000a]") {
    sparse_bus bus;
    r3000a cpu;
    cpu.attach_bus(bus);

    constexpr std::uint32_t gte_command = 0x4A180001U;
    load_program(bus, cpu, 0x1000U,
                 {
                     i(0x0FU, 0, 1, 0x1234U), // LUI AT,1234
                     i(0x0DU, 1, 1, 0x5678U), // ORI AT,AT,5678
                     cop(0x12U, 0x04U, 1, 4), // MTC2 AT,D4
                     cop(0x12U, 0x00U, 2, 4), // MFC2 V0,D4
                     r(2, 0, 3, 0, 0x21U),    // V1 sees old V0 due to load delay
                     r(2, 0, 4, 0, 0x21U),    // A0 sees loaded V0
                     cop(0x12U, 0x06U, 1, 31), // CTC2 AT,C31/FLAG
                     cop(0x12U, 0x02U, 5, 31), // CFC2 A1,C31/FLAG
                     r(5, 0, 6, 0, 0x21U),     // A2 sees old A1 due to load delay
                     r(5, 0, 7, 0, 0x21U),     // A3 sees loaded A1
                     gte_command,
                 });

    run(cpu, 11);
    const auto regs = cpu.cpu_registers();
    CHECK(regs.cop2_data[4] == 0x12345678U);
    CHECK(regs.cop2_control[31] == 0x12345678U);
    CHECK(regs.cop2_command == (gte_command & 0x01FFFFFFU));
    CHECK(regs.r[2] == 0x12345678U);
    CHECK(regs.r[3] == 0U);
    CHECK(regs.r[4] == 0x12345678U);
    CHECK(regs.r[5] == 0x12345678U);
    CHECK(regs.r[6] == 0U);
    CHECK(regs.r[7] == 0x12345678U);
    CHECK(regs.last_exception != r3000a::exception_code::reserved_instruction);

    const auto view = cpu.register_snapshot();
    REQUIRE(view.size() >= 42U);
    CHECK(view[39].name == "CP2_D0");
    CHECK(view[40].name == "CP2_C31");
    CHECK(view[41].name == "CP2_COMMAND");
}

TEST_CASE("r3000a reports misaligned word loads through BadVAddr", "[r3000a]") {
    sparse_bus bus;
    r3000a cpu;
    cpu.attach_bus(bus);

    load_program(bus, cpu, 0x1000U,
                 {
                     i(0x09U, 0, 1, 0x0201U), // ADDIU AT,R0,0201
                     i(0x23U, 1, 2, 0x0000U), // LW V0,0(AT) -> address error
                 });

    run(cpu, 2);
    const auto regs = cpu.cpu_registers();
    CHECK(regs.pc == r3000a::boot_exception_vector);
    CHECK(regs.cop0[r3000a::cop0_epc] == 0x1004U);
    CHECK(regs.cop0[r3000a::cop0_badvaddr] == 0x0201U);
    CHECK(((regs.cop0[r3000a::cop0_cause] & r3000a::cause_exception_code_mask) >> 2U) ==
          static_cast<std::uint32_t>(r3000a::exception_code::address_load));
}

TEST_CASE("r3000a external interrupt line vectors through CP0 status and cause",
          "[r3000a]") {
    sparse_bus bus;
    r3000a cpu;
    cpu.attach_bus(bus);

    load_program(bus, cpu, 0x1000U,
                 {
                     i(0x09U, 0, 1, 0x0001U), // would execute if the IRQ were masked
                 });

    auto regs = cpu.cpu_registers();
    regs.cop0[r3000a::cop0_status] = r3000a::status_bev |
                                     r3000a::status_interrupt_enable |
                                     r3000a::status_external_irq2_mask;
    cpu.set_registers(regs);
    cpu.set_external_interrupt_line(true);

    CHECK(cpu.external_interrupt_line());
    cpu.step_instruction();

    regs = cpu.cpu_registers();
    CHECK(regs.r[1] == 0U);
    CHECK(regs.pc == r3000a::boot_exception_vector);
    CHECK(regs.cop0[r3000a::cop0_epc] == 0x1000U);
    CHECK((regs.cop0[r3000a::cop0_cause] & r3000a::cause_external_irq2_pending) != 0U);
    CHECK(((regs.cop0[r3000a::cop0_cause] & r3000a::cause_exception_code_mask) >> 2U) ==
          static_cast<std::uint32_t>(r3000a::exception_code::interrupt));
}

TEST_CASE("r3000a exceptions in a branch delay slot record BD and keep the vector PC",
          "[r3000a]") {
    sparse_bus bus;
    r3000a cpu;
    cpu.attach_bus(bus);

    load_program(bus, cpu, 0x1000U,
                 {
                     i(0x04U, 0, 0, 0x0002U), // BEQ R0,R0,+2 -> 100C
                     0x0000000DU,             // BREAK in delay slot
                     i(0x09U, 0, 1, 0x0001U), // skipped by exception
                     i(0x09U, 0, 2, 0x0002U), // branch target, also skipped
                 });

    run(cpu, 2);
    const auto regs = cpu.cpu_registers();
    CHECK(regs.pc == r3000a::boot_exception_vector);
    CHECK(regs.cop0[r3000a::cop0_epc] == 0x1000U);
    CHECK((regs.cop0[r3000a::cop0_cause] & r3000a::cause_bd) != 0U);
    CHECK(((regs.cop0[r3000a::cop0_cause] & r3000a::cause_exception_code_mask) >> 2U) ==
          static_cast<std::uint32_t>(r3000a::exception_code::breakpoint));
}

TEST_CASE("r3000a save-state round-trips branch and load-delay state", "[r3000a]") {
    sparse_bus bus;
    r3000a cpu;
    cpu.attach_bus(bus);

    w32(bus, 0x300U, 0x11223344U);
    load_program(bus, cpu, 0x1000U,
                 {
                     i(0x09U, 0, 1, 0x0300U), // ADDIU AT,R0,0300
                     i(0x23U, 1, 2, 0x0000U), // LW V0,0(AT)
                     i(0x04U, 0, 0, 0x0001U), // BEQ R0,R0,+1
                     i(0x09U, 0, 3, 0x0003U), // delay slot
                     i(0x09U, 0, 4, 0x0004U), // target
                 });

    run(cpu, 2); // V0 load is still pending.
    auto seeded = cpu.cpu_registers();
    seeded.cop2_data[4] = 0xCAFEBABEU;
    seeded.cop2_control[31] = 0x80001234U;
    seeded.cop2_command = 0x00180001U;
    cpu.set_registers(seeded);

    std::vector<std::uint8_t> bytes;
    mnemos::chips::state_writer writer(bytes);
    cpu.save_state(writer);

    run(cpu, 3);
    CHECK(cpu.cpu_registers().r[2] == 0x11223344U);

    mnemos::chips::state_reader reader(bytes);
    cpu.load_state(reader);
    REQUIRE(reader.ok());
    run(cpu, 3);

    const auto regs = cpu.cpu_registers();
    CHECK(regs.r[2] == 0x11223344U);
    CHECK(regs.r[3] == 3U);
    CHECK(regs.r[4] == 4U);
    CHECK(regs.cop2_data[4] == 0xCAFEBABEU);
    CHECK(regs.cop2_control[31] == 0x80001234U);
    CHECK(regs.cop2_command == 0x00180001U);
}
