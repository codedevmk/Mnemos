#include "genesis_lockon.hpp"

#include "bus.hpp"
#include "genesis_cart.hpp"   // cart_sram_runtime (composition target)
#include "genesis_system.hpp" // assemble_genesis (boot-master inversion path)

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::manifests::genesis::assemble_genesis;
    using mnemos::manifests::genesis::cart_lockon_runtime;
    using mnemos::manifests::genesis::cart_sram_runtime;
    using mnemos::manifests::genesis::genesis_config;
    using mnemos::manifests::genesis::wire_cart_lockon;

    // A 2 MiB image whose every byte holds `marker` -- so a read reveals which
    // cartridge (base vs inserted) answered the window.
    std::vector<std::uint8_t> filled_rom(std::uint8_t marker, std::size_t size = 0x200000U) {
        return std::vector<std::uint8_t>(size, marker);
    }

    // A bootable base cartridge whose reset vectors and a $000000 marker are
    // distinct from anything the inserted game would place there. PC entry =
    // $00000100 is the recognizable boot-master signature.
    std::vector<std::uint8_t> base_rom() {
        std::vector<std::uint8_t> rom(0x200000U, 0xB5U); // 'B'ase filler
        const auto w32 = [&](std::size_t off, std::uint32_t v) {
            rom[off + 0] = static_cast<std::uint8_t>(v >> 24U);
            rom[off + 1] = static_cast<std::uint8_t>(v >> 16U);
            rom[off + 2] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 3] = static_cast<std::uint8_t>(v);
        };
        const auto w16 = [&](std::size_t off, std::uint16_t v) {
            rom[off + 0] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 1] = static_cast<std::uint8_t>(v);
        };
        w32(0x00, 0x00FFF000U); // SSP
        w32(0x04, 0x00000100U); // PC (boot-master entry signature)
        w16(0x100, 0x60FE);     // BRA.S * (spin)
        return rom;
    }

    // A bootable inserted game with a DIFFERENT reset PC ($00000200) so a test
    // can prove the CPU did NOT boot from it.
    std::vector<std::uint8_t> inserted_rom() {
        std::vector<std::uint8_t> rom(0x200000U, 0x19U); // 'I'nserted filler
        const auto w32 = [&](std::size_t off, std::uint32_t v) {
            rom[off + 0] = static_cast<std::uint8_t>(v >> 24U);
            rom[off + 1] = static_cast<std::uint8_t>(v >> 16U);
            rom[off + 2] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 3] = static_cast<std::uint8_t>(v);
        };
        w32(0x00, 0x00FF0000U); // SSP (distinct)
        w32(0x04, 0x00000200U); // PC (distinct -- must NOT be what boots)
        return rom;
    }
} // namespace

TEST_CASE("wire_cart_lockon is a no-op without an inserted cart") {
    mnemos::topology::bus bus(24, mnemos::topology::endianness::big);
    cart_lockon_runtime lk;
    wire_cart_lockon(bus, lk, {});
    CHECK_FALSE(lk.active);
    CHECK(bus.read8(0x300000U) == 0xFFU); // nothing mapped -> open bus
}

TEST_CASE("wire_cart_lockon maps the inserted cart at $300000") {
    mnemos::topology::bus bus(24, mnemos::topology::endianness::big);
    cart_lockon_runtime lk;
    const auto inserted = filled_rom(0x42U);
    wire_cart_lockon(bus, lk, inserted);
    REQUIRE(lk.active);
    CHECK(bus.read8(0x300000U) == 0x42U); // window base
    CHECK(bus.read8(0x3FFFFFU) == 0x42U); // window top
}

TEST_CASE("$A130F1 bit 0 toggles the $300000 window source") {
    mnemos::topology::bus bus(24, mnemos::topology::endianness::big);
    cart_lockon_runtime lk;
    const auto inserted = filled_rom(0x42U);
    wire_cart_lockon(bus, lk, inserted);

    // Power-on: bit 0 clear -> inserted cart visible.
    CHECK_FALSE(lk.patch_window);
    CHECK(bus.read8(0x300000U) == 0x42U);

    // bit 0 set -> the (unloaded) patch source is selected; window reads open
    // bus rather than faking a source (UPMEM is a documented follow-up).
    bus.write8(0xA130F1U, 0x01U);
    CHECK(lk.patch_window);
    CHECK(bus.read8(0x300000U) == 0xFFU);
    CHECK((bus.read8(0xA130F1U) & 1U) == 1U);

    // Clear bit 0 -> inserted cart visible again.
    bus.write8(0xA130F1U, 0x00U);
    CHECK_FALSE(lk.patch_window);
    CHECK(bus.read8(0x300000U) == 0x42U);
}

TEST_CASE("lock-on $A130F1 composes with the inserted cart's SRAM control") {
    mnemos::topology::bus bus(24, mnemos::topology::endianness::big);
    cart_lockon_runtime lk;
    cart_sram_runtime sram; // bit 0/1 forwarded here
    const auto inserted = filled_rom(0x42U);
    wire_cart_lockon(bus, lk, inserted, &sram);

    bus.write8(0xA130F1U, 0x03U);                   // bit 0 = map/select, bit 1 = write-protect
    CHECK(lk.patch_window);                         // window-select latch took bit 0
    CHECK(sram.enabled);                            // SRAM map bit forwarded
    CHECK(sram.write_protect);                      // SRAM write-protect bit forwarded
    CHECK((bus.read8(0xA130F1U) & 0x02U) == 0x02U); // WP reads back
}

TEST_CASE("assemble_genesis boots from the BASE cart, inserted game at $300000") {
    genesis_config cfg{};
    cfg.inserted_rom = inserted_rom();
    auto sys = assemble_genesis(base_rom(), cfg);

    REQUIRE(sys->lockon.active);

    // Boot-master inversion: $000000 reads resolve to the BASE cart, and the
    // 68000 loaded ITS reset vectors -- PC = the base entry ($100), not the
    // inserted game's ($200).
    CHECK(sys->bus.read8(0x000000U) == 0x00U); // SSP high byte of $00FFF000
    CHECK(sys->bus.read8(0x000007U) == 0x00U); // PC low byte of $00000100
    CHECK(sys->bus.read8(0x000006U) == 0x01U); // PC byte $01 (base entry)
    CHECK(sys->cpu.cpu_registers().pc == 0x00000100U);

    // The inserted game answers the $300000 window (its filler byte past the
    // reset vectors), proving it is mapped there and NOT at $000000.
    CHECK(sys->bus.read8(0x300008U) == 0x19U); // inserted filler (offset 8)
    CHECK(sys->bus.read8(0x3FFFFFU) == 0x19U); // window top
    CHECK(sys->bus.read8(0x100000U) == 0xB5U); // mid-base still the base cart
}

TEST_CASE("assemble_genesis without an inserted cart leaves $300000 unmapped") {
    auto sys = assemble_genesis(base_rom()); // single cart (default config)
    CHECK_FALSE(sys->lockon.active);
    CHECK(sys->bus.read8(0x300000U) == 0xFFU); // open bus -- no lock-on window
    CHECK(sys->cpu.cpu_registers().pc == 0x00000100U);
}
