#include "sh2.hpp"

#include "bus.hpp"
#include "chip_registry.hpp"
#include "introspection_views.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::cpu::sh2;
    using reset_kind = mnemos::chips::reset_kind;

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
        void load(std::uint32_t a, std::initializer_list<std::uint16_t> words) {
            for (const std::uint16_t w : words) {
                w16(a, w);
                a += 2U;
            }
        }
        void set_pc(std::uint32_t pc) {
            auto r = cpu.cpu_registers();
            r.pc = pc;
            cpu.set_registers(r);
        }
    };
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::icpu, sh2>);

TEST_CASE("sh2 reports identity and registers under hitachi.sh7604") {
    const sh2 cpu;
    const auto md = cpu.metadata();
    CHECK(md.manufacturer == "Hitachi");
    CHECK(md.part_number == "SH7604");
    CHECK(md.family == "SH-2");
    CHECK(md.klass == mnemos::chips::chip_class::cpu);

    auto chip = mnemos::chips::create_chip("hitachi.sh7604");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == std::string("SH7604"));
}

TEST_CASE("sh2 resets PC and R15 from the big-endian vector table at VBR=0") {
    machine m;
    m.w32(0x0000U, 0x06000000U); // reset PC
    m.w32(0x0004U, 0x0603FFFCU); // initial SP -> R15
    m.cpu.reset(reset_kind::power_on);

    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x06000000U);
    CHECK(r.r[15] == 0x0603FFFCU);
    CHECK(r.sr == sh2::sr_imask); // interrupts masked out of reset
}

TEST_CASE("sh2 MOV #imm,Rn sign-extends the byte immediate") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xE37FU, 0xE4FFU}); // MOV #0x7F,R3 ; MOV #-1,R4
    CHECK(m.cpu.step_instruction() == 1);
    CHECK(m.cpu.cpu_registers().r[3] == 0x0000007FU);
    CHECK(m.cpu.step_instruction() == 1);
    CHECK(m.cpu.cpu_registers().r[4] == 0xFFFFFFFFU);
}

TEST_CASE("sh2 ADD #imm,Rn adds a sign-extended immediate") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xE205U, 0x72FFU}); // MOV #5,R2 ; ADD #-1,R2
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().r[2] == 4U);
}

TEST_CASE("sh2 MOV Rm,Rn copies one register to another") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xE142U, 0x6513U}); // MOV #0x42,R1 ; MOV R1,R5
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK(r.r[5] == 0x42U);
    CHECK(r.r[1] == 0x42U);
}

TEST_CASE("sh2 advances PC by two per 16-bit instruction") {
    machine m;
    m.set_pc(0x2000U);
    m.load(0x2000U, {0x0009U, 0x0009U}); // NOP ; NOP
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x2002U);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x2004U);
}

TEST_CASE("sh2 round-trips its state") {
    machine m;
    m.set_pc(0x1000U);
    m.load(0x1000U, {0xE309U}); // MOV #9,R3
    m.cpu.step_instruction();

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    m.cpu.save_state(writer);

    sh2 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.cpu_registers().r[3] == 9U);
    CHECK(restored.cpu_registers().pc == 0x1002U);
}

TEST_CASE("sh2 exposes its register file and trace hook via introspection") {
    machine m;
    auto& intro = m.cpu.introspection();

    auto* rv = intro.registers();
    REQUIRE(rv != nullptr);
    CHECK_FALSE(rv->registers().empty());

    auto* tt = intro.trace();
    REQUIRE(tt != nullptr);

    std::vector<std::uint32_t> seen;
    tt->install([&](const mnemos::instrumentation::trace_event& ev) { seen.push_back(ev.pc); });
    m.set_pc(0x3000U);
    m.load(0x3000U, {0x0009U}); // NOP
    m.cpu.step_instruction();
    REQUIRE(seen.size() == 1U);
    CHECK(seen[0] == 0x3000U);

    tt->install({}); // empty clears
    m.cpu.step_instruction();
    CHECK(seen.size() == 1U);
}
