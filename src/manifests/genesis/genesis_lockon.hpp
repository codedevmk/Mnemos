#pragma once

#include <cstdint>
#include <span>

namespace mnemos::topology {
    class bus;
}

namespace mnemos::manifests::genesis {

    struct cart_sram_runtime; // composed with on $A130F1 (forward-declared)

    // "Lock-on" base cartridge passthrough. A base cartridge occupies the lower
    // ROM window ($000000-$1FFFFF) AND is the boot master -- the 68000 loads its
    // reset vectors, NOT the inserted game's. The inserted (locked-on) cartridge
    // maps into the $300000-$3FFFFF window. $A130F1 bit 0 selects that window's
    // source: 0 = the inserted cartridge (the plain pass-through case), 1 = the
    // base cartridge's own 256 KiB patch ROM mirrored x4 (the "UPMEM"/seed mode).
    //
    // This increment implements only the bit-0-clear case (the inserted cartridge
    // is visible at $300000). The UPMEM/patch source (bit 0 set) is not loaded
    // yet: that window reads open bus while selected, which is honest about the
    // missing image (it is NOT hardware-correct). The latch is still tracked and
    // serialized so a later increment can fill the alternate source in place.
    struct cart_lockon_runtime final {
        bool active{false}; // true only when a base+inserted pair was wired
        // $A130F1 bit 0 latch: false = inserted cartridge mapped at $300000,
        // true = the (not-yet-loaded) base patch ROM would map there.
        bool patch_window{false};
    };

    // Map the inserted cartridge `inserted` into the $300000-$3FFFFF window of
    // `bus` and compose the $A130F1 window-select latch onto `out`. The base
    // cartridge is expected to already be flat-mapped at $000000 by the caller
    // (it is the boot master). `sram` (when non-null) is the inserted cart's
    // battery-RAM control state: the lock-on $A130F1 handler forwards bit 0/1 to
    // it so SRAM map/write-protect still work while the lock-on owns the
    // register. State lives in `out` (borrowed by the bus handlers, so `out` must
    // outlive `bus`). No-op when `inserted` is empty.
    void wire_cart_lockon(topology::bus& bus, cart_lockon_runtime& out,
                          std::span<const std::uint8_t> inserted,
                          cart_sram_runtime* sram = nullptr);

} // namespace mnemos::manifests::genesis
