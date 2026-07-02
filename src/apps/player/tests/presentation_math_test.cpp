#include "presentation_math.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using mnemos::apps::player::frame_delta_to_window_delta;
using mnemos::apps::player::integer_letterbox;
using mnemos::apps::player::project_window_point_to_frame;
using mnemos::apps::player::window_delta_to_frame_delta;

TEST_CASE("integer_letterbox centers the largest integer-scaled frame",
          "[apps][player][presentation]") {
    const auto rect = integer_letterbox(1280, 960, 640, 256);

    CHECK(rect.x == 0);
    CHECK(rect.y == 224);
    CHECK(rect.w == 1280);
    CHECK(rect.h == 512);
}

TEST_CASE("project_window_point_to_frame maps through high-DPI pixel letterboxing",
          "[apps][player][presentation]") {
    const auto point =
        project_window_point_to_frame(320.0F, 240.0F, 640, 480, 960, 720, 320, 256);

    REQUIRE(point.inside);
    CHECK(point.rect.x == 160);
    CHECK(point.rect.y == 104);
    CHECK(point.rect.w == 640);
    CHECK(point.rect.h == 512);
    CHECK(point.frame_x == 160);
    CHECK(point.frame_y == 128);
}

TEST_CASE("project_window_point_to_frame rejects letterbox bars",
          "[apps][player][presentation]") {
    const auto point =
        project_window_point_to_frame(320.0F, 10.0F, 640, 480, 960, 720, 320, 256);

    CHECK_FALSE(point.inside);
    CHECK(point.frame_x == -1);
    CHECK(point.frame_y == -1);
}

TEST_CASE("window deltas round-trip through high-DPI presentation scale",
          "[apps][player][presentation]") {
    const double frame_delta = window_delta_to_frame_delta(8.0, 640, 960, 320, 960);
    CHECK(frame_delta == Catch::Approx(4.0));

    const double window_delta = frame_delta_to_window_delta(4, 640, 960, 320, 960);
    CHECK(window_delta == Catch::Approx(8.0));
}
