#pragma once

#include "eeprom_i2c.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace mnemos::topology {
    class bus;
}

namespace mnemos::manifests::genesis {

    // Serial-EEPROM cartridge wiring: the device capacity plus the I2C pin mapping
    // (which bus address/bit carries each line). The type and wiring are not
    // discoverable from the header, so they are keyed off the cartridge serial.
    struct cart_eeprom_config final {
        std::size_t size_bytes{};       // 24Cxx capacity (selects the addressing mode)
        std::uint32_t sda_write_addr{}; // byte the game writes to drive SDA
        std::uint32_t scl_addr{};       // byte the game writes to drive SCL
        std::uint32_t sda_read_addr{};  // byte the game reads to sample SDA
        std::uint8_t line_bit{};        // bit within each port byte
    };

    // Match `rom` against the known serial-EEPROM cartridges (keyed on the header
    // serial at $0180-$018E). Returns nullopt for carts with no serial EEPROM.
    [[nodiscard]] std::optional<cart_eeprom_config>
    detect_cart_eeprom(std::span<const std::uint8_t> rom) noexcept;

    // Live serial-EEPROM state: the device and its pin mapping. The bus handlers
    // borrow this, so whatever owns it must outlive the bus it is wired onto.
    struct cart_eeprom_runtime final {
        std::optional<cart_eeprom_config> info;
        std::optional<chips::storage::eeprom_i2c> device;
        bool scl{true}; // last SCL level driven by the game
        bool sda{true}; // last SDA level driven by the game
    };

    // Detect `rom`'s serial EEPROM and, if present, build the device and map its
    // SDA/SCL onto `bus` at the configured port (priority 2, above ROM/SRAM). State
    // lives in `out` (whose device the handlers borrow, so `out` must outlive
    // `bus`). No-op when the cart declares no serial EEPROM.
    void wire_cart_eeprom(topology::bus& bus, cart_eeprom_runtime& out,
                          std::span<const std::uint8_t> rom);

} // namespace mnemos::manifests::genesis
