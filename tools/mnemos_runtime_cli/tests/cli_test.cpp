#include <mnemos/tools/cli.hpp>

#include <mnemos/manifests/c64/c64_input.hpp>
#include <mnemos/runtime/input.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <sstream>
#include <vector>

using mnemos::tools::cli_options;
using mnemos::tools::hash_framebuffer;
using mnemos::tools::parse_args;
using mnemos::tools::parse_input_log;
using mnemos::tools::run;
namespace input_device = mnemos::tools::input_device;
using key = mnemos::manifests::c64::c64_input::key;
using c64in = mnemos::manifests::c64::c64_input;

namespace {
    // Wrap a literal argv for parse_args.
    int parse(std::vector<const char*> argv, cli_options& out, std::ostream& err) {
        return parse_args(static_cast<int>(argv.size()), argv.data(), out, err) ? 1 : 0;
    }
} // namespace

TEST_CASE("parse_args reads the full option set") {
    cli_options opts;
    std::ostringstream err;
    REQUIRE(parse({"cli", "--manifest", "c64.toml", "--rom-dir", "roms", "--frames", "600",
                   "--dump-hash"},
                  opts, err) == 1);
    CHECK(opts.manifest == "c64.toml");
    CHECK(opts.rom_dir == "roms");
    CHECK(opts.frames == 600U);
    CHECK(opts.dump_hash);
    CHECK_FALSE(opts.help);
}

TEST_CASE("parse_args reads the REU size") {
    cli_options opts;
    std::ostringstream err;
    REQUIRE(parse({"cli", "--manifest", "c64.toml", "--reu", "256"}, opts, err) == 1);
    CHECK(opts.reu_kib == 256U);

    cli_options bad;
    std::ostringstream berr;
    CHECK(parse({"cli", "--reu", "64"}, bad, berr) == 0);
    CHECK_FALSE(berr.str().empty());
    CHECK(bad.reu_kib == 0U); // default: no REU
}

TEST_CASE("parse_args recognises the modem flag") {
    cli_options opts;
    std::ostringstream err;
    REQUIRE(parse({"cli", "--manifest", "c64.toml", "--modem"}, opts, err) == 1);
    CHECK(opts.modem);

    cli_options off;
    std::ostringstream err2;
    REQUIRE(parse({"cli", "--manifest", "c64.toml"}, off, err2) == 1);
    CHECK_FALSE(off.modem);
}

TEST_CASE("parse_args reads --dial and implies the modem") {
    cli_options opts;
    std::ostringstream err;
    REQUIRE(parse({"cli", "--manifest", "c64.toml", "--dial", "bbs.host:6400"}, opts, err) == 1);
    CHECK(opts.dial == "bbs.host:6400");
    CHECK(opts.modem); // --dial implies --modem
}

TEST_CASE("parse_args rejects bad input") {
    SECTION("unknown flag") {
        cli_options opts;
        std::ostringstream err;
        CHECK(parse({"cli", "--nope"}, opts, err) == 0);
        CHECK_FALSE(err.str().empty());
    }
    SECTION("non-numeric frames") {
        cli_options opts;
        std::ostringstream err;
        CHECK(parse({"cli", "--frames", "lots"}, opts, err) == 0);
    }
    SECTION("missing value") {
        cli_options opts;
        std::ostringstream err;
        CHECK(parse({"cli", "--manifest"}, opts, err) == 0);
    }
}

TEST_CASE("parse_args recognises help") {
    cli_options opts;
    std::ostringstream err;
    REQUIRE(parse({"cli", "-h"}, opts, err) == 1);
    CHECK(opts.help);
}

TEST_CASE("hash_framebuffer is deterministic and content-sensitive") {
    std::vector<std::uint32_t> a = {0x00112233U, 0x00445566U, 0x00778899U, 0x00AABBCCU};
    std::vector<std::uint32_t> b = a;
    b[2] = 0x00000000U;

    const mnemos::chips::frame_buffer_view va{a.data(), 2U, 2U};
    const mnemos::chips::frame_buffer_view vb{b.data(), 2U, 2U};

    const std::string ha = hash_framebuffer(va);
    CHECK(ha.size() == 64U);
    CHECK(ha == hash_framebuffer(va)); // stable
    CHECK(ha != hash_framebuffer(vb)); // sensitive to a changed pixel
}

TEST_CASE("run validates arguments before touching hardware") {
    std::ostringstream out;
    std::ostringstream err;

    SECTION("help short-circuits to success") {
        cli_options opts;
        opts.help = true;
        CHECK(run(opts, out, err) == 0);
    }
    SECTION("missing manifest is an error") {
        cli_options opts;
        CHECK(run(opts, out, err) == 2);
    }
    SECTION("an unreadable --input-log file is an error") {
        cli_options opts;
        opts.manifest = "whatever.toml";
        opts.input_log = "no_such_input_log_12345.txt";
        CHECK(run(opts, out, err) == 7);
        CHECK_FALSE(err.str().empty());
    }
    SECTION("a manifest that cannot be opened is an error") {
        cli_options opts;
        opts.manifest = "no_such_manifest_file_12345.toml";
        CHECK(run(opts, out, err) == 2);
        CHECK_FALSE(err.str().empty());
    }
}

TEST_CASE("parse_input_log reads keys, joysticks, paddles and comments") {
    const std::string script = "# type 'A' then RETURN, wiggle joystick 2, set a paddle\n"
                               "\n"
                               "10 press a\n"
                               "12 release a      # comment after a command\n"
                               "12 press return\n"
                               "20 joy2 up,fire\n"
                               "25 joy2 none\n"
                               "30 paddle1 200 64\n"
                               "40 releaseall\n";
    std::istringstream in(script);
    mnemos::runtime::input_buffer buf;
    std::ostringstream err;
    REQUIRE(parse_input_log(in, buf, err));
    CHECK(err.str().empty());

    // Frame 10: press 'a'.
    const auto f10 = buf.events_for_frame(10U);
    REQUIRE(f10.size() == 1U);
    CHECK(f10[0].device == input_device::keyboard);
    CHECK(f10[0].code == static_cast<std::uint8_t>(key::a));
    CHECK(f10[0].pressed);

    // Frame 12: release 'a' then press RETURN (insertion order preserved).
    const auto f12 = buf.events_for_frame(12U);
    REQUIRE(f12.size() == 2U);
    CHECK(f12[0].code == static_cast<std::uint8_t>(key::a));
    CHECK_FALSE(f12[0].pressed);
    CHECK(f12[1].code == static_cast<std::uint8_t>(key::ret));
    CHECK(f12[1].pressed);

    // Frame 20: joystick 2 = up | fire.
    const auto f20 = buf.events_for_frame(20U);
    REQUIRE(f20.size() == 1U);
    CHECK(f20[0].device == input_device::joystick2);
    CHECK(f20[0].code == (c64in::joy_up | c64in::joy_fire));

    // Frame 25: joystick 2 centred.
    CHECK(buf.events_for_frame(25U)[0].code == 0U);

    // Frame 30: paddle 1 -> two events (X then Y).
    const auto f30 = buf.events_for_frame(30U);
    REQUIRE(f30.size() == 2U);
    CHECK(f30[0].device == input_device::paddle1_x);
    CHECK(f30[0].code == 200U);
    CHECK(f30[1].device == input_device::paddle1_y);
    CHECK(f30[1].code == 64U);

    // Frame 40: release all.
    CHECK(buf.events_for_frame(40U)[0].device == input_device::release_all);
}

TEST_CASE("parse_input_log rejects malformed lines") {
    const auto fails = [](const std::string& line) {
        std::istringstream in(line);
        mnemos::runtime::input_buffer buf;
        std::ostringstream err;
        const bool ok = parse_input_log(in, buf, err);
        return !ok && !err.str().empty();
    };
    CHECK(fails("notaframe press a")); // frame is not a number
    CHECK(fails("10 wiggle a"));       // unknown command
    CHECK(fails("10 press"));          // missing key
    CHECK(fails("10 press qux"));      // unknown key
    CHECK(fails("10 joy1 sideways"));  // bad joystick direction
    CHECK(fails("10 paddle1 300 0"));  // paddle out of range
    CHECK(fails("10 paddle2 5"));      // paddle missing Y
}
