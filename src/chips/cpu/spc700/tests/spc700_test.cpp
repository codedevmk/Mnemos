#include "spc700.hpp"

#include "bus.hpp"
#include "chip_registry.hpp"
#include "introspection_views.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::cpu::spc700;
    using reset_kind = mnemos::chips::reset_kind;

    struct machine final {
        std::array<std::uint8_t, 0x10000> ram{};
        mnemos::topology::bus bus{16U, mnemos::topology::endianness::little};
        spc700 cpu;

        machine() {
            bus.map_ram(0x0000U, std::span<std::uint8_t>(ram), 0);
            cpu.attach_bus(bus);
            // The reset vector is read on reset(); default it to $0000 so the
            // power-on PC is deterministic for the tests below.
            ram[0xFFFE] = 0x00U;
            ram[0xFFFF] = 0x00U;
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

static_assert(std::is_base_of_v<mnemos::chips::icpu, spc700>);

TEST_CASE("spc700 reports identity and registers under sony.spc700") {
    const spc700 cpu;
    const auto md = cpu.metadata();
    CHECK(md.manufacturer == "Sony");
    CHECK(md.part_number == "SPC700");
    CHECK(md.klass == mnemos::chips::chip_class::cpu);

    auto chip = mnemos::chips::create_chip("sony.spc700");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::cpu);
    CHECK(chip->metadata().part_number == "SPC700");
}

TEST_CASE("spc700 resets to the documented power-on state") {
    machine m;
    const auto r = m.cpu.cpu_registers();
    CHECK(r.a == 0x00U);
    CHECK(r.x == 0x00U);
    CHECK(r.y == 0x00U);
    CHECK(r.sp == 0xEFU);                  // documented reset SP
    CHECK((r.psw & spc700::flag_i) != 0U); // IRQ disabled out of reset
    CHECK_FALSE(r.halted);
}

TEST_CASE("spc700 reset loads PC from the $FFFE/$FFFF vector") {
    machine m;
    m.ram[0xFFFE] = 0x00U;
    m.ram[0xFFFF] = 0x10U;
    m.cpu.reset(reset_kind::power_on);
    CHECK(m.cpu.cpu_registers().pc == 0x1000U);
}

TEST_CASE("spc700 loads immediates into A/X/Y") {
    machine m;
    m.load(0x0000U, {0xE8U, 0x42U,   // MOV A,#$42
                     0xCDU, 0x10U,   // MOV X,#$10
                     0x8DU, 0x80U}); // MOV Y,#$80
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().a == 0x42U);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().x == 0x10U);
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.y == 0x80U);
    CHECK((r.psw & spc700::flag_n) != 0U); // $80 is negative
}

TEST_CASE("spc700 ADC sets carry/half/overflow/zero flags") {
    machine m;
    m.load(0x0000U, {0xE8U, 0xFFU,   // MOV A,#$FF
                     0x88U, 0x01U}); // ADC A,#$01
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.a == 0x00U);                   // wrapped to 0
    CHECK((r.psw & spc700::flag_c) != 0U); // carry out
    CHECK((r.psw & spc700::flag_z) != 0U); // zero result
    CHECK((r.psw & spc700::flag_h) != 0U); // half-carry
}

TEST_CASE("spc700 SBC computes A - operand - !carry") {
    machine m;
    // SETC clears the borrow; SBC A,#$01 with A=$10 -> $0F, carry stays set.
    m.load(0x0000U, {0x80U,          // SETC
                     0xE8U, 0x10U,   // MOV A,#$10
                     0xA8U, 0x01U}); // SBC A,#$01
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.a == 0x0FU);
    CHECK((r.psw & spc700::flag_c) != 0U); // no borrow
}

TEST_CASE("spc700 direct-page store/load honours the P flag page") {
    machine m;
    m.load(0x0000U, {0xE8U, 0x99U,   // MOV A,#$99
                     0xC4U, 0x20U,   // MOV $20,A   (P=0 -> $0020)
                     0xE8U, 0x00U,   // MOV A,#$00
                     0xE4U, 0x20U}); // MOV A,$20   -> reloads $99
    for (int i = 0; i < 4; ++i) {
        m.cpu.step_instruction();
    }
    CHECK(m.ram[0x0020] == 0x99U);
    CHECK(m.cpu.cpu_registers().a == 0x99U);
}

TEST_CASE("spc700 takes absolute jumps") {
    machine m;
    m.load(0x0000U, {0x5FU, 0x00U, 0x20U}); // JMP $2000
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x2000U);
}

TEST_CASE("spc700 CALL pushes the return address and RET restores it") {
    machine m;
    m.load(0x0000U, {0x3FU, 0x00U, 0x10U}); // CALL $1000
    m.load(0x1000U, {0x6FU});               // RET
    m.cpu.step_instruction();
    auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x1000U);
    CHECK(r.sp == 0xEDU); // two bytes pushed from $EF
    // Return address ($0003) pushed high-then-low into page $01.
    CHECK(m.ram[0x01EF] == 0x00U); // high byte
    CHECK(m.ram[0x01EE] == 0x03U); // low byte

    m.cpu.step_instruction(); // RET
    r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x0003U);
    CHECK(r.sp == 0xEFU);
}

TEST_CASE("spc700 PUSH/POP move A through the page-$01 stack") {
    machine m;
    m.load(0x0000U, {0xE8U, 0x5AU, // MOV A,#$5A
                     0x2DU,        // PUSH A
                     0xE8U, 0x00U, // MOV A,#$00
                     0xAEU});      // POP A -> $5A
    for (int i = 0; i < 4; ++i) {
        m.cpu.step_instruction();
    }
    CHECK(m.ram[0x01EF] == 0x5AU);
    CHECK(m.cpu.cpu_registers().a == 0x5AU);
    CHECK(m.cpu.cpu_registers().sp == 0xEFU);
}

TEST_CASE("spc700 branches on the zero flag") {
    machine m;
    // MOV A,#$00 sets Z; BNE must NOT take, BEQ must take.
    m.load(0x0000U, {0xE8U, 0x00U,   // MOV A,#$00 (Z=1)
                     0xD0U, 0x10U,   // BNE +$10 (not taken)
                     0xF0U, 0x7CU}); // BEQ +$7C (taken)
    m.cpu.step_instruction();
    m.cpu.step_instruction(); // BNE
    CHECK(m.cpu.cpu_registers().pc == 0x0004U);
    m.cpu.step_instruction(); // BEQ from $0004, +$7C lands at $0006 + $7C
    CHECK(m.cpu.cpu_registers().pc == 0x0082U);
}

TEST_CASE("spc700 MOVW loads the virtual YA register from a dp word") {
    machine m;
    m.ram[0x0030] = 0x34U;           // low -> A
    m.ram[0x0031] = 0x12U;           // high -> Y
    m.load(0x0000U, {0xBAU, 0x30U}); // MOVW YA,$30
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.a == 0x34U);
    CHECK(r.y == 0x12U);
    CHECK(m.cpu.ya() == 0x1234U);
}

TEST_CASE("spc700 MUL YA multiplies Y by A into the YA pair") {
    machine m;
    m.load(0x0000U, {0x8DU, 0x10U, // MOV Y,#$10
                     0xE8U, 0x10U, // MOV A,#$10
                     0xCFU});      // MUL YA -> $0100
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.ya() == 0x0100U);
    CHECK(m.cpu.cpu_registers().y == 0x01U);
    CHECK(m.cpu.cpu_registers().a == 0x00U);
}

TEST_CASE("spc700 DIV by zero yields $FF and sets overflow") {
    machine m;
    m.load(0x0000U, {0x8DU, 0x01U, // MOV Y,#$01
                     0xE8U, 0x00U, // MOV A,#$00 (YA=$0100)
                     0xCDU, 0x00U, // MOV X,#$00 (divisor 0)
                     0x9EU});      // DIV YA,X
    for (int i = 0; i < 4; ++i) {
        m.cpu.step_instruction();
    }
    const auto r = m.cpu.cpu_registers();
    CHECK(r.a == 0xFFU);
    CHECK((r.psw & spc700::flag_v) != 0U);
}

TEST_CASE("spc700 tick catches up by whole instructions") {
    machine m;
    m.load(0x0000U, {0x00U, 0x00U, 0x00U, 0x00U}); // NOPs
    m.cpu.tick(6U);
    CHECK(m.cpu.elapsed_cycles() >= 6U);
    CHECK(m.cpu.cpu_registers().pc >= 0x0003U);
}

TEST_CASE("spc700 trace_target fires once per executed instruction") {
    machine m;
    m.load(0x0000U, {0x00U, 0x00U, 0x00U}); // three NOPs

    auto* trace = m.cpu.introspection().trace();
    REQUIRE(trace != nullptr);

    std::vector<mnemos::instrumentation::trace_event> events;
    trace->install(
        [&events](const mnemos::instrumentation::trace_event& ev) { events.push_back(ev); });

    m.cpu.step_instruction();
    m.cpu.step_instruction();
    m.cpu.step_instruction();

    REQUIRE(events.size() == 3U);
    CHECK(events[0].pc == 0x0000U);
    CHECK(events[1].pc == 0x0001U);
    CHECK(events[2].pc == 0x0002U);

    trace->install({});
    m.cpu.step_instruction();
    CHECK(events.size() == 3U);
}

TEST_CASE("spc700 register_view returns the live register snapshot") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.pc = 0xABCDU;
    r.a = 0x12U;
    m.cpu.set_registers(r);

    auto* regs = m.cpu.introspection().registers();
    REQUIRE(regs != nullptr);
    auto descriptors = regs->registers();
    REQUIRE(descriptors.size() == 7U);
    bool saw_pc = false;
    bool saw_a = false;
    for (const auto& d : descriptors) {
        if (d.name == "PC") {
            saw_pc = true;
            CHECK(d.value == 0xABCDU);
        }
        if (d.name == "A") {
            saw_a = true;
            CHECK(d.value == 0x12U);
        }
    }
    CHECK(saw_pc);
    CHECK(saw_a);
}

TEST_CASE("spc700 /RESET hold parks the CPU and release restarts from the vector") {
    machine m;
    m.ram[0xFFFE] = 0x00U;
    m.ram[0xFFFF] = 0x00U;
    m.cpu.reset(reset_kind::power_on);
    m.load(0x0000U, {0xE8U, 0x42U}); // MOV A,#$42
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().a == 0x42U);

    m.cpu.set_reset_line(true);
    CHECK(m.cpu.reset_line_held());
    CHECK(m.cpu.cpu_registers().pc == 0x0000U);

    // Held: ticks burn cycles without touching state.
    m.cpu.tick(32U);
    CHECK(m.cpu.cpu_registers().pc == 0x0000U);
    CHECK(m.cpu.cpu_registers().a != 0x42U);

    // Release: runs the program from the reset vector ($0000) again.
    m.cpu.set_reset_line(false);
    CHECK_FALSE(m.cpu.reset_line_held());
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().a == 0x42U);
}

TEST_CASE("spc700 save_state / load_state round-trips bit-identically") {
    machine m;
    // Drive the CPU into a non-trivial state.
    m.load(0x0000U, {0xE8U, 0x77U, // MOV A,#$77
                     0xCDU, 0x33U, // MOV X,#$33
                     0x8DU, 0x55U, // MOV Y,#$55
                     0x2DU});      // PUSH A
    for (int i = 0; i < 4; ++i) {
        m.cpu.step_instruction();
    }

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    m.cpu.save_state(writer);

    const auto before = m.cpu.cpu_registers();

    // Corrupt the live state, then restore from the blob.
    auto wrecked = before;
    wrecked.a = 0x00U;
    wrecked.x = 0x00U;
    wrecked.y = 0x00U;
    wrecked.sp = 0xFFU;
    wrecked.psw = 0x00U;
    wrecked.pc = 0xFFFFU;
    m.cpu.set_registers(wrecked);

    mnemos::chips::state_reader reader(blob);
    m.cpu.load_state(reader);
    CHECK(reader.ok());

    const auto after = m.cpu.cpu_registers();
    CHECK(after.a == before.a);
    CHECK(after.x == before.x);
    CHECK(after.y == before.y);
    CHECK(after.sp == before.sp);
    CHECK(after.psw == before.psw);
    CHECK(after.pc == before.pc);
    CHECK(after.halted == before.halted);
    CHECK(m.cpu.elapsed_cycles() > 0U);

    // A second save produces an identical blob (full bit-for-bit equivalence).
    std::vector<std::uint8_t> blob2;
    mnemos::chips::state_writer writer2(blob2);
    m.cpu.save_state(writer2);
    CHECK(blob2 == blob);
}
