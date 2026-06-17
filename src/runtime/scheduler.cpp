#include "scheduler.hpp"

#include <limits>
#include <utility>

namespace mnemos::runtime {

    namespace {

        void record_first_error(schedule_error& target, schedule_error error) noexcept {
            if (target == schedule_error::none) {
                target = error;
            }
        }

        bool contains_frame_source(std::span<const scheduled_chip> chips,
                                   const chips::ivideo* frame_source) noexcept {
            if (frame_source == nullptr) {
                return true;
            }
            for (const scheduled_chip& chip : chips) {
                if (chip.chip == frame_source) {
                    return true;
                }
            }
            return false;
        }

        std::uint32_t accumulator_limit(const scheduled_chip& chip) noexcept {
            return chip.rate_num != 0U ? chip.rate_num : chip.divider;
        }

    } // namespace

    schedule_status validate_scheduled_chip(const scheduled_chip& chip) noexcept {
        if (chip.chip == nullptr) {
            return foundation::unexpected(schedule_error::null_chip);
        }
        if (chip.rate_num == 0U) {
            if (chip.divider == 0U) {
                return foundation::unexpected(schedule_error::zero_divider);
            }
            return {};
        }
        if (chip.rate_den == 0U) {
            return foundation::unexpected(schedule_error::zero_rational_denominator);
        }
        if (chip.rate_den > std::numeric_limits<std::uint32_t>::max() - chip.rate_num) {
            return foundation::unexpected(schedule_error::overflowing_rational_rate);
        }
        return {};
    }

    schedule_status validate_schedule(std::span<const scheduled_chip> chips,
                                      const chips::ivideo* frame_source) noexcept {
        for (const scheduled_chip& chip : chips) {
            schedule_status status = validate_scheduled_chip(chip);
            if (!status.has_value()) {
                return status;
            }
        }
        if (!contains_frame_source(chips, frame_source)) {
            return foundation::unexpected(schedule_error::frame_source_not_scheduled);
        }
        return {};
    }

    scheduler::scheduler(std::vector<scheduled_chip> chips, chips::ivideo* frame_source) noexcept {
        configure(std::move(chips), frame_source);
    }

    void scheduler::configure(std::vector<scheduled_chip> chips,
                              chips::ivideo* frame_source) noexcept {
        chips_.clear();
        fixed_divider_indices_.clear();
        frame_source_ = frame_source;
        config_error_ = schedule_error::none;

        chips_.reserve(chips.size());
        for (const scheduled_chip& chip : chips) {
            schedule_status status = validate_scheduled_chip(chip);
            if (status.has_value()) {
                chips_.push_back(chip);
            } else {
                record_first_error(config_error_, status.error());
            }
        }

        if (!contains_frame_source(chips_, frame_source_)) {
            record_first_error(config_error_, schedule_error::frame_source_not_scheduled);
            frame_source_ = nullptr;
        }

        accumulator_.assign(chips_.size(), 0U);
        rebuild_dispatch_tables();
    }

    void scheduler::rebuild_dispatch_tables() noexcept {
        uniform_lockstep_ = !chips_.empty();
        fixed_divider_batch_ = !chips_.empty();
        divider_one_prefix_count_ = 0U;
        fixed_divider_indices_.clear();

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
        if (chips_.empty() || cycles == 0U) {
            return;
        }

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

    void scheduler::save_state(chips::state_writer& writer) const {
        writer.u64(master_cycle_);
        writer.u32(static_cast<std::uint32_t>(accumulator_.size()));
        for (const std::uint32_t accumulator : accumulator_) {
            writer.u32(accumulator);
        }
    }

    void scheduler::load_state(chips::state_reader& reader) {
        const std::uint64_t master_cycle = reader.u64();
        const std::uint32_t count = reader.u32();
        if (!reader.ok() || count != accumulator_.size()) {
            reader.fail();
            return;
        }
        std::vector<std::uint32_t> restored;
        restored.reserve(accumulator_.size());
        for (std::size_t i = 0; i < accumulator_.size(); ++i) {
            const std::uint32_t value = reader.u32();
            if (!reader.ok() || value >= accumulator_limit(chips_[i])) {
                reader.fail();
                return;
            }
            restored.push_back(value);
        }
        master_cycle_ = master_cycle;
        accumulator_ = std::move(restored);
    }

    foundation::expected<scheduler, schedule_error>
    make_scheduler(std::vector<scheduled_chip> chips, chips::ivideo* frame_source) noexcept {
        schedule_status status =
            validate_schedule(std::span<const scheduled_chip>(chips), frame_source);
        if (!status.has_value()) {
            return foundation::unexpected(status.error());
        }
        return scheduler(std::move(chips), frame_source);
    }

} // namespace mnemos::runtime
