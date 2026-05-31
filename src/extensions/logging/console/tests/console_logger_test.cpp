#include "console_logger_provider.hpp"

#include "log_record.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <string>

namespace {
    using namespace mnemos::logging;
}

TEST_CASE("format_console_line renders level, category, message, fields", "[logging]") {
    const std::array<log_field, 1> fields{log_field{.name = "pc", .value = "1F00"}};
    const log_record_view record{.timestamp = {},
                                 .level = log_level::warning,
                                 .category = {},
                                 .message = "stall detected",
                                 .fields = fields,
                                 .source = {}};

    const std::string line = console::format_console_line(record, "genesis.cpu");

    CHECK(line.find("[warning]") != std::string::npos);
    CHECK(line.find("genesis.cpu") != std::string::npos);
    CHECK(line.find("stall detected") != std::string::npos);
    CHECK(line.find("pc=1F00") != std::string::npos);
    REQUIRE_FALSE(line.empty());
    CHECK(line.back() == '\n');
}

TEST_CASE("format_console_line appends source location when present", "[logging]") {
    const log_record_view record{.timestamp = {},
                                 .level = log_level::error,
                                 .category = {},
                                 .message = "boom",
                                 .fields = {},
                                 .source = {.file = "cpu.cpp", .line = 42U, .function = "tick"}};

    const std::string line = console::format_console_line(record, "sys");

    CHECK(line.find("@cpu.cpp:42") != std::string::npos);
}

TEST_CASE("console provider reports stream availability and never derefs null", "[logging]") {
    console::console_logger_provider active(stderr);
    auto a = active.create_logger("x");
    CHECK(a->is_enabled(log_level::error));

    console::console_logger_provider inactive(nullptr);
    auto b = inactive.create_logger("x");
    CHECK_FALSE(b->is_enabled(log_level::error));
    b->log(log_level::error, "no crash"); // must be a no-op on a null stream
}

TEST_CASE("console logger never emits an off-level record", "[logging]") {
    console::console_logger_provider provider(stderr);
    auto log = provider.create_logger("x");
    CHECK_FALSE(log->is_enabled(log_level::off));
    log->log(log_level::off, "must not print"); // gated by the off sentinel
}
