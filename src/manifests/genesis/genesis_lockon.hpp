#pragma once

#include <cstdint>
#include <span>

namespace mnemos::topology {
    class bus;
}

namespace mnemos::manifests::genesis {

    // Lock-on cartridge support (Sonic & Knuckles pass-through). A second cart is
    // plugged into the slot on top of the base cart; the base maps at $000000 as
    // usual, and the locked-on cart's ROM appears at $200000-$3FFFFF:
    //
    //   - $200000-$2FFFFF: the locked-on cart's lower 1 MiB, always visible.
    //   - $300000-$3FFFFF: the "lock-on chip" half, gated by $A130F1 bit 0. The
    //     base S&K boot code reads/sets this latch; clearing it hides the upper
    //     half (open bus). Power-on visible, so a two-ROM combine boots flat.
    //
    // This is the two-ROM combine model: it reproduces Sonic 3 & Knuckles (S&K +
    // Sonic 3) by mapping the two images together. The 256 KiB internal "UPMEM"
    // S&K carries -- the blue-sphere generator and the Knuckles-in-Sonic-1/2
    // modes -- is NOT modelled (it needs S&K's own internal data and the
    // size-dependent detection); see todo.md.
    struct lock_on_runtime final {
        bool active{false}; // true only when a lock-on cart is wired
        bool enabled{true}; // $A130F1 bit 0: lock-on chip half ($300000) visible
    };

    // Map `lock_on_rom` (the locked-on cartridge image) over $200000-$3FFFFF as a
    // priority-1 region above the base ROM, plus the $A130F1 lock-on latch. State
    // lives in `out` (borrowed by the bus handlers, so `out` must outlive `bus`);
    // the `lock_on_rom` span must likewise outlive the bus. No-op when empty.
    // Shared by assemble_genesis and build_genesis_runtime.
    void wire_cart_lockon(topology::bus& bus, lock_on_runtime& out,
                          std::span<const std::uint8_t> lock_on_rom);

} // namespace mnemos::manifests::genesis
