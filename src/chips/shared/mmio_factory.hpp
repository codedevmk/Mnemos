#pragma once

// Named MMIO-block factories the manifest builder consults to wire
// system-specific MMIO regions that the standard chip-MMIO-range,
// mapper-overlay, and RAM/ROM paths can't express. Examples:
//
//   - Sega Genesis controller-port block ($A10000-$A1001F): a 32-byte
//     stateful MMIO region with bespoke read/write semantics for the
//     version register, controller pads, and serial control.
//   - Sega Genesis Z80 BUSREQ ($A11100) / RESET ($A11200) registers.
//   - Sega Genesis Z80 banking window ($A12000-$A12FFF).
//   - Other 7-bit register quirks each system invents.
//
// A `[[mmio_block]]` manifest entry names a factory by string; the host
// provides the factory in an mmio_factory_table. The builder calls
// `factory(base, size)` once per block and binds the returned read/write
// pair onto the named bus.
//
// Tier 2 placement: chips don't actually consume this directly, but the
// types are shared by the manifest layer (tier 4) and host (tier 7), and
// follow the same tier rationale as callbacks/predicates.

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace mnemos::chips {

    // The read/write pair an MMIO factory hands the builder. Signatures
    // match the existing topology::bus map_mmio shape so the builder can
    // forward them verbatim.
    struct mmio_handlers final {
        std::function<std::uint8_t(std::uint32_t address)> on_read;
        std::function<void(std::uint32_t address, std::uint8_t value)> on_write;
    };

    // Factory: given the block's base address and size, return its handler
    // pair. Captures whatever system state the host needs (Genesis_system
    // struct, controller-port array, etc.) by closure.
    using mmio_factory_fn =
        std::function<mmio_handlers(std::uint32_t base, std::uint32_t size)>;

    using mmio_factory_table = std::unordered_map<std::string, mmio_factory_fn>;

} // namespace mnemos::chips
