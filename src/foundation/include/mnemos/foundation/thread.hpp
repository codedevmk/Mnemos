#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <latch>
#include <mutex>
#include <stop_token>
#include <thread>

namespace mnemos::foundation {

    using jthread = std::jthread;
    using latch = std::latch;
    using mutex = std::mutex;
    using scoped_lock = std::scoped_lock<mutex>;
    using stop_source = std::stop_source;
    using stop_token = std::stop_token;

    enum class wait_result : std::uint8_t {
        signaled,
        stopped,
        timeout,
    };

    class manual_reset_signal final {
      public:
        explicit manual_reset_signal(bool signaled = false) noexcept : signaled_(signaled) {}

        manual_reset_signal(const manual_reset_signal&) = delete;
        manual_reset_signal& operator=(const manual_reset_signal&) = delete;

        void set() {
            {
                std::lock_guard lock{mutex_};
                signaled_ = true;
            }
            condition_.notify_all();
        }

        void reset() {
            std::lock_guard lock{mutex_};
            signaled_ = false;
        }

        [[nodiscard]] bool is_set() const {
            std::lock_guard lock{mutex_};
            return signaled_;
        }

        [[nodiscard]] wait_result wait(stop_token stop = {}) {
            std::unique_lock lock{mutex_};
            if (condition_.wait(lock, stop, [this] { return signaled_; })) {
                return wait_result::signaled;
            }

            return wait_result::stopped;
        }

        template <typename Rep, typename Period>
        [[nodiscard]] wait_result wait_for(stop_token stop,
                                           std::chrono::duration<Rep, Period> timeout) {
            std::unique_lock lock{mutex_};
            if (condition_.wait_for(lock, stop, timeout, [this] { return signaled_; })) {
                return wait_result::signaled;
            }

            return stop.stop_requested() ? wait_result::stopped : wait_result::timeout;
        }

      private:
        mutable mutex mutex_;
        std::condition_variable_any condition_;
        bool signaled_;
    };

    class auto_reset_signal final {
      public:
        explicit auto_reset_signal(bool signaled = false) noexcept : signaled_(signaled) {}

        auto_reset_signal(const auto_reset_signal&) = delete;
        auto_reset_signal& operator=(const auto_reset_signal&) = delete;

        void set() {
            {
                std::lock_guard lock{mutex_};
                signaled_ = true;
            }
            condition_.notify_one();
        }

        void reset() {
            std::lock_guard lock{mutex_};
            signaled_ = false;
        }

        [[nodiscard]] bool is_set() const {
            std::lock_guard lock{mutex_};
            return signaled_;
        }

        [[nodiscard]] wait_result wait(stop_token stop = {}) {
            std::unique_lock lock{mutex_};
            if (!condition_.wait(lock, stop, [this] { return signaled_; })) {
                return wait_result::stopped;
            }

            signaled_ = false;
            return wait_result::signaled;
        }

        template <typename Rep, typename Period>
        [[nodiscard]] wait_result wait_for(stop_token stop,
                                           std::chrono::duration<Rep, Period> timeout) {
            std::unique_lock lock{mutex_};
            if (!condition_.wait_for(lock, stop, timeout, [this] { return signaled_; })) {
                return stop.stop_requested() ? wait_result::stopped : wait_result::timeout;
            }

            signaled_ = false;
            return wait_result::signaled;
        }

      private:
        mutable mutex mutex_;
        std::condition_variable_any condition_;
        bool signaled_;
    };

} // namespace mnemos::foundation
