#include <mnemos/chips/cpu/m6510.hpp>

#include <mnemos/chips/common/bus.hpp>
#include <mnemos/chips/common/chip_registry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace {

    class fake_bus final : public mnemos::chips::i_bus {
      public:
        std::array<std::uint8_t, 0x10000U> memory{};

        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override {
            return memory[address & 0xFFFFU];
        }

        void write8(std::uint32_t address, std::uint8_t value) override {
            memory[address & 0xFFFFU] = value;
        }
    };

} // namespace

static_assert(std::is_base_of_v<mnemos::chips::i_cpu, mnemos::chips::cpu::m6510>);
static_assert(mnemos::chips::cpu::m6510::static_class == mnemos::chips::chip_class::cpu);

TEST_CASE("m6510 reports its identity") {
    const mnemos::chips::cpu::m6510 cpu;
    const mnemos::chips::chip_metadata metadata = cpu.metadata();

    CHECK(metadata.manufacturer == "MOS Technology");
    CHECK(metadata.part_number == "6510");
    CHECK(metadata.family == "6502");
    CHECK(metadata.klass == mnemos::chips::chip_class::cpu);
}

TEST_CASE("m6510 power-on reset loads the reset vector and sets I") {
    fake_bus bus;
    bus.memory[0xFFFCU] = static_cast<std::uint8_t>(0x00U);
    bus.memory[0xFFFDU] = static_cast<std::uint8_t>(0xE0U);

    mnemos::chips::cpu::m6510 cpu;
    cpu.attach_bus(bus);
    cpu.reset(mnemos::chips::reset_kind::power_on);

    CHECK(cpu.cpu_registers().pc == 0xE000U);
    CHECK(cpu.cpu_registers().sp == 0xFDU);
    CHECK(cpu.flag(mnemos::chips::cpu::m6510::status_flag::irq_disable));
    CHECK(cpu.flag(mnemos::chips::cpu::m6510::status_flag::unused));
    CHECK(cpu.elapsed_cycles() == 0U);
}

TEST_CASE("m6510 status flags set and clear independently") {
    mnemos::chips::cpu::m6510 cpu;
    using status_flag = mnemos::chips::cpu::m6510::status_flag;

    cpu.set_flag(status_flag::carry, true);
    cpu.set_flag(status_flag::negative, true);

    CHECK(cpu.flag(status_flag::carry));
    CHECK(cpu.flag(status_flag::negative));
    CHECK_FALSE(cpu.flag(status_flag::zero));

    cpu.set_flag(status_flag::carry, false);

    CHECK_FALSE(cpu.flag(status_flag::carry));
    CHECK(cpu.flag(status_flag::negative));
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
