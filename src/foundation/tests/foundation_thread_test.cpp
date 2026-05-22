#include <mnemos/foundation/thread.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <type_traits>

using namespace std::chrono_literals;

static_assert(!std::is_copy_constructible_v<mnemos::foundation::manual_reset_signal>);
static_assert(!std::is_copy_assignable_v<mnemos::foundation::manual_reset_signal>);
static_assert(!std::is_copy_constructible_v<mnemos::foundation::auto_reset_signal>);
static_assert(!std::is_copy_assignable_v<mnemos::foundation::auto_reset_signal>);

TEST_CASE("manual reset signal releases waiters and remains signaled") {
    mnemos::foundation::manual_reset_signal signal;
    mnemos::foundation::latch started{1};
    std::atomic<bool> released{false};

    mnemos::foundation::jthread worker{[&](mnemos::foundation::stop_token stop) {
        started.count_down();
        released.store(signal.wait(stop) == mnemos::foundation::wait_result::signaled);
    }};

    started.wait();
    CHECK_FALSE(released.load());

    signal.set();
    worker.join();

    CHECK(released.load());
    CHECK(signal.is_set());
    CHECK(signal.wait() == mnemos::foundation::wait_result::signaled);
}

TEST_CASE("manual reset signal reports stop requests") {
    mnemos::foundation::manual_reset_signal signal;
    mnemos::foundation::latch started{1};
    std::atomic<int> result{static_cast<int>(mnemos::foundation::wait_result::timeout)};

    mnemos::foundation::jthread worker{[&](mnemos::foundation::stop_token stop) {
        started.count_down();
        result.store(static_cast<int>(signal.wait(stop)));
    }};

    started.wait();
    worker.request_stop();
    worker.join();

    CHECK(result.load() == static_cast<int>(mnemos::foundation::wait_result::stopped));
}

TEST_CASE("manual reset signal reports timeout without changing state") {
    mnemos::foundation::manual_reset_signal signal;

    CHECK(signal.wait_for({}, 1ms) == mnemos::foundation::wait_result::timeout);
    CHECK_FALSE(signal.is_set());
}

TEST_CASE("auto reset signal consumes the signal after one waiter") {
    mnemos::foundation::auto_reset_signal signal{true};

    CHECK(signal.is_set());
    CHECK(signal.wait() == mnemos::foundation::wait_result::signaled);
    CHECK_FALSE(signal.is_set());
    CHECK(signal.wait_for({}, 1ms) == mnemos::foundation::wait_result::timeout);
}

TEST_CASE("auto reset signal wakes one worker and resets") {
    mnemos::foundation::auto_reset_signal signal;
    mnemos::foundation::latch started{1};
    std::atomic<bool> released{false};

    mnemos::foundation::jthread worker{[&](mnemos::foundation::stop_token stop) {
        started.count_down();
        released.store(signal.wait(stop) == mnemos::foundation::wait_result::signaled);
    }};

    started.wait();
    signal.set();
    worker.join();

    CHECK(released.load());
    CHECK_FALSE(signal.is_set());
}
