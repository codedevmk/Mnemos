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
    CHECK(family_from_name("irem_m15") == system_family::irem_m15);
    CHECK(family_from_name("m15") == system_family::irem_m15);
    CHECK(family_from_name("irem_m72") == system_family::irem_m72);
    CHECK(family_from_name("irem_m81") == system_family::irem_m81);
    CHECK(family_from_name("m81") == system_family::irem_m81);
    CHECK(family_from_name("irem_m82") == system_family::irem_m82);
    CHECK(family_from_name("m82") == system_family::irem_m82);
    CHECK(family_from_name("irem_m84") == system_family::irem_m84);
    CHECK(family_from_name("m84") == system_family::irem_m84);
    CHECK(family_from_name("irem_m107") == system_family::irem_m107);
    CHECK(family_from_name("m107") == system_family::irem_m107);
    CHECK(family_from_name("taito_f2") == system_family::taito_f2);
    CHECK(family_from_name("cps1") == system_family::capcom_cps1);
    CHECK(family_from_name("cps2") == system_family::capcom_cps2);
    CHECK(family_from_name("spectrum") == system_family::spectrum);
    CHECK(family_from_name("nes") == system_family::nes);
    CHECK(family_from_name("msx") == system_family::msx);
    CHECK(family_from_name("msx2") == system_family::msx2);
}

TEST_CASE("system_family: names are case-insensitive") {
    CHECK(family_from_name("Genesis") == system_family::genesis);
    CHECK(family_from_name("SEGA32X") == system_family::sega32x);
    CHECK(family_from_name("SegaCD") == system_family::segacd);
    CHECK(family_from_name("IREM_M15") == system_family::irem_m15);
    CHECK(family_from_name("IREM_M81") == system_family::irem_m81);
    CHECK(family_from_name("IREM_M82") == system_family::irem_m82);
    CHECK(family_from_name("IREM_M84") == system_family::irem_m84);
    CHECK(family_from_name("IREM_M107") == system_family::irem_m107);
    CHECK(family_from_name("TAITO_F2") == system_family::taito_f2);
    CHECK(family_from_name("CPS2") == system_family::capcom_cps2);
    CHECK(family_from_name("MSX2") == system_family::msx2);
}

TEST_CASE("system_family: unknown names are rejected, never guessed") {
    CHECK(family_from_name("") == std::nullopt);
    CHECK(family_from_name("megadrive") == std::nullopt);
    CHECK(family_from_name("32x") == std::nullopt);
    CHECK(family_from_name("game.bin") == std::nullopt);
}

TEST_CASE("system_family: family_from_name and family_id round-trip") {
    for (const auto family :
         {system_family::genesis, system_family::sms, system_family::gg, system_family::c64,
          system_family::segacd, system_family::sega32x, system_family::irem_m72,
          system_family::irem_m15, system_family::irem_m81, system_family::irem_m82,
          system_family::irem_m84, system_family::irem_m107, system_family::taito_f2,
          system_family::capcom_cps1, system_family::capcom_cps2, system_family::spectrum,
          system_family::nes, system_family::msx, system_family::msx2,
          system_family::amiga500}) {
        CHECK(family_from_name(family_id(family)) == family);
    }
}

TEST_CASE("system_family: family_names lists every accepted id") {
    const std::string names = family_names();
    for (const auto family :
         {system_family::genesis, system_family::sms, system_family::gg, system_family::c64,
          system_family::segacd, system_family::sega32x, system_family::irem_m72,
          system_family::irem_m15, system_family::irem_m81, system_family::irem_m82,
          system_family::irem_m84, system_family::irem_m107, system_family::taito_f2,
          system_family::capcom_cps1, system_family::capcom_cps2, system_family::spectrum,
          system_family::nes, system_family::msx, system_family::msx2,
          system_family::amiga500}) {
        CHECK(names.find(family_id(family)) != std::string::npos);
    }
}

TEST_CASE("system_family: family_label returns the expected display name") {
    CHECK(std::string{family_label(system_family::sms)} == "SMS");
    CHECK(std::string{family_label(system_family::genesis)} == "Genesis");
    CHECK(std::string{family_label(system_family::segacd)} == "Sega CD");
    CHECK(std::string{family_label(system_family::sega32x)} == "32X");
    CHECK(std::string{family_label(system_family::irem_m15)} == "Irem M15");
    CHECK(std::string{family_label(system_family::irem_m81)} == "Irem M81");
    CHECK(std::string{family_label(system_family::irem_m82)} == "Irem M82");
    CHECK(std::string{family_label(system_family::irem_m84)} == "Irem M84");
    CHECK(std::string{family_label(system_family::irem_m107)} == "Irem M107");
    CHECK(std::string{family_label(system_family::taito_f2)} == "Taito F2");
    CHECK(std::string{family_label(system_family::capcom_cps1)} == "CPS1");
    CHECK(std::string{family_label(system_family::capcom_cps2)} == "CPS2");
    CHECK(std::string{family_label(system_family::spectrum)} == "ZX Spectrum");
    CHECK(std::string{family_label(system_family::nes)} == "NES");
    CHECK(std::string{family_label(system_family::msx)} == "MSX");
    CHECK(std::string{family_label(system_family::msx2)} == "MSX2");
}
