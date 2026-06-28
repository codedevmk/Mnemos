#include "m90_game_manifests.hpp"
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

#ifndef MNEMOS_IREM_M90_GAMES_DIR
#define MNEMOS_IREM_M90_GAMES_DIR ""
#endif

namespace {

    using mnemos::manifests::common::rom_set_decl;
    using mnemos::manifests::common::rom_set_file;
    using mnemos::manifests::common::rom_set_region;

    struct expected_contract final {
        std::size_t main_file_size{};
        std::optional<std::string_view> parent{};
        std::size_t sound_size{};
        std::size_t graphics_size{};
        std::size_t samples_size{};
        std::size_t banked_size{};
    };

    [[nodiscard]] const std::map<std::string, expected_contract, std::less<>>&
    expected_contracts() {
        static const std::map<std::string, expected_contract, std::less<>> contracts{
            {"atompunk",
             {.main_file_size = 0x20000U,
              .parent = "bbmanw",
              .sound_size = mnemos::manifests::irem_m90::sound_rom_size,
              .graphics_size = 0x200000U,
              .samples_size = 0x020000U}},
            {"bbmanw",
             {.main_file_size = 0x40000U,
              .sound_size = mnemos::manifests::irem_m90::sound_rom_size,
              .graphics_size = 0x200000U,
              .samples_size = 0x020000U}},
            {"bbmanwj",
             {.main_file_size = 0x40000U,
              .parent = "bbmanw",
              .sound_size = mnemos::manifests::irem_m90::sound_rom_size,
              .graphics_size = 0x200000U,
              .samples_size = 0x020000U}},
            {"bbmanwja",
             {.main_file_size = 0x40000U,
              .parent = "bbmanw",
              .sound_size = mnemos::manifests::irem_m90::sound_rom_size,
              .graphics_size = 0x200000U,
              .samples_size = 0x020000U}},
            {"gussun",
             {.main_file_size = 0x40000U,
              .parent = "riskchal",
              .sound_size = mnemos::manifests::irem_m90::sound_rom_size,
              .graphics_size = 0x200000U,
              .samples_size = 0x040000U}},
            {"hasamu",
             {.main_file_size = 0x20000U,
              .sound_size = mnemos::manifests::irem_m90::sound_rom_size,
              .graphics_size = 0x080000U}},
            {"newapunk",
             {.main_file_size = 0x40000U,
              .parent = "bbmanw",
              .sound_size = mnemos::manifests::irem_m90::sound_rom_size,
              .graphics_size = 0x200000U,
              .samples_size = 0x020000U}},
            {"quizf1",
             {.main_file_size = 0x40000U,
              .sound_size = mnemos::manifests::irem_m90::sound_rom_size,
              .graphics_size = 0x200000U,
              .samples_size = 0x040000U,
              .banked_size = 0x100000U}},
            {"riskchal",
             {.main_file_size = 0x40000U,
              .sound_size = mnemos::manifests::irem_m90::sound_rom_size,
              .graphics_size = 0x200000U,
              .samples_size = 0x040000U}},
        };
        return contracts;
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
        for (const auto& file : region.files) {
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

    void require_interleaved_main_region(const rom_set_region& region,
                                         std::size_t expected_file_size) {
        CHECK(region.size == mnemos::manifests::irem_m90::main_rom_size);
        require_region_contract(region);
        REQUIRE(region.files.size() == 4U);
        const rom_set_file& high = region.files[0];
        const rom_set_file& low = region.files[1];
        CHECK(high.stride == 2U);
        CHECK(low.stride == 2U);
        CHECK((high.offset & 1U) == 1U);
        CHECK((low.offset & 1U) == 0U);
        CHECK((high.offset & ~std::size_t{1U}) == low.offset);
        CHECK(high.size == expected_file_size);
        CHECK(low.size == expected_file_size);

        const rom_set_file& reset_high = region.files[2];
        const rom_set_file& reset_low = region.files[3];
        CHECK(reset_high.name == high.name);
        CHECK(reset_low.name == low.name);
        CHECK(reset_high.offset == 0x0FFFF1U);
        CHECK(reset_low.offset == 0x0FFFF0U);
        CHECK(reset_high.stride == 2U);
        CHECK(reset_low.stride == 2U);
        CHECK(reset_high.source_offset == expected_file_size - 8U);
        CHECK(reset_low.source_offset == expected_file_size - 8U);
        CHECK(reset_high.length == 0x8U);
        CHECK(reset_low.length == 0x8U);
        CHECK(reset_high.size == expected_file_size);
        CHECK(reset_low.size == expected_file_size);
        REQUIRE(reset_high.crc32.has_value());
        REQUIRE(reset_low.crc32.has_value());
        REQUIRE(high.crc32.has_value());
        REQUIRE(low.crc32.has_value());
        CHECK(*reset_high.crc32 == *high.crc32);
        CHECK(*reset_low.crc32 == *low.crc32);
    }

    void require_optional_region(const rom_set_decl& decl, std::string_view name,
                                 std::size_t expected_size) {
        const rom_set_region* region = find_region(decl, name);
        if (expected_size == 0U) {
            CHECK(region == nullptr);
            return;
        }
        REQUIRE(region != nullptr);
        CHECK(region->size == expected_size);
        require_region_contract(*region);
    }

    [[nodiscard]] rom_set_decl parse_decl(std::string_view text, std::string source) {
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, std::move(source));
        REQUIRE(parsed.ok());
        return std::move(*parsed.value);
    }

} // namespace

TEST_CASE("m90 embedded manifests cover the local M90-generation contracts", "[m90][rom]") {
    const auto& contracts = expected_contracts();
    std::map<std::string, rom_set_decl, std::less<>> declarations;
    for (const auto& [set_name, toml] : mnemos::manifests::irem_m90::embedded::game_manifests) {
        INFO("set=" << set_name);
        declarations.emplace(std::string{set_name},
                             parse_decl(toml, "embedded:irem_m90/" + std::string{set_name}));
    }

    std::set<std::string, std::less<>> names;
    for (const auto& [set_name, raw_decl] : declarations) {
        INFO("set=" << set_name);
        const auto contract = contracts.find(set_name);
        REQUIRE(contract != contracts.end());

        rom_set_decl decl = raw_decl;
        if (decl.parent.has_value()) {
            const auto parent = declarations.find(*decl.parent);
            REQUIRE(parent != declarations.end());
            decl =
                mnemos::manifests::common::inherit_parent_regions(parent->second, std::move(decl));
        }
        CHECK(decl.name == set_name);
        CHECK(decl.board == "irem_m90");
        CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::horizontal);
        if (contract->second.parent.has_value()) {
            REQUIRE(raw_decl.parent.has_value());
            CHECK(*raw_decl.parent == *contract->second.parent);
        } else {
            CHECK_FALSE(raw_decl.parent.has_value());
        }

        const rom_set_region* main = find_region(decl, "maincpu");
        REQUIRE(main != nullptr);
        require_interleaved_main_region(*main, contract->second.main_file_size);

        require_optional_region(decl, "soundcpu", contract->second.sound_size);
        require_optional_region(decl, "graphics", contract->second.graphics_size);
        require_optional_region(decl, "samples", contract->second.samples_size);
        require_optional_region(decl, "banked", contract->second.banked_size);
        names.emplace(set_name);
    }

    CHECK(names.size() == contracts.size());
    for (const auto& [expected, _] : contracts) {
        CHECK(names.contains(expected));
        CHECK_FALSE(mnemos::manifests::irem_m90::game_manifest_toml(expected).empty());
    }
}

TEST_CASE("m90 embedded manifests stay in sync with disk TOML", "[m90][rom]") {
    if (std::string_view{MNEMOS_IREM_M90_GAMES_DIR}.empty()) {
        SKIP("MNEMOS_IREM_M90_GAMES_DIR is not defined");
    }

    for (const auto& [set_name, _] : expected_contracts()) {
        const std::filesystem::path path =
            std::filesystem::path{MNEMOS_IREM_M90_GAMES_DIR} / (set_name + ".toml");
        const std::string disk = read_text_file(path);
        CHECK(mnemos::manifests::irem_m90::game_manifest_toml(set_name) == disk);
        rom_set_decl decl = parse_decl(disk, path.string());
        CHECK(decl.name == set_name);
        CHECK(decl.board == "irem_m90");
    }
}
