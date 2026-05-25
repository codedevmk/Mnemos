#pragma once

#include <cstdint>
#include <deque>
#include <vector>

// Rewind ring (TDS §11.5): a fixed-depth circular buffer of per-frame save states.
// v0.1 stores full states (no delta encoding). Rewinding seeks the most recent
// state at or before a target frame; the caller re-executes forward from there.
namespace mnemos::runtime {

    class rewind_ring final {
      public:
        static constexpr std::size_t default_depth = 600U; // ~10 s at 60 Hz

        explicit rewind_ring(std::size_t depth = default_depth) noexcept
            : depth_(depth == 0U ? 1U : depth) {}

        // Append the state for `frame`. Frames are expected to be non-decreasing;
        // once the ring is full the oldest state is evicted.
        void push(std::uint64_t frame, std::vector<std::uint8_t> state);

        // The most recent stored state at or before `frame`, or nullptr if none.
        [[nodiscard]] const std::vector<std::uint8_t>* at_or_before(std::uint64_t frame) const;

        [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
        [[nodiscard]] std::size_t depth() const noexcept { return depth_; }
        [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
        void clear() noexcept { entries_.clear(); }

      private:
        struct entry final {
            std::uint64_t frame{};
            std::vector<std::uint8_t> state;
        };

        std::deque<entry> entries_; // oldest at front, newest at back
        std::size_t depth_;
    };

} // namespace mnemos::runtime
