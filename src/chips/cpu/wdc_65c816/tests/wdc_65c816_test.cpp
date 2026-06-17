#include "wdc_65c816.hpp"

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
    using mnemos::chips::cpu::wdc_65c816;
    using reset_kind = mnemos::chips::reset_kind;

    // A 24-bit flat address space backed by a single 16 MiB array, wired to the
    // topology bus the same way the z80 test wires its 64 KiB RAM. The reset
    // vector is preloaded so reset lands at a defined PC.
    struct machine final {
        std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(0x1000000U, 0U);
        mnemos::topology::bus bus{24U, mnemos::topology::endianness::little};
        wdc_65c816 cpu;

        explicit machine(std::uint16_t reset_vector = 0x0000U) {
            bus.map_ram(0x000000U, std::span<std::uint8_t>(ram), 0);
            // Preload the reset vector ($00FFFC/$00FFFD) before attach so the
            // CPU resets to a known program counter.
            ram[0x00FFFCU] = static_cast<std::uint8_t>(reset_vector & 0xFFU);
            ram[0x00FFFDU] = static_cast<std::uint8_t>(reset_vector >> 8U);
            cpu.attach_bus(bus);
            cpu.reset(reset_kind::power_on);
        }
        void load(std::uint32_t addr, std::initializer_list<std::uint8_t> bytes) {
            std::uint32_t a = addr;
            for (const std::uint8_t byte : bytes) {
                ram[a++] = byte;
            }
        }
    };
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::icpu, wdc_65c816>);

// (a) registry round-trip + metadata classification.
TEST_CASE("wdc_65c816 reports identity and registers under wdc.65c816") {
    const wdc_65c816 cpu;
    const auto md = cpu.metadata();
    CHECK(md.manufacturer == "WDC");
    CHECK(md.part_number == "65C816");
    CHECK(md.klass == mnemos::chips::chip_class::cpu);

    auto chip = mnemos::chips::create_chip("wdc.65c816");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == "65C816");
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::cpu);
}

// (b) reset to a defined state.
TEST_CASE("wdc_65c816 resets to the documented emulation-mode state") {
    machine m{0x8000U};
    const auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x8000U); // from the reset vector
    CHECK(r.e);             // emulation mode forced
    CHECK((r.p & wdc_65c816::flag_m) != 0U);
    CHECK((r.p & wdc_65c816::flag_x) != 0U);
    CHECK((r.p & wdc_65c816::flag_i) != 0U);
    CHECK(r.dbr == 0U);
    CHECK(r.pbr == 0U);
    CHECK(m.cpu.emulation());
    CHECK(m.cpu.a_is_8bit());
    CHECK(m.cpu.xy_is_8bit());
}

// (c) hand-assembled opcode execution against the mock bus.
TEST_CASE("wdc_65c816 loads an 8-bit immediate and sets N/Z") {
    machine m;
    m.load(0x0000U, {0xA9U, 0x42U}); // LDA #$42
    m.cpu.step_instruction();
    const auto r = m.cpu.cpu_registers();
    CHECK((r.a & 0xFFU) == 0x42U);
    CHECK((r.p & wdc_65c816::flag_z) == 0U);
    CHECK((r.p & wdc_65c816::flag_n) == 0U);
}

TEST_CASE("wdc_65c816 LDA #$00 sets the zero flag") {
    machine m;
    m.load(0x0000U, {0xA9U, 0x00U}); // LDA #$00
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().p & wdc_65c816::flag_z) != 0U);
}

TEST_CASE("wdc_65c816 XCE enters native mode and REP widens the accumulator") {
    machine m;
    // CLC ; XCE -> native (E=0). Then REP #$20 clears M -> A is 16-bit.
    // LDA #$1234 (16-bit immediate). STA $2000 stores both bytes.
    m.load(0x0000U, {0x18U,                 // CLC
                     0xFBU,                 // XCE
                     0xC2U, 0x20U,          // REP #$20  (clear M)
                     0xA9U, 0x34U, 0x12U,   // LDA #$1234
                     0x8DU, 0x00U, 0x20U}); // STA $2000
    m.cpu.step_instruction();               // CLC
    m.cpu.step_instruction();               // XCE
    CHECK_FALSE(m.cpu.emulation());
    m.cpu.step_instruction(); // REP #$20
    CHECK_FALSE(m.cpu.a_is_8bit());
    m.cpu.step_instruction(); // LDA #$1234
    CHECK(m.cpu.cpu_registers().a == 0x1234U);
    m.cpu.step_instruction(); // STA $2000
    CHECK(m.ram[0x2000U] == 0x34U);
    CHECK(m.ram[0x2001U] == 0x12U);
}

TEST_CASE("wdc_65c816 ADC produces carry and overflow in 8-bit mode") {
    machine m;
    // CLC ; LDA #$50 ; ADC #$50 -> $A0, V set (signed overflow), N set.
    m.load(0x0000U, {0x18U, 0xA9U, 0x50U, 0x69U, 0x50U});
    m.cpu.step_instruction(); // CLC
    m.cpu.step_instruction(); // LDA
    m.cpu.step_instruction(); // ADC
    const auto r = m.cpu.cpu_registers();
    CHECK((r.a & 0xFFU) == 0xA0U);
    CHECK((r.p & wdc_65c816::flag_v) != 0U);
    CHECK((r.p & wdc_65c816::flag_n) != 0U);
    CHECK((r.p & wdc_65c816::flag_c) == 0U);
}

TEST_CASE("wdc_65c816 branches and jumps move the program counter") {
    machine m;
    // LDA #$00 (sets Z) ; BEQ +2 ; (skipped LDA #$FF) ; landing LDA #$11
    m.load(0x0000U, {0xA9U, 0x00U,   // LDA #$00 -> Z=1
                     0xF0U, 0x02U,   // BEQ +2 (skip the next two bytes)
                     0xA9U, 0xFFU,   // LDA #$FF (should be skipped)
                     0xA9U, 0x11U}); // LDA #$11
    m.cpu.step_instruction();        // LDA #$00
    m.cpu.step_instruction();        // BEQ taken
    CHECK(m.cpu.cpu_registers().pc == 0x0006U);
    m.cpu.step_instruction(); // LDA #$11
    CHECK((m.cpu.cpu_registers().a & 0xFFU) == 0x11U);
}

TEST_CASE("wdc_65c816 JSR/RTS round-trips through the stack") {
    machine m;
    m.load(0x0000U, {0x20U, 0x00U, 0x10U}); // JSR $1000
    m.load(0x1000U, {0x60U});               // RTS
    m.cpu.step_instruction();               // JSR
    auto r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x1000U);
    m.cpu.step_instruction(); // RTS
    r = m.cpu.cpu_registers();
    CHECK(r.pc == 0x0003U); // returns past the JSR operand
}

TEST_CASE("wdc_65c816 PHA/PLA preserve the accumulator through the stack") {
    machine m;
    m.load(0x0000U, {0xA9U, 0x7EU, // LDA #$7E
                     0x48U,        // PHA
                     0xA9U, 0x00U, // LDA #$00
                     0x68U});      // PLA -> A back to $7E
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    m.cpu.step_instruction();
    CHECK((m.cpu.cpu_registers().a & 0xFFU) == 0x7EU);
}

TEST_CASE("wdc_65c816 services an NMI through the emulation vector") {
    machine m;
    m.ram[0x00FFFAU] = 0x00U; // NMI emulation vector low
    m.ram[0x00FFFBU] = 0x90U; // NMI emulation vector high -> $9000
    m.cpu.set_nmi_line(true);
    m.cpu.step_instruction();
    CHECK(m.cpu.cpu_registers().pc == 0x9000U);
    CHECK((m.cpu.cpu_registers().p & wdc_65c816::flag_i) != 0U);
}

TEST_CASE("wdc_65c816 tick catches up by whole instructions") {
    machine m;
    m.load(0x0000U, {0xEAU, 0xEAU, 0xEAU, 0xEAU}); // NOPs (2 cycles each)
    m.cpu.tick(6U);
    CHECK(m.cpu.elapsed_cycles() >= 6U);
    CHECK(m.cpu.cpu_registers().pc >= 0x0003U);
}

// (d) save_state / load_state round-trips bit-identically.
TEST_CASE("wdc_65c816 save_state/load_state round-trips bit-identically") {
    machine m;
    // Drive the CPU into native mode with a non-trivial register state.
    m.load(0x0000U, {0x18U, 0xFBU,          // CLC ; XCE -> native
                     0xC2U, 0x30U,          // REP #$30 (clear M and X -> 16-bit A/X/Y)
                     0xA9U, 0xCDU, 0xABU,   // LDA #$ABCD
                     0xA2U, 0x34U, 0x12U,   // LDX #$1234
                     0xA0U, 0x78U, 0x56U}); // LDY #$5678
    for (int i = 0; i < 6; ++i) {
        m.cpu.step_instruction();
    }
    const auto before = m.cpu.cpu_registers();
    CHECK(before.a == 0xABCDU);
    CHECK(before.x == 0x1234U);
    CHECK(before.y == 0x5678U);
    CHECK_FALSE(before.e);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    m.cpu.save_state(writer);

    // Mutate the live CPU so a failed reload would be visible.
    wdc_65c816 other;
    other.attach_bus(m.bus);
    const std::span<const std::uint8_t> blob_span{blob};
    mnemos::chips::state_reader reader(blob_span);
    other.load_state(reader);
    CHECK(reader.ok());

    const auto after = other.cpu_registers();
    CHECK(after.a == before.a);
    CHECK(after.x == before.x);
    CHECK(after.y == before.y);
    CHECK(after.d == before.d);
    CHECK(after.s == before.s);
    CHECK(after.pc == before.pc);
    CHECK(after.pbr == before.pbr);
    CHECK(after.dbr == before.dbr);
    CHECK(after.p == before.p);
    CHECK(after.e == before.e);
    CHECK(after.halted == before.halted);
    CHECK(other.elapsed_cycles() == m.cpu.elapsed_cycles());

    // Re-serialising the reloaded state must reproduce the byte stream exactly.
    std::vector<std::uint8_t> blob2;
    mnemos::chips::state_writer writer2(blob2);
    other.save_state(writer2);
    CHECK(blob2 == blob);
}

TEST_CASE("wdc_65c816 register_view exposes the live register snapshot") {
    machine m;
    auto r = m.cpu.cpu_registers();
    r.pc = 0xBEEFU;
    r.a = 0x1234U;
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
            CHECK(d.value == 0xBEEFU);
        }
        if (d.name == "A") {
            saw_a = true;
            CHECK(d.value == 0x1234U);
        }
    }
    CHECK(saw_pc);
    CHECK(saw_a);
}
