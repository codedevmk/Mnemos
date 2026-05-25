#include <mnemos/runtime/input.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using mnemos::runtime::input_buffer;
using mnemos::runtime::input_event;

TEST_CASE("input_buffer keeps events sorted by frame regardless of post order") {
    input_buffer buffer;
    buffer.post({.frame = 5U, .device = 0U, .code = 0x10U, .pressed = true});
    buffer.post({.frame = 1U, .device = 0U, .code = 0x20U, .pressed = true});
    buffer.post({.frame = 5U, .device = 0U, .code = 0x10U, .pressed = false});
    buffer.post({.frame = 3U, .device = 1U, .code = 0x01U, .pressed = true});

    const auto all = buffer.all();
    REQUIRE(all.size() == 4U);
    CHECK(all[0].frame == 1U);
    CHECK(all[1].frame == 3U);
    CHECK(all[2].frame == 5U);
    CHECK(all[3].frame == 5U);
}

TEST_CASE("input_buffer returns the contiguous slice for a frame in post order") {
    input_buffer buffer;
    buffer.post({.frame = 5U, .device = 0U, .code = 0x10U, .pressed = true});
    buffer.post({.frame = 1U, .device = 0U, .code = 0x20U, .pressed = true});
    buffer.post({.frame = 5U, .device = 0U, .code = 0x11U, .pressed = true});

    const auto frame5 = buffer.events_for_frame(5U);
    REQUIRE(frame5.size() == 2U);
    CHECK(frame5[0].code == 0x10U); // post order preserved within the frame
    CHECK(frame5[1].code == 0x11U);

    CHECK(buffer.events_for_frame(1U).size() == 1U);
    CHECK(buffer.events_for_frame(2U).empty()); // no events -> empty slice
}

TEST_CASE("input_buffer clear empties the log") {
    input_buffer buffer;
    buffer.post({.frame = 0U, .device = 0U, .code = 1U, .pressed = true});
    REQUIRE_FALSE(buffer.empty());
    buffer.clear();
    CHECK(buffer.empty());
    CHECK(buffer.size() == 0U);
}
