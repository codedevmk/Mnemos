#include "sh2.hpp"

#include "bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>

namespace {
    using mnemos::chips::cpu::sh2;

    struct machine final {
        std::array<std::uint8_t, 0x10000> ram{};
        mnemos::topology::bus bus{32U, mnemos::topology::endianness::big};
        sh2 cpu;

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
        [[nodiscard]] std::uint32_t r32(std::uint32_t a) const {
            return (static_cast<std::uint32_t>(ram[a]) << 24U) |
                   (static_cast<std::uint32_t>(ram[a + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(ram[a + 2U]) << 8U) | ram[a + 3U];
        }
        void load(std::uint32_t a, std::initializer_list<std::uint16_t> words) {
            for (const std::uint16_t w : words) {
                w16(a, w);
                a += 2U;
            }
        }
    };
} // namespace

TEST_CASE("sh2 TRAPA pushes a frame and RTE returns through it") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 0x20U * 4U, 0x5000U); // vector 0x20 handler -> 0x5000
    m.load(0x1000U, {0xC320U});           // TRAPA #0x20
    m.load(0x5000U, {0x002BU, 0x0009U});  // RTE ; (delay) NOP
    CHECK(m.cpu.step_instruction() == 8); // TRAPA
    auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x5000U);               // vectored through VBR + 0x20*4
    CHECK(a.r[15] == 0x3008U);            // SR + PC pushed
    CHECK(m.cpu.step_instruction() == 4); // RTE
    const auto b = m.cpu.cpu_registers();
    CHECK(b.pc == 0x1002U);    // returned to the instruction after TRAPA
    CHECK(b.r[15] == 0x3010U); // stack restored
}

TEST_CASE("sh2 address-error exception vectors odd instruction fetches") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1001U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 9U * 4U, 0x7000U); // CPU address-error handler -> 0x7000

    CHECK(m.cpu.step_instruction() == 1);
    const auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);
    CHECK(a.r[15] == 0x3008U);
    CHECK(m.ram[0x3008] == 0x00U);
    CHECK(m.ram[0x300B] == 0x01U); // saved PC = odd fetch address 0x1001
}

TEST_CASE("sh2 address-error exception vectors misaligned word and long data accesses") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[1] = 0x2001U;
    r.r[2] = 0xAAAAAAAAU;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 9U * 4U, 0x7000U);
    m.load(0x1000U, {0x6211U}); // MOV.W @R1,R2

    m.cpu.step_instruction();
    auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);
    CHECK(a.r[2] == 0xAAAAAAAAU);
    CHECK(a.r[15] == 0x3008U);
    CHECK(m.ram[0x300B] == 0x02U); // saved PC = instruction after the faulting op

    r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[3] = 0x3002U;
    r.r[4] = 0x12345678U;
    r.r[15] = 0x3010U;
    r.pc = 0x1100U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.load(0x1100U, {0x2346U}); // MOV.L R4,@-R3, target 0x2FFE is not long-aligned
    m.cpu.step_instruction();
    const auto b = m.cpu.cpu_registers();
    CHECK(b.pc == 0x7000U);
    CHECK(b.r[3] == 0x3002U); // predecrement did not retire
    CHECK(m.ram[0x2FFE] == 0x00U);
    CHECK(m.ram[0x300B] == 0x02U);
}

TEST_CASE("sh2 address-error exception is deferred from delay-slot data access") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[1] = 0x2001U;
    r.r[2] = 0xAAAAAAAAU;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 9U * 4U, 0x7000U);
    m.load(0x1000U, {0xA006U, 0x6211U}); // BRA 0x1010 ; (slot) MOV.W @R1,R2

    CHECK(m.cpu.step_instruction() == 2);
    const auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);
    CHECK(a.r[2] == 0xAAAAAAAAU);
    CHECK(a.r[15] == 0x3008U);
    CHECK(m.r32(0x3008U) == 0x00001010U); // saved PC = committed branch target
}

TEST_CASE("sh2 address-error exception enforces on-chip access classes") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[1] = 0xFFFFFF00U;
    r.r[2] = 0xAAAAAAAAU;
    r.r[15] = 0x3010U;
    r.pc = 0x1200U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 9U * 4U, 0x7000U);
    m.load(0x1200U, {0x6210U}); // MOV.B @R1,R2: byte access in high on-chip space
    m.cpu.step_instruction();
    auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);
    CHECK(a.r[2] == 0xAAAAAAAAU);
    CHECK(m.r32(0x3008U) == 0x00001202U);

    r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[1] = 0xFFFFFE00U;
    r.r[2] = 0xBBBBBBBBU;
    r.r[15] = 0x3010U;
    r.pc = 0x1300U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.load(0x1300U, {0x6212U}); // MOV.L @R1,R2: long access in low on-chip space
    m.cpu.step_instruction();
    const auto b = m.cpu.cpu_registers();
    CHECK(b.pc == 0x7000U);
    CHECK(b.r[2] == 0xBBBBBBBBU);
    CHECK(m.r32(0x3008U) == 0x00001302U);
}

TEST_CASE("sh2 address-error exception rejects PC-relative loads into on-chip space") {
    machine m;
    std::array<std::uint8_t, 0x1000> high_ram{};
    m.bus.map_ram(0xFFFFF000U, std::span<std::uint8_t>(high_ram), 1);
    high_ram[0xDFCU] = 0x92U; // MOV.W @(0,PC),R2 at 0xFFFFFDFC
    high_ram[0xDFDU] = 0x00U;

    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[2] = 0xAAAAAAAAU;
    r.r[15] = 0x3010U;
    r.pc = 0xFFFFFDFCU;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 9U * 4U, 0x7000U);

    m.cpu.step_instruction();
    const auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);
    CHECK(a.r[2] == 0xAAAAAAAAU);
    CHECK(m.r32(0x3008U) == 0xFFFFFDFEU);
}

TEST_CASE("sh2 stacking fault on a misaligned SP vectors through 9 without recursing") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3011U; // misaligned SP: the exception-frame push address-errors
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 4U * 4U, 0x5000U); // general-illegal handler (vector 4)
    m.w32(0x4000U + 9U * 4U, 0x7000U); // address-error handler (vector 9)
    m.load(0x1000U, {0x0000U});        // illegal opcode -> vector 4, but the push faults

    m.cpu.step_instruction();
    const auto a = m.cpu.cpu_registers();
    // The stacking fault diverts to vector 9 (not the illegal handler) and does
    // not recurse or reset; SP is still decremented by the stacked frames.
    CHECK(a.pc == 0x7000U);
    CHECK(a.pc != 0x5000U);
    CHECK(a.r[15] < 0x3011U);
}

TEST_CASE("sh2 address-error faults byte/word access to cache-control spaces") {
    machine m;
    m.w32(0x4000U + 9U * 4U, 0x7000U); // address-error handler

    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[1] = 0x60000000U; // address-array space (longword-only)
    r.r[2] = 0xAAAAAAAAU;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x6210U}); // MOV.B @R1,R2 -- byte access to the address array
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x7000U);
    CHECK(m.cpu.cpu_registers().r[2] == 0xAAAAAAAAU); // the load did not retire

    auto r2 = m.cpu.cpu_registers();
    r2.vbr = 0x4000U;
    r2.r[1] = 0x40000000U; // purge space
    r2.r[15] = 0x3010U;
    r2.pc = 0x1100U;
    r2.sr = 0U;
    m.cpu.set_registers(r2);
    m.load(0x1100U, {0x6211U}); // MOV.W @R1,R2 -- word access to the purge space
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x7000U);

    // The 32X data array ($C0000000) is the cache-as-RAM scratch, NOT a fault: a
    // long access there proceeds normally to the (unmapped here) external bus.
    auto r3 = m.cpu.cpu_registers();
    r3.vbr = 0x4000U;
    r3.r[1] = 0xC0000000U;
    r3.pc = 0x1200U;
    r3.sr = 0U;
    m.cpu.set_registers(r3);
    m.load(0x1200U, {0x6212U}); // MOV.L @R1,R2
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x1202U); // advanced normally, no vector-9
}

TEST_CASE("sh2 accepts an external IRQ above the mask") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U; // IMASK = 0
    m.cpu.set_registers(r);
    m.w32(0x4000U + 0x44U * 4U, 0x6000U); // vector 0x44 handler -> 0x6000
    m.load(0x1000U, {0x0009U});           // NOP (interrupted instruction)
    m.load(0x6000U, {0x0009U});           // handler entry: NOP
    int seen_level = -1;
    std::uint8_t seen_vec = 0U;
    m.cpu.set_irq_accept_callback([&](int lvl, std::uint8_t v) {
        seen_level = lvl;
        seen_vec = v;
    });
    m.cpu.set_irq(12, 0x44U); // VINT: level 12, vector 0x44
    m.cpu.step_instruction(); // services the IRQ, then runs the handler's first op
    const auto rr = m.cpu.cpu_registers();
    CHECK(seen_level == 12);
    CHECK(seen_vec == 0x44U);
    CHECK(rr.r[15] == 0x3008U);                    // frame pushed
    CHECK(((rr.sr & sh2::sr_imask) >> 4U) == 12U); // IMASK raised to the level
    CHECK(rr.pc == 0x6002U);                       // handler's NOP executed
}

TEST_CASE("sh2 holds an IRQ at or below the mask") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 12U << 4U; // IMASK = 12
    m.cpu.set_registers(r);
    m.load(0x1000U, {0x0009U});
    m.cpu.set_irq(12, 0x44U); // level 12 is not > mask 12 -> not accepted
    m.cpu.step_instruction();
    const auto rr = m.cpu.cpu_registers();
    CHECK(rr.pc == 0x1002U);    // ran the NOP, no vectoring
    CHECK(rr.r[15] == 0x3010U); // no frame pushed
}

TEST_CASE("sh2 LDC to SR inhibits IRQ acceptance for one instruction") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 15U << 4U; // IMASK = 15 (all masked)
    r.r[1] = 0U;      // LDC R1,SR will unmask everything (SR -> 0)
    m.cpu.set_registers(r);
    m.w32(0x4000U + 0x44U * 4U, 0x6000U);
    m.load(0x1000U, {0x410EU, 0xE505U}); // LDC R1,SR ; MOV #5,R5
    m.load(0x6000U, {0x0009U});
    m.cpu.set_irq(12, 0x44U);
    m.cpu.step_instruction(); // LDC R1,SR: unmasks + arms the one-instruction inhibit
    m.cpu.step_instruction(); // MOV #5,R5 runs on the inhibited boundary (no IRQ yet)
    auto a = m.cpu.cpu_registers();
    CHECK(a.r[5] == 5U);
    CHECK(a.r[15] == 0x3010U); // IRQ still pending, not taken
    m.cpu.step_instruction();  // inhibit cleared -> IRQ accepted here
    CHECK(m.cpu.cpu_registers().r[15] == 0x3008U);
}

TEST_CASE("sh2 SLEEP halts until an interrupt wakes it") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 0x44U * 4U, 0x6000U);
    m.load(0x1000U, {0x001BU, 0xE509U});  // SLEEP ; MOV #9,R5 (resumes here after RTE)
    m.load(0x6000U, {0x0009U});           // handler: NOP
    CHECK(m.cpu.step_instruction() == 3); // SLEEP -> halted
    CHECK(m.cpu.cpu_registers().pc == 0x1002U);
    m.cpu.step_instruction(); // still halted (no interrupt): nothing runs
    CHECK(m.cpu.cpu_registers().pc == 0x1002U);
    CHECK(m.cpu.cpu_registers().r[5] == 0U);
    m.cpu.set_irq(12, 0x44U);
    m.cpu.step_instruction(); // the IRQ resumes the CPU and runs the handler
    const auto a = m.cpu.cpu_registers();
    CHECK(a.r[15] == 0x3008U); // frame pushed
    CHECK(a.pc == 0x6002U);    // handler's NOP executed
}

TEST_CASE("sh2 an undecoded opcode raises general illegal-instruction") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 4U * 4U, 0x7000U); // general-illegal handler (vector 4) -> 0x7000
    m.load(0x1000U, {0xF000U});        // FPU opcode: illegal on the SH7604
    m.cpu.step_instruction();
    const auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);    // vectored to the illegal-instruction handler
    CHECK(a.r[15] == 0x3008U); // SR + faulting PC pushed
}

TEST_CASE("sh2 an illegal opcode in a delay slot raises slot-illegal") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.vbr = 0x4000U;
    r.r[15] = 0x3010U;
    r.pc = 0x1000U;
    r.sr = 0U;
    m.cpu.set_registers(r);
    m.w32(0x4000U + 6U * 4U, 0x7000U);   // slot-illegal handler (vector 6) -> 0x7000
    m.load(0x1000U, {0xA00AU, 0xF000U}); // BRA ; (delay) illegal FPU opcode
    m.cpu.step_instruction();
    const auto a = m.cpu.cpu_registers();
    CHECK(a.pc == 0x7000U);    // vectored to slot-illegal, NOT the branch target
    CHECK(a.r[15] == 0x3008U); // frame pushed (saved PC = the branch's resume target)
}
