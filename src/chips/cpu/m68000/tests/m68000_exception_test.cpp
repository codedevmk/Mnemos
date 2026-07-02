#include "m68000.hpp"

#include "bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>

namespace {
    using mnemos::chips::cpu::m68000;

    struct machine final {
        std::array<std::uint8_t, 0x10000> ram{};
        mnemos::topology::bus bus{24U, mnemos::topology::endianness::big};
        m68000 cpu;

        machine() {
            bus.map_ram(0x0000U, std::span<std::uint8_t>(ram), 0);
            cpu.attach_bus(bus);
        }
        void w16(std::uint32_t a, std::uint16_t v) {
            ram[a] = static_cast<std::uint8_t>(v >> 8U); // big-endian
            ram[a + 1U] = static_cast<std::uint8_t>(v);
        }
        void w32(std::uint32_t a, std::uint32_t v) {
            w16(a, static_cast<std::uint16_t>(v >> 16U));
            w16(a + 2U, static_cast<std::uint16_t>(v));
        }
        void load(std::uint32_t a, std::initializer_list<std::uint16_t> words) {
            for (const std::uint16_t w : words) {
                w16(a, w);
                a += 2U;
            }
        }
    };

    std::uint16_t r16(machine& m, std::uint32_t a) {
        return static_cast<std::uint16_t>((static_cast<std::uint16_t>(m.bus.read8(a)) << 8U) |
                                          m.bus.read8(a + 1U));
    }

    std::uint32_t r32(machine& m, std::uint32_t a) {
        return (static_cast<std::uint32_t>(r16(m, a)) << 16U) | r16(m, a + 2U);
    }
} // namespace

TEST_CASE("m68000 TRAP vectors through the exception table") {
    machine m;
    m.w32(0x0080U, 0x00004000U); // vector 32 (TRAP #0) -> $4000
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[7] = 0x00003000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x4E40U}); // TRAP #0
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00004000U);
    CHECK(r.a[7] == 0x00002FFAU);         // pushed PC (4) + SR (2)
    CHECK(m.bus.read8(0x2FFEU) == 0x10U); // stacked return PC $1002
    CHECK(m.bus.read8(0x2FFFU) == 0x02U);
}

TEST_CASE("m68000 Line-1111 (Line-F) opcode traps through vector $2C with the next PC") {
    machine m;
    m.w32(0x002CU, 0x00004000U); // vector 11 (Line-1111 emulator) -> $4000
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[7] = 0x00003000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0xFF1FU}); // an unimplemented Line-F opcode (group $F)
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00004000U);   // vectored through $2C
    CHECK(r.a[7] == 0x00002FFAU); // pushed PC (4) + SR (2)
    // The MC68000 group-1/2 frame stacks the next PC ($1002). A handler can read
    // the trapping opcode at saved_pc - 2.
    CHECK(m.bus.read8(0x2FFEU) == 0x10U);
    CHECK(m.bus.read8(0x2FFFU) == 0x02U);
    CHECK(m.bus.read8(0x2FFAU) == 0x20U); // stacked old SR ($2000, S set)
    CHECK(m.bus.read8(0x2FFBU) == 0x00U);
}

TEST_CASE("m68000 Line-1010 (Line-A) opcode traps through vector $28 with the next PC") {
    machine m;
    m.w32(0x0028U, 0x00004000U); // vector 10 (Line-1010 emulator) -> $4000
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[7] = 0x00003000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0xA000U}); // an unimplemented Line-A opcode (group $A)
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00004000U);           // vectored through $28
    CHECK(r.a[7] == 0x00002FFAU);         // pushed PC (4) + SR (2)
    CHECK(m.bus.read8(0x2FFEU) == 0x10U); // stacked next PC $1002
    CHECK(m.bus.read8(0x2FFFU) == 0x02U);
}

TEST_CASE("m68000 ILLEGAL ($4AFC) traps through vector 4 with the next PC") {
    machine m;
    m.w32(0x0010U, 0x00004000U); // vector 4 (illegal instruction) -> $4000
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[7] = 0x00003000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x4AFCU}); // the dedicated ILLEGAL opcode
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00004000U);           // vectored through $10
    CHECK(r.a[7] == 0x00002FFAU);         // pushed PC (4) + SR (2)
    CHECK(m.bus.read8(0x2FFEU) == 0x10U); // stacked next PC $1002
    CHECK(m.bus.read8(0x2FFFU) == 0x02U);
}

TEST_CASE("m68000 MOVEC encodings trap as illegal on MC68000 after the extension word") {
    static constexpr std::array<std::uint16_t, 2> opcodes{0x4E7AU, 0x4E7BU};
    for (const std::uint16_t op : opcodes) {
        INFO("opcode $" << std::hex << op);
        machine m;
        m.w32(0x0010U, 0x00004000U); // vector 4 (illegal instruction) -> $4000
        m68000::registers s{};
        s.sr = m68000::sr_s;
        s.a[7] = 0x00003000U;
        s.pc = 0x1000U;
        m.cpu.set_registers(s);
        m.load(0x1000U, {op, 0x8000U}); // MOVEC has an extension word on 68010+
        m.cpu.step_instruction();
        const auto r = m.cpu.cpu_registers();
        CHECK(r.pc == 0x00004000U);
        CHECK(r.a[7] == 0x00002FFAU);
        CHECK(m.bus.read8(0x2FFEU) == 0x10U); // stacked next PC $1004
        CHECK(m.bus.read8(0x2FFFU) == 0x04U);
    }
}

TEST_CASE("m68000 odd instruction fetch raises an address-error group-0 frame") {
    machine m;
    m.w32(0x000CU, 0x00004000U); // address-error vector (3) -> $4000
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[7] = 0x00003000U;
    s.pc = 0x1001U;
    m.cpu.set_registers(s);

    m.cpu.step_instruction();

    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00004000U);
    CHECK(r.a[7] == 0x00002FF2U);
    CHECK(r16(m, 0x2FF2U) == 0x0016U);     // read, instruction, supervisor program FC
    CHECK(r32(m, 0x2FF4U) == 0x00001001U); // faulting fetch address
    CHECK(r16(m, 0x2FF8U) == 0x0000U);     // no opcode word was fetched
    CHECK(r16(m, 0x2FFAU) == 0x2000U);     // old SR
    CHECK(r32(m, 0x2FFCU) == 0x00001001U); // saved PC for the aborted fetch
}

TEST_CASE("m68000 odd word read raises address error without committing the MOVE") {
    machine m;
    m.w32(0x000CU, 0x00004000U); // address-error vector (3) -> $4000
    m.w16(0x2001U, 0x1234U);
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[0] = 0x00002001U;
    s.a[7] = 0x00003000U;
    s.d[0] = 0x89ABCDEFU;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x3010U}); // MOVE.W (A0),D0

    m.cpu.step_instruction();

    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00004000U);
    CHECK(r.a[7] == 0x00002FF2U);
    CHECK(r.a[0] == 0x00002001U);
    CHECK(r.d[0] == 0x89ABCDEFU);
    CHECK(r16(m, 0x2FF2U) == 0x001DU);     // read, not-instruction, supervisor data FC
    CHECK(r32(m, 0x2FF4U) == 0x00002001U); // faulting data address
    CHECK(r16(m, 0x2FF8U) == 0x3010U);     // instruction register
    CHECK(r16(m, 0x2FFAU) == 0x2000U);     // old SR
    CHECK(r32(m, 0x2FFCU) == 0x00001000U); // instruction PC restored into the frame
}

TEST_CASE("m68000 odd long read raises address error before mutating the destination") {
    machine m;
    m.w32(0x000CU, 0x00004000U); // address-error vector (3) -> $4000
    m.w32(0x2001U, 0x12345678U);
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[0] = 0x00002001U;
    s.a[7] = 0x00003000U;
    s.d[0] = 0xCAFEBABEU;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x2010U}); // MOVE.L (A0),D0

    m.cpu.step_instruction();

    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00004000U);
    CHECK(r.a[7] == 0x00002FF2U);
    CHECK(r.d[0] == 0xCAFEBABEU);
    CHECK(r16(m, 0x2FF2U) == 0x001DU);
    CHECK(r32(m, 0x2FF4U) == 0x00002001U);
    CHECK(r16(m, 0x2FF8U) == 0x2010U);
}

TEST_CASE("m68000 odd word write raises address error without writing memory") {
    machine m;
    m.w32(0x000CU, 0x00004000U); // address-error vector (3) -> $4000
    m.ram[0x2001U] = 0xCCU;
    m.ram[0x2002U] = 0xDDU;
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[0] = 0x00002001U;
    s.a[7] = 0x00003000U;
    s.d[0] = 0x0000A55AU;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x3080U}); // MOVE.W D0,(A0)

    m.cpu.step_instruction();

    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00004000U);
    CHECK(r.a[7] == 0x00002FF2U);
    CHECK(r.a[0] == 0x00002001U);
    CHECK(r.d[0] == 0x0000A55AU);
    CHECK(m.ram[0x2001U] == 0xCCU);
    CHECK(m.ram[0x2002U] == 0xDDU);
    CHECK(r16(m, 0x2FF2U) == 0x000DU);     // write, not-instruction, supervisor data FC
    CHECK(r32(m, 0x2FF4U) == 0x00002001U); // faulting data address
    CHECK(r16(m, 0x2FF8U) == 0x3080U);     // instruction register
    CHECK(r16(m, 0x2FFAU) == 0x2000U);     // old SR
    CHECK(r32(m, 0x2FFCU) == 0x00001000U);
}

TEST_CASE("m68000 bus-error fetch raises a vector-2 group-0 frame") {
    machine m;
    m.w32(0x0008U, 0x00004000U); // bus-error vector (2) -> $4000
    m.bus.map_bus_error(0x1000U, 0x02U, 1);
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[7] = 0x00003000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);

    m.cpu.step_instruction();

    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00004000U);
    CHECK(r.a[7] == 0x00002FF2U);
    CHECK(r16(m, 0x2FF2U) == 0x0016U);     // read, instruction, supervisor program FC
    CHECK(r32(m, 0x2FF4U) == 0x00001000U); // faulting bus address
    CHECK(r16(m, 0x2FF8U) == 0x0000U);     // no opcode word was acknowledged
    CHECK(r16(m, 0x2FFAU) == 0x2000U);     // old SR
    CHECK(r32(m, 0x2FFCU) == 0x00001000U);
}

TEST_CASE("m68000 bus-error word read raises vector 2 without committing the MOVE") {
    machine m;
    m.w32(0x0008U, 0x00004000U); // bus-error vector (2) -> $4000
    m.w16(0x2000U, 0x1234U);
    m.bus.map_bus_error(0x2000U, 0x02U, 1);
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[0] = 0x00002000U;
    s.a[7] = 0x00003000U;
    s.d[0] = 0x89ABCDEFU;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x3010U}); // MOVE.W (A0),D0

    m.cpu.step_instruction();

    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00004000U);
    CHECK(r.a[7] == 0x00002FF2U);
    CHECK(r.a[0] == 0x00002000U);
    CHECK(r.d[0] == 0x89ABCDEFU);
    CHECK(r16(m, 0x2FF2U) == 0x001DU);     // read, not-instruction, supervisor data FC
    CHECK(r32(m, 0x2FF4U) == 0x00002000U); // faulting bus address
    CHECK(r16(m, 0x2FF8U) == 0x3010U);
    CHECK(r16(m, 0x2FFAU) == 0x2000U);
    CHECK(r32(m, 0x2FFCU) == 0x00001000U);
}

TEST_CASE("m68000 bus-error word write raises vector 2 without writing the fault window") {
    machine m;
    m.w32(0x0008U, 0x00004000U); // bus-error vector (2) -> $4000
    m.ram[0x2000U] = 0xCCU;
    m.ram[0x2001U] = 0xDDU;
    m.bus.map_bus_error(0x2000U, 0x02U, 1);
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[0] = 0x00002000U;
    s.a[7] = 0x00003000U;
    s.d[0] = 0x0000A55AU;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x3080U}); // MOVE.W D0,(A0)

    m.cpu.step_instruction();

    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00004000U);
    CHECK(r.a[7] == 0x00002FF2U);
    CHECK(r.a[0] == 0x00002000U);
    CHECK(r.d[0] == 0x0000A55AU);
    CHECK(m.ram[0x2000U] == 0xCCU);
    CHECK(m.ram[0x2001U] == 0xDDU);
    CHECK(r16(m, 0x2FF2U) == 0x000DU);     // write, not-instruction, supervisor data FC
    CHECK(r32(m, 0x2FF4U) == 0x00002000U); // faulting bus address
    CHECK(r16(m, 0x2FF8U) == 0x3080U);
    CHECK(r16(m, 0x2FFAU) == 0x2000U);
    CHECK(r32(m, 0x2FFCU) == 0x00001000U);
}

TEST_CASE("m68000 RTE restores SR and PC from the stack frame") {
    machine m;
    m.w16(0x2FFAU, 0x2000U);     // saved SR (S set)
    m.w32(0x2FFCU, 0x00001234U); // saved PC
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[7] = 0x00002FFAU;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x4E73U}); // RTE
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00001234U);
    CHECK((r.sr & m68000::sr_s) != 0U);
    CHECK(r.a[7] == 0x00003000U);
}

TEST_CASE("m68000 RTE restores user mode and swaps back to USP") {
    machine m;
    m.w16(0x2FFAU, 0x0000U);     // saved SR (user mode)
    m.w32(0x2FFCU, 0x00001234U); // saved PC
    m68000::registers s{};
    s.sr = m68000::sr_s;
    s.a[7] = 0x00002FFAU;
    s.usp = 0x00002100U;
    s.ssp = 0x00002FFAU;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x4E73U}); // RTE

    m.cpu.step_instruction();

    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00001234U);
    CHECK((r.sr & m68000::sr_s) == 0U);
    CHECK(r.a[7] == 0x00002100U);
    CHECK(r.usp == 0x00002100U);
    CHECK(r.ssp == 0x00003000U);
}

TEST_CASE("m68000 services an autovectored interrupt") {
    machine m;
    m.w32(0x0070U, 0x00005000U); // autovector level 4 = vector 28 -> $5000
    m68000::registers s{};
    s.sr = m68000::sr_s; // IPM = 0, so a level-4 request is accepted
    s.a[7] = 0x00003000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.cpu.set_irq_level(4);
    m.cpu.step_instruction(); // takes the interrupt instead of executing
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00005000U);
    CHECK(((r.sr >> 8U) & 7U) == 4U); // IPM raised to the accepted level
    CHECK(r.a[7] == 0x00002FFAU);
    CHECK(m.cpu.elapsed_cycles() == 42U);
}

TEST_CASE("m68000 can opt into Genesis interrupt phase timing") {
    machine m;
    m.w32(0x0070U, 0x00005000U); // autovector level 4 = vector 28 -> $5000
    m68000::registers s{};
    s.sr = m68000::sr_s; // IPM = 0, so a level-4 request is accepted
    s.a[7] = 0x00003000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.cpu.set_genesis_interrupt_phase_timing_enabled(true);
    m.cpu.set_irq_level(4);
    const int cycles = m.cpu.step_instruction();

    CHECK(cycles == 50);
    CHECK(m.cpu.elapsed_cycles() == 50U);
}

TEST_CASE("m68000 traps a privileged instruction executed in user mode") {
    machine m;
    m.w32(0x0020U, 0x00006000U); // privilege-violation vector (8) -> $6000
    m68000::registers s{};
    s.sr = 0U; // user mode
    s.a[7] = 0x00003000U;
    s.ssp = 0x00004000U;
    s.pc = 0x1000U;
    m.cpu.set_registers(s);
    m.load(0x1000U, {0x4E72U, 0x2000U}); // STOP #$2000 (privileged)
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x00006000U);
    CHECK((r.sr & m68000::sr_s) != 0U); // now supervisor
    CHECK(r.a[7] == 0x00003FFAU);       // pushed onto SSP ($4000 - 6)
}
