#include "iec_bus.hpp"

#include <catch2/catch_test_macros.hpp>

using mnemos::chips::iec_bus;
using line = mnemos::chips::iec_bus::line;

TEST_CASE("iec_bus lines float high until a device pulls them") {
    iec_bus bus;
    CHECK(bus.released(line::atn));
    CHECK(bus.released(line::clk));
    CHECK(bus.released(line::data));

    bus.set_driver(0U, line::atn, true); // the C64 asserts ATN
    CHECK(bus.asserted(line::atn));
    CHECK(bus.released(line::clk));
}

TEST_CASE("iec_bus is wired-OR across devices") {
    iec_bus bus;
    bus.set_driver(0U, line::data, true); // C64 pulls DATA
    bus.set_driver(8U, line::data, true); // drive 8 also pulls DATA
    CHECK(bus.asserted(line::data));

    bus.set_driver(0U, line::data, false); // C64 releases
    CHECK(bus.asserted(line::data));       // still held by drive 8

    bus.set_driver(8U, line::data, false); // drive 8 releases
    CHECK(bus.released(line::data));       // now free
}

TEST_CASE("iec_bus release_all drops every line for one device") {
    iec_bus bus;
    bus.set_driver(9U, line::atn, true);
    bus.set_driver(9U, line::clk, true);
    bus.set_driver(0U, line::clk, true); // another driver on CLK

    bus.release_all(9U);
    CHECK(bus.released(line::atn));
    CHECK(bus.asserted(line::clk)); // device 0 still holds CLK
}

TEST_CASE("iec_bus reset releases everything") {
    iec_bus bus;
    bus.set_driver(0U, line::atn, true);
    bus.set_driver(8U, line::data, true);
    bus.reset();
    CHECK(bus.released(line::atn));
    CHECK(bus.released(line::data));
}
