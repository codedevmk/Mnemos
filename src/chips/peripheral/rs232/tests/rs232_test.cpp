#include <mnemos/chips/peripheral/rs232.hpp>

#include <mnemos/chips/common/chip_registry.hpp>
#include <mnemos/chips/common/state.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {
    using mnemos::chips::peripheral::rs232;
    using reset_kind = mnemos::chips::reset_kind;

    constexpr std::uint32_t cpb = 16U; // a short bit time keeps the tick counts small

    // Hold the TXD line at `level` for one full bit time while the UART ticks.
    void drive_bit(rs232& u, bool level) {
        for (std::uint32_t i = 0; i < cpb; ++i) {
            u.set_txd(level);
            u.tick(1U);
        }
    }
} // namespace

TEST_CASE("rs232 captures a framed byte off the TXD line", "[rs232]") {
    rs232 u;
    u.set_cycles_per_bit(cpb);
    std::vector<std::uint8_t> got;
    u.set_byte_sink([&](std::uint8_t b) { got.push_back(b); });

    constexpr std::uint8_t value = 0xC3U; // 1100_0011
    drive_bit(u, true);                   // idle mark
    drive_bit(u, false);                  // start bit (space)
    for (int bit = 0; bit < 8; ++bit) {
        drive_bit(u, ((value >> bit) & 1U) != 0U); // data bits, LSB first
    }
    drive_bit(u, true); // stop bit (mark)
    drive_bit(u, true); // idle again, so the stop-bit centre is sampled

    REQUIRE(got.size() == 1U);
    CHECK(got[0] == value);
}

TEST_CASE("rs232 shifts a byte out on RXD and pulses FLAG", "[rs232]") {
    rs232 a; // the RXD generator (DCE -> C64)
    rs232 b; // the TXD capturer (C64 -> DCE), driven from a's RXD line
    a.set_cycles_per_bit(cpb);
    b.set_cycles_per_bit(cpb);

    constexpr std::uint8_t value = 0x53U; // 'S'
    bool sent = false;
    a.set_byte_source([&](std::uint8_t& out) {
        if (sent) {
            return false;
        }
        out = value;
        sent = true;
        return true;
    });
    int flag_pulses = 0;
    a.set_flag_sink([&]() { ++flag_pulses; });

    std::vector<std::uint8_t> got;
    b.set_byte_sink([&](std::uint8_t byte) { got.push_back(byte); });

    // Loop a's RXD into b's TXD: a generates the waveform, b recovers the byte.
    for (int cycle = 0; cycle < 12 * static_cast<int>(cpb); ++cycle) {
        a.tick(1U);
        b.set_txd(a.rxd());
        b.tick(1U);
    }

    CHECK(flag_pulses == 1); // exactly one start-bit edge
    REQUIRE(got.size() == 1U);
    CHECK(got[0] == value); // the byte survived the round trip
}

TEST_CASE("rs232 relays a whole string through the loopback", "[rs232]") {
    rs232 a;
    rs232 b;
    a.set_cycles_per_bit(cpb);
    b.set_cycles_per_bit(cpb);

    const std::string message = "HELLO";
    std::size_t next = 0;
    a.set_byte_source([&](std::uint8_t& out) {
        if (next >= message.size()) {
            return false;
        }
        out = static_cast<std::uint8_t>(message[next++]);
        return true;
    });
    std::string got;
    b.set_byte_sink([&](std::uint8_t byte) { got.push_back(static_cast<char>(byte)); });

    for (int cycle = 0; cycle < 12 * static_cast<int>(cpb) * static_cast<int>(message.size());
         ++cycle) {
        a.tick(1U);
        b.set_txd(a.rxd());
        b.tick(1U);
    }
    CHECK(got == message);
}

TEST_CASE("rs232 round-trips its state mid-frame", "[rs232]") {
    rs232 u;
    u.set_cycles_per_bit(cpb);
    u.set_txd(false); // begin a start bit
    u.tick(cpb / 2U); // partway into capturing
    REQUIRE(u.receiving());

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    u.save_state(writer);

    rs232 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.receiving());
    CHECK(restored.cycles_per_bit() == cpb);
}

TEST_CASE("rs232 registers under commodore.rs232", "[rs232]") {
    auto chip = mnemos::chips::create_chip("commodore.rs232");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().family == std::string("uart"));
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::peripheral);
}
