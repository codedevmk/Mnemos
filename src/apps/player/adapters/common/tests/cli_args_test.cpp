#include "cli_args.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
    using mnemos::apps::player::adapters::input_for_frame;
    using mnemos::apps::player::adapters::parse_extract_assets_args;
    using mnemos::apps::player::adapters::parse_extract_audio_args;
    using mnemos::apps::player::adapters::parse_no_autostart;
    using mnemos::apps::player::adapters::parse_press_events;
    using mnemos::apps::player::adapters::parse_rom_arg;
    using mnemos::apps::player::adapters::parse_rom_args;
    using mnemos::apps::player::adapters::parse_screenshot_args;
    using mnemos::apps::player::adapters::parse_system_arg;

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

TEST_CASE("cli_args: --system and -s return the engine name") {
    auto a = make_argv({"player", "--system", "sega32x", "--rom", "game.zip"});
    REQUIRE(parse_system_arg(a.argc(), a.argv.data()) == "sega32x");

    auto b = make_argv({"player", "-s", "genesis", "-r", "game.bin"});
    REQUIRE(parse_system_arg(b.argc(), b.argv.data()) == "genesis");
}

TEST_CASE("cli_args: missing or valueless --system returns nullopt") {
    auto a = make_argv({"player", "--rom", "game.bin"});
    REQUIRE(parse_system_arg(a.argc(), a.argv.data()) == std::nullopt);

    auto b = make_argv({"player", "--system"});
    REQUIRE(parse_system_arg(b.argc(), b.argv.data()) == std::nullopt);
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

TEST_CASE("cli_args: parse_rom_args collects all media paths in order") {
    auto a = make_argv({"player", "--rom", "disk1.d64", "--disk", "disk2.d64", "-r", "disk3.d64"});
    const auto paths = parse_rom_args(a.argc(), a.argv.data());
    REQUIRE(paths.size() == 3U);
    CHECK(paths[0] == "disk1.d64");
    CHECK(paths[1] == "disk2.d64");
    CHECK(paths[2] == "disk3.d64");
}

TEST_CASE("cli_args: parse_rom_args is empty without media flags") {
    auto a = make_argv({"player", "--region", "pal"});
    CHECK(parse_rom_args(a.argc(), a.argv.data()).empty());
}

TEST_CASE("cli_args: autostart defaults on, --no-autostart turns it off") {
    auto a = make_argv({"player", "--rom", "g.d64"});
    CHECK_FALSE(parse_no_autostart(a.argc(), a.argv.data()));

    auto b = make_argv({"player", "--rom", "g.d64", "--no-autostart"});
    CHECK(parse_no_autostart(b.argc(), b.argv.data()));
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

TEST_CASE("cli_args: --extract-assets + --extract-frames returns the request") {
    auto a = make_argv({"player", "--extract-assets", "scratch/rip", "--extract-frames", "90"});
    const auto req = parse_extract_assets_args(a.argc(), a.argv.data());
    REQUIRE(req.has_value());
    CHECK(req->base == "scratch/rip");
    CHECK(req->frames == 90U);
}

TEST_CASE("cli_args: --extract-assets defaults to 0 frames without --extract-frames") {
    auto a = make_argv({"player", "--extract-assets", "scratch/rip"});
    const auto req = parse_extract_assets_args(a.argc(), a.argv.data());
    REQUIRE(req.has_value());
    CHECK(req->base == "scratch/rip");
    CHECK(req->frames == 0U);
}

TEST_CASE("cli_args: --extract-assets rejects an option-shaped value") {
    // The token after --extract-assets is another flag, so there is no base
    // path -> the headless path stays disabled rather than ripping to "--...".
    auto a = make_argv({"player", "--extract-assets", "--extract-frames", "90"});
    CHECK(parse_extract_assets_args(a.argc(), a.argv.data()) == std::nullopt);
}

TEST_CASE("cli_args: --extract-frames alone (no --extract-assets) returns nullopt") {
    auto a = make_argv({"player", "--extract-frames", "30"});
    CHECK(parse_extract_assets_args(a.argc(), a.argv.data()) == std::nullopt);
}

TEST_CASE("cli_args: --extract-assets without a value returns nullopt") {
    auto a = make_argv({"player", "--extract-assets"});
    CHECK(parse_extract_assets_args(a.argc(), a.argv.data()) == std::nullopt);
}

TEST_CASE("cli_args: --extract-audio + --extract-frames returns the request") {
    auto a = make_argv({"player", "--extract-audio", "scratch/snd", "--extract-frames", "120"});
    const auto req = parse_extract_audio_args(a.argc(), a.argv.data());
    REQUIRE(req.has_value());
    CHECK(req->base == "scratch/snd");
    CHECK(req->frames == 120U);
}

TEST_CASE("cli_args: --extract-audio defaults to 0 frames and rejects an option value") {
    auto a = make_argv({"player", "--extract-audio", "scratch/snd"});
    const auto req = parse_extract_audio_args(a.argc(), a.argv.data());
    REQUIRE(req.has_value());
    CHECK(req->frames == 0U);

    auto b = make_argv({"player", "--extract-audio", "--extract-frames", "30"});
    CHECK(parse_extract_audio_args(b.argc(), b.argv.data()) == std::nullopt);
}

TEST_CASE("cli_args: parse_press_events parses button@frame[+duration]") {
    auto a = make_argv({"player", "--press", "start@60", "--press", "a@10+8"});
    const auto ev = parse_press_events(a.argc(), a.argv.data());
    REQUIRE(ev.size() == 2U);
    CHECK(ev[0].button == "start");
    CHECK(ev[0].frame == 60U);
    CHECK(ev[0].duration == 4U); // default
    CHECK(ev[1].button == "a");
    CHECK(ev[1].frame == 10U);
    CHECK(ev[1].duration == 8U);
}

TEST_CASE("cli_args: parse_press_events lowercases and skips bad specs") {
    auto a = make_argv({"player", "--press", "START@30", "--press", "bogus@5", "--press", "noat"});
    const auto ev = parse_press_events(a.argc(), a.argv.data());
    REQUIRE(ev.size() == 1U); // unknown button + missing '@' dropped
    CHECK(ev[0].button == "start");
    CHECK(ev[0].frame == 30U);
}

TEST_CASE("cli_args: input_for_frame holds a button over its [frame, frame+duration) window") {
    auto a = make_argv({"player", "--press", "start@60+3"});
    const auto ev = parse_press_events(a.argc(), a.argv.data());

    CHECK_FALSE(input_for_frame(ev, 59U).start); // before
    CHECK(input_for_frame(ev, 60U).start);       // first held frame
    CHECK(input_for_frame(ev, 62U).start);       // last held frame
    CHECK_FALSE(input_for_frame(ev, 63U).start); // released
    CHECK_FALSE(input_for_frame(ev, 60U).a);     // other buttons stay released
}

TEST_CASE("cli_args: input_for_frame combines overlapping events") {
    auto a = make_argv({"player", "--press", "left@10+20", "--press", "a@15+2"});
    const auto ev = parse_press_events(a.argc(), a.argv.data());
    const auto at16 = input_for_frame(ev, 16U);
    CHECK(at16.left);
    CHECK(at16.a);
    const auto at20 = input_for_frame(ev, 20U);
    CHECK(at20.left);
    CHECK_FALSE(at20.a); // a's 2-frame window (15,16) has passed
}

TEST_CASE("cli_args: input_for_frame with no events is all-released") {
    const std::vector<mnemos::apps::player::adapters::press_event> none;
    const auto s = input_for_frame(none, 100U);
    CHECK_FALSE(s.start);
    CHECK_FALSE(s.a);
    CHECK_FALSE(s.up);
}
