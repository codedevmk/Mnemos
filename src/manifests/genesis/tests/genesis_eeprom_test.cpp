#include "genesis_eeprom.hpp"

#include "bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace {
    using mnemos::manifests::genesis::cart_eeprom_runtime;
    using mnemos::manifests::genesis::detect_cart_eeprom;
    using mnemos::manifests::genesis::wire_cart_eeprom;

    // A ROM image carrying `serial` in the header product-code field ($0180).
    std::vector<std::uint8_t> rom_with_serial(std::string_view serial) {
        std::vector<std::uint8_t> rom(0x4000U, 0U);
        for (std::size_t i = 0; i < serial.size() && (0x180U + i) < rom.size(); ++i) {
            rom[0x180U + i] = static_cast<std::uint8_t>(serial[i]);
        }
        return rom;
    }
} // namespace

TEST_CASE("detect_cart_eeprom matches a known Acclaim 2ME serial") {
    const auto rom = rom_with_serial("GM T-081276 00"); // 24C16 part
    const auto cfg = detect_cart_eeprom(rom);
    REQUIRE(cfg.has_value());
    CHECK(cfg->size_bytes == 2048U);
    CHECK(cfg->sda_write_addr == 0x200000U);
    CHECK(cfg->scl_addr == 0x200001U);
    CHECK(cfg->sda_read_addr == 0x200001U);
    CHECK(cfg->line_bit == 0U);
}

TEST_CASE("detect_cart_eeprom sizes the larger Acclaim carts as 24C65") {
    const auto cfg = detect_cart_eeprom(rom_with_serial("GM T-81476 -00")); // Frank Thomas-class
    REQUIRE(cfg.has_value());
    CHECK(cfg->size_bytes == 8192U);
}

TEST_CASE("detect_cart_eeprom returns nullopt for an unknown cart") {
    CHECK_FALSE(detect_cart_eeprom(rom_with_serial("GM T-999999 00")).has_value());
}

TEST_CASE("detect_cart_eeprom returns nullopt for a too-short ROM") {
    CHECK_FALSE(detect_cart_eeprom(std::vector<std::uint8_t>(0x100U, 0U)).has_value());
}

TEST_CASE("wire_cart_eeprom round-trips a byte through the bus I2C lines") {
    mnemos::topology::bus bus(24); // 24-bit Genesis address space
    cart_eeprom_runtime ee;
    const auto rom = rom_with_serial("GM T-081276 00");
    wire_cart_eeprom(bus, ee, rom);
    REQUIRE(ee.info.has_value());
    REQUIRE(ee.device.has_value());

    // Drive an I2C write transaction over the bus: SDA write = $200000 bit0,
    // SCL = $200001 bit0, SDA read = $200001 bit0.
    const auto set_sda = [&bus](bool v) { bus.write8(0x200000U, v ? 1U : 0U); };
    const auto set_scl = [&bus](bool v) { bus.write8(0x200001U, v ? 1U : 0U); };
    const auto start = [&] {
        set_sda(true);
        set_scl(true);
        set_sda(false); // SDA falls while SCL high
        set_scl(false);
    };
    const auto stop = [&] {
        set_sda(false);
        set_scl(true);
        set_sda(true); // SDA rises while SCL high
    };
    const auto write_byte = [&](std::uint8_t b) {
        for (int i = 7; i >= 0; --i) {
            set_sda(((b >> i) & 1U) != 0U);
            set_scl(true);
            set_scl(false);
        }
        set_sda(true); // release for ACK
        set_scl(true);
        set_scl(false);
    };

    start();
    write_byte(0xA0); // write control
    write_byte(0x05); // word address
    write_byte(0x42); // data
    stop();
    CHECK(ee.device->bytes()[0x05] == 0x42U);
}
