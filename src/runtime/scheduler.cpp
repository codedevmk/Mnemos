#include "scheduler.hpp"

#include <utility>

namespace mnemos::runtime {

    scheduler::scheduler(std::vector<scheduled_chip> chips, chips::ivideo* frame_source) noexcept
        : chips_(std::move(chips)), accumulator_(chips_.size(), 0U), frame_source_(frame_source) {
        uniform_lockstep_ = !chips_.empty();
        fixed_divider_batch_ = !chips_.empty();
        bool seen_divided = false;
        for (std::size_t i = 0; i < chips_.size(); ++i) {
            const auto& sc = chips_[i];
            if (sc.divider != 1U || sc.rate_num != 0U) {
                uniform_lockstep_ = false;
            }
            if (sc.rate_num != 0U) {
                fixed_divider_batch_ = false;
            } else if (sc.divider == 1U) {
                if (seen_divided) {
                    fixed_divider_batch_ = false;
                } else {
                    ++divider_one_prefix_count_;
                }
            } else {
                seen_divided = true;
                fixed_divider_indices_.push_back(i);
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

        if (fixed_divider_batch_) {
            std::uint64_t remaining = cycles;
            while (remaining > 0U) {
                std::uint64_t step = remaining;
                for (const std::size_t i : fixed_divider_indices_) {
                    const scheduled_chip& sc = chips_[i];
                    const auto wait = static_cast<std::uint64_t>(sc.divider - accumulator_[i]);
                    if (wait < step) {
                        step = wait;
                    }
                }

                for (std::size_t i = 0; i < divider_one_prefix_count_; ++i) {
                    chips_[i].chip->tick(step);
                }
                for (const std::size_t i : fixed_divider_indices_) {
                    const scheduled_chip& sc = chips_[i];
                    accumulator_[i] += static_cast<std::uint32_t>(step);
                    if (accumulator_[i] >= sc.divider) {
                        accumulator_[i] = 0U;
                        sc.chip->tick(1U);
                    }
                }
                remaining -= step;
            }
            return;
        }

        for (std::uint64_t c = 0; c < cycles; ++c) {
            for (std::size_t i = 0; i < chips_.size(); ++i) {
                const scheduled_chip& sc = chips_[i];
                if (sc.rate_num != 0U) {
                    // Rational rate: rate_den chip cycles per rate_num master
                    // cycles, spread evenly by the accumulator.
                    accumulator_[i] += sc.rate_den;
                    while (accumulator_[i] >= sc.rate_num) {
                        accumulator_[i] -= sc.rate_num;
                        sc.chip->tick(1U);
                    }
                } else if (++accumulator_[i] >= sc.divider) {
                    accumulator_[i] = 0U;
                    sc.chip->tick(1U);
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
