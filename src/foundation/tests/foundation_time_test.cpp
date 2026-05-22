#include <mnemos/foundation/time.hpp>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

using namespace std::chrono_literals;

static_assert(!std::is_default_constructible_v<mnemos::foundation::monotonic_timer>);
static_assert(!std::is_default_constructible_v<mnemos::foundation::frame_timer>);

TEST_CASE("monotonic timer reports elapsed duration from injected time points") {
    const auto start = mnemos::foundation::steady_time{1s};
    mnemos::foundation::monotonic_timer timer{start};

    CHECK(timer.start_time() == start);
    CHECK(timer.elapsed(start + 16ms) == 16ms);
    CHECK(timer.elapsed_seconds(start + 1500ms) == 1.5);

    timer.reset(start + 20ms);

    CHECK(timer.start_time() == start + 20ms);
    CHECK(timer.elapsed(start + 25ms) == 5ms);
}

TEST_CASE("frame timer returns deterministic deltas") {
    mnemos::foundation::frame_timer timer{mnemos::foundation::steady_time{0ns}};

    CHECK(timer.last_tick() == mnemos::foundation::steady_time{0ns});
    CHECK(timer.delta() == 0ns);
    CHECK(timer.delta_seconds() == 0.0);

    CHECK(timer.tick(mnemos::foundation::steady_time{16ms}) == 16ms);
    CHECK(timer.last_tick() == mnemos::foundation::steady_time{16ms});
    CHECK(timer.delta() == 16ms);

    CHECK(timer.tick(mnemos::foundation::steady_time{33ms}) == 17ms);
    CHECK(timer.delta() == 17ms);
}

TEST_CASE("duration helper converts nanoseconds to seconds") {
    CHECK(mnemos::foundation::to_seconds(1'500'000'000ns) == 1.5);
}
