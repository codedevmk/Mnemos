#include <mnemos/topology/bus.hpp>

#include <mnemos/chips/common/bus.hpp>

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

TEST_CASE("bus is usable through the chips::i_bus interface") {
    std::array<std::uint8_t, 0x10> ram{};
    bus b(16U);
    b.map_ram(0x0000U, ram);

    mnemos::chips::i_bus& as_bus = b;
    as_bus.write8(0x0003U, 0x42U);
    CHECK(as_bus.read8(0x0003U) == 0x42U);
}
