#include "scheduler.hpp"

#include <utility>

namespace mnemos::runtime {

    scheduler::scheduler(std::vector<scheduled_chip> chips, chips::i_video* frame_source) noexcept
        : chips_(std::move(chips)), accumulator_(chips_.size(), 0U), frame_source_(frame_source) {
        uniform_lockstep_ = !chips_.empty();
        for (const auto& sc : chips_) {
            if (sc.divider != 1U) {
                uniform_lockstep_ = false;
                break;
            }
        }
    }

    std::uint64_t scheduler::frame_index() const noexcept {
        return frame_source_ != nullptr ? frame_source_->frame_index() : 0U;
    }

    void scheduler::run_master_cycles(std::uint64_t cycles) {
        master_cycle_ += cycles;

        // Fast path: all chips tick every cycle, so a single batched tick per chip
        // is exactly equivalent to `cycles` lockstep single-cycle ticks.
        if (uniform_lockstep_) {
            for (const auto& sc : chips_) {
                sc.chip->tick(cycles);
            }
            return;
        }

        for (std::uint64_t c = 0; c < cycles; ++c) {
            for (std::size_t i = 0; i < chips_.size(); ++i) {
                if (++accumulator_[i] >= chips_[i].divider) {
                    accumulator_[i] = 0U;
                    chips_[i].chip->tick(1U);
                }
            }
        }
    }

    std::uint64_t scheduler::run_frame() {
        if (frame_source_ == nullptr) {
            return 0U;
        }

        // Lockstep across chips at one-frame-of-the-source granularity would lose
        // CPU/VIC interleave, so step a line at a time: the frame source emits a
        // frame after total_lines * cycles_per_line master cycles, but we advance
        // in single cycles until its frame counter moves.
        const std::uint64_t start = frame_source_->frame_index();
        while (frame_source_->frame_index() == start) {
            run_master_cycles(1U);
        }
        return frame_source_->frame_index();
    }

    void scheduler::run_frames(std::uint64_t count) {
        for (std::uint64_t f = 0; f < count; ++f) {
            run_frame();
        }
    }

} // namespace mnemos::runtime
