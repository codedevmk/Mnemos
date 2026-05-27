#pragma once

// Pluggable scheduler construction for player_system adapters. The default
// path produces a `runtime::scheduler` exactly as adapters did inline before;
// substitutable so tooling (deterministic-replay drivers, frame profilers,
// step-mode debuggers) can supply a customised scheduler at construction
// time without modifying adapter code.
//
// Item 4B (slice-based multi-clock scheduler for 32X / Saturn / Sega CD)
// drops in as a second factory implementation when that work lands; the
// adapter contract here stays the same.

#include "scheduler.hpp"

#include <cstdint>
#include <vector>

namespace mnemos::chips {
    class ivideo;
} // namespace mnemos::chips

namespace mnemos::frontend_sdk {

    class scheduler_factory {
      public:
        scheduler_factory() = default;
        scheduler_factory(const scheduler_factory&) = delete;
        scheduler_factory& operator=(const scheduler_factory&) = delete;
        virtual ~scheduler_factory() = default;

        // Build the scheduler that drives a system's chips. `chips` is the
        // scheduler order (each entry pairs a chip with its master-clock
        // divider); `frame_source` is the video chip whose frame_index()
        // increment marks frame completion (may be null for headless tests).
        // Returned by value -- the adapter stores it as a member.
        [[nodiscard]] virtual runtime::scheduler
        create(std::vector<runtime::scheduled_chip> chips,
               chips::ivideo* frame_source) = 0;
    };

    // The behaviour adapters used inline before this interface existed:
    // returns a vanilla runtime::scheduler over the provided chip list.
    class default_scheduler_factory final : public scheduler_factory {
      public:
        [[nodiscard]] runtime::scheduler
        create(std::vector<runtime::scheduled_chip> chips,
               chips::ivideo* frame_source) override;
    };

} // namespace mnemos::frontend_sdk
