#pragma once

#include <cstdint>
#include <functional>

namespace mnemos::chips {

    // Abstract memory-access contract a chip uses to reach the rest of a system.
    //
    // Concrete buses live in tier 3 (topology). Declaring the interface here in
    // tier 2 lets chips depend on the abstraction instead of on topology, which
    // keeps the foundation -> chips -> topology direction intact (see ADR 0004).
    // Addresses are 32-bit so the same contract serves wider CPUs; 8-bit parts
    // pass their 16-bit addresses widened to 32 bits.
    class ibus {
      public:
        ibus() = default;
        ibus(const ibus&) = delete;
        ibus& operator=(const ibus&) = delete;
        virtual ~ibus() = default;

        [[nodiscard]] virtual std::uint8_t read8(std::uint32_t address) = 0;
        virtual void write8(std::uint32_t address, std::uint8_t value) = 0;

        // Big-endian wide accesses. The defaults compose byte accesses, so
        // every implementation keeps byte-exact semantics (MMIO side effects,
        // watchpoints); a concrete bus may override them with a single
        // resolution over RAM/ROM -- the hot path for 16/32-bit CPUs whose
        // every fetch and load otherwise pays per-byte dispatch.
        [[nodiscard]] virtual std::uint16_t read16_be(std::uint32_t address) {
            return static_cast<std::uint16_t>((static_cast<std::uint16_t>(read8(address)) << 8U) |
                                              read8(address + 1U));
        }
        virtual void write16_be(std::uint32_t address, std::uint16_t value) {
            write8(address, static_cast<std::uint8_t>(value >> 8U));
            write8(address + 1U, static_cast<std::uint8_t>(value));
        }
        [[nodiscard]] virtual std::uint32_t read32_be(std::uint32_t address) {
            return (static_cast<std::uint32_t>(read16_be(address)) << 16U) |
                   read16_be(address + 2U);
        }
        virtual void write32_be(std::uint32_t address, std::uint32_t value) {
            write16_be(address, static_cast<std::uint16_t>(value >> 16U));
            write16_be(address + 2U, static_cast<std::uint16_t>(value));
        }

        // Little-endian wide accesses -- the V30/x86-family counterpart of the
        // big-endian set above, with the same contract: defaults compose byte
        // accesses, concrete buses may override with a single resolution over
        // RAM/ROM.
        [[nodiscard]] virtual std::uint16_t read16_le(std::uint32_t address) {
            return static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(read8(address)) |
                (static_cast<std::uint16_t>(read8(address + 1U)) << 8U));
        }
        virtual void write16_le(std::uint32_t address, std::uint16_t value) {
            write8(address, static_cast<std::uint8_t>(value));
            write8(address + 1U, static_cast<std::uint8_t>(value >> 8U));
        }

        // A stable read window over bus storage: the caller may index
        // data[0 .. end-start] (data points at `start`) with no further
        // dispatch -- the CPU fetch fast path. Valid until the bus signals
        // invalidation. An implementation that hands out spans MUST notify
        // every registered listener whenever a span may have moved or grown
        // side effects (remap, bank retarget, access observer installed).
        struct direct_span final {
            const std::uint8_t* data{};
            std::uint32_t start{};
            std::uint32_t end{}; // inclusive
        };
        [[nodiscard]] virtual bool direct_read_span(std::uint32_t /*address*/,
                                                    direct_span& /*out*/) {
            return false; // default: no direct access, callers use read8/...
        }
        virtual void add_invalidation_listener(std::function<void()> /*listener*/) {}
    };

} // namespace mnemos::chips
