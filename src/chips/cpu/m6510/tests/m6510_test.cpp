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
