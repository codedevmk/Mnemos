#pragma once

#include "ibus.hpp" // chips::ibus + ibus::direct_span

#include <cstdint>

namespace mnemos::chips {

    // Shared instruction-fetch fast-path span for the 16-bit big-endian cores
    // (SH-2, M68000). The core caches a contiguous, directly-addressable region
    // returned by ibus::direct_read_span and reads instruction words straight out
    // of it; writes through the bus land in the same storage (self-modifying code
    // stays visible), and a bus invalidation drops the cache. Only the cached span
    // state and the refill live here -- each core keeps its own fast-path read in
    // its decode loop. CRTP, not a virtual base, so there is no indirection on the
    // hot path.
    //
    // The Derived core provides (private; befriend this template):
    //   ibus* fetch_bus() const noexcept   -- the attached bus, or nullptr
    //   std::uint16_t rd16(std::uint32_t)  -- the slow bus-read fallback
    // and may shadow:
    //   bool fetch_span_excluded(std::uint32_t) const noexcept -- addresses that
    //       must never be served from a span (e.g. SH-2's on-chip register window).
    template <typename Derived> class cpu_fetch_span {
      protected:
        cpu_fetch_span() = default;

        const std::uint8_t* fetch_data_{}; // cached region base (nullptr = none)
        std::uint32_t fetch_lo_{};         // first address the region covers
        std::uint32_t fetch_len_{};        // region length in bytes (0 = none)

        // Wire the cache to a bus: clear it now and on every invalidation.
        void install_fetch_invalidation(ibus& bus) noexcept {
            fetch_len_ = 0U;
            bus.add_invalidation_listener([this]() noexcept { fetch_len_ = 0U; });
        }

        // Refill the span from the bus, then read the big-endian word at `a`. MMIO,
        // excluded windows, and observer-guarded buses give no span -- the core's
        // rd16 fallback runs.
        std::uint16_t fetch_span_refill(std::uint32_t a) {
            Derived* const self = static_cast<Derived*>(this);
            if (self->fetch_bus() != nullptr && !self->fetch_span_excluded(a)) {
                ibus::direct_span span;
                // Instruction fetch: an opcode/data-split board (CPS-2) serves a
                // decrypted opcode image here; every other bus returns its data
                // span unchanged (direct_opcode_span defaults to direct_read_span).
                if (self->fetch_bus()->direct_opcode_span(a, span)) {
                    const std::uint32_t len = span.end - span.start;
                    fetch_data_ = span.data;
                    fetch_lo_ = span.start;
                    fetch_len_ = len == 0xFFFFFFFFU ? len : len + 1U;
                    const std::uint32_t off = a - span.start;
                    if (off + 2U <= fetch_len_) {
                        return static_cast<std::uint16_t>(
                            (static_cast<std::uint16_t>(span.data[off]) << 8U) |
                            span.data[off + 1U]);
                    }
                }
            }
            return self->rd16(a);
        }

        // Default: serve every address from a span. Cores with addresses that must
        // bypass the cache shadow this.
        [[nodiscard]] bool fetch_span_excluded(std::uint32_t /*a*/) const noexcept { return false; }
    };

} // namespace mnemos::chips
