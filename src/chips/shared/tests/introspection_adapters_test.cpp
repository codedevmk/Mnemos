#include "introspection_adapters.hpp"

#include "chip.hpp" // register_descriptor

#include <catch2/catch_test_macros.hpp>

#include <span>

namespace {
    using namespace mnemos::instrumentation;
    using mnemos::chips::register_descriptor;
} // namespace

TEST_CASE("callback_register_view forwards to its provider") {
    bool called = false;
    callback_register_view view([&called]() {
        called = true;
        return std::span<const register_descriptor>{};
    });
    static_cast<void>(view.registers());
    CHECK(called);
}

TEST_CASE("function_trace_target forwards install to the installer") {
    trace_target::callback installed;
    function_trace_target target(
        [&installed](trace_target::callback cb) { installed = std::move(cb); });

    bool fired = false;
    target.install([&fired](const trace_event&) { fired = true; });
    REQUIRE(static_cast<bool>(installed));
    installed(trace_event{.pc = 1U, .cycles = 2U});
    CHECK(fired);
}

TEST_CASE("function_reg_write_trace forwards install to the installer") {
    reg_write_trace::callback installed;
    function_reg_write_trace target(
        [&installed](reg_write_trace::callback cb) { installed = std::move(cb); });

    bool fired = false;
    target.install([&fired](const reg_write_event&) { fired = true; });
    REQUIRE(static_cast<bool>(installed));
    installed(reg_write_event{.port = 3U, .value = 4U});
    CHECK(fired);
}

TEST_CASE("introspection_builder exposes only the configured capabilities") {
    introspection_builder builder;
    CHECK(builder.registers() == nullptr);
    CHECK(builder.trace() == nullptr);
    CHECK(builder.reg_writes() == nullptr);

    builder.with_registers([]() { return std::span<const register_descriptor>{}; })
        .with_trace([](trace_target::callback) {});

    CHECK(builder.registers() != nullptr);
    CHECK(builder.trace() != nullptr);
    CHECK(builder.reg_writes() == nullptr); // not configured
}
