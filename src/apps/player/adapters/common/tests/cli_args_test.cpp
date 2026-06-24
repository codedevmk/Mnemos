#include "cli_args.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
    using mnemos::apps::player::adapters::animation_record_format;
    using mnemos::apps::player::adapters::input_for_frame;
    using mnemos::apps::player::adapters::parse_animation_record_args;
    using mnemos::apps::player::adapters::parse_capabilities_arg;
    using mnemos::apps::player::adapters::parse_extract_assets_args;
    using mnemos::apps::player::adapters::parse_extract_audio_args;
    using mnemos::apps::player::adapters::parse_fm_unit_arg;
    using mnemos::apps::player::adapters::parse_load_state_arg;
    using mnemos::apps::player::adapters::parse_no_autostart;
    using mnemos::apps::player::adapters::parse_press_events;
    using mnemos::apps::player::adapters::parse_rom_arg;
    using mnemos::apps::player::adapters::parse_rom_args;
    using mnemos::apps::player::adapters::parse_save_state_args;
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

TEST_CASE("cli_args: --capabilities enables the headless capability summary") {
    auto a = make_argv({"player", "--system", "c64", "--rom", "g.d64"});
    CHECK_FALSE(parse_capabilities_arg(a.argc(), a.argv.data()));

    auto b = make_argv({"player", "--system", "c64", "--rom", "g.d64", "--capabilities"});
    CHECK(parse_capabilities_arg(b.argc(), b.argv.data()));
}

TEST_CASE("cli_args: --fm enables the optional FM expansion") {
    auto a = make_argv({"player", "--system", "sms", "--rom", "g.sms"});
    CHECK_FALSE(parse_fm_unit_arg(a.argc(), a.argv.data()));

    auto b = make_argv({"player", "--system", "sms", "--rom", "g.sms", "--fm"});
    CHECK(parse_fm_unit_arg(b.argc(), b.argv.data()));
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

TEST_CASE("cli_args: --save-state accepts an output path and optional frame count") {
    auto a = make_argv({"player", "--save-state", "scratch/slot0.mns"});
    const auto boot_req = parse_save_state_args(a.argc(), a.argv.data());
    REQUIRE(boot_req.has_value());
    CHECK(boot_req->path == "scratch/slot0.mns");
    CHECK(boot_req->frames == 0U);

    auto b = make_argv({"player", "--save-state", "scratch/slot1.mns", "--frames", "120"});
    const auto stepped_req = parse_save_state_args(b.argc(), b.argv.data());
    REQUIRE(stepped_req.has_value());
    CHECK(stepped_req->path == "scratch/slot1.mns");
    CHECK(stepped_req->frames == 120U);
}

TEST_CASE("cli_args: --save-state rejects missing and option-shaped paths") {
    auto missing = make_argv({"player", "--save-state"});
    CHECK(parse_save_state_args(missing.argc(), missing.argv.data()) == std::nullopt);

    auto option = make_argv({"player", "--save-state", "--frames", "60"});
    CHECK(parse_save_state_args(option.argc(), option.argv.data()) == std::nullopt);
}

TEST_CASE("cli_args: --load-state accepts only a concrete path") {
    auto a = make_argv({"player", "--load-state", "scratch/slot0.mns"});
    REQUIRE(parse_load_state_arg(a.argc(), a.argv.data()) == "scratch/slot0.mns");

    auto missing = make_argv({"player", "--load-state"});
    CHECK(parse_load_state_arg(missing.argc(), missing.argv.data()) == std::nullopt);

    auto option = make_argv({"player", "--load-state", "--screenshot", "out.ppm"});
    CHECK(parse_load_state_arg(option.argc(), option.argv.data()) == std::nullopt);
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

TEST_CASE("cli_args: --record-gif + --frames returns the animation request") {
    auto a = make_argv({"player", "--record-gif", "scratch/clip.gif", "--frames", "30"});
    const auto req = parse_animation_record_args(a.argc(), a.argv.data());
    REQUIRE(req.has_value());
    CHECK(req->output == "scratch/clip.gif");
    CHECK(req->frames == 30U);
    CHECK(req->format == animation_record_format::gif);
}

TEST_CASE("cli_args: --record-movie + --frames returns the sequence request") {
    auto a = make_argv({"player", "--record-movie", "scratch/clip", "--frames", "120"});
    const auto req = parse_animation_record_args(a.argc(), a.argv.data());
    REQUIRE(req.has_value());
    CHECK(req->output == "scratch/clip");
    CHECK(req->frames == 120U);
    CHECK(req->format == animation_record_format::movie_frames);
}

TEST_CASE("cli_args: recording rejects missing, zero, and option-shaped values") {
    auto no_frames = make_argv({"player", "--record-gif", "scratch/clip.gif"});
    CHECK(parse_animation_record_args(no_frames.argc(), no_frames.argv.data()) == std::nullopt);

    auto zero_frames = make_argv({"player", "--record-gif", "scratch/clip.gif", "--frames", "0"});
    CHECK(parse_animation_record_args(zero_frames.argc(), zero_frames.argv.data()) == std::nullopt);

    auto option_output = make_argv({"player", "--record-movie", "--frames", "60"});
    CHECK(parse_animation_record_args(option_output.argc(), option_output.argv.data()) ==
          std::nullopt);
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

TEST_CASE("cli_args: input_for_frame exposes arcade service and test events") {
    auto a = make_argv({"player", "--press", "service@5+2", "--press", "test@6+3"});
    const auto ev = parse_press_events(a.argc(), a.argv.data());
    REQUIRE(ev.size() == 2U);
    CHECK(ev[0].button == "service");
    CHECK(ev[1].button == "test");

    CHECK(input_for_frame(ev, 5U).service);
    CHECK_FALSE(input_for_frame(ev, 5U).test);
    const auto at6 = input_for_frame(ev, 6U);
    CHECK(at6.service);
    CHECK(at6.test);
    CHECK_FALSE(input_for_frame(ev, 9U).test);
}

TEST_CASE("cli_args: input_for_frame exposes analog paddle events") {
    auto a = make_argv({"player", "--press", "paddle=0x123@4+2", "--press", "dial=42@7"});
    const auto ev = parse_press_events(a.argc(), a.argv.data());
    REQUIRE(ev.size() == 2U);
    CHECK(ev[0].button == "paddle");
    REQUIRE(ev[0].paddle_value.has_value());
    CHECK(*ev[0].paddle_value == 0x0123U);
    CHECK(ev[1].button == "paddle");
    REQUIRE(ev[1].paddle_value.has_value());
    CHECK(*ev[1].paddle_value == 42U);

    CHECK(input_for_frame(ev, 3U).paddle == 0U);
    CHECK(input_for_frame(ev, 4U).paddle == 0x0123U);
    CHECK(input_for_frame(ev, 5U).paddle == 0x0123U);
    CHECK(input_for_frame(ev, 6U).paddle == 0U);
    CHECK(input_for_frame(ev, 7U).paddle == 42U);
}

TEST_CASE("cli_args: input_for_frame with no events is all-released") {
    const std::vector<mnemos::apps::player::adapters::press_event> none;
    const auto s = input_for_frame(none, 100U);
    CHECK_FALSE(s.start);
    CHECK_FALSE(s.a);
    CHECK_FALSE(s.up);
}

TEST_CASE("cli_args: --dip parses hex and decimal DIP banks") {
    using mnemos::apps::player::adapters::parse_dip_arg;
    {
        auto h = make_argv({"player", "--dip", "0xFE7F"});
        const auto dips = parse_dip_arg(h.argc(), h.argv.data());
        REQUIRE(dips.has_value());
        CHECK(*dips == 0xFE7FU);
    }
    {
        auto h = make_argv({"player", "--dip", "4096"});
        const auto dips = parse_dip_arg(h.argc(), h.argv.data());
        REQUIRE(dips.has_value());
        CHECK(*dips == 4096U);
    }
    {
        auto h = make_argv({"player", "--rom", "x.zip"});
        CHECK_FALSE(parse_dip_arg(h.argc(), h.argv.data()).has_value());
    }
    {
        auto h = make_argv({"player", "--dip", "0x10000"}); // out of 16-bit range
        CHECK_FALSE(parse_dip_arg(h.argc(), h.argv.data()).has_value());
    }
    {
        auto h = make_argv({"player", "--dip", "bogus"});
        CHECK_FALSE(parse_dip_arg(h.argc(), h.argv.data()).has_value());
    }
}
