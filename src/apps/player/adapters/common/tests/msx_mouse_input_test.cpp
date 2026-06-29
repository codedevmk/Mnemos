#include "msx_mouse_input.hpp"

#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

namespace {
    using mnemos::apps::player::adapters::msx_mouse_tracker;
    using mnemos::peripheral::controller_state;
} // namespace

TEST_CASE("msx_mouse_tracker converts absolute pointer positions to deltas") {
    msx_mouse_tracker tracker;

    controller_state state{};
    state.aim_x = 10;
    state.aim_y = 20;
    auto delta = tracker.apply(state);
    CHECK(delta.x == 0);
    CHECK(delta.y == 0);
    CHECK_FALSE(delta.left_button);
    CHECK_FALSE(delta.right_button);

    state.aim_x = 14;
    state.aim_y = 15;
    state.trigger = true;
    state.b = true;
    delta = tracker.apply(state);
    CHECK(delta.x == 4);
    CHECK(delta.y == -5);
    CHECK(delta.left_button);
    CHECK(delta.right_button);
}

TEST_CASE("msx_mouse_tracker resets delta origin while pointer is off screen") {
    msx_mouse_tracker tracker;

    controller_state state{};
    state.aim_x = 10;
    state.aim_y = 20;
    (void)tracker.apply(state);

    state.aim_x = -1;
    state.aim_y = -1;
    (void)tracker.apply(state);

    state.aim_x = 40;
    state.aim_y = 50;
    const auto delta = tracker.apply(state);
    CHECK(delta.x == 0);
    CHECK(delta.y == 0);
}

TEST_CASE("msx_mouse_tracker persists the absolute position origin") {
    msx_mouse_tracker tracker;

    controller_state state{};
    state.aim_x = 3;
    state.aim_y = 7;
    (void)tracker.apply(state);

    std::vector<std::uint8_t> bytes;
    mnemos::chips::state_writer writer(bytes);
    tracker.save_state(writer);

    msx_mouse_tracker restored;
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{bytes});
    restored.load_state(reader);
    REQUIRE(reader.ok());

    state.aim_x = 11;
    state.aim_y = 5;
    const auto delta = restored.apply(state);
    CHECK(delta.x == 8);
    CHECK(delta.y == -2);
}
