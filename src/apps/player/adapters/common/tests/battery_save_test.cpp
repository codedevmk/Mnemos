#include "battery_save.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace {
    using mnemos::apps::player::adapters::load_battery_ram;
    using mnemos::apps::player::adapters::save_battery_ram;
    using mnemos::apps::player::adapters::srm_path_for;

    [[nodiscard]] std::string temp_srm(const char* tag) {
        return (std::filesystem::temp_directory_path() / tag).string();
    }
} // namespace

TEST_CASE("srm_path_for swaps or appends the .srm extension") {
    CHECK(srm_path_for("game.bin") == "game.srm");
    CHECK(srm_path_for("game.md") == "game.srm");
    CHECK(srm_path_for("game") == "game.srm");           // no extension -> append
    CHECK(srm_path_for("archive.zip") == "archive.srm"); // .srm sits beside the zip
    CHECK(srm_path_for("C:/roms/Sonic.md") == "C:/roms/Sonic.srm");
    CHECK(srm_path_for("/home/u/Sonic.bin") == "/home/u/Sonic.srm");
}

TEST_CASE("srm_path_for keeps directory dots and dotfile bodies") {
    // A dot in a directory component is not an extension.
    CHECK(srm_path_for("/v1.2/game") == "/v1.2/game.srm");
    // A leading-dot basename keeps its body and just gains the suffix.
    CHECK(srm_path_for("dir/.sram") == "dir/.sram.srm");
}

TEST_CASE("battery_ram round-trips through an atomic save/load") {
    const auto path = temp_srm("mnemos_battery_save_test.srm");
    std::filesystem::remove(path);

    const std::array<std::uint8_t, 8> written = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    REQUIRE(save_battery_ram(path, written));
    CHECK(std::filesystem::exists(path));
    CHECK_FALSE(std::filesystem::exists(path + ".tmp")); // temp renamed away

    std::array<std::uint8_t, 8> read{};
    REQUIRE(load_battery_ram(path, read));
    CHECK(read == written);

    std::filesystem::remove(path);
}

TEST_CASE("load_battery_ram tolerates a save smaller than the SRAM") {
    const auto path = temp_srm("mnemos_battery_save_short.srm");
    std::filesystem::remove(path);

    const std::array<std::uint8_t, 3> small = {0x01, 0x02, 0x03};
    REQUIRE(save_battery_ram(path, small));

    std::array<std::uint8_t, 6> buf{};
    buf.fill(0xFF); // powered-on fill
    REQUIRE(load_battery_ram(path, buf));
    CHECK(buf == std::array<std::uint8_t, 6>{0x01, 0x02, 0x03, 0xFF, 0xFF, 0xFF});

    std::filesystem::remove(path);
}

TEST_CASE("load_battery_ram reports a missing save without touching the buffer") {
    const auto missing = temp_srm("mnemos_battery_save_absent.srm");
    std::filesystem::remove(missing);

    std::array<std::uint8_t, 4> buf = {0xAA, 0xBB, 0xCC, 0xDD};
    CHECK_FALSE(load_battery_ram(missing, buf));
    CHECK(buf == std::array<std::uint8_t, 4>{0xAA, 0xBB, 0xCC, 0xDD}); // untouched
}

TEST_CASE("save/load are no-ops for an empty image") {
    const auto path = temp_srm("mnemos_battery_save_empty.srm");
    std::filesystem::remove(path);

    CHECK_FALSE(save_battery_ram(path, {}));
    CHECK_FALSE(std::filesystem::exists(path)); // nothing created

    std::array<std::uint8_t, 0> empty{};
    CHECK_FALSE(load_battery_ram(path, empty));
}
