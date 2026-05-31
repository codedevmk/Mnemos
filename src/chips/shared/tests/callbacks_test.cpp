// Pins down the typed-callback lookup helper that chips use to resolve
// host-supplied named callbacks during configure(). Type mismatches must
// return nullptr (silent miss); exact-signature matches return a pointer
// to the held std::function.

#include "callbacks.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <functional>

using mnemos::chips::callback_table;
using mnemos::chips::callback_value;
using mnemos::chips::find_callback;

TEST_CASE("find_callback returns nullptr for an unknown name", "[callbacks]") {
    callback_table cbs;
    CHECK(find_callback<void(int)>(cbs, "missing") == nullptr);
}

TEST_CASE("find_callback returns the function for a matching signature", "[callbacks]") {
    callback_table cbs;
    int observed_level = -1;
    cbs.emplace("vdp_irq_ack", callback_value{std::function<void(int)>{
                                   [&observed_level](int level) { observed_level = level; }}});

    const auto* fn = find_callback<void(int)>(cbs, "vdp_irq_ack");
    REQUIRE(fn != nullptr);
    (*fn)(6);
    CHECK(observed_level == 6);
}

TEST_CASE("find_callback returns nullptr when the variant holds a different signature",
          "[callbacks]") {
    callback_table cbs;
    cbs.emplace("vblank", callback_value{std::function<void(bool)>{[](bool) {}}});

    // The name is present but registered under void(bool); requesting void(int)
    // must miss rather than coerce.
    CHECK(find_callback<void(int)>(cbs, "vblank") == nullptr);
    // The actual registered signature resolves.
    CHECK(find_callback<void(bool)>(cbs, "vblank") != nullptr);
}

TEST_CASE("find_callback supports all four currently-defined signatures", "[callbacks]") {
    callback_table cbs;
    cbs.emplace("a", callback_value{std::function<void(int)>{[](int) {}}});
    cbs.emplace("b", callback_value{std::function<void(std::uint32_t)>{[](std::uint32_t) {}}});
    cbs.emplace("c", callback_value{std::function<std::uint16_t(std::uint32_t)>{
                         [](std::uint32_t) -> std::uint16_t { return 0; }}});
    cbs.emplace("d", callback_value{std::function<void(bool)>{[](bool) {}}});

    CHECK(find_callback<void(int)>(cbs, "a") != nullptr);
    CHECK(find_callback<void(std::uint32_t)>(cbs, "b") != nullptr);
    CHECK(find_callback<std::uint16_t(std::uint32_t)>(cbs, "c") != nullptr);
    CHECK(find_callback<void(bool)>(cbs, "d") != nullptr);
}
