#include "m6510.hpp"

#include "ibus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>

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

TEST_CASE("ANE applies the unstable magic constant") {
    test_system sys;
    sys.boot(0xC000U,
             {0xA9U, 0x00U, 0xA2U, 0xFFU, 0x8BU, 0xFFU}); // LDA #0; LDX #$FF; ANE #$FF
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
        sys.boot(0xC000U,
                 {0xA9U, 0xFFU, 0xA2U, 0xFFU, 0xA0U, 0x00U, 0x9FU, 0x34U, 0x12U});
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
        sys.boot(0xC000U,
                 {0xA9U, 0xFFU, 0xA2U, 0x0FU, 0xA0U, 0x00U, 0x9BU, 0x34U, 0x12U});
        for (int i = 0; i < 4; ++i) {
            sys.step_instruction();
        }
        CHECK(sys.cpu.cpu_registers().sp == 0x0FU); // SP = A & X
        CHECK(sys.bus.memory[0x1234U] == 0x03U);    // $0F & $13
    }
}
