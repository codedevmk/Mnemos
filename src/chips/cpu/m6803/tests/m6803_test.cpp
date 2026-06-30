#include "m6803.hpp"

#include "bus.hpp"
#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {
    struct machine final {
        std::array<std::uint8_t, 0x10000U> memory{};
        mnemos::topology::bus bus{16U, mnemos::topology::endianness::big};
        mnemos::chips::cpu::m6803 cpu;

        machine() {
            bus.map_ram(0x0000U, memory);
            cpu.attach_bus(bus);
        }

        void set_reset_vector(std::uint16_t address) {
            memory[0xFFFEU] = static_cast<std::uint8_t>(address >> 8U);
            memory[0xFFFFU] = static_cast<std::uint8_t>(address);
        }

        void load(std::uint16_t address, std::span<const std::uint8_t> bytes) {
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                memory[static_cast<std::uint16_t>(address + i)] = bytes[i];
            }
        }
    };

    void run_instructions(machine& m, int count) {
        for (int i = 0; i < count; ++i) {
            static_cast<void>(m.cpu.step_instruction());
        }
    }
} // namespace

TEST_CASE("m6803 reset loads the big-endian reset vector", "[m6803]") {
    machine m;
    m.set_reset_vector(0xFA80U);
    m.cpu.reset(mnemos::chips::reset_kind::power_on);

    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0xFA80U);
    CHECK(r.sp == 0x00FFU);
    CHECK((r.ccr & mnemos::chips::cpu::m6803::flag_i) != 0U);
}

TEST_CASE("m6803 publishes metadata factory and register introspection", "[m6803]") {
    auto created = mnemos::chips::create_chip("motorola.m6803");
    REQUIRE(created != nullptr);
    CHECK(created->metadata().part_number == "MC6803");
    CHECK(created->metadata().klass == mnemos::chips::chip_class::cpu);

    auto* regs = created->introspection().registers();
    REQUIRE(regs != nullptr);
    const auto snapshot = regs->registers();
    REQUIRE(snapshot.size() == 6U);
    CHECK(snapshot[0].name == "A");
    CHECK(snapshot[4].name == "PC");
}

TEST_CASE("m6803 executes load store and conditional branch instructions", "[m6803]") {
    machine m;
    m.set_reset_vector(0x8000U);
    const std::uint8_t program[] = {
        0x86U, 0x42U,       // LDAA #$42
        0x97U, 0x10U,       // STAA <$10
        0xC6U, 0x02U,       // LDAB #$02
        0x5AU,             // DECB
        0x26U, 0xFDU,       // BNE back to DECB
        0xF7U, 0x81U, 0x00U // STAB $8100
    };
    m.load(0x8000U, program);

    m.cpu.reset(mnemos::chips::reset_kind::power_on);
    run_instructions(m, 7);

    const auto r = m.cpu.cpu_registers();
    CHECK(m.memory[0x0010U] == 0x42U);
    CHECK(m.memory[0x8100U] == 0x00U);
    CHECK((r.ccr & mnemos::chips::cpu::m6803::flag_z) != 0U);
}

TEST_CASE("m6803 indexed addressing and 16-bit X transfers work", "[m6803]") {
    machine m;
    m.set_reset_vector(0x9000U);
    const std::uint8_t program[] = {
        0xCEU, 0x20U, 0x00U, // LDX #$2000
        0x86U, 0xA5U,       // LDAA #$A5
        0xA7U, 0x10U,       // STAA $10,X
        0xFEU, 0x90U, 0x10U, // LDX $9010
        0xEFU, 0x04U        // STX $04,X
    };
    m.load(0x9000U, program);
    m.memory[0x9010U] = 0x30U;
    m.memory[0x9011U] = 0x00U;

    m.cpu.reset(mnemos::chips::reset_kind::power_on);
    run_instructions(m, 5);

    CHECK(m.memory[0x2010U] == 0xA5U);
    CHECK(m.memory[0x3004U] == 0x30U);
    CHECK(m.memory[0x3005U] == 0x00U);
}

TEST_CASE("m6803 JSR and RTS round-trip through the stack", "[m6803]") {
    machine m;
    m.set_reset_vector(0x8000U);
    const std::uint8_t program[] = {
        0x8EU, 0x01U, 0xFFU, // LDS #$01FF
        0xBDU, 0x81U, 0x00U, // JSR $8100
        0x97U, 0x20U        // STAA <$20
    };
    const std::uint8_t subroutine[] = {
        0x86U, 0x5AU, // LDAA #$5A
        0x39U        // RTS
    };
    m.load(0x8000U, program);
    m.load(0x8100U, subroutine);

    m.cpu.reset(mnemos::chips::reset_kind::power_on);
    run_instructions(m, 5);

    const auto r = m.cpu.cpu_registers();
    CHECK(m.memory[0x0020U] == 0x5AU);
    CHECK(r.sp == 0x01FFU);
}

TEST_CASE("m6803 arithmetic updates carry overflow zero and negative flags", "[m6803]") {
    machine m;
    m.set_reset_vector(0x8200U);
    const std::uint8_t program[] = {
        0x86U, 0x7FU, // LDAA #$7F
        0x8BU, 0x01U, // ADDA #$01 -> $80, V set
        0xC6U, 0x00U, // LDAB #$00
        0xC0U, 0x01U  // SUBB #$01 -> $FF, C/N set
    };
    m.load(0x8200U, program);

    m.cpu.reset(mnemos::chips::reset_kind::power_on);
    run_instructions(m, 2);
    CHECK((m.cpu.cpu_registers().ccr & mnemos::chips::cpu::m6803::flag_v) != 0U);
    run_instructions(m, 2);

    const auto r = m.cpu.cpu_registers();
    CHECK(r.a == 0x80U);
    CHECK(r.b == 0xFFU);
    CHECK((r.ccr & mnemos::chips::cpu::m6803::flag_c) != 0U);
    CHECK((r.ccr & mnemos::chips::cpu::m6803::flag_n) != 0U);
    CHECK((r.ccr & mnemos::chips::cpu::m6803::flag_z) == 0U);
}

TEST_CASE("m6803 save-state preserves registers and cycle pacing", "[m6803]") {
    machine source;
    source.set_reset_vector(0x8300U);
    const std::uint8_t program[] = {0x86U, 0x11U, 0xC6U, 0x22U, 0xCEU, 0x33U, 0x44U};
    source.load(0x8300U, program);
    source.cpu.reset(mnemos::chips::reset_kind::power_on);
    run_instructions(source, 3);

    std::vector<std::uint8_t> state;
    mnemos::chips::state_writer writer(state);
    source.cpu.save_state(writer);

    machine restored;
    restored.cpu.attach_bus(restored.bus);
    mnemos::chips::state_reader reader(state);
    restored.cpu.load_state(reader);

    REQUIRE(reader.ok());
    const auto a = source.cpu.cpu_registers();
    const auto b = restored.cpu.cpu_registers();
    CHECK(b.a == a.a);
    CHECK(b.b == a.b);
    CHECK(b.x == a.x);
    CHECK(b.pc == a.pc);
    CHECK(restored.cpu.elapsed_cycles() == source.cpu.elapsed_cycles());
}

TEST_CASE("m6803 /RESET hold parks the CPU and release restarts from the reset vector",
          "[m6803]") {
    machine m;
    m.set_reset_vector(0x8400U);
    const std::uint8_t program[] = {0x86U, 0x42U, 0x97U, 0x20U}; // LDAA #$42 ; STAA <$20
    m.load(0x8400U, program);

    m.cpu.reset(mnemos::chips::reset_kind::power_on);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().a == 0x42U);

    m.cpu.set_reset_line(true);
    CHECK(m.cpu.reset_line_held());
    CHECK(m.cpu.cpu_registers().pc == 0x8400U);

    m.cpu.tick(64U);
    CHECK(m.memory[0x0020U] == 0x00U);
    CHECK(m.cpu.cpu_registers().pc == 0x8400U);

    m.cpu.set_reset_line(false);
    CHECK_FALSE(m.cpu.reset_line_held());
    run_instructions(m, 2);
    CHECK(m.memory[0x0020U] == 0x42U);
}
