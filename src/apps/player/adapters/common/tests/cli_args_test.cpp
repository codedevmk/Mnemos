#include "cli_args.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
    using mnemos::apps::player::adapters::parse_rom_arg;
    using mnemos::apps::player::adapters::parse_screenshot_args;

    // parse_*_arg takes a `char* []`, so the helper hands them a mutable
    // backing vector and a parallel vector of pointers into it.
    struct argv_holder {
        std::vector<std::string> storage;
        std::vector<char*> argv;
        [[nodiscard]] int argc() const noexcept { return static_cast<int>(argv.size()); }
    };

    argv_holder make_argv(std::initializer_list<const char*> args) {
        argv_holder h;
        h.storage.reserve(args.size());
        h.argv.reserve(args.size());
        for (const auto* s : args) {
            h.storage.emplace_back(s);
        }
        for (auto& s : h.storage) {
            h.argv.push_back(s.data());
        }
        return h;
    }
} // namespace

TEST_CASE("cli_args: --rom and -r return the path") {
    auto a = make_argv({"player", "--rom", "game.bin"});
    REQUIRE(parse_rom_arg(a.argc(), a.argv.data()) == "game.bin");

    auto b = make_argv({"player", "-r", "other.smd"});
    REQUIRE(parse_rom_arg(b.argc(), b.argv.data()) == "other.smd");
}

TEST_CASE("cli_args: missing --rom returns nullopt") {
    auto a = make_argv({"player"});
    REQUIRE(parse_rom_arg(a.argc(), a.argv.data()) == std::nullopt);

    auto b = make_argv({"player", "--region", "pal"});
    REQUIRE(parse_rom_arg(b.argc(), b.argv.data()) == std::nullopt);
}

TEST_CASE("cli_args: --rom without a value returns nullopt") {
    auto a = make_argv({"player", "--rom"});
    REQUIRE(parse_rom_arg(a.argc(), a.argv.data()) == std::nullopt);
}

TEST_CASE("cli_args: --screenshot + --frames returns the request") {
    auto a = make_argv({"player", "--screenshot", "out.ppm", "--frames", "120"});
    const auto req = parse_screenshot_args(a.argc(), a.argv.data());
    REQUIRE(req.has_value());
    CHECK(req->path == "out.ppm");
    CHECK(req->frames == 120U);
}

TEST_CASE("cli_args: --screenshot alone (no --frames) returns nullopt") {
    auto a = make_argv({"player", "--screenshot", "out.ppm"});
    CHECK(parse_screenshot_args(a.argc(), a.argv.data()) == std::nullopt);
}

TEST_CASE("cli_args: --frames alone (no --screenshot) returns nullopt") {
    auto a = make_argv({"player", "--frames", "100"});
    CHECK(parse_screenshot_args(a.argc(), a.argv.data()) == std::nullopt);
}

TEST_CASE("cli_args: neither --screenshot nor --frames returns nullopt") {
    auto a = make_argv({"player", "--rom", "x.bin"});
    CHECK(parse_screenshot_args(a.argc(), a.argv.data()) == std::nullopt);
}
