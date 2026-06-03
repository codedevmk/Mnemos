#pragma once

#include <cstdint>

namespace mnemos::apm::memory {

    // Identity of a guest memory bank, attached at allocation so the sidecar can
    // attribute every observed access to a chip + bank + guest address. `chip` and
    // `bank` are opaque ids assigned by the engine binding (the allocator stays
    // system-agnostic); `guest_base` is the bank's base in the guest address space,
    // so a host address can be mapped back to the guest address it represents.
    struct memory_tag {
        std::uint32_t chip;       // engine-assigned chip id
        std::uint32_t bank;       // bank id within the chip
        std::uint64_t guest_base; // base address in the guest's address space
    };

} // namespace mnemos::apm::memory
