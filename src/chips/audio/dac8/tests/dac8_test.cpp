#include "dac8.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::audio::dac8;
} // namespace

TEST_CASE("dac8 registers through the chip registry", "[dac8]") {
    auto chip = mnemos::chips::create_chip("generic.dac8");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == "dac8");
}

TEST_CASE("dac8 holds the written level and centres at silence", "[dac8]") {
    dac8 dac;
    CHECK(dac.output() == 0); // 0x80 midpoint

    dac.write(0xFFU);
    CHECK(dac.output() == (0xFF - 0x80) * 64);
    dac.write(0x00U);
    CHECK(dac.output() == -0x80 * 64);
    dac.write(0x80U);
    CHECK(dac.output() == 0);
}

TEST_CASE("dac8 state round-trips", "[dac8]") {
    dac8 dac;
    dac.write(0x42U);
    dac.tick(123U);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    dac.save_state(writer);

    dac8 restored;
    mnemos::chips::state_reader reader(snapshot);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.level() == 0x42U);
    CHECK(restored.elapsed_clocks() == 123U);
}
