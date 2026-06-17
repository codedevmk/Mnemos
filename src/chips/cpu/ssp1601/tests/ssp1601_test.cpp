#include "ssp1601.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
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
    using mnemos::chips::cpu::ssp1601;
    using reset_kind = mnemos::chips::reset_kind;

    // A minimal word-addressed bus: 16-bit words exposed big-endian through the
    // byte ibus contract. The DSP addresses words (bus byte address = word << 1).
    class word_bus final : public mnemos::chips::ibus {
      public:
        std::array<std::uint16_t, 0x10000> mem{};

        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            const std::uint16_t w = mem[(address >> 1U) & 0xFFFFU];
            return (address & 1U) != 0U ? static_cast<std::uint8_t>(w)
                                        : static_cast<std::uint8_t>(w >> 8U);
        }
        void write8(std::uint32_t address, std::uint8_t value) override {
            std::uint16_t& w = mem[(address >> 1U) & 0xFFFFU];
            if ((address & 1U) != 0U) {
                w = static_cast<std::uint16_t>((w & 0xFF00U) | value);
            } else {
                w = static_cast<std::uint16_t>((w & 0x00FFU) | (value << 8U));
            }
        }
    };

    struct machine final {
        word_bus bus;
        ssp1601 cpu;

        machine() {
            cpu.attach_bus(bus);
            cpu.reset(reset_kind::power_on);
        }
        // Load a sequence of 16-bit program words at a word address.
        void load(std::uint16_t word_addr, std::initializer_list<std::uint16_t> words) {
            std::uint16_t a = word_addr;
            for (const std::uint16_t w : words) {
                bus.mem[a++] = w;
            }
        }
    };
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::icpu, ssp1601>);

TEST_CASE("ssp1601 reports identity and registers under samsung.ssp1601") {
    const ssp1601 cpu;
    const auto md = cpu.metadata();
    CHECK(md.manufacturer == "Samsung");
    CHECK(md.part_number == "SSP1601");
    CHECK(md.klass == mnemos::chips::chip_class::cpu);

    auto chip = mnemos::chips::create_chip("samsung.ssp1601");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == "SSP1601");
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::cpu);
}

TEST_CASE("ssp1601 resets to a defined state") {
    machine m;
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x0000U);
    CHECK(r.sp == 0U);
    CHECK(r.a == 0U);
    CHECK(r.p == 0U);
    CHECK(r.st == 0U);
    CHECK(r.r[6] == 0x00FCU); // SVP boot sentinel
    CHECK_FALSE(r.halted);
}

TEST_CASE("ssp1601 loads an immediate into a general register") {
    machine m;
    // LD X, $1234   (group 2, dst = X(1)) ; LD Y, $0005
    m.load(0x0000U, {0x2001U, 0x1234U, 0x2002U, 0x0005U});
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().x == 0x1234U);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().y == 0x0005U);
    CHECK(m.cpu.cpu_registers().pc == 0x0004U);
}

TEST_CASE("ssp1601 moves data between general registers") {
    machine m;
    // LD X, $00AB ; LD Y, X   (group 1, dst=Y(2) src=X(1) -> 0001 0000 0010 0001)
    m.load(0x0000U, {0x2001U, 0x00ABU, 0x1021U});
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().y == 0x00ABU);
}

TEST_CASE("ssp1601 ALU ADD against an immediate sets the accumulator and flags") {
    machine m;
    // LD A, imm  via ALU-LD (op 6) ; ADD A, imm
    // ALU A, imm : group 4, aop in bits 8..10.
    // LD (aop=6): 0100 0110 0000 0000 = 0x4600 ; operand 0x0001
    // ADD(aop=0): 0x4000 ; operand 0x0002
    m.load(0x0000U, {0x4600U, 0x0001U, 0x4000U, 0x0002U});
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().a == 0x00010000U); // operand lands in the high word
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().a == 0x00030000U);
    CHECK((m.cpu.cpu_registers().st & ssp1601::flag_z) == 0U);
    CHECK((m.cpu.cpu_registers().st & ssp1601::flag_n) == 0U);
}

TEST_CASE("ssp1601 ALU SUB to zero sets the zero flag") {
    machine m;
    // LD A, 0x0007 (aop=6) ; SUB A, 0x0007 (aop=1 -> 0x4100)
    m.load(0x0000U, {0x4600U, 0x0007U, 0x4100U, 0x0007U});
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().a == 0U);
    CHECK((m.cpu.cpu_registers().st & ssp1601::flag_z) != 0U);
}

TEST_CASE("ssp1601 CMP preserves the accumulator but updates flags") {
    machine m;
    // LD A, 0x0004 ; CMP A, 0x0004 (aop=2 -> 0x4200)
    m.load(0x0000U, {0x4600U, 0x0004U, 0x4200U, 0x0004U});
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().a == 0x00040000U); // unchanged
    CHECK((m.cpu.cpu_registers().st & ssp1601::flag_z) != 0U);
}

TEST_CASE("ssp1601 multiplies X by Y into P and moves P to A") {
    machine m;
    // LD X, 6 ; LD Y, 7 ; P = X*Y (0x5000) ; A = P (0x5001)
    m.load(0x0000U, {0x2001U, 0x0006U, 0x2002U, 0x0007U, 0x5000U, 0x5001U});
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().p == 42U);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().a == 42U);
}

TEST_CASE("ssp1601 signed multiply produces a negative product") {
    machine m;
    // LD X, 0xFFFF (-1) ; LD Y, 0x0002 (2) ; P = X*Y -> -2
    m.load(0x0000U, {0x2001U, 0xFFFFU, 0x2002U, 0x0002U, 0x5000U});
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().p == 0xFFFFFFFEU); // -2 in two's complement
}

TEST_CASE("ssp1601 CALL pushes the return address and RET pops it") {
    machine m;
    // CALL $0040 (group 6, cond=always, bit4=CALL -> 0x6010) ; (target word)
    // at $0040: RET (0x6F00)
    m.load(0x0000U, {0x6010U, 0x0040U});
    m.load(0x0040U, {0x6F00U});
    m.cpu.step_instruction(); // CALL
    auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x0040U);
    CHECK(r.sp == 1U);
    CHECK(r.stack[0] == 0x0002U); // return address (after the 2-word CALL)

    m.cpu.step_instruction(); // RET
    r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x0002U);
    CHECK(r.sp == 0U);
}

TEST_CASE("ssp1601 conditional jump is taken only when the condition holds") {
    machine m;
    // LD A,0 via ALU-LD sets Z. Then JMP Z,$0030 (cond_z=1, no CALL -> 0x6100).
    // not-taken case: JMP NZ,$0030 (cond_nz=2 -> 0x6200) should fall through.
    m.load(0x0000U, {0x4600U, 0x0000U, // A = 0 -> Z set
                     0x6100U, 0x0030U, // JMP Z, $0030  (taken)
                     0x0000U});
    m.load(0x0030U, {0x6200U, 0x0050U, // JMP NZ, $0050 (Z still set -> not taken)
                     0x0000U});
    m.cpu.step_instruction(); // A=0
    CHECK((m.cpu.cpu_registers().st & ssp1601::flag_z) != 0U);
    m.cpu.step_instruction(); // JMP Z taken
    CHECK(m.cpu.cpu_registers().pc == 0x0030U);
    m.cpu.step_instruction(); // JMP NZ not taken -> falls through to $0032
    CHECK(m.cpu.cpu_registers().pc == 0x0032U);
}

TEST_CASE("ssp1601 loads the accumulator from a pointer register") {
    machine m;
    // r0 -> $0100, where a value lives. LD A,(r0) is group 7, load form 0x7000.
    auto r = m.cpu.cpu_registers();
    r.r[0] = 0x0100U;
    m.cpu.set_registers(r);
    m.bus.mem[0x0100] = 0xBEEFU;
    m.load(0x0000U, {0x7000U});
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().a == 0xBEEF0000U);
}

TEST_CASE("ssp1601 stores the accumulator high word through a pointer register") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.r[1] = 0x0200U;
    r.a = 0xCAFE1234U;
    m.cpu.set_registers(r);
    // LD (r1), A : store form (bit 11) + reg 1 -> 0x7801
    m.load(0x0000U, {0x7801U});
    m.cpu.step_instruction();
    CHECK(m.bus.mem[0x0200] == 0xCAFEU); // high word of A
}

TEST_CASE("ssp1601 HALT parks the DSP") {
    machine m;
    m.load(0x0000U, {0xFFFFU, 0x0000U});
    m.cpu.step_instruction();
    CHECK(m.cpu.halted());
    const auto pc = m.cpu.cpu_registers().pc;
    m.cpu.step_instruction(); // halted: burns a cycle, no fetch
    CHECK(m.cpu.cpu_registers().pc == pc);
}

TEST_CASE("ssp1601 tick catches up by whole instructions") {
    machine m;
    m.load(0x0000U, {0x0000U, 0x0000U, 0x0000U, 0x0000U}); // NOPs (1 cycle each)
    m.cpu.tick(3U);
    CHECK(m.cpu.elapsed_cycles() >= 3U);
    CHECK(m.cpu.cpu_registers().pc >= 0x0003U);
}

TEST_CASE("ssp1601 trace fires once per executed instruction with pc") {
    machine m;
    m.load(0x0000U, {0x0000U, 0x0000U, 0x0000U}); // three NOPs

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
    CHECK(events[0].cycles == 0U);
    CHECK(events[1].cycles == 1U);
    CHECK(events[2].cycles == 2U);
}

TEST_CASE("ssp1601 register_view exposes the live register snapshot") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.pc = 0x1357U;
    r.a = 0xABCD1234U;
    m.cpu.set_registers(r);

    auto* regs = m.cpu.introspection().registers();
    REQUIRE(regs != nullptr);
    auto descriptors = regs->registers();
    REQUIRE(descriptors.size() == 11U);
    bool saw_pc = false;
    bool saw_a = false;
    for (const auto& d : descriptors) {
        if (d.name == "PC") {
            saw_pc = true;
            CHECK(d.value == 0x1357U);
        }
        if (d.name == "A") {
            saw_a = true;
            CHECK(d.value == 0xABCD1234U);
        }
    }
    CHECK(saw_pc);
    CHECK(saw_a);
}

TEST_CASE("ssp1601 save_state / load_state round-trips bit-identically") {
    machine m;
    // Run a few instructions to dirty state, plus seed IRAM.
    m.cpu.iram()[0] = 0x55AAU;
    m.cpu.iram()[1023] = 0x1234U;
    m.load(0x0000U, {0x2001U, 0xDEADU, 0x5000U, 0x6010U, 0x0040U});
    m.cpu.step_instruction(); // LD X
    m.cpu.step_instruction(); // P = X*Y
    m.cpu.step_instruction(); // CALL

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    m.cpu.save_state(writer);

    const auto before = m.cpu.cpu_registers();

    // Mutate, then restore and compare.
    ssp1601 other;
    other.attach_bus(m.bus);
    mnemos::chips::state_reader reader(blob);
    other.load_state(reader);
    CHECK(reader.ok());

    const auto after = other.cpu_registers();
    CHECK(after.x == before.x);
    CHECK(after.y == before.y);
    CHECK(after.a == before.a);
    CHECK(after.p == before.p);
    CHECK(after.st == before.st);
    CHECK(after.pc == before.pc);
    CHECK(after.sp == before.sp);
    CHECK(after.stack == before.stack);
    CHECK(after.r == before.r);
    CHECK(after.halted == before.halted);
    CHECK(other.iram()[0] == 0x55AAU);
    CHECK(other.iram()[1023] == 0x1234U);
    CHECK(other.elapsed_cycles() == m.cpu.elapsed_cycles());

    // Re-saving the restored chip yields a byte-identical blob.
    std::vector<std::uint8_t> blob2;
    mnemos::chips::state_writer writer2(blob2);
    other.save_state(writer2);
    CHECK(blob2 == blob);
}
