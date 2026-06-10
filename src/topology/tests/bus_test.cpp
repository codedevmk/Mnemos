#include "bus.hpp"

#include "ibus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>

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
