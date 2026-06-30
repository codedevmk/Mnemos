#include "state_file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace {
    using mnemos::apps::player::adapters::load_save_state_file;
    using mnemos::apps::player::adapters::save_save_state_file;
    using mnemos::apps::player::adapters::state_path_for;

    [[nodiscard]] std::string temp_state(const char* tag) {
        return (std::filesystem::temp_directory_path() / tag).string();
    }
} // namespace

TEST_CASE("state_path_for swaps or appends the .mns extension") {
    CHECK(state_path_for("game.zip") == "game.mns");
    CHECK(state_path_for("game") == "game.mns");
    CHECK(state_path_for("C:/roms/1944.zip") == "C:/roms/1944.mns");
    CHECK(state_path_for("/home/u/.state") == "/home/u/.state.mns");
    CHECK(state_path_for("/v1.2/game") == "/v1.2/game.mns");
}

TEST_CASE("save-state files round-trip through an atomic write") {
    const auto path = temp_state("mnemos_state_file_test.mns");
    std::filesystem::remove(path);

    const std::array<std::uint8_t, 6> written = {0x4DU, 0x4EU, 0x53U, 0x01U, 0x02U, 0x03U};
    REQUIRE(save_save_state_file(path, written));
    CHECK(std::filesystem::exists(path));
    CHECK_FALSE(std::filesystem::exists(path + ".tmp"));

    const auto read = load_save_state_file(path);
    REQUIRE(read.has_value());
    REQUIRE(read->size() == written.size());
    CHECK(std::equal(read->begin(), read->end(), written.begin(), written.end()));

    std::filesystem::remove(path);
}

TEST_CASE("save-state file helpers reject empty writes and report missing files") {
    const auto path = temp_state("mnemos_state_file_missing.mns");
    std::filesystem::remove(path);

    CHECK_FALSE(save_save_state_file(path, {}));
    CHECK_FALSE(std::filesystem::exists(path));
    CHECK_FALSE(load_save_state_file(path).has_value());
}
