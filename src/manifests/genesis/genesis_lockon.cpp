#include "genesis_lockon.hpp"

#include "bus.hpp"

#include <cstddef>

namespace mnemos::manifests::genesis {

    namespace {
        constexpr std::uint32_t lock_on_base = 0x200000U; // locked-on cart window start
        constexpr std::uint32_t lock_on_size = 0x200000U; // $200000-$3FFFFF (2 MiB)
        constexpr std::uint32_t chip_half = 0x100000U;    // $300000+ = the gated lock-on chip
    } // namespace

    void wire_cart_lockon(topology::bus& bus, lock_on_runtime& out,
                          std::span<const std::uint8_t> lock_on_rom) {
        if (lock_on_rom.empty()) {
            return;
        }
        out.active = true;
        out.enabled = true;
        auto* s = &out;

        // Locked-on cart ROM at $200000-$3FFFFF (priority 1 over the base ROM).
        // The upper half ($300000-$3FFFFF) is the lock-on chip: open bus when the
        // $A130F1 latch is cleared. Writes are dropped (ROM).
        bus.map_mmio(
            lock_on_base, lock_on_size,
            [s, lock_on_rom](std::uint32_t addr) -> std::uint8_t {
                const std::uint32_t off = addr - lock_on_base;
                if (off >= chip_half && !s->enabled) {
                    return 0xFFU; // lock-on chip disabled -> open bus
                }
                return off < lock_on_rom.size() ? lock_on_rom[off] : 0xFFU;
            },
            [](std::uint32_t, std::uint8_t) {}, 1);

        // $A130F1 bit 0: lock-on chip enable (S&K toggles it after detecting the
        // pass-through cart). Bits 1+ are unused here.
        bus.map_mmio(
            0xA130F1U, 1U, [s](std::uint32_t) -> std::uint8_t { return s->enabled ? 1U : 0U; },
            [s](std::uint32_t, std::uint8_t v) { s->enabled = (v & 1U) != 0U; }, 1);
    }

} // namespace mnemos::manifests::genesis
