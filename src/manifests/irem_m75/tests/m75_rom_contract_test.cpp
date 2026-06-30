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
    using mnemos::manifests::common::rom_set_dip_switch;
    using mnemos::manifests::common::rom_set_file;
    using mnemos::manifests::common::rom_set_region;

    struct expected_contract final {
        std::size_t region_size{};
        std::optional<std::size_t> file_count{};
    };

    [[nodiscard]] const std::map<std::string, expected_contract, std::less<>>&
    expected_regions(std::string_view set_name) {
        if (set_name == "vigilantbl") {
            static const std::map<std::string, expected_contract, std::less<>> bootleg_regions{
                {"maincpu",
                 {.region_size = mnemos::manifests::irem_m75::main_rom_size,
                  .file_count = 2U}},
                {"soundcpu",
                 {.region_size = mnemos::manifests::irem_m75::sound_rom_size,
                  .file_count = 1U}},
                {"chars",
                 {.region_size = mnemos::manifests::irem_m75::char_gfx_size,
                  .file_count = 2U}},
                {"sprites",
                 {.region_size = mnemos::manifests::irem_m75::sprite_gfx_size,
                  .file_count = 8U}},
                {"bgtiles",
                 {.region_size = mnemos::manifests::irem_m75::bg_tile_gfx_size,
                  .file_count = 3U}},
                {"samples",
                 {.region_size = mnemos::manifests::irem_m75::sample_rom_size,
                  .file_count = 1U}},
                {"proms", {.region_size = 0x000300U, .file_count = 3U}},
                {"plds",
                 {.region_size = mnemos::manifests::irem_m75::plds_size, .file_count = 1U}},
            };
            return bootleg_regions;
        }
        if (set_name == "kikcubic") {
            static const std::map<std::string, expected_contract, std::less<>> kikcubic_regions{
                {"maincpu",
                 {.region_size = mnemos::manifests::irem_m75::main_rom_size,
                  .file_count = 3U}},
                {"soundcpu",
                 {.region_size = mnemos::manifests::irem_m75::sound_rom_size,
                  .file_count = 1U}},
                {"chars",
                 {.region_size = mnemos::manifests::irem_m75::char_gfx_size,
                  .file_count = 2U}},
                {"sprites",
                 {.region_size = mnemos::manifests::irem_m75::sprite_gfx_size,
                  .file_count = 2U}},
                {"samples",
                 {.region_size = mnemos::manifests::irem_m75::sample_rom_size,
                  .file_count = 1U}},
                {"proms",
                 {.region_size = mnemos::manifests::irem_m75::kikcubic_proms_size,
                  .file_count = 3U}},
            };
            return kikcubic_regions;
        }
        static const std::map<std::string, expected_contract, std::less<>> regions{
            {"maincpu",
             {.region_size = mnemos::manifests::irem_m75::main_rom_size, .file_count = 2U}},
            {"soundcpu",
             {.region_size = mnemos::manifests::irem_m75::sound_rom_size, .file_count = 1U}},
            {"chars",
             {.region_size = mnemos::manifests::irem_m75::char_gfx_size, .file_count = 2U}},
            {"sprites",
             {.region_size = mnemos::manifests::irem_m75::sprite_gfx_size, .file_count = 8U}},
            {"bgtiles",
             {.region_size = mnemos::manifests::irem_m75::bg_tile_gfx_size,
              .file_count = std::nullopt}},
            {"samples",
             {.region_size = mnemos::manifests::irem_m75::sample_rom_size, .file_count = 1U}},
            {"proms", {.region_size = mnemos::manifests::irem_m75::proms_size, .file_count = 1U}},
            {"plds", {.region_size = mnemos::manifests::irem_m75::plds_size, .file_count = 3U}},
        };
        return regions;
    }

    [[nodiscard]] std::size_t expected_dip_count(std::string_view set_name) noexcept {
        return set_name == "kikcubic" ? 13U : 14U;
    }

    [[nodiscard]] std::uint16_t expected_raw_dip_default(std::string_view set_name) noexcept {
        return set_name == "kikcubic" ? 0xD5FFU : 0xFDFFU;
    }

    [[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    [[nodiscard]] std::string normalized_text(std::string text) {
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        return text;
    }

    [[nodiscard]] const rom_set_region* find_region(const rom_set_decl& decl,
                                                    std::string_view name) noexcept {
        const auto it =
            std::find_if(decl.regions.begin(), decl.regions.end(),
                         [name](const rom_set_region& region) { return region.name == name; });
        return it == decl.regions.end() ? nullptr : &*it;
    }

    [[nodiscard]] const rom_set_dip_switch* find_dip(const rom_set_decl& decl,
                                                     std::string_view name) noexcept {
        const auto it =
            std::find_if(decl.dips.begin(), decl.dips.end(),
                         [name](const rom_set_dip_switch& dip) { return dip.name == name; });
        return it == decl.dips.end() ? nullptr : &*it;
    }

    [[nodiscard]] std::uint16_t raw_dip_default(const rom_set_decl& decl,
                                                std::uint16_t fallback) noexcept {
        std::uint16_t value = fallback;
        for (const auto& dip : decl.dips) {
            value = static_cast<std::uint16_t>((value & ~dip.mask) | dip.default_value);
        }
        return value;
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

    [[nodiscard]] std::map<std::string, rom_set_decl, std::less<>> embedded_declarations() {
        std::map<std::string, rom_set_decl, std::less<>> declarations;
        for (const auto& [set_name, toml] : mnemos::manifests::irem_m75::embedded::game_manifests) {
            rom_set_decl decl = parse_decl(toml, "embedded:irem_m75/" + std::string{set_name});
            CHECK(decl.name == set_name);
            declarations.emplace(std::string{set_name}, std::move(decl));
        }
        return declarations;
    }

} // namespace

TEST_CASE("m75 embedded manifests cover the local M75 parent and clone contracts", "[m75][rom]") {
    const auto declarations = embedded_declarations();
    std::set<std::string, std::less<>> names;
    for (const auto& [set_name, raw_decl] : declarations) {
        INFO("set=" << set_name);

        rom_set_decl decl = raw_decl;
        if (decl.parent.has_value()) {
            const auto parent_it = declarations.find(*decl.parent);
            REQUIRE(parent_it != declarations.end());
            decl = mnemos::manifests::common::inherit_parent_regions(parent_it->second,
                                                                     std::move(decl));
        }
        names.emplace(decl.name);
        CHECK(decl.board == "irem_m75");
        CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::horizontal);

        for (const auto& [region_name, contract] : expected_regions(decl.name)) {
            const rom_set_region* region = find_region(decl, region_name);
            REQUIRE(region != nullptr);
            CHECK(region->size == contract.region_size);
            if (contract.file_count.has_value()) {
                CHECK(region->files.size() == *contract.file_count);
            }
            require_region_contract(*region);
        }

        CHECK(decl.dips.size() == expected_dip_count(decl.name));
        CHECK(raw_dip_default(decl, 0xFFFFU) == expected_raw_dip_default(decl.name));
    }

    CHECK(names == std::set<std::string, std::less<>>{"kikcubic", "vigilant", "vigilanta",
                                                      "vigilantb", "vigilantbl", "vigilantc",
                                                      "vigilantd", "vigilantg", "vigilanto"});
    CHECK_FALSE(mnemos::manifests::irem_m75::game_manifest_toml("kikcubic").empty());
    CHECK_FALSE(mnemos::manifests::irem_m75::game_manifest_toml("vigilant").empty());
    REQUIRE(declarations.at("vigilanta").parent.has_value());
    CHECK(*declarations.at("vigilanta").parent == "vigilant");
    REQUIRE(declarations.at("vigilantb").parent.has_value());
    CHECK(*declarations.at("vigilantb").parent == "vigilant");
    REQUIRE(declarations.at("vigilantbl").parent.has_value());
    CHECK(*declarations.at("vigilantbl").parent == "vigilant");
    REQUIRE(declarations.at("vigilantc").parent.has_value());
    CHECK(*declarations.at("vigilantc").parent == "vigilant");
    REQUIRE(declarations.at("vigilantd").parent.has_value());
    CHECK(*declarations.at("vigilantd").parent == "vigilant");
    REQUIRE(declarations.at("vigilantg").parent.has_value());
    CHECK(*declarations.at("vigilantg").parent == "vigilant");
    REQUIRE(declarations.at("vigilanto").parent.has_value());
    CHECK(*declarations.at("vigilanto").parent == "vigilant");

    const auto& vigilant = declarations.at("vigilant");
    const rom_set_dip_switch* fighters = find_dip(vigilant, "Number of Fighters");
    REQUIRE(fighters != nullptr);
    CHECK(fighters->mask == 0x0003U);
    CHECK(fighters->default_value == 0x0003U);
    REQUIRE(fighters->options.size() == 4U);

    const rom_set_dip_switch* coinage = find_dip(vigilant, "Coinage");
    REQUIRE(coinage != nullptr);
    CHECK(coinage->mask == 0x00F0U);
    CHECK(coinage->default_value == 0x00F0U);
    REQUIRE(coinage->condition.has_value());
    CHECK(coinage->condition->mask == 0x0400U);
    CHECK(coinage->condition->value == 0x0400U);
    REQUIRE(coinage->options.size() == 16U);

    const rom_set_dip_switch* coin1 = find_dip(vigilant, "Coin 1");
    REQUIRE(coin1 != nullptr);
    CHECK(coin1->mask == 0x0030U);
    REQUIRE(coin1->condition.has_value());
    CHECK(coin1->condition->mask == 0x0400U);
    CHECK(coin1->condition->value == 0x0000U);

    const rom_set_dip_switch* cabinet = find_dip(vigilant, "Cabinet Type");
    REQUIRE(cabinet != nullptr);
    CHECK(cabinet->mask == 0x0200U);
    CHECK(cabinet->default_value == 0x0000U);

    const rom_set_dip_switch* switch8 = find_dip(vigilant, "Switch 8");
    REQUIRE(switch8 != nullptr);
    CHECK(switch8->mask == 0x8000U);
    CHECK(switch8->default_value == 0x8000U);

    const auto& kikcubic = declarations.at("kikcubic");
    const rom_set_region* kikcubic_proms = find_region(kikcubic, "proms");
    REQUIRE(kikcubic_proms != nullptr);
    CHECK(kikcubic_proms->size == mnemos::manifests::irem_m75::kikcubic_proms_size);
    const rom_set_dip_switch* kikcubic_lives = find_dip(kikcubic, "Lives");
    REQUIRE(kikcubic_lives != nullptr);
    CHECK(kikcubic_lives->mask == 0x000CU);
    CHECK(kikcubic_lives->default_value == 0x000CU);
    const rom_set_dip_switch* kikcubic_service = find_dip(kikcubic, "Service Mode");
    REQUIRE(kikcubic_service != nullptr);
    CHECK(kikcubic_service->mask == 0x8000U);
    CHECK(kikcubic_service->default_value == 0x8000U);
}

TEST_CASE("m75 embedded manifests stay in sync with disk TOML", "[m75][rom]") {
    if (std::string_view{MNEMOS_IREM_M75_GAMES_DIR}.empty()) {
        SKIP("MNEMOS_IREM_M75_GAMES_DIR is not defined");
    }

    const auto declarations = embedded_declarations();
    std::set<std::string, std::less<>> disk_names;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator{MNEMOS_IREM_M75_GAMES_DIR}) {
        if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
            continue;
        }
        const std::string set_name = entry.path().stem().string();
        INFO("set=" << set_name);
        const std::string disk = read_text_file(entry.path());
        CHECK(normalized_text(
                  std::string{mnemos::manifests::irem_m75::game_manifest_toml(set_name)}) ==
              normalized_text(disk));
        rom_set_decl decl = parse_decl(disk, entry.path().string());
        CHECK(decl.name == set_name);
        CHECK(decl.board == "irem_m75");
        disk_names.emplace(set_name);
    }

    std::set<std::string, std::less<>> embedded_names;
    for (const auto& [set_name, _] : declarations) {
        embedded_names.emplace(set_name);
    }
    CHECK(disk_names == embedded_names);
}
