#include "bus.hpp"

#include "ibus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {
    using mnemos::topology::bus;
}

TEST_CASE("bus routes RAM reads and writes to backing storage") {
    std::array<std::uint8_t, 0x1000> ram{};
    bus b(16U);
    b.map_ram(0x0000U, ram);

    b.write8(0x0010U, 0xABU);
    CHECK(b.read8(0x0010U) == 0xABU);
    CHECK(ram[0x10U] == 0xABU);
}

TEST_CASE("bus ROM reads back and ignores writes") {
    const std::array<std::uint8_t, 4> rom{0x11U, 0x22U, 0x33U, 0x44U};
    bus b(16U);
    b.map_rom(0xE000U, std::span<const std::uint8_t>(rom));

    CHECK(b.read8(0xE000U) == 0x11U);
    CHECK(b.read8(0xE003U) == 0x44U);
    b.write8(0xE000U, 0xFFU); // dropped
    CHECK(b.read8(0xE000U) == 0x11U);
}

TEST_CASE("bus MMIO routes to the supplied handlers") {
    std::uint8_t last_write = 0U;
    const std::uint8_t reg_value = 0x5AU;
    bus b(16U);
    b.map_mmio(
        0xD000U, 0x400U, [&](std::uint32_t) -> std::uint8_t { return reg_value; },
        [&](std::uint32_t, std::uint8_t v) { last_write = v; });

    CHECK(b.read8(0xD123U) == 0x5AU);
    b.write8(0xD200U, 0x77U);
    CHECK(last_write == 0x77U);
}

TEST_CASE("bus resolves disjoint regions and their boundaries") {
    std::array<std::uint8_t, 0x100> low{};
    std::array<std::uint8_t, 0x100> high{};
    bus b(16U);
    b.map_ram(0x0000U, low);  // $0000-$00FF
    b.map_ram(0x8000U, high); // $8000-$80FF

    b.write8(0x00FFU, 0x01U);
    b.write8(0x8000U, 0x02U);
    CHECK(b.read8(0x00FFU) == 0x01U);
    CHECK(b.read8(0x8000U) == 0x02U);
    CHECK(b.read8(0x0100U) == 0xFFU); // gap -> open bus
    CHECK(b.read8(0x8100U) == 0xFFU); // past the high region -> open bus
}

TEST_CASE("bus error regions report faults without changing ordinary open-bus gaps") {
    std::array<std::uint8_t, 0x100> ram{};
    bus b(16U);
    b.map_ram(0x0000U, ram);
    b.map_bus_error(0x0200U, 0x10U);

    CHECK(b.read8(0x0100U) == 0xFFU); // ordinary gap remains open bus
    CHECK_FALSE(b.consume_bus_fault().asserted);

    CHECK(b.read8(0x0204U) == 0xFFU); // faulting read still returns open-bus data
    auto fault = b.consume_bus_fault();
    CHECK(fault.asserted);
    CHECK(fault.address == 0x0204U);
    CHECK_FALSE(fault.write);
    CHECK_FALSE(b.consume_bus_fault().asserted);

    b.write8(0x0205U, 0xA5U);
    fault = b.consume_bus_fault();
    CHECK(fault.asserted);
    CHECK(fault.address == 0x0205U);
    CHECK(fault.write);
}

TEST_CASE("bus error regions participate in wide byte-exact accesses") {
    std::array<std::uint8_t, 0x100> ram{};
    ram[0x20U] = 0x12U;
    bus b(16U, mnemos::topology::endianness::big);
    b.map_ram(0x0000U, ram, 0);
    b.map_bus_error(0x0021U, 0x01U, 1);

    CHECK(b.read16_be(0x0020U) == 0x12FFU);
    auto fault = b.consume_bus_fault();
    CHECK(fault.asserted);
    CHECK(fault.address == 0x0021U);
    CHECK_FALSE(fault.write);

    b.write16_be(0x0020U, 0xBEEFU);
    fault = b.consume_bus_fault();
    CHECK(fault.asserted);
    CHECK(fault.address == 0x0021U);
    CHECK(fault.write);
    CHECK(ram[0x20U] == 0xBEU); // first byte completed before the second byte faulted
    CHECK(ram[0x21U] == 0x00U);
}

TEST_CASE("bus masks addresses to its width") {
    std::array<std::uint8_t, 0x10> ram{};
    bus b(16U);
    b.map_ram(0x0000U, ram);

    b.write8(0x10005U, 0x99U); // wraps to $0005 in a 16-bit space
    CHECK(b.read8(0x0005U) == 0x99U);
}

TEST_CASE("bus overlay: a ROM read-overlay shadows RAM but writes fall through") {
    std::array<std::uint8_t, 0x10000> ram{};
    std::array<std::uint8_t, 0x2000> basic{};
    basic[0] = 0xAAU;
    bool banked = true;

    bus b(16U);
    b.map_ram(0x0000U, ram, 0); // base layer, always active
    // ROM overlay active only on reads while banked (C64 BASIC behaviour).
    b.map_rom(0xA000U, std::span<const std::uint8_t>(basic), 1,
              [&](std::uint32_t, bool is_write) { return banked && !is_write; });

    CHECK(b.read8(0xA000U) == 0xAAU); // read sees BASIC ROM
    b.write8(0xA000U, 0x55U);         // write falls through to RAM underneath
    CHECK(ram[0xA000U] == 0x55U);
    CHECK(b.read8(0xA000U) == 0xAAU); // read still sees ROM, not the RAM write

    banked = false;
    CHECK(b.read8(0xA000U) == 0x55U); // unbanked: RAM is visible again
}

TEST_CASE("bus overlay: an active I/O region wins reads and writes over RAM") {
    std::array<std::uint8_t, 0x10000> ram{};
    bool io_enabled = true;
    std::uint8_t io_reg = 0x5AU;
    std::uint8_t io_last = 0U;

    bus b(16U);
    b.map_ram(0x0000U, ram, 0);
    b.map_mmio(
        0xD400U, 0x400U, [&](std::uint32_t) -> std::uint8_t { return io_reg; },
        [&](std::uint32_t, std::uint8_t v) { io_last = v; }, 2,
        [&](std::uint32_t, bool) { return io_enabled; });

    CHECK(b.read8(0xD400U) == 0x5AU);
    b.write8(0xD400U, 0x77U);
    CHECK(io_last == 0x77U);
    CHECK(ram[0xD400U] == 0x00U); // RAM untouched while I/O is active

    io_enabled = false;
    b.write8(0xD400U, 0x99U);
    CHECK(ram[0xD400U] == 0x99U); // now the write lands in RAM
    CHECK(b.read8(0xD400U) == 0x99U);
}

TEST_CASE("bus is usable through the chips::ibus interface") {
    std::array<std::uint8_t, 0x10> ram{};
    bus b(16U);
    b.map_ram(0x0000U, ram);

    mnemos::chips::ibus& as_bus = b;
    as_bus.write8(0x0003U, 0x42U);
    CHECK(as_bus.read8(0x0003U) == 0x42U);
}

TEST_CASE("bus survives an MMIO handler that remaps the bus mid-access") {
    // A control-register write that maps an overlay (the 32X ADEN pattern):
    // regions_ may reallocate while the handler's own std::function is
    // executing, so the bus must not touch the resolved region afterwards.
    static constexpr std::array<std::uint8_t, 4> overlay{0xDEU, 0xADU, 0xBEU, 0xEFU};
    bus b(24U);
    std::array<std::uint8_t, 0x10> ram{};
    b.map_ram(0x1000U, ram);
    bus* self = &b;
    int writes = 0;
    b.map_mmio(
        0x8000U, 0x10U, [](std::uint32_t) -> std::uint8_t { return 0x55U; },
        [self, &writes](std::uint32_t, std::uint8_t) {
            ++writes;
            // Force regions_ growth (and likely reallocation) from inside the
            // handler call.
            for (int i = 0; i < 16; ++i) {
                self->map_rom(0x100000U + static_cast<std::uint32_t>(i) * 0x10U, overlay, 1);
            }
        },
        1);
    b.write8(0x8000U, 0x01U);
    CHECK(writes == 1);
    CHECK(b.read8(0x100000U) == 0xDEU); // the overlay mapped by the handler is live
    CHECK(b.read8(0x8000U) == 0x55U);   // the MMIO region itself still resolves
}

TEST_CASE("bus rejects empty and zero-size mappings instead of claiming the space") {
    bus b(24U);
    b.map_ram(0x0000U, std::span<std::uint8_t>{});
    b.map_rom(0x0000U, std::span<const std::uint8_t>{});
    b.map_mmio(
        0x0000U, 0U, [](std::uint32_t) -> std::uint8_t { return 0x00U; },
        [](std::uint32_t, std::uint8_t) {});
    // An empty span used to underflow `end` to 0xFFFFFFFF and serve OOB reads.
    CHECK(b.read8(0x0000U) == 0xFFU); // open bus
    CHECK(b.read8(0x123456U) == 0xFFU);
}

TEST_CASE("bus little-endian wide accesses hit the fast span and fall back byte-exact") {
    std::array<std::uint8_t, 0x100> ram{};
    bus b(16U);
    b.map_ram(0x0000U, ram);

    SECTION("RAM round-trips low byte first") {
        b.write16_le(0x0010U, 0xBEEFU);
        CHECK(ram[0x10U] == 0xEFU);
        CHECK(ram[0x11U] == 0xBEU);
        CHECK(b.read16_le(0x0010U) == 0xBEEFU);
        // Prime the fast span via a byte read, then take the wide fast path.
        CHECK(b.read8(0x0020U) == 0x00U);
        b.write16_le(0x0020U, 0x1234U);
        CHECK(b.read16_le(0x0020U) == 0x1234U);
    }

    SECTION("MMIO composes byte accesses in low-to-high order") {
        std::vector<std::uint8_t> writes;
        b.map_mmio(
            0xD000U, 0x10U,
            [&](std::uint32_t addr) -> std::uint8_t {
                return static_cast<std::uint8_t>(addr); // low byte of the address
            },
            [&](std::uint32_t, std::uint8_t v) { writes.push_back(v); });

        CHECK(b.read16_le(0xD002U) == 0x0302U); // [D002]=02 low, [D003]=03 high
        b.write16_le(0xD004U, 0xA1B2U);
        REQUIRE(writes.size() == 2U);
        CHECK(writes[0] == 0xB2U); // low byte first
        CHECK(writes[1] == 0xA1U);
    }

    SECTION("ROM ignores wide writes on the fast path") {
        const std::array<std::uint8_t, 4> rom{0x11U, 0x22U, 0x33U, 0x44U};
        b.map_rom(0xE000U, std::span<const std::uint8_t>(rom));
        CHECK(b.read16_le(0xE000U) == 0x2211U);
        b.write16_le(0xE000U, 0xFFFFU); // dropped
        CHECK(b.read16_le(0xE000U) == 0x2211U);
    }

    SECTION("an installed observer forces the byte-exact path") {
        std::size_t events = 0U;
        b.set_access_observer([&](const mnemos::topology::access_event&) { ++events; });
        b.write16_le(0x0030U, 0xCAFEU);
        CHECK(b.read16_le(0x0030U) == 0xCAFEU);
        CHECK(events == 4U); // two byte writes + two byte reads observed
    }
}
