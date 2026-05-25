#pragma once

#include "chip.hpp"

#include <cstdint>
#include <vector>

namespace mnemos::runtime {

    // One chip on the master clock: the chip and how many master-clock cycles
    // advance it by exactly one chip cycle. A divider of 1 means the chip ticks
    // every master cycle (lockstep with the master clock).
    struct scheduled_chip final {
        chips::ichip* chip{};
        std::uint32_t divider{1U};
    };

    // Fixed-divider master-clock scheduler (TDS §11.2).
    //
    // Each master cycle, every chip whose divider boundary is reached is ticked by
    // one chip cycle, in the order the chips were supplied. This is deterministic
    // and correct for the v0.1 systems, whose chips share a single phase clock; a
    // slice-based scheduler for dynamic clock ratios is a v0.2 concern.
    //
    // Chip order is the per-cycle dispatch order: for the C64, list the VIC-II
    // before the CPU so the CPU observes the freshly advanced beam each cycle.
    //
    // Frame boundaries are observed from a designated video chip's frame_index();
    // the scheduler itself stays rendering-agnostic.
    class scheduler final {
      public:
        scheduler(std::vector<scheduled_chip> chips, chips::ivideo* frame_source) noexcept;

        // Advance the master clock by `cycles`, dispatching due chip ticks.
        void run_master_cycles(std::uint64_t cycles);

        // Advance until the frame source completes its next frame; returns the new
        // frame index. With no frame source this is a no-op returning 0.
        std::uint64_t run_frame();

        // Advance `count` whole frames.
        void run_frames(std::uint64_t count);

        [[nodiscard]] std::uint64_t master_cycle() const noexcept { return master_cycle_; }
        [[nodiscard]] std::uint64_t frame_index() const noexcept;
        [[nodiscard]] chips::ivideo* frame_source() const noexcept { return frame_source_; }

      private:
        std::vector<scheduled_chip> chips_;
        std::vector<std::uint32_t> accumulator_; // per-chip master cycles since last tick
        chips::ivideo* frame_source_{};
        std::uint64_t master_cycle_{};
        bool uniform_lockstep_{}; // every divider == 1: tick all chips together
    };

} // namespace mnemos::runtime
