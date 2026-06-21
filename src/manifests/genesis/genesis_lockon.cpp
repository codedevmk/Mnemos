#include "genesis_lockon.hpp"

#include "bus.hpp"
#include "genesis_cart.hpp" // cart_sram_runtime (forwarded $A130F1 bits)

#include <cstddef>

namespace mnemos::manifests::genesis {

    namespace {
        constexpr std::uint32_t lockon_window = 0x300000U; // inserted-cart window base
        constexpr std::uint32_t lockon_size = 0x100000U;   // $300000-$3FFFFF
    } // namespace

    void wire_cart_lockon(topology::bus& bus, cart_lockon_runtime& out,
                          std::span<const std::uint8_t> inserted, cart_sram_runtime* sram) {
        if (inserted.empty()) {
            return; // single-cart boot: leave the bus untouched
        }
        out.active = true;
        auto* s = &out;

        // Inserted cartridge at $300000-$3FFFFF, visible only while the window
        // selects the inserted source (bit 0 clear). When bit 0 is set the base
        // patch ROM ("UPMEM") would map here -- not loaded yet, so the access
        // falls through to open bus rather than faking a source.
        bus.map_mmio(
            lockon_window, lockon_size,
            [s, inserted](std::uint32_t addr) -> std::uint8_t {
                const std::size_t off = addr - lockon_window;
                return off < inserted.size() ? inserted[off] : 0xFFU;
            },
            [](std::uint32_t, std::uint8_t) {}, // ROM ignores writes
            /*priority=*/1, [s](std::uint32_t, bool) { return !s->patch_window; });

        // $A130F1: the lock-on owns the register (priority 2, above the SRAM
        // handler) since the same byte selects the $300000 source. Bit 0 drives
        // the window latch; bit 0/1 are still forwarded to the inserted cart's
        // SRAM control so its battery-RAM map/write-protect keep working.
        bus.map_mmio(
            0xA130F1U, 1U,
            [s, sram](std::uint32_t) -> std::uint8_t {
                std::uint8_t v = s->patch_window ? 0x01U : 0x00U;
                if (sram != nullptr) {
                    v = static_cast<std::uint8_t>(v | (sram->write_protect ? 0x02U : 0x00U));
                }
                return v;
            },
            [s, sram](std::uint32_t, std::uint8_t v) {
                s->patch_window = (v & 0x01U) != 0U;
                if (sram != nullptr) {
                    sram->enabled = (v & 0x01U) != 0U;
                    sram->write_protect = (v & 0x02U) != 0U;
                }
            },
            /*priority=*/2);
    }

} // namespace mnemos::manifests::genesis
