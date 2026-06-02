#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace mnemos::topology {
    class bus;
}

namespace mnemos::manifests::genesis {

    // Sega "SSF2" cartridge ROM bank-switching, for images larger than the 4 MiB
    // cartridge window. The window $000000-$3FFFFF is eight 512 KiB slots; slot 0
    // is fixed to ROM bank 0 (the running code), slots 1-7 are paged by the
    // registers at $A130F3/F5/F7/F9/FB/FD/FF (one per slot, value = 512 KiB bank).
    // Power-on mapping is linear (slot K -> bank K), so a freshly booted cart looks
    // like a flat ROM until it pages a high bank in.
    struct cart_banking_runtime final {
        bool active{false}; // true only for >4 MiB carts (banking wired)
        // 512 KiB ROM bank selected for each 512 KiB slot of the cartridge window.
        std::array<std::uint8_t, 8> bank{{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}};
    };

    // If `rom` exceeds the 4 MiB cartridge window, map a banked view of it over
    // $000000-$3FFFFF (priority 1, above the flat ROM) plus the $A130F3-FF bank
    // registers; state lives in `out` (borrowed by the bus handlers, so `out` must
    // outlive `bus`). No-op for <= 4 MiB carts. Shared by build_genesis_runtime and
    // assemble_genesis so the two paths stay byte-identical.
    void wire_cart_banking(topology::bus& bus, cart_banking_runtime& out,
                           std::span<const std::uint8_t> rom);

} // namespace mnemos::manifests::genesis
