#include "region_args.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>

namespace {
    using mnemos::apps::player::adapters::parse_region_arg;
    using mnemos::apps::player::adapters::region_override;
    using mnemos::apps::player::adapters::resolve_video_region;
    using mnemos::apps::player::adapters::region_source_label;

    // parse_region_arg takes argv as `char* []`, so the test helper has to
    // hand it a mutable array of mutable C-strings.
    region_override parse(std::initializer_list<const char*> args) {
        std::array<char*, 16> argv{};
        REQUIRE(args.size() <= argv.size());
        std::array<std::string, 16> storage{};
        int i = 0;
        for (const auto* s : args) {
            storage[static_cast<std::size_t>(i)] = s;
            argv[static_cast<std::size_t>(i)] = storage[static_cast<std::size_t>(i)].data();
            ++i;
        }
        return parse_region_arg(i, argv.data());
    }
} // namespace

TEST_CASE("region_args: no --region flag returns auto_detect") {
    CHECK(parse({"player"}) == region_override::auto_detect);
    CHECK(parse({"player", "--rom", "x.bin"}) == region_override::auto_detect);
}

TEST_CASE("region_args: --region pal / --region ntsc set the override") {
    CHECK(parse({"player", "--region", "pal"}) == region_override::pal);
    CHECK(parse({"player", "--region", "ntsc"}) == region_override::ntsc);
}

TEST_CASE("region_args: --region accepts mixed-case spellings") {
    CHECK(parse({"player", "--region", "PAL"}) == region_override::pal);
    CHECK(parse({"player", "--region", "Ntsc"}) == region_override::ntsc);
    CHECK(parse({"player", "--region", "nTsC"}) == region_override::ntsc);
}

TEST_CASE("region_args: unknown --region value falls back to auto_detect") {
    CHECK(parse({"player", "--region", "secam"}) == region_override::auto_detect);
    CHECK(parse({"player", "--region", ""}) == region_override::auto_detect);
}

TEST_CASE("region_args: --region without a following value is ignored") {
    // The parser walks i in [1, argc-1) so the trailing --region with no
    // value never matches; same as if no flag were passed.
    CHECK(parse({"player", "--region"}) == region_override::auto_detect);
}

TEST_CASE("region_args: resolve_video_region honours override over cart default") {
    CHECK(resolve_video_region(region_override::pal, mnemos::video_region::ntsc) ==
          mnemos::video_region::pal);
    CHECK(resolve_video_region(region_override::ntsc, mnemos::video_region::pal) ==
          mnemos::video_region::ntsc);
}

TEST_CASE("region_args: resolve_video_region keeps cart default when auto_detect") {
    CHECK(resolve_video_region(region_override::auto_detect, mnemos::video_region::pal) ==
          mnemos::video_region::pal);
    CHECK(resolve_video_region(region_override::auto_detect, mnemos::video_region::ntsc) ==
          mnemos::video_region::ntsc);
}

TEST_CASE("region_args: region_source_label distinguishes auto vs explicit") {
    CHECK(std::string{region_source_label(region_override::auto_detect)} == "auto-detected");
    CHECK(std::string{region_source_label(region_override::pal)} == "explicit --region");
    CHECK(std::string{region_source_label(region_override::ntsc)} == "explicit --region");
}
