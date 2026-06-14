#pragma once

#include <cstdint>

namespace mnemos::chips {

    // Shared instruction-stepped catch-up loop. tick(cycles) accumulates a cycle
    // budget and runs whole instructions until it is spent, carrying the small
    // over-run forward as debt so timing stays exact across calls. CRTP, not a
    // virtual base, so the per-instruction self->step_instruction() is a direct
    // call.
    //
    // The Derived core provides (private; befriend this template):
    //   int step_instruction()  -- run exactly one instruction; return its cycles.
    template <typename Derived>
    class cpu_catch_up {
      protected:
        cpu_catch_up() = default;

        std::int64_t cycle_debt_{}; // catch-up accumulator for tick()

        void run_catch_up(std::uint64_t cycles) {
            Derived* const self = static_cast<Derived*>(this);
            cycle_debt_ += static_cast<std::int64_t>(cycles);
            while (cycle_debt_ > 0) {
                cycle_debt_ -= static_cast<std::int64_t>(self->step_instruction());
            }
        }
    };

} // namespace mnemos::chips
