#pragma once

#include <cstdint>

namespace mnemos::chips {

    // Abstract memory-access contract a chip uses to reach the rest of a system.
    //
    // Concrete buses live in tier 3 (topology). Declaring the interface here in
    // tier 2 lets chips depend on the abstraction instead of on topology, which
    // keeps the foundation -> chips -> topology direction intact (see ADR 0004).
    // Addresses are 32-bit so the same contract serves wider CPUs; 8-bit parts
    // pass their 16-bit addresses widened to 32 bits.
    class i_bus {
      public:
        i_bus() = default;
        i_bus(const i_bus&) = delete;
        i_bus& operator=(const i_bus&) = delete;
        virtual ~i_bus() = default;

        [[nodiscard]] virtual std::uint8_t read8(std::uint32_t address) = 0;
        virtual void write8(std::uint32_t address, std::uint8_t value) = 0;
    };

} // namespace mnemos::chips
