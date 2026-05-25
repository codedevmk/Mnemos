#pragma once

#include <chrono>

namespace mnemos::foundation {

    using steady_clock = std::chrono::steady_clock;
    using steady_time = steady_clock::time_point;
    using nanoseconds = std::chrono::nanoseconds;
    using microseconds = std::chrono::microseconds;
    using milliseconds = std::chrono::milliseconds;
    using seconds = std::chrono::seconds;

    [[nodiscard]] constexpr double to_seconds(nanoseconds duration) noexcept {
        return static_cast<double>(duration.count()) / 1'000'000'000.0;
    }

    class monotonic_timer final {
      public:
        explicit monotonic_timer(steady_time start) noexcept : start_(start) {}

        void reset(steady_time now) noexcept { start_ = now; }

        [[nodiscard]] steady_time start_time() const noexcept { return start_; }

        [[nodiscard]] nanoseconds elapsed(steady_time now) const noexcept {
            return std::chrono::duration_cast<nanoseconds>(now - start_);
        }

        [[nodiscard]] double elapsed_seconds(steady_time now) const noexcept {
            return to_seconds(elapsed(now));
        }

      private:
        steady_time start_;
    };

    class frame_timer final {
      public:
        explicit frame_timer(steady_time start) noexcept : last_tick_(start) {}

        [[nodiscard]] steady_time last_tick() const noexcept { return last_tick_; }

        [[nodiscard]] nanoseconds delta() const noexcept { return delta_; }

        [[nodiscard]] double delta_seconds() const noexcept { return to_seconds(delta_); }

        [[nodiscard]] nanoseconds tick(steady_time now) noexcept {
            delta_ = std::chrono::duration_cast<nanoseconds>(now - last_tick_);
            last_tick_ = now;
            return delta_;
        }

      private:
        steady_time last_tick_;
        nanoseconds delta_{nanoseconds::zero()};
    };

} // namespace mnemos::foundation
