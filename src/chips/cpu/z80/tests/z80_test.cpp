#include "z80.hpp"

#include "bus.hpp"
#include "chip_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <type_traits>

namespace {
    using mnemos::chips::cpu::z80;
    using reset_kind = mnemos::chips::reset_kind;

    struct machine final {
        std::array<std::uint8_t, 0x10000> ram{};
        mnemos::topology::bus bus{16U, mnemos::topology::endianness::little};
        z80 cpu;

        machine() {
            bus.map_ram(0x0000U, std::span<std::uint8_t>(ram), 0);
            cpu.attach_bus(bus);
            cpu.reset(reset_kind::power_on);
        }
        void load(std::uint16_t addr, std::initializer_list<std::uint8_t> bytes) {
            std::uint16_t a = addr;
            for (const std::uint8_t byte : bytes) {
                ram[a++] = byte;
            }
        }
    };
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::i_cpu, z80>);
static_assert(z80::static_class == mnemos::chips::chip_class::cpu);

TEST_CASE("z80 reports identity and registers under zilog.z80") {
    const z80 cpu;
    const auto md = cpu.metadata();
    CHECK(md.manufacturer == "Zilog");
    CHECK(md.part_number == "Z80");
    CHECK(md.klass == mnemos::chips::chip_class::cpu);

    auto chip = mnemos::chips::create_chip("zilog.z80");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == "Z80");
}

TEST_CASE("z80 resets to the documented power-on state") {
    machine m;
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x0000U);
    CHECK(r.sp == 0xFFFFU);
    CHECK(r.af == 0xFFFFU);
    CHECK(r.im == 0U);
    CHECK_FALSE(r.iff1);
}

TEST_CASE("z80 loads immediates and moves registers") {
    machine m;
    m.load(0x0000U, {0x3EU, 0x42U, 0x06U, 0x10U, 0x78U}); // LD A,$42 ; LD B,$10 ; LD A,B
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().af >> 8U) == 0x42U);
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().bc >> 8U) == 0x10U);
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().af >> 8U) == 0x10U); // A <- B
}

TEST_CASE("z80 ADD sets carry/half/zero flags") {
    machine m;
    m.load(0x0000U, {0x3EU, 0xFFU, 0xC6U, 0x01U}); // LD A,$FF ; ADD A,$01
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK((r.af >> 8U) == 0x00U);      // wrapped to 0
    CHECK((r.af & z80::flag_c) != 0U); // carry out
    CHECK((r.af & z80::flag_z) != 0U); // zero result
    CHECK((r.af & z80::flag_h) != 0U); // half-carry
    CHECK((r.af & z80::flag_n) == 0U); // add clears N
}

TEST_CASE("z80 INC sets the overflow flag at 0x7F->0x80") {
    machine m;
    m.load(0x0000U, {0x3EU, 0x7FU, 0x3CU}); // LD A,$7F ; INC A
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK((r.af >> 8U) == 0x80U);
    CHECK((r.af & z80::flag_p) != 0U); // signed overflow
    CHECK((r.af & z80::flag_s) != 0U); // negative
    CHECK((r.af & z80::flag_h) != 0U);
}

TEST_CASE("z80 executes 16-bit loads and increments") {
    machine m;
    m.load(0x0000U, {0x21U, 0x34U, 0x12U, 0x23U}); // LD HL,$1234 ; INC HL
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().hl == 0x1234U);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().hl == 0x1235U);
}

TEST_CASE("z80 takes absolute jumps") {
    machine m;
    m.load(0x0000U, {0xC3U, 0x00U, 0x20U}); // JP $2000
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x2000U);
}

TEST_CASE("z80 calls and returns through the stack") {
    machine m;
    m.load(0x0000U, {0xCDU, 0x00U, 0x10U}); // CALL $1000
    m.load(0x1000U, {0xC9U});               // RET
    m.cpu.step_instruction();
    auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x1000U);
    CHECK(r.sp == 0xFFFDU);
    CHECK(m.ram[0xFFFD] == 0x03U); // return address low
    CHECK(m.ram[0xFFFE] == 0x00U); // return address high

    m.cpu.step_instruction(); // RET
    r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x0003U);
    CHECK(r.sp == 0xFFFFU);
}

TEST_CASE("z80 pushes and pops register pairs") {
    machine m;
    m.load(0x0000U, {0x01U, 0x34U, 0x12U, 0xC5U, 0xD1U}); // LD BC,$1234 ; PUSH BC ; POP DE
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.de == 0x1234U);
    CHECK(r.sp == 0xFFFFU);
}

TEST_CASE("z80 CB bit/set/res operate on registers") {
    machine m;
    m.load(0x0000U, {0x3EU, 0x00U,   // LD A,0
                     0xCBU, 0xC7U,   // SET 0,A   -> A=1
                     0xCBU, 0x47U,   // BIT 0,A   -> Z clear
                     0xCBU, 0x87U}); // RES 0,A  -> A=0
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().af >> 8U) == 0x01U);
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().af & z80::flag_z) == 0U); // bit 0 is set
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().af >> 8U) == 0x00U);
}

TEST_CASE("z80 ED LDIR block-copies memory") {
    machine m;
    m.ram[0x2000] = 0x11U;
    m.ram[0x2001] = 0x22U;
    m.ram[0x2002] = 0x33U;
    m.ram[0x2003] = 0x44U;
    auto r = m.cpu.cpu_registers();
    r.hl = 0x2000U;
    r.de = 0x3000U;
    r.bc = 0x0004U;
    r.pc = 0x0000U;
    m.cpu.set_registers(r);
    m.load(0x0000U, {0xEDU, 0xB0U}); // LDIR

    for (int i = 0; i < 4; ++i) {
        m.cpu.step_instruction(); // each pass copies one byte and repeats
    }
    CHECK(m.ram[0x3000] == 0x11U);
    CHECK(m.ram[0x3003] == 0x44U);
    r = m.cpu.cpu_registers();
    CHECK(r.bc == 0x0000U);
    CHECK(r.pc == 0x0002U); // fell through past LDIR
}

TEST_CASE("z80 DD prefix loads the IX index register") {
    machine m;
    m.load(0x0000U, {0xDDU, 0x21U, 0x34U, 0x12U,   // LD IX,$1234
                     0xDDU, 0x36U, 0x02U, 0x99U}); // LD (IX+2),$99
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().ix == 0x1234U);
    m.cpu.step_instruction();
    CHECK(m.ram[0x1236] == 0x99U); // IX+2
}

TEST_CASE("z80 IN/OUT route through the port callbacks") {
    machine m;
    std::uint16_t out_port = 0;
    std::uint8_t out_val = 0;
    m.cpu.set_port_out([&](std::uint16_t p, std::uint8_t v) {
        out_port = p;
        out_val = v;
    });
    m.cpu.set_port_in([&](std::uint16_t) -> std::uint8_t { return 0xABU; });

    m.load(0x0000U,
           {0x3EU, 0x12U, 0xD3U, 0x34U, 0xDBU, 0x34U}); // LD A,$12; OUT ($34),A; IN A,($34)
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(out_port == 0x1234U); // (A << 8) | port
    CHECK(out_val == 0x12U);
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().af >> 8U) == 0xABU);
}

TEST_CASE("z80 services an NMI") {
    machine m;
    m.cpu.set_nmi_line(true);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x0066U);
    CHECK(m.cpu.cpu_registers().sp == 0xFFFDU); // return address pushed
}

TEST_CASE("z80 services a mode-1 maskable interrupt after EI delay") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.im = 1U;
    r.pc = 0x0000U;
    m.cpu.set_registers(r);
    m.load(0x0000U, {0xFBU, 0x00U}); // EI ; NOP

    m.cpu.step_instruction(); // EI (interrupts enabled only after the next op)
    m.cpu.set_irq_line(true);
    m.cpu.step_instruction(); // NOP runs; the IRQ is not yet serviced
    CHECK(m.cpu.cpu_registers().pc == 0x0002U);
    m.cpu.step_instruction(); // now the IRQ vectors to $0038
    CHECK(m.cpu.cpu_registers().pc == 0x0038U);
}

TEST_CASE("z80 tick catches up by whole instructions") {
    machine m;
    m.load(0x0000U, {0x00U, 0x00U, 0x00U, 0x00U}); // NOPs (4T each)
    m.cpu.tick(10U);                               // ~10 cycles -> 3 NOPs (12T)
    CHECK(m.cpu.elapsed_cycles() >= 10U);
    CHECK(m.cpu.cpu_registers().pc >= 0x0003U);
}
