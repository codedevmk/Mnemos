#include "system_family.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {
    using mnemos::apps::player::adapters::family_from_name;
    using mnemos::apps::player::adapters::family_id;
    using mnemos::apps::player::adapters::family_label;
    using mnemos::apps::player::adapters::family_names;
    using mnemos::apps::player::adapters::system_family;
} // namespace

TEST_CASE("system_family: every registry id maps to its family") {
    CHECK(family_from_name("genesis") == system_family::genesis);
    CHECK(family_from_name("sms") == system_family::sms);
    CHECK(family_from_name("gg") == system_family::gg);
    CHECK(family_from_name("c64") == system_family::c64);
    CHECK(family_from_name("segacd") == system_family::segacd);
    CHECK(family_from_name("sega32x") == system_family::sega32x);
    CHECK(family_from_name("irem_m72") == system_family::irem_m72);
    CHECK(family_from_name("cps1") == system_family::capcom_cps1);
}

TEST_CASE("system_family: names are case-insensitive") {
    CHECK(family_from_name("Genesis") == system_family::genesis);
    CHECK(family_from_name("SEGA32X") == system_family::sega32x);
    CHECK(family_from_name("SegaCD") == system_family::segacd);
}

TEST_CASE("system_family: unknown names are rejected, never guessed") {
    CHECK(family_from_name("") == std::nullopt);
    CHECK(family_from_name("megadrive") == std::nullopt);
    CHECK(family_from_name("32x") == std::nullopt);
    CHECK(family_from_name("game.bin") == std::nullopt);
}

TEST_CASE("system_family: family_from_name and family_id round-trip") {
    for (const auto family : {system_family::genesis, system_family::sms, system_family::gg,
                              system_family::c64, system_family::segacd, system_family::sega32x,
                              system_family::irem_m72, system_family::capcom_cps1}) {
        CHECK(family_from_name(family_id(family)) == family);
    }
}

TEST_CASE("system_family: family_names lists every accepted id") {
    const std::string names = family_names();
    for (const auto family : {system_family::genesis, system_family::sms, system_family::gg,
                              system_family::c64, system_family::segacd, system_family::sega32x,
                              system_family::irem_m72, system_family::capcom_cps1}) {
        CHECK(names.find(family_id(family)) != std::string::npos);
    }
}

TEST_CASE("system_family: family_label returns the expected display name") {
    CHECK(std::string{family_label(system_family::sms)} == "SMS");
    CHECK(std::string{family_label(system_family::genesis)} == "Genesis");
    CHECK(std::string{family_label(system_family::segacd)} == "Sega CD");
    CHECK(std::string{family_label(system_family::sega32x)} == "32X");
    CHECK(std::string{family_label(system_family::capcom_cps1)} == "CPS1");
}
