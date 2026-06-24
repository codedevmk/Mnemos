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
