#include <mnemos/tools/cli.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <sstream>
#include <vector>

using mnemos::tools::cli_options;
using mnemos::tools::hash_framebuffer;
using mnemos::tools::parse_args;
using mnemos::tools::run;

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
    SECTION("the deferred --input-log flag is reported, not ignored") {
        cli_options opts;
        opts.manifest = "whatever.toml";
        opts.input_log = "log.bin";
        CHECK(run(opts, out, err) == 7);
    }
    SECTION("a manifest that cannot be opened is an error") {
        cli_options opts;
        opts.manifest = "no_such_manifest_file_12345.toml";
        CHECK(run(opts, out, err) == 2);
        CHECK_FALSE(err.str().empty());
    }
}
