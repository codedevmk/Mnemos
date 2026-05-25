#include "via_6522.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::bus_controller::via_6522;
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::i_bus_controller, via_6522>);

TEST_CASE("via_6522 reports identity and registers under mos.6522") {
    const via_6522 via;
    const auto md = via.metadata();
    CHECK(md.manufacturer == "MOS Technology");
    CHECK(md.part_number == "6522");
    CHECK(md.klass == mnemos::chips::chip_class::bus_controller);

    const auto* descriptor = mnemos::chips::find_factory("mos.6522");
    REQUIRE(descriptor != nullptr);
    REQUIRE(mnemos::chips::create_chip("mos.6522") != nullptr);
}

TEST_CASE("via_6522 timer 1 one-shot flags T1 on underflow; reading T1C-L clears it") {
    via_6522 via;
    via.write(0x04U, 0x03U); // T1C-L latch
    via.write(0x05U, 0x00U); // T1C-H -> counter = 3, start
    CHECK((via.read(0x0DU) & 0x40U) == 0U);

    via.tick(4U);
    CHECK((via.read(0x0DU) & 0x40U) != 0U); // T1 underflow latched
    (void)via.read(0x04U);                  // reading T1C-L acknowledges T1
    CHECK((via.read(0x0DU) & 0x40U) == 0U);
}

TEST_CASE("via_6522 IER gates the composite IRQ and the callback") {
    via_6522 via;
    bool level = false;
    via_6522::config cfg;
    cfg.irq_edge = [&](bool asserted) { level = asserted; };
    via.configure(cfg);

    via.write(0x0EU, 0xC0U); // IER: set (bit7) + T1 (bit6)
    via.write(0x04U, 0x02U);
    via.write(0x05U, 0x00U); // counter = 2
    via.tick(3U);
    CHECK(level);
    CHECK(via.irq_asserted());

    (void)via.read(0x04U); // clear T1
    CHECK_FALSE(level);
}

TEST_CASE("via_6522 timer 1 continuous re-arms after acknowledge") {
    via_6522 via;
    via.write(0x0BU, 0x40U); // ACR: T1 continuous
    via.write(0x04U, 0x01U);
    via.write(0x05U, 0x00U);
    via.tick(2U);
    REQUIRE((via.read(0x0DU) & 0x40U) != 0U);
    (void)via.read(0x04U); // acknowledge
    CHECK((via.read(0x0DU) & 0x40U) == 0U);

    via.tick(6U); // reloads from latch and underflows again
    CHECK((via.read(0x0DU) & 0x40U) != 0U);
}

TEST_CASE("via_6522 timer 2 pulse mode counts PB6 edges, not phi2") {
    via_6522 via;
    via.write(0x0BU, 0x20U); // ACR: T2 pulse-count
    via.write(0x08U, 0x02U); // T2C-L latch low
    via.write(0x09U, 0x00U); // T2C-H -> counter = 2

    via.tick(100U); // phi2 must not move T2 in pulse mode
    CHECK((via.read(0x0DU) & 0x20U) == 0U);

    via.pb6_pulse();
    via.pb6_pulse();
    via.pb6_pulse(); // 2 -> 1 -> 0 -> underflow
    CHECK((via.read(0x0DU) & 0x20U) != 0U);
}

TEST_CASE("via_6522 CA1 active edge flags CA1; reading ORA clears it") {
    via_6522 via;
    via.write(0x0CU, 0x00U); // PCR: CA1 negative edge
    via.ca1_edge(true);
    via.ca1_edge(false); // falling edge = active
    CHECK((via.read(0x0DU) & 0x02U) != 0U);
    (void)via.read(0x01U); // reading ORA acknowledges CA1
    CHECK((via.read(0x0DU) & 0x02U) == 0U);
}

TEST_CASE("via_6522 composites port A through DDR") {
    via_6522 via;
    via_6522::config cfg;
    cfg.read_port_a = []() -> std::uint8_t { return 0xAAU; };
    via.configure(cfg);

    via.write(0x03U, 0xF0U); // DDRA: high nibble output
    via.write(0x01U, 0x5CU); // ORA
    // output bits $5C & $F0 = $50; input bits $AA & $0F = $0A -> $5A
    CHECK(via.read(0x01U) == 0x5AU);
    CHECK(via.port_a_pins() == 0x5AU);
}

TEST_CASE("via_6522 PB7 reports the T1 timer output when ACR bit 7 is set") {
    via_6522 via;
    via.write(0x0BU, 0x80U); // ACR: T1 -> PB7
    via.write(0x05U, 0x10U); // T1C-H: starts T1, drives PB7 low
    CHECK((via.port_b_pins() & 0x80U) == 0U);

    via.tick(5000U); // T1 (one-shot) underflows and drives PB7 high
    CHECK((via.port_b_pins() & 0x80U) != 0U);
}

TEST_CASE("via_6522 save/load round-trips") {
    via_6522 a;
    a.write(0x0BU, 0x40U); // ACR
    a.write(0x04U, 0x12U);
    a.write(0x05U, 0x34U);
    a.write(0x0EU, 0xC0U); // IER
    a.tick(10U);

    std::vector<std::uint8_t> buf1;
    mnemos::chips::state_writer w(buf1);
    a.save_state(w);

    via_6522 b;
    mnemos::chips::state_reader r(buf1);
    b.load_state(r);
    CHECK(r.ok());

    std::vector<std::uint8_t> buf2;
    mnemos::chips::state_writer w2(buf2);
    b.save_state(w2);
    CHECK(buf1 == buf2);
}
