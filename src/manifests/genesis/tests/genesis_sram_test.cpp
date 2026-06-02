#include "genesis_runtime.hpp"

#include "bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::manifests::genesis::build_genesis_runtime;

    // A minimal cart image: plausible reset vectors plus an odd-byte SRAM header
    // ($F8, $200001-$203FFF = 8 KiB), the dominant real-world layout.
    std::vector<std::uint8_t> rom_with_odd_sram() {
        std::vector<std::uint8_t> rom(0x20000U, 0U);
        rom[2] = 0xFFU; // SSP high word -> $00FF0000 (work RAM)
        rom[7] = 0x00U;
        rom[6] = 0x02U; // PC -> $00000200
        rom[0x1B0U] = static_cast<std::uint8_t>('R');
        rom[0x1B1U] = static_cast<std::uint8_t>('A');
        rom[0x1B2U] = 0xF8U; // odd-byte
        rom[0x1B5U] = 0x20U; // start $00200001
        rom[0x1B7U] = 0x01U;
        rom[0x1B9U] = 0x20U; // end $00203FFF
        rom[0x1BAU] = 0x3FU;
        rom[0x1BBU] = 0xFFU;
        return rom;
    }
} // namespace

TEST_CASE("genesis cartridge SRAM round-trips through the main bus") {
    auto rt = build_genesis_runtime(rom_with_odd_sram());
    REQUIRE(rt->sram.info.has_value());
    CHECK(rt->sram.data.size() == 0x2000U); // 8 KiB of odd-byte storage
    auto* bus = rt->state.main_bus;

    bus->write8(0x200001U, 0x42U);
    bus->write8(0x203FFFU, 0x99U); // last odd address
    CHECK(bus->read8(0x200001U) == 0x42U);
    CHECK(bus->read8(0x203FFFU) == 0x99U);
    CHECK(bus->read8(0x200003U) == 0xFFU); // untouched cell (powers on 0xFF)
    CHECK(bus->read8(0x200002U) == 0xFFU); // even address: unpopulated byte lane
}

TEST_CASE("genesis $A130F1 gates SRAM writes") {
    auto rt = build_genesis_runtime(rom_with_odd_sram());
    auto* bus = rt->state.main_bus;

    bus->write8(0x200001U, 0x42U);
    CHECK(bus->read8(0x200001U) == 0x42U);
    bus->write8(0xA130F1U, 0x00U);         // disable SRAM
    bus->write8(0x200001U, 0x77U);         // write dropped while disabled
    bus->write8(0xA130F1U, 0x01U);         // re-enable
    CHECK(bus->read8(0x200001U) == 0x42U); // the disabled write never landed
}
