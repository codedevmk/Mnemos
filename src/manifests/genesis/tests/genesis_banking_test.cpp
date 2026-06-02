#include "genesis_banking.hpp"

#include "bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::manifests::genesis::cart_banking_runtime;
    using mnemos::manifests::genesis::wire_cart_banking;

    constexpr std::size_t bank_size = 0x80000U; // 512 KiB

    // A ROM of `n_banks` 512 KiB banks; every byte of bank K holds the value K, so
    // a read reveals which bank the accessed slot is paged to.
    std::vector<std::uint8_t> banked_rom(std::size_t n_banks) {
        std::vector<std::uint8_t> rom(n_banks * bank_size, 0U);
        for (std::size_t b = 0; b < n_banks; ++b) {
            std::fill_n(rom.begin() + static_cast<std::ptrdiff_t>(b * bank_size), bank_size,
                        static_cast<std::uint8_t>(b));
        }
        return rom;
    }
} // namespace

TEST_CASE("wire_cart_banking is a no-op for a <= 4 MiB cart") {
    mnemos::topology::bus bus(24);
    cart_banking_runtime bk;
    const auto rom = banked_rom(8); // exactly 4 MiB
    wire_cart_banking(bus, bk, rom);
    CHECK_FALSE(bk.active);
}

TEST_CASE("wire_cart_banking powers on linear and fixes slot 0 to bank 0") {
    mnemos::topology::bus bus(24);
    cart_banking_runtime bk;
    const auto rom = banked_rom(10); // 5 MiB (SSF2-class)
    wire_cart_banking(bus, bk, rom);
    REQUIRE(bk.active);
    CHECK(bus.read8(0x000000U) == 0U); // slot 0 -> bank 0
    CHECK(bus.read8(0x080000U) == 1U); // slot 1 -> bank 1
    CHECK(bus.read8(0x200000U) == 4U); // slot 4 -> bank 4
    CHECK(bus.read8(0x380000U) == 7U); // slot 7 -> bank 7
}

TEST_CASE("wire_cart_banking pages a high bank in via $A130Fx") {
    mnemos::topology::bus bus(24);
    cart_banking_runtime bk;
    const auto rom = banked_rom(10);
    wire_cart_banking(bus, bk, rom);

    bus.write8(0xA130F9U, 0x08U);      // slot 4 ($200000) <- bank 8
    CHECK(bus.read8(0x200000U) == 8U); // now reads bank 8
    CHECK(bus.read8(0x200001U) == 8U); // anywhere in the slot
    bus.write8(0xA130FFU, 0x09U);      // slot 7 ($380000) <- bank 9
    CHECK(bus.read8(0x380000U) == 9U);
    CHECK(bus.read8(0x000000U) == 0U); // slot 0 stays fixed at bank 0
}

TEST_CASE("wire_cart_banking returns open bus past the ROM image") {
    mnemos::topology::bus bus(24);
    cart_banking_runtime bk;
    const auto rom = banked_rom(10); // banks 0..9 valid
    wire_cart_banking(bus, bk, rom);
    bus.write8(0xA130F9U, 0x40U); // slot 4 <- bank 64 (out of range)
    CHECK(bus.read8(0x200000U) == 0xFFU);
}
