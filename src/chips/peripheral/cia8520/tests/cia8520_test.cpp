#include "cia8520.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::peripheral::cia8520;

    // Register indices (low 4 bits of the address bus).
    constexpr std::uint8_t reg_ta_lo = 0x04U;
    constexpr std::uint8_t reg_ta_hi = 0x05U;
    constexpr std::uint8_t reg_icr = 0x0DU;
    constexpr std::uint8_t reg_cra = 0x0EU;
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::iperipheral, cia8520>);
static_assert(std::is_base_of_v<mnemos::chips::immio, cia8520>);

TEST_CASE("cia8520 registers under commodore.cia8520 as a peripheral") {
    const cia8520 cia;
    const auto md = cia.metadata();
    CHECK(md.part_number == "8520");
    CHECK(md.family == "CIA");
    CHECK(md.klass == mnemos::chips::chip_class::peripheral);

    const auto* descriptor = mnemos::chips::find_factory("commodore.cia8520");
    REQUIRE(descriptor != nullptr);
    CHECK(descriptor->klass == mnemos::chips::chip_class::peripheral);

    auto chip = mnemos::chips::create_chip("commodore.cia8520");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::peripheral);
}

TEST_CASE("cia8520 reset latches the timers at 0xFFFF and clears the IRQ line") {
    cia8520 cia;
    cia.reset(mnemos::chips::reset_kind::power_on);
    CHECK_FALSE(cia.irq_asserted());
    CHECK(cia.read(reg_ta_lo) == 0xFFU);
    CHECK(cia.read(reg_ta_hi) == 0xFFU);
}

TEST_CASE("cia8520 Timer A underflow sets the ICR bit and reading ICR clears it") {
    cia8520 cia;

    // Load Timer A latch = 2, enable the Timer A interrupt mask, start continuous.
    cia.write(reg_ta_lo, 0x02U); // latch low
    cia.write(reg_ta_hi, 0x00U); // latch high -> latch = 2, counter loaded (stopped)
    cia.write(reg_icr, 0x81U);   // ICR: set Timer A mask (bit 0)
    cia.write(reg_cra, 0x01U);   // CRA: START (continuous)

    cia.tick(10U);
    REQUIRE(cia.irq_asserted());

    const auto icr = cia.read(reg_icr);
    CHECK((icr & 0x80U) != 0U); // IR flag set
    CHECK((icr & 0x01U) != 0U); // Timer A source latched

    // Reading ICR clears the latch and drops /IRQ.
    CHECK_FALSE(cia.irq_asserted());
    const auto icr_after = cia.read(reg_icr);
    CHECK((icr_after & 0x01U) == 0U);
    CHECK((icr_after & 0x80U) == 0U);
}

TEST_CASE("cia8520 save/load round-trips") {
    cia8520 a;
    a.write(reg_ta_lo, 0x34U);
    a.write(reg_ta_hi, 0x12U); // latch/counter = 0x1234 (stopped)
    a.write(reg_icr, 0x83U);   // enable TA + TB masks

    std::vector<std::uint8_t> buf1;
    mnemos::chips::state_writer w(buf1);
    a.save_state(w);

    cia8520 b;
    mnemos::chips::state_reader r(buf1);
    b.load_state(r);
    CHECK(r.ok());

    // The restored counter is observable through the register window.
    CHECK(b.read(reg_ta_lo) == 0x34U);
    CHECK(b.read(reg_ta_hi) == 0x12U);

    std::vector<std::uint8_t> buf2;
    mnemos::chips::state_writer w2(buf2);
    b.save_state(w2);
    CHECK(buf1 == buf2);
}

TEST_CASE("cia8520 exposes its register file through introspection") {
    cia8520 cia;
    auto* registers = cia.introspection().registers();
    REQUIRE(registers != nullptr);
    const auto snapshot = registers->registers();
    REQUIRE(snapshot.size() == 4U);
    CHECK(snapshot[0].name == "TA");
    CHECK(snapshot[2].name == "ICR");
}
