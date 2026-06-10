#include "ym2151.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::audio::ym2151;
} // namespace

TEST_CASE("ym2151 registers through the chip registry", "[ym2151]") {
    auto chip = mnemos::chips::create_chip("yamaha.ym2151");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == "ym2151");
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::audio_synth);
}

TEST_CASE("ym2151 latches an address and stores the data byte", "[ym2151]") {
    ym2151 opm;
    opm.write_address(0x28U); // channel 0 KC
    opm.write_data(0x4AU);
    CHECK(opm.register_value(0x28U) == 0x4AU);

    // BUSY holds for one sample period after the data write.
    CHECK((opm.read_status() & ym2151::status_busy) != 0U);
    opm.tick(ym2151::busy_clocks);
    CHECK((opm.read_status() & ym2151::status_busy) == 0U);
}

TEST_CASE("ym2151 timer A overflows on its documented cadence and raises IRQ", "[ym2151]") {
    ym2151 opm;
    std::vector<bool> irq_edges;
    opm.set_irq([&](bool asserted) { irq_edges.push_back(asserted); });

    // CLKA = 1022: overflow every 64 * (1024 - 1022) = 128 chip clocks.
    opm.write_address(0x10U);
    opm.write_data(0xFFU); // CLKA high 8 = 0xFF
    opm.write_address(0x11U);
    opm.write_data(0x02U); // CLKA low 2 = 0b10 -> 1022
    opm.write_address(0x14U);
    opm.write_data(0x05U); // run A + IRQ enable A

    opm.tick(127U);
    CHECK((opm.read_status() & ym2151::status_timer_a) == 0U);
    opm.tick(1U);
    CHECK((opm.read_status() & ym2151::status_timer_a) != 0U);
    REQUIRE(irq_edges.size() == 1U);
    CHECK(irq_edges[0] == true);

    // Write-one-to-clear drops the flag and the line.
    opm.write_address(0x14U);
    opm.write_data(0x15U); // keep running + enabled, reset flag A
    CHECK((opm.read_status() & ym2151::status_timer_a) == 0U);
    REQUIRE(irq_edges.size() == 2U);
    CHECK(irq_edges[1] == false);

    // It keeps overflowing on the same cadence after the reload.
    opm.tick(128U);
    CHECK((opm.read_status() & ym2151::status_timer_a) != 0U);
}

TEST_CASE("ym2151 timer B overflows on its slower cadence", "[ym2151]") {
    ym2151 opm;
    // CLKB = 254: overflow every 1024 * (256 - 254) = 2048 chip clocks.
    opm.write_address(0x12U);
    opm.write_data(0xFEU);
    opm.write_address(0x14U);
    opm.write_data(0x0AU); // run B + IRQ enable B

    opm.tick(2047U);
    CHECK((opm.read_status() & ym2151::status_timer_b) == 0U);
    CHECK_FALSE(opm.irq_asserted());
    opm.tick(1U);
    CHECK((opm.read_status() & ym2151::status_timer_b) != 0U);
    CHECK(opm.irq_asserted());
}

TEST_CASE("ym2151 flag without enable does not assert the IRQ line", "[ym2151]") {
    ym2151 opm;
    opm.write_address(0x10U);
    opm.write_data(0xFFU);
    opm.write_address(0x11U);
    opm.write_data(0x03U); // CLKA = 1023: overflow every 64 clocks
    opm.write_address(0x14U);
    opm.write_data(0x01U); // run A, IRQ disabled

    opm.tick(64U);
    CHECK((opm.read_status() & ym2151::status_timer_a) != 0U);
    CHECK_FALSE(opm.irq_asserted());
}

TEST_CASE("ym2151 emits silence until the synthesis increment", "[ym2151]") {
    ym2151 opm;
    std::vector<std::int16_t> buffer(64U, 0x7FFF);
    opm.update(buffer);
    for (const std::int16_t sample : buffer) {
        CHECK(sample == 0);
    }
}

TEST_CASE("ym2151 save and load round-trips the timer state", "[ym2151]") {
    ym2151 opm;
    opm.write_address(0x12U);
    opm.write_data(0xF0U);
    opm.write_address(0x14U);
    opm.write_data(0x0AU);
    opm.tick(5000U);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    opm.save_state(writer);

    ym2151 restored;
    mnemos::chips::state_reader reader(snapshot);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.register_value(0x12U) == 0xF0U);
    CHECK(restored.read_status() == opm.read_status());
    CHECK(restored.elapsed_clocks() == opm.elapsed_clocks());
}
