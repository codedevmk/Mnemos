#include "genesis_runtime.hpp"
#include "genesis_system.hpp"

#include "bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::manifests::genesis::assemble_genesis;
    using mnemos::manifests::genesis::build_genesis_runtime;
    using mnemos::manifests::genesis::genesis_config;

    // A 2 MiB base cart (Sonic & Knuckles-sized), filled with a marker byte.
    std::vector<std::uint8_t> base_rom() { return std::vector<std::uint8_t>(0x200000U, 0x11U); }

    // A 2 MiB locked-on cart: lower 1 MiB = 0x22, upper 1 MiB (the lock-on chip
    // half) = 0x33, so reads distinguish $200000-$2FFFFF from $300000-$3FFFFF.
    std::vector<std::uint8_t> locked_rom() {
        std::vector<std::uint8_t> rom(0x200000U, 0x22U);
        for (std::size_t i = 0x100000U; i < rom.size(); ++i) {
            rom[i] = 0x33U;
        }
        return rom;
    }

    genesis_config with_lock_on() {
        genesis_config cfg;
        cfg.lock_on_rom = locked_rom();
        return cfg;
    }
} // namespace

TEST_CASE("genesis lock-on maps the pass-through cart at $200000-$3FFFFF") {
    auto sys = assemble_genesis(base_rom(), with_lock_on());
    REQUIRE(sys->lockon.active);
    CHECK(sys->lockon.enabled); // power-on visible, so a two-ROM combine boots flat
    auto& bus = sys->bus;

    CHECK(bus.read8(0x000000U) == 0x11U); // base ROM still at $000000
    CHECK(bus.read8(0x1FFFFFU) == 0x11U);
    CHECK(bus.read8(0x200000U) == 0x22U); // locked-on cart, lower half
    CHECK(bus.read8(0x2FFFFFU) == 0x22U);
    CHECK(bus.read8(0x300000U) == 0x33U); // lock-on chip half (enabled)
    CHECK(bus.read8(0x3FFFFFU) == 0x33U);
}

TEST_CASE("genesis lock-on $A130F1 bit 0 gates the $300000 chip half") {
    auto sys = assemble_genesis(base_rom(), with_lock_on());
    auto& bus = sys->bus;

    CHECK(bus.read8(0x300000U) == 0x33U); // enabled at power-on
    CHECK(bus.read8(0xA130F1U) == 0x01U); // latch reads back the enable bit

    bus.write8(0xA130F1U, 0x00U); // clear bit 0 -> hide the lock-on chip half
    CHECK_FALSE(sys->lockon.enabled);
    CHECK(bus.read8(0x300000U) == 0xFFU); // open bus while disabled
    CHECK(bus.read8(0x200000U) == 0x22U); // lower half unaffected by the latch

    bus.write8(0xA130F1U, 0x01U); // re-enable
    CHECK(sys->lockon.enabled);
    CHECK(bus.read8(0x300000U) == 0x33U);
}

TEST_CASE("genesis without a lock-on cart does not wire the pass-through window") {
    auto sys = assemble_genesis(base_rom()); // no lock-on ROM
    CHECK_FALSE(sys->lockon.active);
}

TEST_CASE("genesis lock-on is wired identically on the manifest path") {
    auto rt = build_genesis_runtime(base_rom(), with_lock_on());
    REQUIRE(rt->lockon.active);
    auto* bus = rt->state.main_bus;

    CHECK(bus->read8(0x200000U) == 0x22U);
    CHECK(bus->read8(0x300000U) == 0x33U);
    bus->write8(0xA130F1U, 0x00U);
    CHECK(bus->read8(0x300000U) == 0xFFU);
}
