#pragma once

#include <cstdint>
#include <functional>
#include <utility>

namespace mnemos::chips::cpu {

    class m68000;

    // Diagnostic facade for the m68000. Owned by the CPU, accessed via
    // `m68000::diagnostics()`. Holds purely observational hooks -- enabling
    // them changes no architectural behaviour, only what an outside observer
    // can see. Operational hooks (IRQ ack, TAS suppression, Z80 bus latency,
    // delayed IRQ) stay on m68000 itself because the system NEEDS them set
    // for correct emulation.
    class m68000_diagnostics final {
      public:
        // Per-instruction cycle-cost decomposition. Populated at the END of
        // each step_instruction(); the trace callback fires at the START, so
        // when the callback observes instruction N this struct still describes
        // instruction N-1. Used by the player's --screenshot trace dump to
        // localise cycle-accounting drift without guessing from raw deltas.
        struct cycle_sources final {
            std::uint8_t refresh_fired{};    // 0 or 1 (at most once per inst)
            std::uint8_t z80_bus_accesses{}; // count of $A0xxxx accesses
            std::uint8_t irq_entered{};      // 0 or 1
        };

        m68000_diagnostics(const m68000_diagnostics&) = delete;
        m68000_diagnostics& operator=(const m68000_diagnostics&) = delete;
        m68000_diagnostics(m68000_diagnostics&&) = delete;
        m68000_diagnostics& operator=(m68000_diagnostics&&) = delete;

        // Per-instruction trace hook (off by default). Fired with the
        // instruction's PC BEFORE any decode. Unset = no trace overhead.
        void set_trace_callback(std::function<void(std::uint32_t pc)> callback) noexcept;

        // Cycle-source tags for the last completed instruction.
        [[nodiscard]] const cycle_sources& last_cycle_sources() const noexcept;

      private:
        friend class m68000;
        explicit m68000_diagnostics(m68000& owner) noexcept : owner_(&owner) {}
        m68000* owner_;
    };

} // namespace mnemos::chips::cpu
