#include <mnemos/chips/cpu/m6510.hpp>

#include <mnemos/chips/common/bus.hpp>
#include <mnemos/chips/common/chip_registry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <type_traits>

namespace {

    using mnemos::chips::cpu::m6510;

    class flat_ram final : public mnemos::chips::i_bus {
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

static_assert(std::is_base_of_v<mnemos::chips::i_cpu, m6510>);
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
    // Input bits read the default pull (high): pull & ~ddr = $FF & $D0 = $D0.
    // Result = $27 | $D0 = $F7.
    CHECK(sys.cpu.read(0x0001U) == 0xF7U);
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

    std::unique_ptr<mnemos::chips::i_chip> chip = mnemos::chips::create_chip("mos.6510");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == "6510");
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::cpu);
}
