#include "m6510.hpp"

#include "chip_registry.hpp"
#include "ibus.hpp"
#include "introspection_views.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <vector>

namespace {

    using mnemos::chips::cpu::m6510;

    class flat_ram final : public mnemos::chips::ibus {
      public:
        std::array<std::uint8_t, 0x10000U> memory{};

        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            return memory[address & 0xFFFFU];
        }

        void write8(std::uint32_t address, std::uint8_t value) override {
            memory[address & 0xFFFFU] = value;
        }
    };

    struct test_system final {
        flat_ram bus;
        m6510 cpu;

        // Load `program` at `origin`, point the reset vector at it, and reset.
        void boot(std::uint16_t origin, std::initializer_list<std::uint8_t> program) {
            std::uint16_t addr = origin;
            for (std::uint8_t byte : program) {
                bus.memory[addr++] = byte;
            }
            bus.memory[0xFFFCU] = static_cast<std::uint8_t>(origin & 0xFFU);
            bus.memory[0xFFFDU] = static_cast<std::uint8_t>(origin >> 8U);
            cpu.attach_bus(bus);
            cpu.reset(mnemos::chips::reset_kind::power_on);
        }

        // Execute one full instruction; return the cycles it consumed.
        std::uint64_t step_instruction() {
            const std::uint64_t before = cpu.elapsed_cycles();
            cpu.tick(1U);
            while (!cpu.at_instruction_boundary()) {
                cpu.tick(1U);
            }
            return cpu.elapsed_cycles() - before;
        }
    };

} // namespace

static_assert(std::is_base_of_v<mnemos::chips::icpu, m6510>);
static_assert(m6510::static_class == mnemos::chips::chip_class::cpu);

TEST_CASE("m6510 reports its identity") {
    const m6510 cpu;
    const mnemos::chips::chip_metadata metadata = cpu.metadata();

    CHECK(metadata.manufacturer == "MOS Technology");
    CHECK(metadata.part_number == "6510");
    CHECK(metadata.family == "6502");
    CHECK(metadata.klass == mnemos::chips::chip_class::cpu);
}

TEST_CASE("m6510 power-on reset loads the reset vector and sets I") {
    test_system sys;
    sys.boot(0xE000U, {});

    CHECK(sys.cpu.cpu_registers().pc == 0xE000U);
    CHECK(sys.cpu.cpu_registers().sp == 0xFDU);
    CHECK(sys.cpu.flag(m6510::status_flag::irq_disable));
    CHECK(sys.cpu.flag(m6510::status_flag::unused));
    CHECK(sys.cpu.elapsed_cycles() == 0U);
}

TEST_CASE("m6510 NOP takes two cycles and advances PC by one") {
    test_system sys;
    sys.boot(0xC000U, {0xEAU}); // NOP

    const std::uint64_t cycles = sys.step_instruction();

    CHECK(cycles == 2U);
    CHECK(sys.cpu.cpu_registers().pc == 0xC001U);
}

TEST_CASE("LDA immediate takes 2 cycles and sets Z") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x00U}); // LDA #$00

    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.cpu.cpu_registers().a == 0x00U);
    CHECK(sys.cpu.flag(m6510::status_flag::zero));
    CHECK_FALSE(sys.cpu.flag(m6510::status_flag::negative));
}

TEST_CASE("LDA zero page takes 3 cycles and sets N") {
    test_system sys;
    sys.boot(0xC000U, {0xA5U, 0x10U}); // LDA $10
    sys.bus.memory[0x0010U] = 0x80U;

    CHECK(sys.step_instruction() == 3U);
    CHECK(sys.cpu.cpu_registers().a == 0x80U);
    CHECK(sys.cpu.flag(m6510::status_flag::negative));
    CHECK_FALSE(sys.cpu.flag(m6510::status_flag::zero));
}

TEST_CASE("LDA absolute takes 4 cycles") {
    test_system sys;
    sys.boot(0xC000U, {0xADU, 0x34U, 0x12U}); // LDA $1234
    sys.bus.memory[0x1234U] = 0x42U;

    CHECK(sys.step_instruction() == 4U);
    CHECK(sys.cpu.cpu_registers().a == 0x42U);
    CHECK_FALSE(sys.cpu.flag(m6510::status_flag::zero));
    CHECK_FALSE(sys.cpu.flag(m6510::status_flag::negative));
}

TEST_CASE("LDA zero page,X wraps in zero page and takes 4 cycles") {
    test_system sys;
    sys.boot(0xC000U, {0xA2U, 0x05U, 0xB5U, 0x10U}); // LDX #$05 ; LDA $10,X -> $15
    sys.bus.memory[0x0015U] = 0x77U;

    CHECK(sys.step_instruction() == 2U); // LDX #
    CHECK(sys.step_instruction() == 4U); // LDA $10,X
    CHECK(sys.cpu.cpu_registers().a == 0x77U);
}

TEST_CASE("LDA absolute,X adds a cycle only on page cross") {
    SECTION("page cross -> 5 cycles") {
        test_system sys;
        sys.boot(0xC000U, {0xA2U, 0x01U, 0xBDU, 0xFFU, 0x12U}); // LDX #$01 ; LDA $12FF,X
        sys.bus.memory[0x1300U] = 0x5AU;                        // $12FF + 1 = $1300

        CHECK(sys.step_instruction() == 2U);
        CHECK(sys.step_instruction() == 5U);
        CHECK(sys.cpu.cpu_registers().a == 0x5AU);
    }
    SECTION("no cross -> 4 cycles") {
        test_system sys;
        sys.boot(0xC000U, {0xA2U, 0x01U, 0xBDU, 0x00U, 0x12U}); // LDX #$01 ; LDA $1200,X
        sys.bus.memory[0x1201U] = 0x3CU;

        CHECK(sys.step_instruction() == 2U);
        CHECK(sys.step_instruction() == 4U);
        CHECK(sys.cpu.cpu_registers().a == 0x3CU);
    }
}

TEST_CASE("LDA (indirect),Y resolves the pointer and adds the cross cycle") {
    test_system sys;
    sys.boot(0xC000U, {0xA0U, 0x01U, 0xB1U, 0x40U}); // LDY #$01 ; LDA ($40),Y
    sys.bus.memory[0x0040U] = 0xFFU;                 // pointer low
    sys.bus.memory[0x0041U] = 0x12U;                 // pointer high -> base $12FF
    sys.bus.memory[0x1300U] = 0x9AU;                 // $12FF + 1 = $1300 (cross)

    CHECK(sys.step_instruction() == 2U); // LDY #
    CHECK(sys.step_instruction() == 6U); // (zp),Y with cross = 5 + 1
    CHECK(sys.cpu.cpu_registers().a == 0x9AU);
}

TEST_CASE("LDX zero page,Y reads the indexed zero-page byte") {
    test_system sys;
    sys.boot(0xC000U, {0xA0U, 0x03U, 0xB6U, 0x10U}); // LDY #$03 ; LDX $10,Y -> $13
    sys.bus.memory[0x0013U] = 0x44U;

    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.step_instruction() == 4U);
    CHECK(sys.cpu.cpu_registers().x == 0x44U);
}

TEST_CASE("STA zero page writes and takes 3 cycles") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0xABU, 0x85U, 0x30U}); // LDA #$AB ; STA $30

    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.step_instruction() == 3U);
    CHECK(sys.bus.memory[0x0030U] == 0xABU);
}

TEST_CASE("STA absolute,X always takes 5 cycles") {
    test_system sys;
    // LDA #$EE ; LDX #$02 ; STA $2000,X
    sys.boot(0xC000U, {0xA9U, 0xEEU, 0xA2U, 0x02U, 0x9DU, 0x00U, 0x20U});

    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.step_instruction() == 5U);
    CHECK(sys.bus.memory[0x2002U] == 0xEEU);
}

TEST_CASE("STA (indirect),Y writes through the pointer in 6 cycles") {
    test_system sys;
    // LDA #$C3 ; LDY #$04 ; STA ($50),Y
    sys.boot(0xC000U, {0xA9U, 0xC3U, 0xA0U, 0x04U, 0x91U, 0x50U});
    sys.bus.memory[0x0050U] = 0x00U; // pointer low
    sys.bus.memory[0x0051U] = 0x21U; // pointer high -> base $2100

    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.step_instruction() == 6U);
    CHECK(sys.bus.memory[0x2104U] == 0xC3U);
}

TEST_CASE("TAX transfers A to X and sets flags in 2 cycles") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x80U, 0xAAU}); // LDA #$80 ; TAX

    sys.step_instruction();
    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.cpu.cpu_registers().x == 0x80U);
    CHECK(sys.cpu.flag(m6510::status_flag::negative));
}

TEST_CASE("INX wraps $FF to $00 and sets Z") {
    test_system sys;
    sys.boot(0xC000U, {0xA2U, 0xFFU, 0xE8U}); // LDX #$FF ; INX

    sys.step_instruction();
    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.cpu.cpu_registers().x == 0x00U);
    CHECK(sys.cpu.flag(m6510::status_flag::zero));
}

TEST_CASE("PHA then PLA round-trips A through the stack") {
    test_system sys;
    // LDA #$42 ; PHA ; LDA #$00 ; PLA
    sys.boot(0xC000U, {0xA9U, 0x42U, 0x48U, 0xA9U, 0x00U, 0x68U});

    sys.step_instruction();              // LDA #$42
    CHECK(sys.step_instruction() == 3U); // PHA
    CHECK(sys.cpu.cpu_registers().sp == 0xFCU);
    sys.step_instruction();              // LDA #$00
    CHECK(sys.step_instruction() == 4U); // PLA
    CHECK(sys.cpu.cpu_registers().a == 0x42U);
    CHECK(sys.cpu.cpu_registers().sp == 0xFDU);
}

TEST_CASE("ADC sets overflow and negative without carry") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x50U, 0x69U, 0x50U}); // LDA #$50 ; ADC #$50

    sys.step_instruction();
    sys.step_instruction();
    CHECK(sys.cpu.cpu_registers().a == 0xA0U);
    CHECK(sys.cpu.flag(m6510::status_flag::overflow));
    CHECK(sys.cpu.flag(m6510::status_flag::negative));
    CHECK_FALSE(sys.cpu.flag(m6510::status_flag::carry));
}

TEST_CASE("SBC subtracts the operand and the borrow") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x50U, 0xE9U, 0x0FU}); // LDA #$50 ; SBC #$0F (carry clear -> borrow)

    sys.step_instruction();
    sys.step_instruction();
    CHECK(sys.cpu.cpu_registers().a == 0x40U); // $50 - $0F - 1
    CHECK(sys.cpu.flag(m6510::status_flag::carry));
}

TEST_CASE("CMP sets carry and zero when equal") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x42U, 0xC9U, 0x42U}); // LDA #$42 ; CMP #$42

    sys.step_instruction();
    sys.step_instruction();
    CHECK(sys.cpu.flag(m6510::status_flag::zero));
    CHECK(sys.cpu.flag(m6510::status_flag::carry));
    CHECK_FALSE(sys.cpu.flag(m6510::status_flag::negative));
}

TEST_CASE("BIT takes N and V from the operand bits") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x01U, 0x24U, 0x10U}); // LDA #$01 ; BIT $10
    sys.bus.memory[0x0010U] = 0xC0U;                 // bit 7 and bit 6 set

    sys.step_instruction();
    sys.step_instruction();
    CHECK(sys.cpu.flag(m6510::status_flag::negative));
    CHECK(sys.cpu.flag(m6510::status_flag::overflow));
    CHECK(sys.cpu.flag(m6510::status_flag::zero)); // $01 & $C0 == 0
}

TEST_CASE("AND, ORA and EOR combine the accumulator") {
    SECTION("AND") {
        test_system sys;
        sys.boot(0xC000U, {0xA9U, 0xF0U, 0x29U, 0x3CU}); // LDA #$F0 ; AND #$3C
        sys.step_instruction();
        sys.step_instruction();
        CHECK(sys.cpu.cpu_registers().a == 0x30U);
    }
    SECTION("ORA") {
        test_system sys;
        sys.boot(0xC000U, {0xA9U, 0xF0U, 0x09U, 0x0FU}); // LDA #$F0 ; ORA #$0F
        sys.step_instruction();
        sys.step_instruction();
        CHECK(sys.cpu.cpu_registers().a == 0xFFU);
    }
    SECTION("EOR") {
        test_system sys;
        sys.boot(0xC000U, {0xA9U, 0xFFU, 0x49U, 0x0FU}); // LDA #$FF ; EOR #$0F
        sys.step_instruction();
        sys.step_instruction();
        CHECK(sys.cpu.cpu_registers().a == 0xF0U);
    }
}

TEST_CASE("ADC in decimal mode produces BCD results") {
    SECTION("09 + 01 = 10") {
        test_system sys;
        sys.boot(0xC000U, {0xA9U, 0x09U, 0x69U, 0x01U}); // LDA #$09 ; ADC #$01
        sys.cpu.set_flag(m6510::status_flag::decimal, true);
        sys.step_instruction();
        sys.step_instruction();
        CHECK(sys.cpu.cpu_registers().a == 0x10U);
        CHECK_FALSE(sys.cpu.flag(m6510::status_flag::carry));
    }
    SECTION("99 + 01 = 00 with carry") {
        test_system sys;
        sys.boot(0xC000U, {0xA9U, 0x99U, 0x69U, 0x01U});
        sys.cpu.set_flag(m6510::status_flag::decimal, true);
        sys.step_instruction();
        sys.step_instruction();
        CHECK(sys.cpu.cpu_registers().a == 0x00U);
        CHECK(sys.cpu.flag(m6510::status_flag::carry));
    }
}

TEST_CASE("SBC in decimal mode produces BCD results") {
    SECTION("50 - 25 = 25") {
        test_system sys;
        sys.boot(0xC000U, {0xA9U, 0x50U, 0xE9U, 0x25U}); // LDA #$50 ; SBC #$25
        sys.cpu.set_flag(m6510::status_flag::decimal, true);
        sys.cpu.set_flag(m6510::status_flag::carry, true); // no borrow in
        sys.step_instruction();
        sys.step_instruction();
        CHECK(sys.cpu.cpu_registers().a == 0x25U);
        CHECK(sys.cpu.flag(m6510::status_flag::carry));
    }
    SECTION("00 - 01 = 99 with borrow") {
        test_system sys;
        sys.boot(0xC000U, {0xA9U, 0x00U, 0xE9U, 0x01U});
        sys.cpu.set_flag(m6510::status_flag::decimal, true);
        sys.cpu.set_flag(m6510::status_flag::carry, true);
        sys.step_instruction();
        sys.step_instruction();
        CHECK(sys.cpu.cpu_registers().a == 0x99U);
        CHECK_FALSE(sys.cpu.flag(m6510::status_flag::carry));
    }
}

TEST_CASE("ASL accumulator shifts left and sets carry from bit 7") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x81U, 0x0AU}); // LDA #$81 ; ASL A

    sys.step_instruction();
    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.cpu.cpu_registers().a == 0x02U); // $81 << 1
    CHECK(sys.cpu.flag(m6510::status_flag::carry));
}

TEST_CASE("ROL accumulator rotates carry into bit 0") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x80U, 0x2AU}); // LDA #$80 ; ROL A
    sys.cpu.set_flag(m6510::status_flag::carry, true);

    sys.step_instruction();              // LDA #$80
    CHECK(sys.step_instruction() == 2U); // ROL A
    CHECK(sys.cpu.cpu_registers().a == 0x01U);
    CHECK(sys.cpu.flag(m6510::status_flag::carry)); // old bit 7
}

TEST_CASE("LSR zero page shifts right in 5 cycles") {
    test_system sys;
    sys.boot(0xC000U, {0x46U, 0x20U}); // LSR $20
    sys.bus.memory[0x0020U] = 0x03U;

    CHECK(sys.step_instruction() == 5U);
    CHECK(sys.bus.memory[0x0020U] == 0x01U);
    CHECK(sys.cpu.flag(m6510::status_flag::carry)); // bit 0 was 1
}

TEST_CASE("INC zero page increments memory in 5 cycles") {
    test_system sys;
    sys.boot(0xC000U, {0xE6U, 0x30U}); // INC $30
    sys.bus.memory[0x0030U] = 0xFFU;

    CHECK(sys.step_instruction() == 5U);
    CHECK(sys.bus.memory[0x0030U] == 0x00U);
    CHECK(sys.cpu.flag(m6510::status_flag::zero));
}

TEST_CASE("DEC absolute decrements memory in 6 cycles") {
    test_system sys;
    sys.boot(0xC000U, {0xCEU, 0x00U, 0x40U}); // DEC $4000
    sys.bus.memory[0x4000U] = 0x01U;

    CHECK(sys.step_instruction() == 6U);
    CHECK(sys.bus.memory[0x4000U] == 0x00U);
    CHECK(sys.cpu.flag(m6510::status_flag::zero));
}

TEST_CASE("ASL absolute,X always takes 7 cycles") {
    test_system sys;
    sys.boot(0xC000U, {0xA2U, 0x01U, 0x1EU, 0x00U, 0x40U}); // LDX #$01 ; ASL $4000,X
    sys.bus.memory[0x4001U] = 0x40U;

    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.step_instruction() == 7U);
    CHECK(sys.bus.memory[0x4001U] == 0x80U);
    CHECK(sys.cpu.flag(m6510::status_flag::negative));
    CHECK_FALSE(sys.cpu.flag(m6510::status_flag::carry));
}

TEST_CASE("SEC sets and CLC clears carry in 2 cycles each") {
    test_system sys;
    sys.boot(0xC000U, {0x38U, 0x18U}); // SEC ; CLC

    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.cpu.flag(m6510::status_flag::carry));
    CHECK(sys.step_instruction() == 2U);
    CHECK_FALSE(sys.cpu.flag(m6510::status_flag::carry));
}

TEST_CASE("SED sets and CLD clears decimal") {
    test_system sys;
    sys.boot(0xC000U, {0xF8U, 0xD8U}); // SED ; CLD

    sys.step_instruction();
    CHECK(sys.cpu.flag(m6510::status_flag::decimal));
    sys.step_instruction();
    CHECK_FALSE(sys.cpu.flag(m6510::status_flag::decimal));
}

TEST_CASE("BEQ not taken takes 2 cycles") {
    test_system sys;
    sys.boot(0xC000U, {0xF0U, 0x05U}); // BEQ +5 ; Z clear after reset -> not taken

    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.cpu.cpu_registers().pc == 0xC002U);
}

TEST_CASE("BNE taken without page cross takes 3 cycles") {
    test_system sys;
    sys.boot(0xC000U, {0xD0U, 0x05U}); // BNE +5 ; Z clear -> taken

    CHECK(sys.step_instruction() == 3U);
    CHECK(sys.cpu.cpu_registers().pc == 0xC007U); // $C002 + 5
}

TEST_CASE("BNE taken across a page boundary takes 4 cycles") {
    test_system sys;
    sys.boot(0xC0FDU, {0xD0U, 0x05U}); // at $C0FD: BNE +5

    CHECK(sys.step_instruction() == 4U);
    CHECK(sys.cpu.cpu_registers().pc == 0xC104U); // $C0FF + 5
}

TEST_CASE("JMP absolute sets PC in 3 cycles") {
    test_system sys;
    sys.boot(0xC000U, {0x4CU, 0x34U, 0x12U}); // JMP $1234

    CHECK(sys.step_instruction() == 3U);
    CHECK(sys.cpu.cpu_registers().pc == 0x1234U);
}

TEST_CASE("JMP indirect reproduces the page-boundary bug") {
    test_system sys;
    sys.boot(0xC000U, {0x6CU, 0xFFU, 0x20U}); // JMP ($20FF)
    sys.bus.memory[0x20FFU] = 0x34U;          // target low
    sys.bus.memory[0x2000U] = 0x12U;          // high byte fetched from $2000 (the bug)
    sys.bus.memory[0x2100U] = 0xFFU;          // would-be high byte without the bug

    CHECK(sys.step_instruction() == 5U);
    CHECK(sys.cpu.cpu_registers().pc == 0x1234U);
}

TEST_CASE("JSR pushes the return address and RTS restores it") {
    test_system sys;
    sys.boot(0xC000U, {0x20U, 0x10U, 0xC0U}); // JSR $C010
    sys.bus.memory[0xC010U] = 0x60U;          // RTS

    CHECK(sys.step_instruction() == 6U); // JSR
    CHECK(sys.cpu.cpu_registers().pc == 0xC010U);
    CHECK(sys.cpu.cpu_registers().sp == 0xFBU);
    CHECK(sys.step_instruction() == 6U); // RTS
    CHECK(sys.cpu.cpu_registers().pc == 0xC003U);
    CHECK(sys.cpu.cpu_registers().sp == 0xFDU);
}

TEST_CASE("BRK vectors through $FFFE and RTI returns") {
    test_system sys;
    sys.boot(0xC000U, {0x00U});      // BRK
    sys.bus.memory[0xFFFEU] = 0x00U; // IRQ/BRK vector -> $E000
    sys.bus.memory[0xFFFFU] = 0xE0U;
    sys.bus.memory[0xE000U] = 0x40U; // RTI

    CHECK(sys.step_instruction() == 7U); // BRK
    CHECK(sys.cpu.cpu_registers().pc == 0xE000U);
    CHECK(sys.cpu.flag(m6510::status_flag::irq_disable));
    CHECK(sys.step_instruction() == 6U);          // RTI
    CHECK(sys.cpu.cpu_registers().pc == 0xC002U); // BRK pushed PC + 2
}

TEST_CASE("IRQ is serviced in 7 cycles when I is clear") {
    test_system sys;
    sys.boot(0xC000U, {0x58U, 0xEAU}); // CLI ; NOP
    sys.bus.memory[0xFFFEU] = 0x00U;   // IRQ/BRK vector -> $E000
    sys.bus.memory[0xFFFFU] = 0xE0U;

    sys.step_instruction(); // CLI clears I
    sys.cpu.set_irq_line(true);

    CHECK(sys.step_instruction() == 7U); // IRQ sequence runs instead of NOP
    CHECK(sys.cpu.cpu_registers().pc == 0xE000U);
    CHECK(sys.cpu.flag(m6510::status_flag::irq_disable));
    // Pushed status has B clear and the unused bit set.
    CHECK((sys.bus.memory[0x01FBU] & 0x10U) == 0U);
    CHECK((sys.bus.memory[0x01FBU] & 0x20U) != 0U);
}

TEST_CASE("IRQ is ignored while I is set") {
    test_system sys;
    sys.boot(0xC000U, {0xEAU}); // NOP (I set after reset)
    sys.cpu.set_irq_line(true);

    CHECK(sys.step_instruction() == 2U); // NOP runs; IRQ masked
    CHECK(sys.cpu.cpu_registers().pc == 0xC001U);
}

TEST_CASE("NMI is serviced even while I is set") {
    test_system sys;
    sys.boot(0xC000U, {0xEAU});
    sys.bus.memory[0xFFFAU] = 0x00U; // NMI vector -> $F000
    sys.bus.memory[0xFFFBU] = 0xF0U;
    sys.cpu.set_nmi_line(true); // inactive->active edge latches NMI

    CHECK(sys.step_instruction() == 7U);
    CHECK(sys.cpu.cpu_registers().pc == 0xF000U);
}

TEST_CASE("LAX loads A and X together") {
    test_system sys;
    sys.boot(0xC000U, {0xA7U, 0x40U}); // LAX $40
    sys.bus.memory[0x0040U] = 0x88U;

    CHECK(sys.step_instruction() == 3U);
    CHECK(sys.cpu.cpu_registers().a == 0x88U);
    CHECK(sys.cpu.cpu_registers().x == 0x88U);
    CHECK(sys.cpu.flag(m6510::status_flag::negative));
}

TEST_CASE("SAX stores A AND X") {
    test_system sys;
    // LDA #$CC ; LDX #$0F ; SAX $40
    sys.boot(0xC000U, {0xA9U, 0xCCU, 0xA2U, 0x0FU, 0x87U, 0x40U});

    sys.step_instruction();
    sys.step_instruction();
    CHECK(sys.step_instruction() == 3U);
    CHECK(sys.bus.memory[0x0040U] == 0x0CU); // $CC & $0F
}

TEST_CASE("SLO shifts memory left and ORs the result into A") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x01U, 0x07U, 0x40U}); // LDA #$01 ; SLO $40
    sys.bus.memory[0x0040U] = 0x80U;

    sys.step_instruction();
    CHECK(sys.step_instruction() == 5U);
    CHECK(sys.bus.memory[0x0040U] == 0x00U); // $80 << 1
    CHECK(sys.cpu.flag(m6510::status_flag::carry));
    CHECK(sys.cpu.cpu_registers().a == 0x01U); // $01 | $00
}

TEST_CASE("DCP decrements memory and compares with A") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x10U, 0xC7U, 0x40U}); // LDA #$10 ; DCP $40
    sys.bus.memory[0x0040U] = 0x11U;                 // dec -> $10 == A

    sys.step_instruction();
    CHECK(sys.step_instruction() == 5U);
    CHECK(sys.bus.memory[0x0040U] == 0x10U);
    CHECK(sys.cpu.flag(m6510::status_flag::zero));
    CHECK(sys.cpu.flag(m6510::status_flag::carry));
}

TEST_CASE("ISC increments memory and subtracts it from A") {
    test_system sys;
    // SEC ; LDA #$10 ; ISC $40
    sys.boot(0xC000U, {0x38U, 0xA9U, 0x10U, 0xE7U, 0x40U});
    sys.bus.memory[0x0040U] = 0x04U; // inc -> $05

    sys.step_instruction();
    sys.step_instruction();
    CHECK(sys.step_instruction() == 5U);
    CHECK(sys.bus.memory[0x0040U] == 0x05U);
    CHECK(sys.cpu.cpu_registers().a == 0x0BU); // $10 - $05
}

TEST_CASE("ANC ANDs the immediate and copies N into C") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0xFFU, 0x0BU, 0x80U}); // LDA #$FF ; ANC #$80

    sys.step_instruction();
    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.cpu.cpu_registers().a == 0x80U);
    CHECK(sys.cpu.flag(m6510::status_flag::negative));
    CHECK(sys.cpu.flag(m6510::status_flag::carry));
}

TEST_CASE("SBX sets X = (A & X) - imm with a CMP-style carry") {
    test_system sys;
    // LDA #$FF ; LDX #$0F ; SBX #$05
    sys.boot(0xC000U, {0xA9U, 0xFFU, 0xA2U, 0x0FU, 0xCBU, 0x05U});

    sys.step_instruction();
    sys.step_instruction();
    CHECK(sys.step_instruction() == 2U);
    CHECK(sys.cpu.cpu_registers().x == 0x0AU); // ($FF & $0F) - $05
    CHECK(sys.cpu.flag(m6510::status_flag::carry));
}

TEST_CASE("undocumented NOP abs,X consumes its operand and time") {
    test_system sys;
    sys.boot(0xC000U, {0xA2U, 0x01U, 0x1CU, 0xFFU, 0x12U}); // LDX #$01 ; NOP $12FF,X
    sys.step_instruction();

    CHECK(sys.step_instruction() == 5U); // page cross
    CHECK(sys.cpu.cpu_registers().pc == 0xC005U);
}

TEST_CASE("m6510 register snapshot reports the register file") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x7FU}); // LDA #$7F
    sys.step_instruction();

    const auto regs = sys.cpu.register_snapshot();
    REQUIRE(regs.size() == 6U);
    CHECK(regs[0].name == "A");
    CHECK(regs[0].value == 0x7FU);
    CHECK(regs[0].bit_width == 8U);
    CHECK(regs[4].name == "P");
    CHECK(regs[4].format == mnemos::chips::register_value_format::flags);
    CHECK(regs[5].name == "PC");
    CHECK(regs[5].value == 0xC002U);
    CHECK(regs[5].bit_width == 16U);
}

TEST_CASE("m6510 status flags set and clear independently") {
    m6510 cpu;
    using status_flag = m6510::status_flag;

    cpu.set_flag(status_flag::carry, true);
    cpu.set_flag(status_flag::negative, true);

    CHECK(cpu.flag(status_flag::carry));
    CHECK(cpu.flag(status_flag::negative));
    CHECK_FALSE(cpu.flag(status_flag::zero));

    cpu.set_flag(status_flag::carry, false);

    CHECK_FALSE(cpu.flag(status_flag::carry));
    CHECK(cpu.flag(status_flag::negative));
}

TEST_CASE("m6510 $00/$01 I/O port latches output bits") {
    test_system sys;
    sys.boot(0xC000U, {});

    // DDR $2F = 0010_1111: outputs are bits 0,1,2,3,5; inputs are bits 4,6,7.
    sys.cpu.write(0x0000U, 0x2FU);
    sys.cpu.write(0x0001U, 0x37U);

    CHECK(sys.cpu.read(0x0000U) == 0x2FU);
    // Output bits read back from the latch: data & ddr = $37 & $2F = $27.
    // Input bit 4 reads the pull-up (high) -> $10. The unconnected bits 6,7 read
    // their floating-gate charge, which is 0 here (never driven high). Result $37.
    CHECK(sys.cpu.read(0x0001U) == 0x37U);
}

TEST_CASE("m6510 passes non-port addresses through to the bus") {
    test_system sys;
    sys.boot(0xC000U, {});

    sys.cpu.write(0x0200U, 0xABU);

    CHECK(sys.cpu.read(0x0200U) == 0xABU);
    CHECK(sys.bus.memory[0x0200U] == 0xABU);
}

TEST_CASE("m6510 registers under its canonical id") {
    const mnemos::chips::chip_factory_descriptor* descriptor =
        mnemos::chips::find_factory("mos.6510");
    REQUIRE(descriptor != nullptr);
    CHECK(descriptor->klass == mnemos::chips::chip_class::cpu);

    std::unique_ptr<mnemos::chips::ichip> chip = mnemos::chips::create_chip("mos.6510");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == "6510");
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::cpu);
}

TEST_CASE("m6510 save/load round-trips") {
    m6510 a;
    a.reset(mnemos::chips::reset_kind::power_on);
    m6510::registers regs{};
    regs.a = 0x11U;
    regs.x = 0x22U;
    regs.y = 0x33U;
    regs.sp = 0x44U;
    regs.p = 0x75U;
    regs.pc = 0x6789U;
    a.set_registers(regs);
    a.write(0x00U, 0x2FU);
    a.write(0x01U, 0x37U);

    std::vector<std::uint8_t> buf1;
    mnemos::chips::state_writer w(buf1);
    a.save_state(w);

    m6510 b;
    mnemos::chips::state_reader r(buf1);
    b.load_state(r);
    CHECK(r.ok());
    CHECK(b.cpu_registers().a == 0x11U);
    CHECK(b.cpu_registers().pc == 0x6789U);
    CHECK(b.cpu_registers().sp == 0x44U);

    std::vector<std::uint8_t> buf2;
    mnemos::chips::state_writer w2(buf2);
    b.save_state(w2);
    CHECK(buf1 == buf2);
}

TEST_CASE("ANE applies the unstable magic constant") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x00U, 0xA2U, 0xFFU, 0x8BU, 0xFFU}); // LDA #0; LDX #$FF; ANE #$FF
    sys.step_instruction();
    sys.step_instruction();
    sys.step_instruction();
    CHECK(sys.cpu.cpu_registers().a == 0xEEU); // (0 | $EE) & $FF & $FF
}

TEST_CASE("LXA loads A and X through the unstable magic") {
    test_system sys;
    sys.boot(0xC000U, {0xA9U, 0x0FU, 0xABU, 0xFFU}); // LDA #$0F; LXA #$FF
    sys.step_instruction();
    sys.step_instruction();
    CHECK(sys.cpu.cpu_registers().a == 0xEFU); // ($0F | $EE) & $FF
    CHECK(sys.cpu.cpu_registers().x == 0xEFU);
}

TEST_CASE("LAS ANDs memory with SP into A/X/SP") {
    test_system sys;
    sys.boot(0xC000U, {0xA2U, 0xF0U, 0x9AU, 0xA0U, 0x00U, 0xBBU, 0x34U, 0x12U});
    sys.bus.memory[0x1234U] = 0x3CU;
    sys.step_instruction();                    // LDX #$F0
    sys.step_instruction();                    // TXS
    sys.step_instruction();                    // LDY #$00
    sys.step_instruction();                    // LAS $1234,Y
    CHECK(sys.cpu.cpu_registers().a == 0x30U); // $3C & $F0
    CHECK(sys.cpu.cpu_registers().x == 0x30U);
    CHECK(sys.cpu.cpu_registers().sp == 0x30U);
}

TEST_CASE("SHA/SHX/SHY/TAS store the source ANDed with (high+1)") {
    SECTION("SHA $1234,Y") {
        test_system sys;
        sys.boot(0xC000U, {0xA9U, 0xFFU, 0xA2U, 0xFFU, 0xA0U, 0x00U, 0x9FU, 0x34U, 0x12U});
        for (int i = 0; i < 4; ++i) {
            sys.step_instruction();
        }
        CHECK(sys.bus.memory[0x1234U] == 0x13U); // $FF & ($12 + 1)
    }
    SECTION("SHX $1234,Y") {
        test_system sys;
        sys.boot(0xC000U, {0xA2U, 0xFFU, 0xA0U, 0x00U, 0x9EU, 0x34U, 0x12U});
        for (int i = 0; i < 3; ++i) {
            sys.step_instruction();
        }
        CHECK(sys.bus.memory[0x1234U] == 0x13U);
    }
    SECTION("SHY $1234,X") {
        test_system sys;
        sys.boot(0xC000U, {0xA0U, 0xFFU, 0xA2U, 0x00U, 0x9CU, 0x34U, 0x12U});
        for (int i = 0; i < 3; ++i) {
            sys.step_instruction();
        }
        CHECK(sys.bus.memory[0x1234U] == 0x13U);
    }
    SECTION("TAS $1234,Y sets SP and stores") {
        test_system sys;
        sys.boot(0xC000U, {0xA9U, 0xFFU, 0xA2U, 0x0FU, 0xA0U, 0x00U, 0x9BU, 0x34U, 0x12U});
        for (int i = 0; i < 4; ++i) {
            sys.step_instruction();
        }
        CHECK(sys.cpu.cpu_registers().sp == 0x0FU); // SP = A & X
        CHECK(sys.bus.memory[0x1234U] == 0x03U);    // $0F & $13
    }
}

TEST_CASE("the 6510 port bits 6/7 fade to 0 after switching to input") {
    test_system sys;
    for (std::uint8_t& byte : sys.bus.memory) {
        byte = 0xEAU; // NOP everywhere so ticking just advances cycles
    }
    sys.boot(0xC000U, {});
    sys.cpu.write(0x00U, 0xC0U); // DDR: bits 6,7 output
    sys.cpu.write(0x01U, 0xC0U); // drive them high
    CHECK((sys.cpu.read(0x01U) & 0xC0U) == 0xC0U);

    sys.cpu.write(0x00U, 0x00U);                   // switch to input -> latch the charge
    CHECK((sys.cpu.read(0x01U) & 0xC0U) == 0xC0U); // still charged

    sys.cpu.tick(400000U);                         // run past the fall-off window
    CHECK((sys.cpu.read(0x01U) & 0xC0U) == 0x00U); // decayed
}

TEST_CASE("m6510 trace_target fires once per instruction, not once per cycle") {
    test_system sys;
    // Three NOPs at $0200; each takes 2 cycles. Trace should fire 3 times,
    // not 6, despite the chip being cycle-stepped.
    sys.boot(0x0200U, {0xEAU, 0xEAU, 0xEAU});

    auto* trace = sys.cpu.introspection().trace();
    REQUIRE(trace != nullptr);

    std::vector<mnemos::instrumentation::trace_event> events;
    trace->install([&events](const mnemos::instrumentation::trace_event& ev) {
        events.push_back(ev);
    });

    sys.step_instruction();
    sys.step_instruction();
    sys.step_instruction();

    REQUIRE(events.size() == 3U);
    CHECK(events[0].pc == 0x0200U);
    CHECK(events[1].pc == 0x0201U);
    CHECK(events[2].pc == 0x0202U);
    // Cycles is the cumulative cost at instruction start. NOPs are 2 cycles
    // each, BUT the reset sequence at power-on costs additional cycles
    // before the first user instruction. So check the deltas between events
    // rather than absolute values.
    const auto delta_01 = events[1].cycles - events[0].cycles;
    const auto delta_12 = events[2].cycles - events[1].cycles;
    CHECK(delta_01 == 2U);
    CHECK(delta_12 == 2U);

    // Clearing the callback halts firings.
    trace->install({});
    sys.step_instruction();
    CHECK(events.size() == 3U);
}

TEST_CASE("m6510 register_view returns A/X/Y/SP/P/PC descriptors") {
    test_system sys;
    sys.boot(0x0200U, {0xA9U, 0x42U}); // LDA #$42
    sys.step_instruction();

    auto* regs = sys.cpu.introspection().registers();
    REQUIRE(regs != nullptr);
    auto descriptors = regs->registers();
    REQUIRE(descriptors.size() == 6U);
    bool saw_a = false;
    for (const auto& d : descriptors) {
        if (d.name == "A") {
            saw_a = true;
            CHECK(d.value == 0x42U);
        }
    }
    CHECK(saw_a);
}
