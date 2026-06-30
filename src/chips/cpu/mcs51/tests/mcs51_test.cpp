#include "mcs51.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::cpu::mcs51;

    // 64K external data space for MOVX.
    class xdata_bus final : public mnemos::chips::ibus {
      public:
        std::array<std::uint8_t, 0x10000U> memory{};

        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            return memory[address & 0xFFFFU];
        }
        void write8(std::uint32_t address, std::uint8_t value) override {
            memory[address & 0xFFFFU] = value;
        }
    };

    void run(mcs51& cpu, int instructions) {
        for (int i = 0; i < instructions; ++i) {
            cpu.step_instruction();
        }
    }

    [[nodiscard]] bool is_ajmp(std::uint8_t opcode) noexcept {
        return (opcode & 0x1FU) == 0x01U;
    }

    [[nodiscard]] bool is_acall(std::uint8_t opcode) noexcept {
        return (opcode & 0x1FU) == 0x11U;
    }

    [[nodiscard]] std::size_t instruction_length(std::uint8_t opcode) noexcept {
        if (is_ajmp(opcode) || is_acall(opcode)) {
            return 2U;
        }
        switch (opcode) {
        case 0x02U: // LJMP addr16
        case 0x10U: // JBC bit, rel
        case 0x12U: // LCALL addr16
        case 0x20U: // JB bit, rel
        case 0x30U: // JNB bit, rel
        case 0x43U: // ORL direct, #imm
        case 0x53U: // ANL direct, #imm
        case 0x63U: // XRL direct, #imm
        case 0x75U: // MOV direct, #imm
        case 0x85U: // MOV direct, direct
        case 0x90U: // MOV DPTR, #imm16
        case 0xB4U: // CJNE A, #imm, rel
        case 0xB5U: // CJNE A, direct, rel
        case 0xB6U: // CJNE @R0, #imm, rel
        case 0xB7U: // CJNE @R1, #imm, rel
        case 0xB8U:
        case 0xB9U:
        case 0xBAU:
        case 0xBBU:
        case 0xBCU:
        case 0xBDU:
        case 0xBEU:
        case 0xBFU: // CJNE Rn, #imm, rel
        case 0xD5U: // DJNZ direct, rel
            return 3U;
        case 0x24U:
        case 0x34U:
        case 0x44U:
        case 0x54U:
        case 0x64U:
        case 0x74U:
        case 0x94U: // ALU/MOV A,#imm rows
        case 0x40U:
        case 0x50U:
        case 0x60U:
        case 0x70U:
        case 0x80U: // rel8 branches
        case 0x42U:
        case 0x52U:
        case 0x62U: // direct,A logic rows
        case 0x72U:
        case 0x82U:
        case 0x92U:
        case 0xA0U:
        case 0xA2U:
        case 0xB0U:
        case 0xB2U: // bit operands
        case 0x05U:
        case 0x15U:
        case 0x25U:
        case 0x35U:
        case 0x45U:
        case 0x55U:
        case 0x65U:
        case 0x76U:
        case 0x77U:
        case 0x78U:
        case 0x79U:
        case 0x7AU:
        case 0x7BU:
        case 0x7CU:
        case 0x7DU:
        case 0x7EU:
        case 0x7FU:
        case 0x86U:
        case 0x87U:
        case 0x88U:
        case 0x89U:
        case 0x8AU:
        case 0x8BU:
        case 0x8CU:
        case 0x8DU:
        case 0x8EU:
        case 0x8FU:
        case 0x95U:
        case 0xA6U:
        case 0xA7U:
        case 0xA8U:
        case 0xA9U:
        case 0xAAU:
        case 0xABU:
        case 0xACU:
        case 0xADU:
        case 0xAEU:
        case 0xAFU:
        case 0xC0U:
        case 0xC2U:
        case 0xC5U:
        case 0xD0U:
        case 0xD2U:
        case 0xD8U:
        case 0xD9U:
        case 0xDAU:
        case 0xDBU:
        case 0xDCU:
        case 0xDDU:
        case 0xDEU:
        case 0xDFU:
        case 0xE5U:
        case 0xF5U:
            return 2U;
        default:
            return 1U;
        }
    }

    [[nodiscard]] std::uint16_t absolute_target_for(std::uint8_t opcode,
                                                    std::uint8_t operand) noexcept {
        return static_cast<std::uint16_t>(((opcode >> 5U) << 8U) | operand);
    }

    [[nodiscard]] std::vector<std::uint8_t> opcode_fixture(std::uint8_t opcode) {
        const std::size_t length = instruction_length(opcode);
        std::vector<std::uint8_t> bytes(length, 0x00U);
        bytes[0] = opcode;
        if (length >= 2U) {
            bytes[1] = 0x30U; // safe direct/bit/imm default
        }
        if (length >= 3U) {
            bytes[2] = 0x00U; // rel=0 keeps relative branches at fall-through
        }

        switch (opcode) {
        case 0x02U: // LJMP 0003
        case 0x12U: // LCALL 0003
            bytes[1] = 0x00U;
            bytes[2] = 0x03U;
            break;
        case 0x10U:
        case 0x20U:
        case 0x30U:
        case 0x72U:
        case 0x82U:
        case 0x92U:
        case 0xA0U:
        case 0xA2U:
        case 0xB0U:
        case 0xB2U:
        case 0xC2U:
        case 0xD2U:
            bytes[1] = 0x00U; // bit-addressable IRAM 0x20.0
            break;
        case 0x85U:
            bytes[1] = 0x30U; // source direct
            bytes[2] = 0x31U; // destination direct
            break;
        case 0x40U:
        case 0x50U:
        case 0x60U:
        case 0x70U:
        case 0x80U:
        case 0xD8U:
        case 0xD9U:
        case 0xDAU:
        case 0xDBU:
        case 0xDCU:
        case 0xDDU:
        case 0xDEU:
        case 0xDFU:
            bytes[1] = 0x00U; // rel=0 keeps the expected PC at fall-through.
            break;
        case 0x90U:
            bytes[1] = 0x00U;
            bytes[2] = 0x01U;
            break;
        default:
            break;
        }

        if (is_ajmp(opcode) || is_acall(opcode)) {
            bytes[1] = 0x34U;
        }
        return bytes;
    }

} // namespace

TEST_CASE("mcs51 registers through the chip registry and resets to spec", "[mcs51]") {
    auto chip = mnemos::chips::create_chip("intel.mcs51");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().manufacturer == "Intel");

    mcs51 cpu;
    const auto regs = cpu.cpu_registers();
    CHECK(regs.pc == 0x0000U);
    CHECK(regs.sp == 0x07U);
    CHECK(cpu.peek_direct(0x90U) == 0xFFU); // P1 latch resets high
}

TEST_CASE("mcs51 decodes every opcode form and consumes its operand bytes", "[mcs51]") {
    xdata_bus bus;

    for (unsigned opcode_value = 0; opcode_value <= 0xFFU; ++opcode_value) {
        const auto opcode = static_cast<std::uint8_t>(opcode_value);
        CAPTURE(opcode_value);
        mcs51 cpu;
        cpu.attach_bus(bus);
        const std::vector<std::uint8_t> program = opcode_fixture(opcode);
        cpu.attach_program(program);

        auto regs = cpu.cpu_registers();
        regs.acc = opcode == 0x73U ? 0x00U : 0x12U; // JMP @A+DPTR lands at DPTR.
        regs.b = 0x03U;
        regs.psw = 0x00U;
        regs.sp = (opcode == 0x22U || opcode == 0x32U) ? 0x09U : 0x20U;
        regs.dptr = opcode == 0x73U ? 0x0001U : 0x0100U;
        regs.pc = 0x0000U;
        cpu.set_registers(regs);
        cpu.poke_direct(0x00U, 0x30U); // R0 -> IRAM 0x30 for @R0 forms.
        cpu.poke_direct(0x01U, 0x31U); // R1 -> IRAM 0x31 for @R1 forms.
        cpu.poke_direct(0x08U, 0x34U); // RET/RETI low byte.
        cpu.poke_direct(0x09U, 0x12U); // RET/RETI high byte.
        cpu.poke_direct(0x20U, 0x01U); // bit 0 true for JNB/not-bit forms.
        cpu.poke_direct(0x30U, 0x30U);
        cpu.poke_direct(0x31U, 0x31U);

        const int cycles = cpu.step_instruction();
        CHECK(cycles > 0);

        const auto after = cpu.cpu_registers();
        std::uint16_t expected_pc = static_cast<std::uint16_t>(program.size());
        if (is_ajmp(opcode) || is_acall(opcode)) {
            expected_pc = absolute_target_for(opcode, program[1]);
        } else if (opcode == 0x02U || opcode == 0x12U) {
            expected_pc =
                static_cast<std::uint16_t>((program[1] << 8U) | program[2]);
        } else if (opcode == 0x22U || opcode == 0x32U) {
            expected_pc = 0x1234U;
        } else if (opcode == 0x73U) {
            expected_pc = 0x0001U;
        }
        CHECK(after.pc == expected_pc);

        if (is_acall(opcode) || opcode == 0x12U) {
            CHECK(after.sp == 0x22U);
            CHECK(cpu.peek_direct(0x21U) == program.size());
            CHECK(cpu.peek_direct(0x22U) == 0x00U);
        }
    }
}

TEST_CASE("mcs51 arithmetic sets CY, AC, OV and hardware parity", "[mcs51]") {
    mcs51 cpu;
    // MOV A,#7F; ADD A,#01  -> 0x80: OV (signed overflow), AC, no CY
    const std::vector<std::uint8_t> program{0x74U, 0x7FU, 0x24U, 0x01U,
                                            0x74U, 0xFFU, 0x24U, 0x01U}; // then FF+1 -> CY
    cpu.attach_program(program);
    run(cpu, 2);
    auto regs = cpu.cpu_registers();
    CHECK(regs.acc == 0x80U);
    CHECK((regs.psw & mcs51::psw_ov) != 0U);
    CHECK((regs.psw & mcs51::psw_ac) != 0U);
    CHECK((regs.psw & mcs51::psw_cy) == 0U);
    CHECK((regs.psw & mcs51::psw_p) != 0U); // 0x80 has odd bit count
    run(cpu, 2);
    regs = cpu.cpu_registers();
    CHECK(regs.acc == 0x00U);
    CHECK((regs.psw & mcs51::psw_cy) != 0U);
    CHECK((regs.psw & mcs51::psw_p) == 0U); // 0x00 has even bit count
}

TEST_CASE("mcs51 MUL, DIV and DA cover the B register paths", "[mcs51]") {
    mcs51 cpu;
    // MOV A,#0x40; MOV B,#0x05; MUL AB        -> B:A = 0x0140
    // MOV A,#0x17; MOV B,#0x05; DIV AB        -> A=4 rem B=3
    // MOV A,#0x19; ADD A,#0x27; DA A          -> BCD 19+27 = 46
    const std::vector<std::uint8_t> program{
        0x74U, 0x40U, 0x75U, 0xF0U, 0x05U, 0xA4U, 0x74U, 0x17U, 0x75U,
        0xF0U, 0x05U, 0x84U, 0x74U, 0x19U, 0x24U, 0x27U, 0xD4U,
    };
    cpu.attach_program(program);
    run(cpu, 3);
    auto regs = cpu.cpu_registers();
    CHECK(regs.acc == 0x40U);
    CHECK(regs.b == 0x01U);
    CHECK((regs.psw & mcs51::psw_ov) != 0U); // high byte significant
    run(cpu, 3);
    regs = cpu.cpu_registers();
    CHECK(regs.acc == 0x04U);
    CHECK(regs.b == 0x03U);
    run(cpu, 3);
    regs = cpu.cpu_registers();
    CHECK(regs.acc == 0x46U);
}

TEST_CASE("mcs51 bit space reaches IRAM 0x20-0x2F and the SFR bits", "[mcs51]") {
    mcs51 cpu;
    // SETB 0x08 (IRAM 0x21 bit 0); CPL 0x08; SETB 0xE0 (ACC bit 0);
    // JB 0xE0,+2 (taken, skips the CLR A); CLR A; SETB 0x0A
    const std::vector<std::uint8_t> program{
        0xD2U, 0x08U,        // SETB 08
        0xB2U, 0x08U,        // CPL 08 -> clear again
        0xD2U, 0xE0U,        // SETB ACC.0
        0x20U, 0xE0U, 0x01U, // JB ACC.0, +1 (skips CLR A)
        0xE4U,               // CLR A (skipped)
        0xD2U, 0x0AU,        // SETB 0A (IRAM 0x21 bit 2)
    };
    cpu.attach_program(program);
    run(cpu, 1);
    CHECK(cpu.peek_direct(0x21U) == 0x01U);
    run(cpu, 1);
    CHECK(cpu.peek_direct(0x21U) == 0x00U);
    run(cpu, 3); // SETB ACC.0, JB taken, SETB 0A
    const auto regs = cpu.cpu_registers();
    CHECK(regs.acc == 0x01U); // CLR A was skipped
    CHECK(cpu.peek_direct(0x21U) == 0x04U);
}

TEST_CASE("mcs51 DJNZ loops and CJNE sets carry as an unsigned compare", "[mcs51]") {
    mcs51 cpu;
    // MOV R2,#3; loop: INC A; DJNZ R2,loop; CJNE A,#5,+0 (not taken path
    // still sets CY = A < 5)
    const std::vector<std::uint8_t> program{
        0x7AU, 0x03U,        // MOV R2,#3
        0x04U,               // INC A
        0xDAU, 0xFDU,        // DJNZ R2,-3
        0xB4U, 0x05U, 0x00U, // CJNE A,#5,+0
    };
    cpu.attach_program(program);
    run(cpu, 1 + 3 * 2 + 1); // MOV, 3x(INC+DJNZ), CJNE
    const auto regs = cpu.cpu_registers();
    CHECK(regs.acc == 0x03U);
    CHECK((regs.psw & mcs51::psw_cy) != 0U); // 3 < 5
}

TEST_CASE("mcs51 LCALL pushes and RET pops through the IRAM stack", "[mcs51]") {
    mcs51 cpu;
    // LCALL 0x0010; (return lands here) SJMP self | at 0x10: MOV A,#0x55; RET
    std::vector<std::uint8_t> program(0x20U, 0x00U);
    program[0x00] = 0x12U; // LCALL
    program[0x01] = 0x00U;
    program[0x02] = 0x10U;
    program[0x03] = 0x80U; // SJMP self
    program[0x04] = 0xFEU;
    program[0x10] = 0x74U; // MOV A,#0x55
    program[0x11] = 0x55U;
    program[0x12] = 0x22U; // RET
    cpu.attach_program(program);

    cpu.step_instruction(); // LCALL
    CHECK(cpu.cpu_registers().pc == 0x0010U);
    CHECK(cpu.cpu_registers().sp == 0x09U); // two pushed bytes
    run(cpu, 2);                            // MOV, RET
    const auto regs = cpu.cpu_registers();
    CHECK(regs.acc == 0x55U);
    CHECK(regs.pc == 0x0003U);
    CHECK(regs.sp == 0x07U);
}

TEST_CASE("mcs51 MOVC reads program memory and MOVX reaches the external bus", "[mcs51]") {
    mcs51 cpu;
    xdata_bus bus;
    cpu.attach_bus(bus);
    // MOV DPTR,#0x0010; MOV A,#2; MOVC A,@A+DPTR (table read);
    // MOV DPTR,#0x1234; MOVX @DPTR,A; CLR A; MOVX A,@DPTR
    std::vector<std::uint8_t> program(0x20U, 0x00U);
    const std::vector<std::uint8_t> code{
        0x90U, 0x00U, 0x10U, // MOV DPTR,#0010
        0x74U, 0x02U,        // MOV A,#2
        0x93U,               // MOVC A,@A+DPTR
        0x90U, 0x12U, 0x34U, // MOV DPTR,#1234
        0xF0U,               // MOVX @DPTR,A
        0xE4U,               // CLR A
        0xE0U,               // MOVX A,@DPTR
    };
    for (std::size_t i = 0; i < code.size(); ++i) {
        program[i] = code[i];
    }
    program[0x12] = 0xBEU; // table entry at 0x10 + 2
    cpu.attach_program(program);

    run(cpu, 3);
    CHECK(cpu.cpu_registers().acc == 0xBEU);
    run(cpu, 2);
    CHECK(bus.memory[0x1234U] == 0xBEU);
    run(cpu, 2);
    CHECK(cpu.cpu_registers().acc == 0xBEU);
}

TEST_CASE("mcs51 MOVX @Ri uses the P2 latch as the external high address byte", "[mcs51]") {
    mcs51 cpu;
    xdata_bus bus;
    cpu.attach_bus(bus);
    bus.memory[0x1235U] = 0x5AU;

    const std::vector<std::uint8_t> program{
        0x75U, 0xA0U, 0x12U, // MOV P2,#12
        0x78U, 0x34U,        // MOV R0,#34
        0x74U, 0xA5U,        // MOV A,#A5
        0xF2U,               // MOVX @R0,A -> xdata 1234
        0x79U, 0x35U,        // MOV R1,#35
        0xE3U,               // MOVX A,@R1 <- xdata 1235
    };
    cpu.attach_program(program);

    run(cpu, 6);

    CHECK(bus.memory[0x1234U] == 0xA5U);
    CHECK(cpu.cpu_registers().acc == 0x5AU);
}

TEST_CASE("mcs51 register banks switch on PSW.RS", "[mcs51]") {
    mcs51 cpu;
    // MOV R0,#0x11 (bank 0); MOV PSW,#0x08 (bank 1); MOV R0,#0x22
    const std::vector<std::uint8_t> program{
        0x78U, 0x11U,        // MOV R0,#11
        0x75U, 0xD0U, 0x08U, // MOV PSW,#08 (RS0=1)
        0x78U, 0x22U,        // MOV R0,#22
    };
    cpu.attach_program(program);
    run(cpu, 3);
    CHECK(cpu.peek_direct(0x00U) == 0x11U); // bank 0 R0
    CHECK(cpu.peek_direct(0x08U) == 0x22U); // bank 1 R0
}

TEST_CASE("mcs51 ports read pins through the latch and notify on writes", "[mcs51]") {
    mcs51 cpu;
    int last_port = -1;
    std::uint8_t last_value = 0U;
    cpu.set_port_in([](int port) -> std::uint8_t { return port == 1 ? 0x5AU : 0xFFU; });
    cpu.set_port_out([&](int port, std::uint8_t value) {
        last_port = port;
        last_value = value;
    });
    // MOV A,P1 (latch FF & pins 5A); MOV P3,#0xA5
    const std::vector<std::uint8_t> program{0xE5U, 0x90U, 0x75U, 0xB0U, 0xA5U};
    cpu.attach_program(program);
    run(cpu, 2);
    CHECK(cpu.cpu_registers().acc == 0x5AU);
    CHECK(last_port == 3);
    CHECK(last_value == 0xA5U);
}

TEST_CASE("mcs51 port read-modify-write instructions use the output latch", "[mcs51]") {
    mcs51 cpu;
    std::vector<std::uint8_t> writes;
    cpu.set_port_in([](int port) -> std::uint8_t { return port == 1 ? 0x00U : 0xFFU; });
    cpu.set_port_out([&](int port, std::uint8_t value) {
        if (port == 1) {
            writes.push_back(value);
        }
    });

    const std::vector<std::uint8_t> program{
        0x75U, 0x90U, 0xF0U, // MOV P1,#F0
        0x43U, 0x90U, 0x0FU, // ORL P1,#0F -> FF from latch, not pins
        0x53U, 0x90U, 0xF7U, // ANL P1,#F7 -> F7
        0x63U, 0x90U, 0x08U, // XRL P1,#08 -> FF
        0x05U, 0x90U,        // INC P1 -> 00
        0x15U, 0x90U,        // DEC P1 -> FF
        0xD5U, 0x90U, 0x00U, // DJNZ P1,+0 -> FE
    };
    cpu.attach_program(program);

    run(cpu, 7);

    const std::vector<std::uint8_t> expected{0xF0U, 0xFFU, 0xF7U, 0xFFU,
                                             0x00U, 0xFFU, 0xFEU};
    CHECK(writes == expected);
}

TEST_CASE("mcs51 bit read-modify-write instructions use the port latch", "[mcs51]") {
    mcs51 cpu;
    std::vector<std::uint8_t> writes;
    cpu.set_port_in([](int port) -> std::uint8_t { return port == 1 ? 0x00U : 0xFFU; });
    cpu.set_port_out([&](int port, std::uint8_t value) {
        if (port == 1) {
            writes.push_back(value);
        }
    });

    const std::vector<std::uint8_t> program{
        0x75U, 0x90U, 0x80U, // MOV P1,#80
        0x10U, 0x97U, 0x00U, // JBC P1.7,+0 -> clear bit from latch
        0xD2U, 0x90U,        // SETB P1.0 -> 01
        0xB2U, 0x90U,        // CPL P1.0 -> 00
    };
    cpu.attach_program(program);

    run(cpu, 4);

    const std::vector<std::uint8_t> expected{0x80U, 0x00U, 0x01U, 0x00U};
    CHECK(writes == expected);
}

TEST_CASE("mcs51 edge-sensed INT0 vectors to 0x0003 and RETI returns", "[mcs51]") {
    mcs51 cpu;
    // Main: SETB IT0 (TCON.0 = bit 0x88); MOV IE,#0x81; SJMP self.
    // ISR at 0x03: MOV 0x30,#0xAA; RETI.
    std::vector<std::uint8_t> program(0x20U, 0x00U);
    program[0x00] = 0x80U; // placeholder: jump over the ISR -> SJMP +0x0B
    program[0x01] = 0x0BU;
    program[0x03] = 0x75U; // ISR: MOV 30,#AA
    program[0x04] = 0x30U;
    program[0x05] = 0xAAU;
    program[0x06] = 0x32U; // RETI
    program[0x0D] = 0xD2U; // SETB 0x88 (TCON.IT0)
    program[0x0E] = 0x88U;
    program[0x0F] = 0x75U; // MOV IE,#0x81 (EA | EX0)
    program[0x10] = 0xA8U;
    program[0x11] = 0x81U;
    program[0x12] = 0x80U; // SJMP self
    program[0x13] = 0xFEU;
    cpu.attach_program(program);

    run(cpu, 3); // SJMP over ISR, SETB IT0, MOV IE
    CHECK(cpu.cpu_registers().pc == 0x0012U);

    cpu.set_int0_line(true); // rising edge latches IE0
    cpu.set_int0_line(false);
    cpu.step_instruction(); // interrupt accepted -> ISR entry
    CHECK(cpu.cpu_registers().pc == 0x0003U);
    run(cpu, 2); // MOV 30,#AA; RETI
    CHECK(cpu.peek_direct(0x30U) == 0xAAU);
    CHECK(cpu.cpu_registers().pc == 0x0012U); // back at the idle loop
}

TEST_CASE("mcs51 RETI defers a pending same-priority interrupt for one instruction",
          "[mcs51]") {
    mcs51 cpu;
    // Main arms edge-triggered INT0, then the instruction at 0x25 must execute
    // once after RETI before the pending second INT0 can re-enter the ISR.
    std::vector<std::uint8_t> program(0x30U, 0x00U);
    program[0x00] = 0x80U; // SJMP +0x1E -> main at 0x20
    program[0x01] = 0x1EU;
    program[0x03] = 0x05U; // ISR: INC 30; RETI
    program[0x04] = 0x30U;
    program[0x05] = 0x32U;
    const std::vector<std::uint8_t> main_code{
        0xD2U, 0x88U,        // SETB TCON.IT0
        0x75U, 0xA8U, 0x81U, // MOV IE,#EA|EX0
        0x75U, 0x31U, 0x44U, // MOV 31,#44
        0x80U, 0xFEU,        // SJMP self
    };
    for (std::size_t i = 0; i < main_code.size(); ++i) {
        program[0x20U + i] = main_code[i];
    }
    cpu.attach_program(program);

    run(cpu, 3); // jump to main, SETB IT0, MOV IE
    CHECK(cpu.cpu_registers().pc == 0x0025U);

    cpu.set_int0_line(true);
    cpu.set_int0_line(false);
    cpu.step_instruction(); // first INT0 accepted
    CHECK(cpu.cpu_registers().pc == 0x0003U);
    cpu.step_instruction(); // ISR body
    CHECK(cpu.peek_direct(0x30U) == 0x01U);

    cpu.set_int0_line(true);
    cpu.set_int0_line(false); // second INT0 remains pending through RETI
    cpu.step_instruction();   // RETI returns to mainline at 0x25
    CHECK(cpu.cpu_registers().pc == 0x0025U);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    cpu.save_state(writer);

    mcs51 restored;
    restored.attach_program(program);
    mnemos::chips::state_reader reader(snapshot);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    restored.step_instruction(); // deferred foreground instruction executes first
    CHECK(restored.peek_direct(0x31U) == 0x44U);
    CHECK(restored.cpu_registers().pc == 0x0028U);
    CHECK(restored.peek_direct(0x30U) == 0x01U);

    restored.step_instruction(); // now the pending INT0 may re-enter
    CHECK(restored.cpu_registers().pc == 0x0003U);
    restored.step_instruction();
    CHECK(restored.peek_direct(0x30U) == 0x02U);
}

TEST_CASE("mcs51 IE access defers a newly enabled pending interrupt for one instruction",
          "[mcs51]") {
    mcs51 cpu;
    std::vector<std::uint8_t> program(0x20U, 0x00U);
    program[0x03] = 0x05U; // ISR: INC 30; RETI
    program[0x04] = 0x30U;
    program[0x05] = 0x32U;
    program[0x10] = 0x75U; // MOV IE,#EA|EX0
    program[0x11] = 0xA8U;
    program[0x12] = 0x81U;
    program[0x13] = 0x75U; // MOV 31,#66
    program[0x14] = 0x31U;
    program[0x15] = 0x66U;
    program[0x16] = 0x80U; // SJMP self
    program[0x17] = 0xFEU;
    cpu.attach_program(program);
    auto regs = cpu.cpu_registers();
    regs.pc = 0x0010U;
    cpu.set_registers(regs);
    cpu.poke_direct(0x88U, 0x03U); // TCON.IT0 | latched IE0 before IE enables it.

    cpu.step_instruction(); // MOV IE,#EA|EX0
    CHECK(cpu.cpu_registers().pc == 0x0013U);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    cpu.save_state(writer);

    mcs51 restored;
    restored.attach_program(program);
    mnemos::chips::state_reader reader(snapshot);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    restored.step_instruction(); // pending INT0 waits for the post-IE foreground instruction
    CHECK(restored.cpu_registers().pc == 0x0016U);
    CHECK(restored.peek_direct(0x31U) == 0x66U);
    CHECK(restored.peek_direct(0x30U) == 0x00U);

    restored.step_instruction(); // now the pending INT0 is accepted
    CHECK(restored.cpu_registers().pc == 0x0003U);
    restored.step_instruction();
    CHECK(restored.peek_direct(0x30U) == 0x01U);
}

TEST_CASE("mcs51 IP access in a low ISR defers newly high-priority preemption",
          "[mcs51]") {
    mcs51 cpu;
    std::vector<std::uint8_t> program(0x30U, 0x00U);
    program[0x03] = 0x75U; // INT0 ISR: MOV IP,#PT0
    program[0x04] = 0xB8U;
    program[0x05] = 0x02U;
    program[0x06] = 0x75U; // MOV 31,#44
    program[0x07] = 0x31U;
    program[0x08] = 0x44U;
    program[0x09] = 0x80U; // SJMP self
    program[0x0A] = 0xFEU;
    program[0x0B] = 0x05U; // timer 0 ISR: INC 32; RETI
    program[0x0C] = 0x32U;
    program[0x0D] = 0x32U;
    program[0x20] = 0x80U; // main idle
    program[0x21] = 0xFEU;
    cpu.attach_program(program);
    auto regs = cpu.cpu_registers();
    regs.pc = 0x0020U;
    cpu.set_registers(regs);
    cpu.poke_direct(0xA8U, 0x83U); // IE.EA | IE.EX0 | IE.ET0
    cpu.poke_direct(0x88U, 0x23U); // TCON.IT0 | IE0 | TF0, both low priority.

    cpu.step_instruction(); // poll order accepts INT0 first
    CHECK(cpu.cpu_registers().pc == 0x0003U);

    cpu.step_instruction(); // MOV IP,#PT0 makes timer 0 high priority
    CHECK(cpu.cpu_registers().pc == 0x0006U);

    cpu.step_instruction(); // high-priority timer 0 waits one foreground ISR instruction
    CHECK(cpu.cpu_registers().pc == 0x0009U);
    CHECK(cpu.peek_direct(0x31U) == 0x44U);
    CHECK(cpu.peek_direct(0x32U) == 0x00U);

    cpu.step_instruction(); // now timer 0 preempts the low-priority ISR
    CHECK(cpu.cpu_registers().pc == 0x000BU);
    cpu.step_instruction();
    CHECK(cpu.peek_direct(0x32U) == 0x01U);
}

TEST_CASE("mcs51 high-priority interrupts preempt a low-priority service routine", "[mcs51]") {
    mcs51 cpu;
    // Main: set INT0/INT1 edge-triggered, make INT1 high priority through IP,
    // enable both external sources, then idle. INT0's ISR intentionally loops
    // without RETI so the test can prove only the high-priority source nests.
    std::vector<std::uint8_t> program(0x40U, 0x00U);
    program[0x00] = 0x80U; // SJMP +0x1E -> main at 0x20
    program[0x01] = 0x1EU;
    program[0x03] = 0x75U; // low ISR: MOV 30,#11
    program[0x04] = 0x30U;
    program[0x05] = 0x11U;
    program[0x06] = 0x80U; // SJMP self
    program[0x07] = 0xFEU;
    program[0x13] = 0x75U; // high ISR: MOV 31,#22; RETI
    program[0x14] = 0x31U;
    program[0x15] = 0x22U;
    program[0x16] = 0x32U;
    const std::vector<std::uint8_t> main_code{
        0xD2U, 0x88U,        // SETB TCON.IT0
        0xD2U, 0x8AU,        // SETB TCON.IT1
        0x75U, 0xB8U, 0x04U, // MOV IP,#PX1
        0x75U, 0xA8U, 0x85U, // MOV IE,#EA|EX0|EX1
        0x80U, 0xFEU,        // SJMP self
    };
    for (std::size_t i = 0; i < main_code.size(); ++i) {
        program[0x20U + i] = main_code[i];
    }
    cpu.attach_program(program);

    run(cpu, 5); // jump to main, configure IT0/IT1/IP/IE
    CHECK(cpu.cpu_registers().pc == 0x002AU);

    cpu.set_int0_line(true);
    cpu.set_int0_line(false);
    cpu.step_instruction(); // low-priority INT0 accepted
    CHECK(cpu.cpu_registers().pc == 0x0003U);
    cpu.step_instruction(); // low ISR marker
    CHECK(cpu.peek_direct(0x30U) == 0x11U);
    CHECK(cpu.cpu_registers().pc == 0x0006U);

    cpu.set_int1_line(true);
    cpu.set_int1_line(false);
    cpu.step_instruction(); // high-priority INT1 preempts the low ISR
    CHECK(cpu.cpu_registers().pc == 0x0013U);

    cpu.set_int0_line(true);
    cpu.set_int0_line(false);
    cpu.step_instruction(); // high ISR body executes; low INT0 cannot preempt it
    CHECK(cpu.peek_direct(0x31U) == 0x22U);
    CHECK(cpu.cpu_registers().pc == 0x0016U);
    cpu.step_instruction(); // RETI from high ISR resumes the low ISR
    CHECK(cpu.cpu_registers().pc == 0x0006U);
    cpu.step_instruction(); // pending low INT0 is still masked by the active low ISR
    CHECK(cpu.cpu_registers().pc == 0x0006U);
}

TEST_CASE("mcs51 serial RI and TI flags request vector 0x0023", "[mcs51]") {
    mcs51 cpu;
    // Serial RI/TI flags share the 0x23 vector. The hardware does not clear
    // either flag on interrupt entry; firmware clears SCON.RI/TI explicitly.
    std::vector<std::uint8_t> program(0x30U, 0x00U);
    program[0x23] = 0x75U; // MOV 32,#5A
    program[0x24] = 0x32U;
    program[0x25] = 0x5AU;
    program[0x26] = 0x32U; // RETI
    cpu.attach_program(program);
    cpu.poke_direct(0x98U, 0x01U); // SCON.RI
    cpu.poke_direct(0xA8U, 0x90U); // IE.EA | IE.ES

    cpu.step_instruction(); // serial interrupt accepted
    CHECK(cpu.cpu_registers().pc == 0x0023U);
    cpu.step_instruction(); // ISR marker
    CHECK(cpu.peek_direct(0x32U) == 0x5AU);
    CHECK((cpu.peek_direct(0x98U) & 0x01U) != 0U);
    cpu.step_instruction(); // RETI
    CHECK(cpu.cpu_registers().pc == 0x0000U);
    CHECK((cpu.peek_direct(0x98U) & 0x01U) != 0U);

    cpu.poke_direct(0x98U, 0x02U); // SCON.TI requests the same source
    cpu.step_instruction();
    CHECK(cpu.cpu_registers().pc == 0x0001U); // one foreground instruction after RETI
    cpu.step_instruction();
    CHECK(cpu.cpu_registers().pc == 0x0023U);
}

TEST_CASE("mcs51 SBUF transmit completes after a frame and survives save state", "[mcs51]") {
    mcs51 cpu;
    const std::vector<std::uint8_t> program(16U, 0x00U);
    cpu.attach_program(program);
    std::vector<std::uint8_t> sent;
    cpu.set_serial_transmit_callback([&sent](std::uint8_t value) { sent.push_back(value); });

    cpu.poke_direct(0x99U, 0xA5U); // write-only TX side of SBUF
    CHECK(cpu.peek_direct(0x99U) == 0x00U); // reads expose the independent RX buffer
    run(cpu, 7);
    CHECK((cpu.peek_direct(0x98U) & 0x02U) == 0U);
    CHECK(sent.empty());

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    cpu.save_state(writer);

    mcs51 restored;
    restored.attach_program(program);
    mnemos::chips::state_reader reader(snapshot);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    std::vector<std::uint8_t> restored_sent;
    restored.set_serial_transmit_callback(
        [&restored_sent](std::uint8_t value) { restored_sent.push_back(value); });
    restored.step_instruction();
    CHECK((restored.peek_direct(0x98U) & 0x02U) != 0U);
    REQUIRE(restored_sent.size() == 1U);
    CHECK(restored_sent[0] == 0xA5U);
    CHECK(sent.empty());
}

TEST_CASE("mcs51 serial receive byte updates SBUF RI and RB8 when enabled", "[mcs51]") {
    mcs51 cpu;

    cpu.serial_receive_byte(0x5AU, true);
    CHECK(cpu.peek_direct(0x99U) == 0x00U);
    CHECK((cpu.peek_direct(0x98U) & 0x05U) == 0U);

    cpu.poke_direct(0x98U, 0x10U); // SCON.REN
    cpu.serial_receive_byte(0x5AU, true);
    CHECK(cpu.peek_direct(0x99U) == 0x5AU);
    CHECK((cpu.peek_direct(0x98U) & 0x05U) == 0x05U); // RI | RB8

    cpu.serial_receive_byte(0xA5U, false);
    CHECK(cpu.peek_direct(0x99U) == 0xA5U);
    CHECK((cpu.peek_direct(0x98U) & 0x01U) != 0U);
    CHECK((cpu.peek_direct(0x98U) & 0x04U) == 0U);
}

TEST_CASE("mcs51 timer 1 overflows clock serial mode 1 transmit frames", "[mcs51]") {
    mcs51 cpu;
    const std::vector<std::uint8_t> program(400U, 0x00U);
    cpu.attach_program(program);
    std::vector<std::uint8_t> sent;
    cpu.set_serial_transmit_callback([&sent](std::uint8_t value) { sent.push_back(value); });

    cpu.poke_direct(0x98U, 0x40U); // SCON.SM1: serial mode 1, 10-bit frame
    cpu.poke_direct(0x89U, 0x20U); // TMOD: timer 1 mode 2 auto-reload
    cpu.poke_direct(0x8DU, 0xFFU); // TH1 reload causes one overflow per machine cycle
    cpu.poke_direct(0x8BU, 0xFFU); // TL1 starts at overflow edge
    cpu.poke_direct(0x88U, 0x40U); // TCON.TR1
    cpu.poke_direct(0x99U, 0x3CU);

    run(cpu, 319);
    CHECK((cpu.peek_direct(0x98U) & 0x02U) == 0U);
    CHECK(sent.empty());
    cpu.step_instruction();
    CHECK((cpu.peek_direct(0x98U) & 0x02U) != 0U);
    REQUIRE(sent.size() == 1U);
    CHECK(sent[0] == 0x3CU);
}

TEST_CASE("mcs51 timer 0 mode 2 auto-reloads and interrupts", "[mcs51]") {
    mcs51 cpu;
    // MOV TMOD,#0x02 (T0 mode 2); MOV TH0,#0xFC; MOV TL0,#0xFC;
    // MOV IE,#0x82 (EA|ET0); SETB TR0 (TCON.4 = bit 0x8C); SJMP self.
    // ISR at 0x0B: MOV 0x31,#0x77; RETI.
    std::vector<std::uint8_t> program(0x30U, 0x00U);
    program[0x00] = 0x80U; // SJMP +0x10 -> main at 0x12
    program[0x01] = 0x10U;
    program[0x0B] = 0x75U; // ISR: MOV 31,#77
    program[0x0C] = 0x31U;
    program[0x0D] = 0x77U;
    program[0x0E] = 0x32U; // RETI
    const std::vector<std::uint8_t> main_code{
        0x75U, 0x89U, 0x02U, // MOV TMOD,#02
        0x75U, 0x8CU, 0xFCU, // MOV TH0,#FC
        0x75U, 0x8AU, 0xFCU, // MOV TL0,#FC
        0x75U, 0xA8U, 0x82U, // MOV IE,#82
        0xD2U, 0x8CU,        // SETB TCON.4 (TR0)
        0x80U, 0xFEU,        // SJMP self
    };
    for (std::size_t i = 0; i < main_code.size(); ++i) {
        program[0x12U + i] = main_code[i];
    }
    cpu.attach_program(program);

    run(cpu, 6); // through SETB TR0
    // TL0 counts from 0xFC: overflows after 4 machine cycles of idling.
    run(cpu, 8); // idle SJMPs accumulate cycles; the ISR fires in here
    CHECK(cpu.peek_direct(0x31U) == 0x77U);
}

TEST_CASE("mcs51 timer gate waits for the matching external interrupt pin", "[mcs51]") {
    mcs51 cpu;
    const std::vector<std::uint8_t> program{0x00U, 0x00U};
    cpu.attach_program(program);
    cpu.poke_direct(0x89U, 0x09U); // TMOD: T0 GATE | mode 1
    cpu.poke_direct(0x8CU, 0xFFU); // TH0
    cpu.poke_direct(0x8AU, 0xFFU); // TL0
    cpu.poke_direct(0x88U, 0x10U); // TCON.TR0

    cpu.step_instruction(); // INT0 is low, so GATE holds T0 stopped.
    CHECK(cpu.peek_direct(0x8CU) == 0xFFU);
    CHECK(cpu.peek_direct(0x8AU) == 0xFFU);
    CHECK((cpu.peek_direct(0x88U) & 0x20U) == 0U);

    cpu.set_int0_line(true);
    cpu.step_instruction();
    CHECK(cpu.peek_direct(0x8CU) == 0x00U);
    CHECK(cpu.peek_direct(0x8AU) == 0x00U);
    CHECK((cpu.peek_direct(0x88U) & 0x20U) != 0U);
}

TEST_CASE("mcs51 counter mode counts high-to-low T0 pin transitions", "[mcs51]") {
    mcs51 cpu;
    const std::vector<std::uint8_t> program{0x00U, 0x00U, 0x00U};
    cpu.attach_program(program);
    cpu.poke_direct(0x89U, 0x05U); // TMOD: T0 C/T | mode 1
    cpu.poke_direct(0x8CU, 0xFFU); // TH0
    cpu.poke_direct(0x8AU, 0xFEU); // TL0
    cpu.poke_direct(0x88U, 0x10U); // TCON.TR0

    run(cpu, 3); // Internal machine cycles do not advance an external counter.
    CHECK(cpu.peek_direct(0x8CU) == 0xFFU);
    CHECK(cpu.peek_direct(0x8AU) == 0xFEU);
    CHECK((cpu.peek_direct(0x88U) & 0x20U) == 0U);

    cpu.set_t0_line(true);
    cpu.set_t0_line(false);
    CHECK(cpu.peek_direct(0x8CU) == 0xFFU);
    CHECK(cpu.peek_direct(0x8AU) == 0xFFU);
    CHECK((cpu.peek_direct(0x88U) & 0x20U) == 0U);

    cpu.set_t0_line(true);
    cpu.set_t0_line(false);
    CHECK(cpu.peek_direct(0x8CU) == 0x00U);
    CHECK(cpu.peek_direct(0x8AU) == 0x00U);
    CHECK((cpu.peek_direct(0x88U) & 0x20U) != 0U);
}

TEST_CASE("mcs51 counter pin state survives save state", "[mcs51]") {
    mcs51 cpu;
    const std::vector<std::uint8_t> program{0x00U};
    cpu.attach_program(program);
    cpu.poke_direct(0x89U, 0x05U); // TMOD: T0 C/T | mode 1
    cpu.poke_direct(0x8CU, 0xFFU); // TH0
    cpu.poke_direct(0x8AU, 0xFFU); // TL0
    cpu.poke_direct(0x88U, 0x10U); // TCON.TR0
    cpu.set_t0_line(true);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    cpu.save_state(writer);

    mcs51 restored;
    restored.attach_program(program);
    mnemos::chips::state_reader reader(snapshot);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    restored.set_t0_line(false);
    CHECK(restored.peek_direct(0x8CU) == 0x00U);
    CHECK(restored.peek_direct(0x8AU) == 0x00U);
    CHECK((restored.peek_direct(0x88U) & 0x20U) != 0U);
}

TEST_CASE("mcs51 timer 1 counter mode observes GATE through INT1", "[mcs51]") {
    mcs51 cpu;
    const std::vector<std::uint8_t> program{0x00U, 0x00U};
    cpu.attach_program(program);
    cpu.poke_direct(0x89U, 0xD0U); // TMOD: T1 GATE | C/T | mode 1
    cpu.poke_direct(0x8DU, 0xFFU); // TH1
    cpu.poke_direct(0x8BU, 0xFFU); // TL1
    cpu.poke_direct(0x88U, 0x40U); // TCON.TR1

    cpu.set_t1_line(true);
    cpu.set_t1_line(false); // INT1 is low, so GATE blocks the counter edge.
    CHECK(cpu.peek_direct(0x8DU) == 0xFFU);
    CHECK(cpu.peek_direct(0x8BU) == 0xFFU);
    CHECK((cpu.peek_direct(0x88U) & 0x80U) == 0U);

    cpu.set_int1_line(true);
    cpu.set_t1_line(true);
    cpu.set_t1_line(false);
    CHECK(cpu.peek_direct(0x8DU) == 0x00U);
    CHECK(cpu.peek_direct(0x8BU) == 0x00U);
    CHECK((cpu.peek_direct(0x88U) & 0x80U) != 0U);
}

TEST_CASE("mcs51 timer mode 0 counts as a 13-bit timer", "[mcs51]") {
    mcs51 cpu;
    const std::vector<std::uint8_t> program{0x00U, 0x00U, 0x00U};
    cpu.attach_program(program);
    cpu.poke_direct(0x89U, 0x00U); // TMOD: timer 0 mode 0
    cpu.poke_direct(0x8CU, 0xFFU); // TH0: upper 8 bits
    cpu.poke_direct(0x8AU, 0xBEU); // TL0: upper 3 bits preserved, low 5 bits = 0x1E
    cpu.poke_direct(0x88U, 0x10U); // TCON.TR0

    cpu.step_instruction(); // NOP: 0x1FFE -> 0x1FFF
    CHECK(cpu.peek_direct(0x8CU) == 0xFFU);
    CHECK(cpu.peek_direct(0x8AU) == 0xBFU);
    CHECK((cpu.peek_direct(0x88U) & 0x20U) == 0U);

    cpu.step_instruction(); // 0x1FFF -> 0x0000, TF0 set
    CHECK(cpu.peek_direct(0x8CU) == 0x00U);
    CHECK(cpu.peek_direct(0x8AU) == 0xA0U); // TL0 upper bits are not part of the counter
    CHECK((cpu.peek_direct(0x88U) & 0x20U) != 0U);
}

TEST_CASE("mcs51 timer mode 3 splits timer 0 into TL0 and TH0 halves", "[mcs51]") {
    mcs51 cpu;
    const std::vector<std::uint8_t> program{0x00U, 0x00U, 0x00U};
    cpu.attach_program(program);
    cpu.poke_direct(0x89U, 0x13U); // T0 mode 3, T1 mode 1
    cpu.poke_direct(0x8AU, 0xFEU); // TL0
    cpu.poke_direct(0x8CU, 0xFEU); // TH0
    cpu.poke_direct(0x8BU, 0xFEU); // TL1: stopped while T0 is in mode 3
    cpu.poke_direct(0x8DU, 0xFEU); // TH1
    cpu.poke_direct(0x88U, 0x50U); // TCON.TR0 | TCON.TR1

    cpu.step_instruction();
    CHECK(cpu.peek_direct(0x8AU) == 0xFFU);
    CHECK(cpu.peek_direct(0x8CU) == 0xFFU);
    CHECK(cpu.peek_direct(0x8BU) == 0xFEU);
    CHECK(cpu.peek_direct(0x8DU) == 0xFEU);
    CHECK((cpu.peek_direct(0x88U) & 0xA0U) == 0U);

    cpu.step_instruction();
    CHECK(cpu.peek_direct(0x8AU) == 0x00U);
    CHECK(cpu.peek_direct(0x8CU) == 0x00U);
    CHECK(cpu.peek_direct(0x8BU) == 0xFEU);
    CHECK(cpu.peek_direct(0x8DU) == 0xFEU);
    CHECK((cpu.peek_direct(0x88U) & 0x20U) != 0U); // TF0 from TL0
    CHECK((cpu.peek_direct(0x88U) & 0x80U) != 0U); // TF1 from TH0
}

TEST_CASE("mcs51 timer mode 3 TH0 ignores timer 1 gate and counter controls", "[mcs51]") {
    mcs51 cpu;
    const std::vector<std::uint8_t> program{0x00U};
    cpu.attach_program(program);
    cpu.poke_direct(0x89U, 0xD3U); // T0 mode 3; T1 GATE | C/T | mode 1.
    cpu.poke_direct(0x8CU, 0xFFU); // TH0
    cpu.poke_direct(0x88U, 0x40U); // TCON.TR1

    cpu.step_instruction();

    CHECK(cpu.peek_direct(0x8CU) == 0x00U);
    CHECK((cpu.peek_direct(0x88U) & 0x80U) != 0U);
}

TEST_CASE("mcs51 save and load round-trips mid-program", "[mcs51]") {
    mcs51 cpu;
    const std::vector<std::uint8_t> program{0x74U, 0x11U, 0x24U, 0x22U, 0x24U, 0x33U};
    cpu.attach_program(program);
    run(cpu, 2);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    cpu.save_state(writer);

    run(cpu, 1);
    CHECK(cpu.cpu_registers().acc == 0x66U);

    mcs51 restored;
    restored.attach_program(program);
    mnemos::chips::state_reader reader(snapshot);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.cpu_registers().acc == 0x33U);
    restored.step_instruction();
    CHECK(restored.cpu_registers().acc == 0x66U);
}
