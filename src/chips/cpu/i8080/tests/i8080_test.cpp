#include "i8080.hpp"

#include "bus.hpp"
#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <type_traits>
#include <vector>

namespace {
    using i8080 = mnemos::chips::cpu::i8080;
    using reset_kind = mnemos::chips::reset_kind;

    struct machine final {
        std::array<std::uint8_t, 0x10000> ram{};
        mnemos::topology::bus bus{16U, mnemos::topology::endianness::little};
        i8080 cpu;

        machine() {
            bus.map_ram(0x0000U, std::span<std::uint8_t>(ram), 0);
            cpu.attach_bus(bus);
            cpu.reset(reset_kind::power_on);
        }

        void load(std::uint16_t address, std::initializer_list<std::uint8_t> bytes) {
            std::uint16_t cursor = address;
            for (const std::uint8_t byte : bytes) {
                ram[cursor++] = byte;
            }
        }
    };

    struct split_bus final : mnemos::chips::ibus {
        std::array<std::uint8_t, 0x10000> opcode{};
        std::array<std::uint8_t, 0x10000> data{};

        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            return data[address & 0xFFFFU];
        }

        void write8(std::uint32_t address, std::uint8_t value) override {
            data[address & 0xFFFFU] = value;
        }

        [[nodiscard]] std::uint8_t fetch_opcode8(std::uint32_t address) override {
            return opcode[address & 0xFFFFU];
        }
    };
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::icpu, i8080>);

TEST_CASE("i8080 reports Intel 8080 and 8085 identities") {
    i8080 cpu;
    auto md = cpu.metadata();
    CHECK(md.manufacturer == "Intel");
    CHECK(md.part_number == "8080A");
    CHECK(md.family == "8080");
    CHECK(md.klass == mnemos::chips::chip_class::cpu);

    cpu.set_variant(i8080::variant::intel_8085);
    CHECK(cpu.metadata().part_number == "8085A");

    auto chip_8080 = mnemos::chips::create_chip("intel.i8080");
    REQUIRE(chip_8080 != nullptr);
    CHECK(chip_8080->metadata().part_number == "8080A");

    auto chip_8085 = mnemos::chips::create_chip("intel.i8085");
    REQUIRE(chip_8085 != nullptr);
    CHECK(chip_8085->metadata().part_number == "8085A");
}

TEST_CASE("i8080 resets to the documented fetch state") {
    machine m;
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x0000U);
    CHECK(r.sp == 0xFFFFU);
    CHECK(r.a == 0x00U);
    CHECK(r.f == i8080::flag_const);
    CHECK_FALSE(r.interrupts_enabled);
    CHECK_FALSE(r.halted);
}

TEST_CASE("i8080 fetches opcodes from fetch_opcode8 and operands from read8") {
    split_bus bus;
    i8080 cpu;
    cpu.attach_bus(bus);
    cpu.reset(reset_kind::power_on);

    bus.opcode[0x0000U] = 0x3EU; // MVI A,n from opcode stream.
    bus.opcode[0x0001U] = 0xFFU;
    bus.data[0x0000U] = 0x00U;
    bus.data[0x0001U] = 0x42U;

    cpu.step_instruction();
    CHECK(cpu.cpu_registers().a == 0x42U);
    CHECK(cpu.cpu_registers().pc == 0x0002U);
}

TEST_CASE("i8080 executes load, store, and register transfer instructions") {
    machine m;
    m.load(0x0000U, {0x3EU, 0x42U,       // MVI A,$42
                     0x32U, 0x00U, 0x20U, // STA $2000
                     0x06U, 0x10U,       // MVI B,$10
                     0x78U});            // MOV A,B

    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().a == 0x42U);
    m.cpu.step_instruction();
    CHECK(m.ram[0x2000U] == 0x42U);
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().bc >> 8U) == 0x10U);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().a == 0x10U);
}

TEST_CASE("i8080 arithmetic sets carry, zero, sign, and parity flags") {
    machine m;
    m.load(0x0000U, {0x3EU, 0xFFU, // MVI A,$FF
                     0xC6U, 0x01U, // ADI $01
                     0xD6U, 0x01U}); // SUI $01

    m.cpu.step_instruction();
    m.cpu.step_instruction();
    auto r = m.cpu.cpu_registers();
    CHECK(r.a == 0x00U);
    CHECK((r.f & i8080::flag_c) != 0U);
    CHECK((r.f & i8080::flag_z) != 0U);
    CHECK((r.f & i8080::flag_p) != 0U);

    m.cpu.step_instruction();
    r = m.cpu.cpu_registers();
    CHECK(r.a == 0xFFU);
    CHECK((r.f & i8080::flag_c) != 0U);
    CHECK((r.f & i8080::flag_s) != 0U);
}

TEST_CASE("i8080 calls and returns through the stack") {
    machine m;
    m.load(0x0000U, {0xCDU, 0x00U, 0x10U}); // CALL $1000
    m.load(0x1000U, {0xC9U});               // RET

    m.cpu.step_instruction();
    auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x1000U);
    CHECK(r.sp == 0xFFFDU);
    CHECK(m.ram[0xFFFD] == 0x03U);
    CHECK(m.ram[0xFFFE] == 0x00U);

    m.cpu.step_instruction();
    r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x0003U);
    CHECK(r.sp == 0xFFFFU);
}

TEST_CASE("i8080 routes IN and OUT through port callbacks") {
    machine m;
    std::uint8_t out_port = 0U;
    std::uint8_t out_value = 0U;
    m.cpu.set_port_in([](std::uint8_t port) {
        return port == 0x12U ? std::uint8_t{0x5AU} : std::uint8_t{0xFFU};
    });
    m.cpu.set_port_out([&](std::uint8_t port, std::uint8_t value) {
        out_port = port;
        out_value = value;
    });
    m.load(0x0000U, {0xDBU, 0x12U, // IN $12
                     0xD3U, 0x34U}); // OUT $34

    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().a == 0x5AU);
    m.cpu.step_instruction();
    CHECK(out_port == 0x34U);
    CHECK(out_value == 0x5AU);
}

TEST_CASE("i8080 services maskable interrupts after EI delay") {
    machine m;
    m.load(0x0000U, {0xFBU, 0x00U, 0x76U}); // EI ; NOP ; HLT
    m.cpu.set_irq_vector([] { return std::uint8_t{0xCFU}; }); // RST 1 -> $0008.

    m.cpu.step_instruction();
    m.cpu.set_irq_line(true);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x0002U);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x0008U);
    CHECK(m.ram[0xFFFD] == 0x02U);
    CHECK(m.ram[0xFFFE] == 0x00U);
    CHECK_FALSE(m.cpu.cpu_registers().interrupts_enabled);
}

TEST_CASE("i8085 executes RIM and SIM compatibility instructions") {
    machine m;
    m.cpu.set_variant(i8080::variant::intel_8085);
    m.cpu.set_8085_interrupt_pending(0x05U);
    m.load(0x0000U, {0xFBU,       // EI
                     0x20U,       // RIM
                     0x3EU, 0xC8U, // MVI A,$C8: mask-set enable + serial data enable/data=1
                     0x30U});      // SIM

    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().a & 0x0FU) == 0x0DU);
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK(m.cpu.serial_output() == 1U);
}

TEST_CASE("i8080 save state restores registers, variant, and cycle accounting") {
    machine source;
    source.cpu.set_variant(i8080::variant::intel_8085);
    source.load(0x0000U, {0x3EU, 0x42U, 0x06U, 0x99U});
    source.cpu.step_instruction();
    source.cpu.step_instruction();

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source.cpu.save_state(writer);

    machine restored;
    mnemos::chips::state_reader reader(snapshot);
    restored.cpu.load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored.cpu.cpu_variant() == i8080::variant::intel_8085);
    CHECK(restored.cpu.cpu_registers().a == 0x42U);
    CHECK((restored.cpu.cpu_registers().bc >> 8U) == 0x99U);
    CHECK(restored.cpu.cpu_registers().pc == source.cpu.cpu_registers().pc);
    CHECK(restored.cpu.elapsed_cycles() == source.cpu.elapsed_cycles());
}
