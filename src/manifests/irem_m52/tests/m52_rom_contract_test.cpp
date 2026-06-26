#include "m52_game_manifests.hpp"
#include "rom_set_toml.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    using mnemos::manifests::common::rom_set_decl;
    using mnemos::manifests::common::rom_set_dip_switch;

    [[nodiscard]] std::string read_text(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

    [[nodiscard]] rom_set_decl parse_decl(std::string_view toml, std::string_view source) {
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(toml, source);
        REQUIRE(parsed.ok());
        REQUIRE(parsed.value.has_value());
        return std::move(*parsed.value);
    }

    [[nodiscard]] std::map<std::string, rom_set_decl, std::less<>> embedded_declarations() {
        std::map<std::string, rom_set_decl, std::less<>> declarations;
        for (const std::string set : {"mpatrol", "mpatrolw"}) {
            const std::string_view embedded = mnemos::manifests::irem_m52::game_manifest_toml(set);
            REQUIRE_FALSE(embedded.empty());
            declarations.emplace(set, parse_decl(embedded, "embedded:" + set));
        }
        return declarations;
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
} // namespace

TEST_CASE("Irem M52 manifests parse and retain Moon Patrol contracts", "[irem_m52]") {
    using mnemos::manifests::common::screen_orientation;

    const auto raw_declarations = embedded_declarations();
    std::set<std::string, std::less<>> names;
    for (const auto& [set, raw_decl] : raw_declarations) {
        names.insert(set);
        rom_set_decl decl = raw_decl;
        if (decl.parent.has_value()) {
            const auto parent_it = raw_declarations.find(std::string{*decl.parent});
            REQUIRE(parent_it != raw_declarations.end());
            decl = mnemos::manifests::common::inherit_parent_regions(parent_it->second,
                                                                     std::move(decl));
        }
        CHECK(decl.name == set);
        CHECK(decl.board == "irem_m52");
        CHECK(decl.orientation == screen_orientation::horizontal);
        CHECK_FALSE(decl.regions.empty());
        CHECK(decl.dips.size() == 13U);
        CHECK(raw_dip_default(decl, 0x0000U) == 0x0201U);
    }
    CHECK(names == std::set<std::string, std::less<>>{"mpatrol", "mpatrolw"});

    const auto& parent = raw_declarations.at("mpatrol");
    const auto& clone = raw_declarations.at("mpatrolw");
    REQUIRE(clone.parent.has_value());
    CHECK(*clone.parent == "mpatrol");

    const auto effective = mnemos::manifests::common::inherit_parent_regions(parent, clone);
    CHECK(effective.regions.size() == parent.regions.size());
    CHECK(effective.name == "mpatrolw");

    const rom_set_dip_switch* cars = find_dip(parent, "Patrol Cars");
    REQUIRE(cars != nullptr);
    CHECK(cars->mask == 0x0003U);
    CHECK(cars->default_value == 0x0001U);
    REQUIRE(cars->options.size() == 4U);

    const rom_set_dip_switch* coinage = find_dip(parent, "Coinage");
    REQUIRE(coinage != nullptr);
    CHECK(coinage->mask == 0x00F0U);
    CHECK(coinage->default_value == 0x0000U);
    REQUIRE(coinage->condition.has_value());
    CHECK(coinage->condition->mask == 0x0400U);
    CHECK(coinage->condition->value == 0x0000U);
    REQUIRE(coinage->options.size() == 12U);

    const rom_set_dip_switch* cabinet = find_dip(parent, "Cabinet Type");
    REQUIRE(cabinet != nullptr);
    CHECK(cabinet->mask == 0x0200U);
    CHECK(cabinet->default_value == 0x0200U);

    const rom_set_dip_switch* test_mode = find_dip(parent, "Test Mode");
    REQUIRE(test_mode != nullptr);
    CHECK(test_mode->mask == 0x8000U);
    CHECK(test_mode->default_value == 0x0000U);
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
