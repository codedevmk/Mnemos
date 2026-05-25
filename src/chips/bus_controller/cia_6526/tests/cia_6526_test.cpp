#include <mnemos/chips/bus_controller/cia_6526.hpp>

#include <mnemos/chips/common/chip_registry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <type_traits>

namespace {
    using mnemos::chips::bus_controller::cia_6526;
}

static_assert(std::is_base_of_v<mnemos::chips::i_bus_controller, cia_6526>);
static_assert(cia_6526::static_class == mnemos::chips::chip_class::bus_controller);

TEST_CASE("cia_6526 reports identity and registers under mos.6526") {
    const cia_6526 cia;
    const auto md = cia.metadata();
    CHECK(md.manufacturer == "MOS Technology");
    CHECK(md.part_number == "6526");
    CHECK(md.klass == mnemos::chips::chip_class::bus_controller);

    const auto* descriptor = mnemos::chips::find_factory("mos.6526");
    REQUIRE(descriptor != nullptr);
    CHECK(descriptor->klass == mnemos::chips::chip_class::bus_controller);
    REQUIRE(mnemos::chips::create_chip("mos.6526") != nullptr);
}

TEST_CASE("cia_6526 Timer A continuous underflow raises a masked IRQ") {
    cia_6526 cia;
    cia.write(0x04U, 0x02U); // TA latch low
    cia.write(0x05U, 0x00U); // TA latch high -> latch = 2, counter loaded (stopped)
    cia.write(0x0DU, 0x81U); // ICR: set Timer A mask
    cia.write(0x0EU, 0x01U); // CRA: START (continuous)

    cia.tick(10U);
    CHECK(cia.irq_asserted());

    const auto icr = cia.read(0x0DU);
    CHECK((icr & 0x80U) != 0U);      // IR
    CHECK((icr & 0x01U) != 0U);      // Timer A source
    CHECK_FALSE(cia.irq_asserted()); // read clears the latch and drops /IRQ
}

TEST_CASE("cia_6526 Timer A one-shot stops after a single underflow") {
    cia_6526 cia;
    cia.write(0x04U, 0x02U);
    cia.write(0x05U, 0x00U);
    cia.write(0x0EU, 0x09U); // CRA: START | RUNMODE (one-shot)

    cia.tick(10U);
    CHECK((cia.read(0x0EU) & 0x01U) == 0U); // START self-cleared
}

TEST_CASE("cia_6526 NMOS edge IRQ: enabling the mask after the source latched stays quiet") {
    cia_6526 cia;
    cia.write(0x04U, 0x01U);
    cia.write(0x05U, 0x00U);
    cia.write(0x0EU, 0x09U); // one-shot START, mask not set

    cia.tick(10U); // underflows once, then stops; source latched, line masked away
    CHECK_FALSE(cia.irq_asserted());

    cia.write(0x0DU, 0x81U); // enable Timer A mask *after* the edge
    cia.tick(2U);
    CHECK_FALSE(cia.irq_asserted()); // no spurious IRQ (Lorenz imr behaviour)
}

TEST_CASE("cia_6526 Timer B cascades on Timer A underflow") {
    cia_6526 cia;
    cia.write(0x04U, 0x01U); // TA latch = 1 (frequent underflow)
    cia.write(0x05U, 0x00U);
    cia.write(0x06U, 0x02U); // TB latch = 2
    cia.write(0x07U, 0x00U);
    cia.write(0x0DU, 0x82U); // enable Timer B mask
    cia.write(0x0FU, 0x41U); // CRB: INMODE = TA underflow | START
    cia.write(0x0EU, 0x01U); // CRA: START

    cia.tick(40U);
    CHECK(cia.irq_asserted());
    CHECK((cia.read(0x0DU) & 0x02U) != 0U); // Timer B source
}

TEST_CASE("cia_6526 port A composites the output latch and input pins through DDR") {
    cia_6526 cia;
    std::uint8_t driven = 0U;
    cia_6526::config cfg;
    cfg.read_port_a = []() -> std::uint8_t { return 0xAAU; };
    cfg.write_port_a = [&driven](std::uint8_t v) { driven = v; };
    cia.configure(cfg);

    cia.write(0x02U, 0xF0U); // DDRA: high nibble output
    cia.write(0x00U, 0x5CU); // PRA output latch
    // output bits: $5C & $F0 = $50; input bits: $AA & $0F = $0A -> $5A
    CHECK(cia.read(0x00U) == 0x5AU);
    CHECK(driven == 0x5CU); // write_port_a saw the latch
}

TEST_CASE("cia_6526 TOD advances tenths from the source divider") {
    cia_6526 cia;
    cia_6526::config cfg;
    cfg.tod_tick_hz = 60U; // divider_reload = 1
    cfg.tod_src_hz = 60U;  // phase_reload defaults to 6 (TODIN=0)
    cia.configure(cfg);

    cia.tick(6U);                    // 6 source edges = one tenth
    CHECK(cia.read(0x08U) == 0x01U); // TOD 1/10 seconds = 1
}

TEST_CASE("cia_6526 TOD alarm raises an IRQ on match") {
    cia_6526 cia;
    cia_6526::config cfg;
    cfg.tod_tick_hz = 60U;
    cfg.tod_src_hz = 60U;
    cia.configure(cfg);

    cia.write(0x0DU, 0x84U); // enable ALARM mask
    cia.write(0x0FU, 0x80U); // CRB.ALARM=1 -> TOD writes set the alarm
    cia.write(0x08U, 0x01U); // alarm tenths = 1
    cia.tick(6U);            // TOD reaches 0.1s -> alarm match
    cia.tick(1U);            // publish the IRQ pin
    CHECK(cia.irq_asserted());
    CHECK((cia.read(0x0DU) & 0x04U) != 0U); // ALARM source
}

TEST_CASE("cia_6526 serial shift register latches an input byte after 8 CNT edges") {
    cia_6526 cia;
    cia.write(0x0DU, 0x88U); // enable SDR IRQ mask; SPMODE input (default)

    const std::uint8_t pattern = 0xB1U;
    for (int bit = 7; bit >= 0; --bit) {
        cia.sp_level(((pattern >> bit) & 1U) != 0U);
        cia.cnt_edge(false);
        cia.cnt_edge(true); // rising edge samples SP
    }

    CHECK(cia.read(0x0CU) == pattern); // SDR readback
    cia.tick(1U);                      // publish the IRQ pin
    CHECK(cia.irq_asserted());
    CHECK((cia.read(0x0DU) & 0x08U) != 0U); // SDR source
}

TEST_CASE("cia_6526 reset returns timers to the 0xFFFF latch") {
    cia_6526 cia;
    cia.write(0x04U, 0x12U);
    cia.write(0x05U, 0x34U); // TA latch/counter = $3412 (stopped)
    cia.reset(mnemos::chips::reset_kind::hard);
    CHECK(cia.read(0x04U) == 0xFFU);
    CHECK(cia.read(0x05U) == 0xFFU);
}

TEST_CASE("cia_6526 is reachable through i_mmio") {
    auto chip = mnemos::chips::create_chip("mos.6526");
    REQUIRE(chip != nullptr);
    auto* mmio = dynamic_cast<mnemos::chips::i_mmio*>(chip.get());
    REQUIRE(mmio != nullptr);
    mmio->mmio_write(0x02U, 0xFFU); // DDRA
    CHECK(mmio->mmio_read(0x02U) == 0xFFU);
}

TEST_CASE("cia_6526 register snapshot reports timers and interrupt state") {
    cia_6526 cia;
    const auto regs = cia.register_snapshot();
    REQUIRE(regs.size() == 4U);
    CHECK(regs[0].name == "TA");
    CHECK(regs[0].value == 0xFFFFU);
    CHECK(regs[2].name == "ICR");
}
