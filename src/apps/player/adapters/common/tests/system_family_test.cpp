#include "system_family.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {
    using mnemos::apps::player::adapters::detect_family;
    using mnemos::apps::player::adapters::family_label;
    using mnemos::apps::player::adapters::system_family;
} // namespace

TEST_CASE("system_family: .sms and .sg map to SMS") {
    CHECK(detect_family("game.sms") == system_family::sms);
    CHECK(detect_family("game.sg") == system_family::sms);
    CHECK(detect_family("C:/roms/Sonic.SMS") == system_family::sms);  // case-insensitive
    CHECK(detect_family("/r/Phantasy.Sg") == system_family::sms);
}

TEST_CASE("system_family: Genesis extensions and no-extension map to Genesis") {
    CHECK(detect_family("game.bin") == system_family::genesis);
    CHECK(detect_family("game.md") == system_family::genesis);
    CHECK(detect_family("game.gen") == system_family::genesis);
    CHECK(detect_family("game.smd") == system_family::genesis);
    CHECK(detect_family("game.68k") == system_family::genesis);
    CHECK(detect_family("game") == system_family::genesis);            // no extension
    CHECK(detect_family("README.md") == system_family::genesis);       // .md still wins
}

TEST_CASE("system_family: unknown extension falls through to Genesis") {
    // Default-to-Genesis is intentional: most Genesis dumps in the wild
    // have heterogeneous extensions, so we don't fight the user when they
    // hand us a .rom or similar.
    CHECK(detect_family("game.rom") == system_family::genesis);
    CHECK(detect_family("game.xyz") == system_family::genesis);
}

TEST_CASE("system_family: family_label returns the expected display name") {
    CHECK(std::string{family_label(system_family::sms)} == "SMS");
    CHECK(std::string{family_label(system_family::genesis)} == "Genesis");
}
