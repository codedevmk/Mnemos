#include "m52_game_manifests.hpp"
#include "rom_set_toml.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
    namespace fs = std::filesystem;

    [[nodiscard]] std::string read_text(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
} // namespace

TEST_CASE("Irem M52 manifests parse and retain Moon Patrol contracts", "[irem_m52]") {
    using mnemos::manifests::common::screen_orientation;

    const std::vector<std::string> sets{"mpatrol", "mpatrolw"};
    for (const std::string& set : sets) {
        const std::string_view embedded = mnemos::manifests::irem_m52::game_manifest_toml(set);
        REQUIRE_FALSE(embedded.empty());
        const auto parsed =
            mnemos::manifests::common::parse_rom_set_decl(embedded, "embedded:" + set);
        REQUIRE(parsed.ok());
        REQUIRE(parsed.value.has_value());
        CHECK(parsed.value->name == set);
        CHECK(parsed.value->board == "irem_m52");
        CHECK(parsed.value->orientation == screen_orientation::horizontal);
        CHECK_FALSE(parsed.value->regions.empty());
    }

    const auto parent = mnemos::manifests::common::parse_rom_set_decl(
        mnemos::manifests::irem_m52::game_manifest_toml("mpatrol"), "parent");
    const auto clone = mnemos::manifests::common::parse_rom_set_decl(
        mnemos::manifests::irem_m52::game_manifest_toml("mpatrolw"), "clone");
    REQUIRE(parent.ok());
    REQUIRE(clone.ok());
    REQUIRE(clone.value->parent.has_value());
    CHECK(*clone.value->parent == "mpatrol");

    const auto effective =
        mnemos::manifests::common::inherit_parent_regions(*parent.value, *clone.value);
    CHECK(effective.regions.size() == parent.value->regions.size());
    CHECK(effective.name == "mpatrolw");
}

TEST_CASE("Irem M52 disk TOML matches embedded TOML", "[irem_m52]") {
#if defined(MNEMOS_IREM_M52_GAMES_DIR)
    for (const auto& entry : fs::directory_iterator(MNEMOS_IREM_M52_GAMES_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
            continue;
        }
        const std::string set = entry.path().stem().string();
        const std::string disk = read_text(entry.path());
        CHECK(disk == mnemos::manifests::irem_m52::game_manifest_toml(set));
    }
#endif
}
