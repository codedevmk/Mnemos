#include "genesis_eeprom.hpp"

#include "bus.hpp"

#include <array>
#include <string_view>

namespace mnemos::manifests::genesis {

    namespace {
        struct serial_entry final {
            std::string_view code;  // product code in the header serial field
            std::size_t size_bytes; // 24Cxx capacity
        };

        // Acclaim serial-EEPROM carts (the "2ME" mapper family). All share one pin
        // mapping; only the capacity differs. Keyed on the header product code, the
        // only reliable discriminator (some carts carry no external-RAM header at
        // all). Capacities per the 24Cxx part each cart is fitted with.
        constexpr std::array<serial_entry, 5> kAcclaimEeprom{{
            {"T-081276", 2048U}, // 24C16
            {"T-81406", 2048U},  // 24C16 (also the compilation carrying it)
            {"T-081586", 8192U}, // 24C65
            {"T-81476", 8192U},  // 24C65
            {"T-81576", 8192U},  // 24C65
        }};

        // All known carts wire SDA-write to the even port byte, SCL and SDA-read to
        // the odd byte, all on bit 0 (verified from the cartridge I2C driver code).
        constexpr std::uint32_t kPortBase = 0x200000U;
    } // namespace

    std::optional<cart_eeprom_config>
    detect_cart_eeprom(std::span<const std::uint8_t> rom) noexcept {
        if (rom.size() < 0x18FU) {
            return std::nullopt;
        }
        const std::string_view serial(reinterpret_cast<const char*>(rom.data() + 0x180U), 0x0FU);
        for (const auto& e : kAcclaimEeprom) {
            if (serial.find(e.code) != std::string_view::npos) {
                return cart_eeprom_config{.size_bytes = e.size_bytes,
                                          .sda_write_addr = kPortBase,
                                          .scl_addr = kPortBase + 1U,
                                          .sda_read_addr = kPortBase + 1U,
                                          .line_bit = 0U};
            }
        }
        return std::nullopt;
    }

    void wire_cart_eeprom(topology::bus& bus, cart_eeprom_runtime& out,
                          std::span<const std::uint8_t> rom) {
        out.info = detect_cart_eeprom(rom);
        if (!out.info) {
            return;
        }
        const cart_eeprom_config cfg = *out.info;
        out.device.emplace(cfg.size_bytes);
        auto* s = &out;
        const std::uint8_t mask = static_cast<std::uint8_t>(1U << cfg.line_bit);

        // Priority 2: above the cartridge ROM (0) and any battery SRAM (1), since
        // the EEPROM port can overlap the ROM image on the larger carts.
        bus.map_mmio(
            kPortBase, 2U,
            [s, cfg, mask](std::uint32_t addr) -> std::uint8_t {
                if (addr == cfg.sda_read_addr) {
                    // SDA on its bit; other bits read back high (open bus).
                    return s->device->sda() ? 0xFFU : static_cast<std::uint8_t>(0xFFU & ~mask);
                }
                return 0xFFU;
            },
            [s, cfg, mask](std::uint32_t addr, std::uint8_t v) {
                if (addr == cfg.sda_write_addr) {
                    s->sda = (v & mask) != 0U;
                }
                if (addr == cfg.scl_addr) {
                    s->scl = (v & mask) != 0U;
                }
                s->device->update(s->scl, s->sda);
            },
            /*priority=*/2);
    }

} // namespace mnemos::manifests::genesis
