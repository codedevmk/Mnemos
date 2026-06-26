#include "m75_game_manifests.hpp"
#include "rom_set_toml.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>

#ifndef MNEMOS_IREM_M75_GAMES_DIR
#define MNEMOS_IREM_M75_GAMES_DIR ""
#endif

namespace {

    using mnemos::manifests::common::rom_set_decl;
    using mnemos::manifests::common::rom_set_file;
    using mnemos::manifests::common::rom_set_region;

    struct expected_contract final {
        std::size_t region_size{};
        std::size_t file_count{};
    };

    [[nodiscard]] const std::map<std::string, expected_contract, std::less<>>&
    expected_regions() {
        static const std::map<std::string, expected_contract, std::less<>> regions{
            {"maincpu", {.region_size = mnemos::manifests::irem_m75::main_rom_size,
                         .file_count = 2U}},
            {"soundcpu", {.region_size = mnemos::manifests::irem_m75::sound_rom_size,
                          .file_count = 1U}},
            {"chars", {.region_size = mnemos::manifests::irem_m75::char_gfx_size,
                       .file_count = 2U}},
            {"sprites", {.region_size = mnemos::manifests::irem_m75::sprite_gfx_size,
                         .file_count = 8U}},
            {"bgtiles", {.region_size = mnemos::manifests::irem_m75::bg_tile_gfx_size,
                         .file_count = 3U}},
            {"samples", {.region_size = mnemos::manifests::irem_m75::sample_rom_size,
                         .file_count = 1U}},
            {"proms", {.region_size = mnemos::manifests::irem_m75::proms_size,
                       .file_count = 1U}},
            {"plds", {.region_size = mnemos::manifests::irem_m75::plds_size,
                      .file_count = 3U}},
        };
        return regions;
    }

    [[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    [[nodiscard]] const rom_set_region* find_region(const rom_set_decl& decl,
                                                    std::string_view name) noexcept {
        const auto it =
            std::find_if(decl.regions.begin(), decl.regions.end(),
                         [name](const rom_set_region& region) { return region.name == name; });
        return it == decl.regions.end() ? nullptr : &*it;
    }

    void require_region_contract(const rom_set_region& region) {
        CHECK(region.size > 0U);
        REQUIRE_FALSE(region.files.empty());
        for (const rom_set_file& file : region.files) {
            INFO("region=" << region.name << " file=" << file.name);
            CHECK_FALSE(file.name.empty());
            CHECK(file.offset < region.size);
            CHECK(file.stride >= 1U);
            CHECK(file.unit >= 1U);
            CHECK(file.size > 0U);
            CHECK(file.crc32.has_value());
            const std::size_t source_bytes = file.length == 0U ? file.size : file.length;
            REQUIRE(source_bytes > 0U);
            const std::size_t chunks = (source_bytes + file.unit - 1U) / file.unit;
            const std::size_t last_start = file.offset + (chunks - 1U) * file.stride;
            CHECK(last_start < region.size);
        }
    }

    [[nodiscard]] rom_set_decl parse_decl(std::string_view text, std::string source) {
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, std::move(source));
        REQUIRE(parsed.ok());
        return std::move(*parsed.value);
    }

} // namespace

TEST_CASE("m75 embedded manifest covers the Vigilante parent contract", "[m75][rom]") {
    std::set<std::string, std::less<>> names;
    for (const auto& [set_name, toml] : mnemos::manifests::irem_m75::embedded::game_manifests) {
        INFO("set=" << set_name);
        names.emplace(std::string{set_name});

        rom_set_decl decl = parse_decl(toml, "embedded:irem_m75/" + std::string{set_name});
        CHECK(decl.name == set_name);
        CHECK(decl.board == "irem_m75");
        CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::horizontal);

        for (const auto& [region_name, contract] : expected_regions()) {
            const rom_set_region* region = find_region(decl, region_name);
            REQUIRE(region != nullptr);
            CHECK(region->size == contract.region_size);
            CHECK(region->files.size() == contract.file_count);
            require_region_contract(*region);
        }
    }

    CHECK(names == std::set<std::string, std::less<>>{"vigilant"});
    CHECK_FALSE(mnemos::manifests::irem_m75::game_manifest_toml("vigilant").empty());
}

TEST_CASE("m75 embedded manifests stay in sync with disk TOML", "[m75][rom]") {
    if (std::string_view{MNEMOS_IREM_M75_GAMES_DIR}.empty()) {
        SKIP("MNEMOS_IREM_M75_GAMES_DIR is not defined");
    }

    const std::filesystem::path path =
        std::filesystem::path{MNEMOS_IREM_M75_GAMES_DIR} / "vigilant.toml";
    const std::string disk = read_text_file(path);
    CHECK(mnemos::manifests::irem_m75::game_manifest_toml("vigilant") == disk);
    rom_set_decl decl = parse_decl(disk, path.string());
    CHECK(decl.name == "vigilant");
    CHECK(decl.board == "irem_m75");
}
