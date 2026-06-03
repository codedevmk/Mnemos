#include "page_guard.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::apm::memory::access_kind;
    using mnemos::apm::memory::guard_event;
    using mnemos::apm::memory::page_guard;
} // namespace

// The core mechanism the whole sidecar rests on: a guarded write must be
// intercepted (so we learn the writer's host IP), yet still complete (so the
// traced program is unaffected). Address 0x809 mimics the Genesis $FFF809 phase
// counter we are hunting.
TEST_CASE("page_guard intercepts a guarded write, records it, and the write still lands") {
    if (!page_guard::supported()) {
        SUCCEED("page_guard not implemented on this platform");
        return;
    }

    page_guard guard;
    auto* region = static_cast<std::uint8_t*>(guard.allocate(4096));
    REQUIRE(region != nullptr);

    std::vector<guard_event> events;
    auto* target = region + 0x809;
    guard.watch(target, 1, access_kind::write,
                [&](const guard_event& e) { events.push_back(e); });

    // volatile so the store is not optimised away before the read-back below.
    *static_cast<volatile std::uint8_t*>(target) = 0x42U;

    REQUIRE(events.size() == 1);
    CHECK(events[0].address == target);
    CHECK(events[0].kind == access_kind::write);
    CHECK(events[0].host_ip != 0U);
    CHECK(*target == 0x42U); // recovery worked: the value reached memory
}

// The real target ($FFF809) is a counter written every frame. The page must be
// re-armed after each recovery, or only the first write is ever seen -- a bug
// the single-write case above cannot detect.
TEST_CASE("page_guard re-arms: every write to a watched counter is intercepted") {
    if (!page_guard::supported()) {
        SUCCEED("page_guard not implemented on this platform");
        return;
    }

    page_guard guard;
    auto* region = static_cast<std::uint8_t*>(guard.allocate(4096));
    REQUIRE(region != nullptr);

    int hits = 0;
    auto* counter = region + 0x809;
    guard.watch(counter, 1, access_kind::write, [&](const guard_event&) { ++hits; });

    auto* vcounter = static_cast<volatile std::uint8_t*>(counter);
    for (int i = 0; i < 16; ++i) {
        *vcounter = static_cast<std::uint8_t>(*vcounter + 1U);
    }

    CHECK(hits == 16);     // a read-modify-write faults on the store each iteration
    CHECK(*counter == 16U);
}

// Page protection is per-page (4 KiB), so unrelated writes on the same page also
// fault. They must be recovered silently -- never reported -- or the sidecar
// drowns in false events around any hot address.
TEST_CASE("page_guard filters: writes outside the watched range are not reported") {
    if (!page_guard::supported()) {
        SUCCEED("page_guard not implemented on this platform");
        return;
    }

    page_guard guard;
    auto* region = static_cast<std::uint8_t*>(guard.allocate(4096));
    REQUIRE(region != nullptr);

    int hits = 0;
    auto* watched = region + 0x100;
    guard.watch(watched, 1, access_kind::write, [&](const guard_event&) { ++hits; });

    auto* neighbour = static_cast<volatile std::uint8_t*>(region + 0x200); // same page, not watched
    *neighbour = 0x99U;

    CHECK(hits == 0);          // neighbour write filtered out
    CHECK(*(region + 0x200) == 0x99U); // but still recovered: the value landed
}
