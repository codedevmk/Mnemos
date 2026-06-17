#pragma once

#include "chip.hpp"
#include "expected_ext.hpp"
#include "state.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace mnemos::runtime {

    // One chip on the master clock: the chip and how many master-clock cycles
    // advance it by exactly one chip cycle. A divider of 1 means the chip ticks
    // every master cycle (lockstep with the master clock).
    //
    // A chip whose crystal is not an integer divider of the master clock (the
    // M72's 3.579545 MHz YM2151 against the 32 MHz board crystal) sets the
    // RATIONAL rate instead: the chip ticks `rate_den` times per `rate_num`
    // master cycles, spread evenly by an integer accumulator (acc += rate_den
    // per master cycle; each time acc reaches rate_num the chip ticks once and
    // acc decreases by rate_num). Integer-only state, fixed dispatch order --
    // determinism is preserved. rate_num == 0 selects the plain divider.
    struct scheduled_chip final {
        chips::ichip* chip{};
        std::uint32_t divider{1U};
        std::uint32_t rate_num{0U}; // master cycles per `rate_den` chip cycles
        std::uint32_t rate_den{0U};
    };

    enum class schedule_error : std::uint8_t {
        none,
        null_chip,
        zero_divider,
        zero_rational_denominator,
        overflowing_rational_rate,
        frame_source_not_scheduled,
    };

    [[nodiscard]] constexpr std::string_view schedule_error_name(schedule_error error) noexcept {
        switch (error) {
        case schedule_error::none:
            return "none";
        case schedule_error::null_chip:
            return "null_chip";
        case schedule_error::zero_divider:
            return "zero_divider";
        case schedule_error::zero_rational_denominator:
            return "zero_rational_denominator";
        case schedule_error::overflowing_rational_rate:
            return "overflowing_rational_rate";
        case schedule_error::frame_source_not_scheduled:
            return "frame_source_not_scheduled";
        }

        return "unknown";
    }

    using schedule_status = foundation::status<schedule_error>;

    [[nodiscard]] schedule_status validate_scheduled_chip(const scheduled_chip& chip) noexcept;
    [[nodiscard]] schedule_status validate_schedule(std::span<const scheduled_chip> chips,
                                                    const chips::ivideo* frame_source) noexcept;

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

        // Serialise the pacing state (master cycle + per-chip accumulators) so a
        // save/restore resumes at the exact sub-divider phase. The chip set and
        // dividers are fixed at construction and recomputed there, so only the
        // mutable counters are stored. load_state fails (state_reader::fail) if the
        // accumulator count does not match this scheduler's chip set.
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);

        [[nodiscard]] std::uint64_t master_cycle() const noexcept { return master_cycle_; }
        [[nodiscard]] std::uint64_t frame_index() const noexcept;
        [[nodiscard]] chips::ivideo* frame_source() const noexcept { return frame_source_; }
        [[nodiscard]] bool config_valid() const noexcept {
            return config_error_ == schedule_error::none;
        }
        [[nodiscard]] schedule_error config_error() const noexcept { return config_error_; }

      private:
        void configure(std::vector<scheduled_chip> chips, chips::ivideo* frame_source) noexcept;
        void rebuild_dispatch_tables() noexcept;

        std::vector<scheduled_chip> chips_;
        // Per-chip progress toward the next tick: master cycles since the last
        // tick for divider chips, the rational accumulator (bounded below
        // rate_num + rate_den) for rational-rate chips.
        std::vector<std::uint32_t> accumulator_;
        chips::ivideo* frame_source_{};
        std::uint64_t master_cycle_{};
        bool uniform_lockstep_{};    // every divider == 1: tick all chips together
        bool fixed_divider_batch_{}; // fixed dividers with batch-safe divider-1 prefix
        std::size_t divider_one_prefix_count_{};
        std::vector<std::size_t> fixed_divider_indices_;
        schedule_error config_error_{schedule_error::none};
    };

    [[nodiscard]] foundation::expected<scheduler, schedule_error>
    make_scheduler(std::vector<scheduled_chip> chips, chips::ivideo* frame_source) noexcept;

} // namespace mnemos::runtime
