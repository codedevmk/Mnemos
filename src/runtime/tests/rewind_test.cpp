#include "rewind.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using mnemos::runtime::rewind_ring;

namespace {
    std::vector<std::uint8_t> tag(std::uint8_t b) { return {b}; }
} // namespace

TEST_CASE("rewind_ring seeks the most recent state at or before a frame") {
    rewind_ring ring(8U);
    ring.push(0U, tag(0xA0U));
    ring.push(10U, tag(0xA1U));
    ring.push(20U, tag(0xA2U));

    REQUIRE(ring.size() == 3U);
    CHECK(ring.at_or_before(25U)->front() == 0xA2U); // newest <= 25
    CHECK(ring.at_or_before(20U)->front() == 0xA2U); // exact
    CHECK(ring.at_or_before(15U)->front() == 0xA1U); // between 10 and 20
    CHECK(ring.at_or_before(0U)->front() == 0xA0U);
    CHECK(ring.at_or_before(20U) != nullptr);
}

TEST_CASE("rewind_ring returns nullptr before the earliest state") {
    rewind_ring ring;
    ring.push(5U, tag(1U));
    CHECK(ring.at_or_before(4U) == nullptr);
    CHECK(ring.at_or_before(5U) != nullptr);
}

TEST_CASE("rewind_ring evicts the oldest beyond its depth") {
    rewind_ring ring(3U);
    for (std::uint64_t f = 0; f < 6U; ++f) {
        ring.push(f, tag(static_cast<std::uint8_t>(f)));
    }
    CHECK(ring.size() == 3U);                // only the last three kept
    CHECK(ring.at_or_before(2U) == nullptr); // frames 0-2 evicted
    CHECK(ring.at_or_before(3U)->front() == 3U);
    CHECK(ring.at_or_before(5U)->front() == 5U);
}

TEST_CASE("rewind_ring clears") {
    rewind_ring ring;
    ring.push(1U, tag(1U));
    REQUIRE_FALSE(ring.empty());
    ring.clear();
    CHECK(ring.empty());
    CHECK(ring.at_or_before(100U) == nullptr);
}
