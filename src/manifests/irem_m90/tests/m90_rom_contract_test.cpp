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
        bool has_soundcpu{};
        bool has_samples{};
    };

    [[nodiscard]] const std::map<std::string, expected_contract, std::less<>>&
    expected_contracts() {
        static const std::map<std::string, expected_contract, std::less<>> contracts{
            {"atompunk", {.main_file_size = 0x20000U}},
            {"newapunk", {.main_file_size = 0x40000U}},
            {"bbmanwj", {.main_file_size = 0x40000U, .has_soundcpu = true,
                         .has_samples = true}},
            {"bbmanwja", {.main_file_size = 0x40000U, .has_soundcpu = true,
                          .has_samples = true}},
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
        REQUIRE(region.files.size() == 2U);
        const rom_set_file& high = region.files[0];
        const rom_set_file& low = region.files[1];
        CHECK(high.stride == 2U);
        CHECK(low.stride == 2U);
        CHECK((high.offset & 1U) == 1U);
        CHECK((low.offset & 1U) == 0U);
        CHECK((high.offset & ~std::size_t{1U}) == low.offset);
        CHECK(high.size == expected_file_size);
        CHECK(low.size == expected_file_size);
    }

    [[nodiscard]] rom_set_decl parse_decl(std::string_view text, std::string source) {
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, std::move(source));
        REQUIRE(parsed.ok());
        return std::move(*parsed.value);
    }

} // namespace

TEST_CASE("m90 embedded manifests cover local Atomic Punk/Bomber Man World contracts",
          "[m90][rom]") {
    const auto& contracts = expected_contracts();
    std::set<std::string, std::less<>> names;
    for (const auto& [set_name, toml] : mnemos::manifests::irem_m90::embedded::game_manifests) {
        INFO("set=" << set_name);
        names.emplace(std::string{set_name});
        const auto contract = contracts.find(set_name);
        REQUIRE(contract != contracts.end());

        rom_set_decl decl = parse_decl(toml, "embedded:irem_m90/" + std::string{set_name});
        CHECK(decl.name == set_name);
        CHECK(decl.board == "irem_m90");
        CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::horizontal);

        const rom_set_region* main = find_region(decl, "maincpu");
        REQUIRE(main != nullptr);
        require_interleaved_main_region(*main, contract->second.main_file_size);

        const rom_set_region* sound = find_region(decl, "soundcpu");
        if (contract->second.has_soundcpu) {
            REQUIRE(sound != nullptr);
            CHECK(sound->size == mnemos::manifests::irem_m90::sound_rom_size);
            require_region_contract(*sound);
        } else {
            CHECK(sound == nullptr);
        }

        const rom_set_region* samples = find_region(decl, "samples");
        if (contract->second.has_samples) {
            REQUIRE(samples != nullptr);
            CHECK(samples->size == mnemos::manifests::irem_m90::sample_rom_size);
            require_region_contract(*samples);
        } else {
            CHECK(samples == nullptr);
        }
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
