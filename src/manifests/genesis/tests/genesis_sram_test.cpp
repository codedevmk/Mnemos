#include "genesis_runtime.hpp"

#include "bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::manifests::genesis::build_genesis_runtime;

    // Write the reset vectors + the "RA" external-RAM header into a ROM image.
    void put_header(std::vector<std::uint8_t>& rom, std::uint32_t sram_start,
                    std::uint32_t sram_end) {
        rom[2] = 0xFFU; // SSP high word -> $00FF0000 (work RAM)
        rom[6] = 0x02U; // PC -> $00000200
        rom[0x1B0U] = static_cast<std::uint8_t>('R');
        rom[0x1B1U] = static_cast<std::uint8_t>('A');
        const auto be32 = [&rom](std::uint32_t off, std::uint32_t v) {
            rom[off] = static_cast<std::uint8_t>(v >> 24U);
            rom[off + 1U] = static_cast<std::uint8_t>(v >> 16U);
            rom[off + 2U] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 3U] = static_cast<std::uint8_t>(v);
        };
        be32(0x1B4U, sram_start);
        be32(0x1B8U, sram_end);
    }

    // A small cart (128 KiB) with odd-byte SRAM at $200001-$203FFF, above the ROM.
    std::vector<std::uint8_t> rom_with_odd_sram() {
        std::vector<std::uint8_t> rom(0x20000U, 0U);
        put_header(rom, 0x200001U, 0x203FFFU);
        return rom;
    }
} // namespace

TEST_CASE("genesis cartridge SRAM round-trips through the main bus") {
    auto rt = build_genesis_runtime(rom_with_odd_sram());
    REQUIRE(rt->sram.info.has_value());
    CHECK(rt->sram.data.size() == 0x3FFFU); // one cell per window address
    CHECK(rt->sram.enabled);                // window is above the 128 KiB ROM
    auto* bus = rt->state.main_bus;

    bus->write8(0x200001U, 0x42U);
    bus->write8(0x203FFFU, 0x99U); // last window address
    CHECK(bus->read8(0x200001U) == 0x42U);
    CHECK(bus->read8(0x203FFFU) == 0x99U);
    CHECK(bus->read8(0x200003U) == 0xFFU); // untouched cell (powers on 0xFF)
    CHECK(bus->read8(0x200002U) == 0xFFU); // off-lane cell, never written
}

TEST_CASE("genesis $A130F1 bit 0 gates SRAM mapping") {
    auto rt = build_genesis_runtime(rom_with_odd_sram());
    auto* bus = rt->state.main_bus;

    bus->write8(0x200001U, 0x42U);
    CHECK(bus->read8(0x200001U) == 0x42U);
    bus->write8(0xA130F1U, 0x00U);         // unmap SRAM
    bus->write8(0x200001U, 0x77U);         // write dropped while unmapped (ROM beneath)
    bus->write8(0xA130F1U, 0x01U);         // remap
    CHECK(bus->read8(0x200001U) == 0x42U); // the dropped write never landed
}

TEST_CASE("genesis $A130F1 bit 1 write-protects mapped SRAM") {
    auto rt = build_genesis_runtime(rom_with_odd_sram());
    auto* bus = rt->state.main_bus;

    bus->write8(0x200001U, 0x42U);
    bus->write8(0xA130F1U, 0x03U);         // mapped + write-protected
    bus->write8(0x200001U, 0x77U);         // dropped: protected
    CHECK(bus->read8(0x200001U) == 0x42U); // reads still work
    bus->write8(0xA130F1U, 0x01U);         // mapped, protection cleared
    bus->write8(0x200001U, 0x77U);
    CHECK(bus->read8(0x200001U) == 0x77U);
}

TEST_CASE("genesis SRAM overlapping ROM powers on unmapped") {
    // A 3 MB cart whose SRAM window ($200001) lies inside the ROM image must boot
    // ROM-visible; the game banks SRAM in via $A130F1 only when it needs saves.
    std::vector<std::uint8_t> rom(0x300000U, 0xA5U);
    put_header(rom, 0x200001U, 0x203FFFU);
    auto rt = build_genesis_runtime(std::move(rom));
    REQUIRE(rt->sram.info.has_value());
    CHECK_FALSE(rt->sram.enabled); // overlaps ROM -> hidden at power-on
    auto* bus = rt->state.main_bus;

    CHECK(bus->read8(0x200001U) == 0xA5U); // reads the ROM beneath, not SRAM
    bus->write8(0xA130F1U, 0x01U);         // bank SRAM in
    bus->write8(0x200001U, 0x42U);
    CHECK(bus->read8(0x200001U) == 0x42U); // now the SRAM cell answers
}
