#include "file.hpp"
#include "m62_game_manifests.hpp"
#include "m62_system.hpp"
#include "rom_set_toml.hpp"
#include "zip_archive.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef MNEMOS_IREM_M62_GAMES_DIR
#define MNEMOS_IREM_M62_GAMES_DIR ""
#endif

namespace {

    using mnemos::manifests::common::rom_set_decl;
    using mnemos::manifests::common::rom_set_file;
    using mnemos::manifests::common::rom_set_image;
    using mnemos::manifests::common::rom_set_region;

    struct expected_region_contract final {
        std::size_t raw_size{};
        std::size_t file_count{};
    };

    struct expected_contract final {
        std::size_t raw_size{};
        std::size_t file_count{};
        std::map<std::string, expected_region_contract, std::less<>> regions{};
    };

    [[nodiscard]] const std::map<std::string, expected_contract, std::less<>>&
    expected_contracts() {
        static const std::map<std::string, expected_contract, std::less<>> contracts{
            {"battroad", {.raw_size = 0x30740U, .file_count = 33U}},
            {"horizon", {.raw_size = 0x2e720U, .file_count = 21U}},
            {"ldrun",
             {.regions = {{"maincpu", {.raw_size = 0x10000U, .file_count = 4U}},
                           {"soundcpu", {.raw_size = 0x10000U, .file_count = 2U}},
                           {"gfx1", {.raw_size = 0x6000U, .file_count = 3U}},
                           {"gfx2", {.raw_size = 0x6000U, .file_count = 3U}},
                           {"spr_height_prom", {.raw_size = 0x20U, .file_count = 1U}},
                           {"spr_color_proms", {.raw_size = 0x300U, .file_count = 3U}},
                           {"chr_color_proms", {.raw_size = 0x300U, .file_count = 3U}},
                           {"timing", {.raw_size = 0x100U, .file_count = 1U}}}}},
            {"ldruna", {.raw_size = 0x6000U, .file_count = 3U}},
            {"ldrun2",
             {.regions = {{"maincpu", {.raw_size = 0x14000U, .file_count = 6U}},
                           {"soundcpu", {.raw_size = 0x10000U, .file_count = 3U}},
                           {"gfx1", {.raw_size = 0x6000U, .file_count = 3U}},
                           {"gfx2", {.raw_size = 0xC000U, .file_count = 6U}},
                           {"spr_height_prom", {.raw_size = 0x20U, .file_count = 1U}},
                           {"spr_color_proms", {.raw_size = 0x300U, .file_count = 3U}},
                           {"chr_color_proms", {.raw_size = 0x300U, .file_count = 3U}},
                           {"timing", {.raw_size = 0x100U, .file_count = 1U}}}}},
            {"ldrun3",
             {.regions = {{"maincpu", {.raw_size = 0x10000U, .file_count = 3U}},
                           {"soundcpu", {.raw_size = 0x10000U, .file_count = 2U}},
                           {"gfx1", {.raw_size = 0xC000U, .file_count = 3U}},
                           {"gfx2", {.raw_size = 0x18000U, .file_count = 6U}},
                           {"proms", {.raw_size = 0x100U, .file_count = 1U}},
                           {"spr_height_prom", {.raw_size = 0x20U, .file_count = 1U}},
                           {"spr_color_proms", {.raw_size = 0x300U, .file_count = 3U}},
                           {"chr_color_proms", {.raw_size = 0x300U, .file_count = 3U}},
                           {"timing", {.raw_size = 0x100U, .file_count = 1U}}}}},
            {"ldrun3j", {.raw_size = 0x18000U, .file_count = 6U}},
            {"ldrun4",
             {.regions = {{"maincpu", {.raw_size = 0x10000U, .file_count = 2U}},
                           {"soundcpu", {.raw_size = 0x10000U, .file_count = 2U}},
                           {"gfx1", {.raw_size = 0xC000U, .file_count = 3U}},
                           {"gfx2", {.raw_size = 0x18000U, .file_count = 6U}},
                           {"proms", {.raw_size = 0x100U, .file_count = 1U}},
                           {"spr_height_prom", {.raw_size = 0x20U, .file_count = 1U}},
                           {"spr_color_proms", {.raw_size = 0x300U, .file_count = 3U}},
                           {"chr_color_proms", {.raw_size = 0x300U, .file_count = 3U}},
                           {"timing", {.raw_size = 0x100U, .file_count = 1U}},
                           {"adpcm_samples", {.raw_size = 0x8000U, .file_count = 1U}}}}},
            {"lotlot",
             {.regions = {{"maincpu", {.raw_size = 0x10000U, .file_count = 2U}},
                           {"soundcpu", {.raw_size = 0x10000U, .file_count = 1U}},
                           {"gfx1", {.raw_size = 0x6000U, .file_count = 3U}},
                           {"gfx2", {.raw_size = 0x6000U, .file_count = 3U}},
                           {"gfx3", {.raw_size = 0x6000U, .file_count = 3U}},
                           {"spr_height_prom", {.raw_size = 0x20U, .file_count = 1U}},
                           {"spr_color_proms", {.raw_size = 0x300U, .file_count = 3U}},
                           {"chr_color_proms", {.raw_size = 0x300U, .file_count = 3U}},
                           {"bg_color_proms", {.raw_size = 0x300U, .file_count = 3U}},
                           {"timing", {.raw_size = 0x100U, .file_count = 1U}},
                           {"k_proms", {.raw_size = 0x400U, .file_count = 2U}}}}},
            {"spelunk2",
             {.regions = {{"maincpu", {.raw_size = 0x10000U, .file_count = 2U}},
                           {"soundcpu", {.raw_size = 0x10000U, .file_count = 2U}},
                           {"gfx1", {.raw_size = 0x38000U, .file_count = 9U}},
                           {"gfx2", {.raw_size = 0x18000U, .file_count = 6U}},
                           {"spr_height_prom", {.raw_size = 0x20U, .file_count = 1U}},
                           {"spr_color_proms", {.raw_size = 0x300U, .file_count = 3U}},
                           {"r_proms", {.raw_size = 0x600U, .file_count = 4U}},
                           {"timing", {.raw_size = 0x100U, .file_count = 1U}}}}},
            {"youjyudn", {.raw_size = 0x50a24U, .file_count = 27U}},
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

    [[nodiscard]] bool ends_with_zip(std::string_view name) {
        constexpr std::string_view suffix = ".zip";
        if (name.size() < suffix.size()) {
            return false;
        }
        return std::equal(suffix.rbegin(), suffix.rend(), name.rbegin(), [](char lhs, char rhs) {
            const auto l = static_cast<unsigned char>(lhs);
            const auto r = static_cast<unsigned char>(rhs);
            return std::tolower(l) == std::tolower(r);
        });
    }

    [[nodiscard]] bool zip_entry_is_file(const mnemos::compression::zip_entry& entry) noexcept {
        return !entry.name.empty() && entry.name.back() != '/' && entry.name.back() != '\\';
    }

    [[nodiscard]] std::set<std::string, std::less<>> embedded_set_names() {
        std::set<std::string, std::less<>> names;
        for (const auto& [set_name, _] : mnemos::manifests::irem_m62::embedded::game_manifests) {
            names.emplace(std::string{set_name});
        }
        return names;
    }

    [[nodiscard]] std::optional<std::string>
    single_nested_zip_set_id(std::span<const std::uint8_t> bytes) {
        auto archive = mnemos::compression::zip_archive::open(bytes);
        if (!archive.has_value()) {
            return std::nullopt;
        }

        const mnemos::compression::zip_entry* nested = nullptr;
        std::size_t file_count = 0U;
        for (const auto& entry : archive->entries()) {
            if (!zip_entry_is_file(entry)) {
                continue;
            }
            ++file_count;
            if (ends_with_zip(entry.name)) {
                nested = &entry;
            }
        }
        if (file_count != 1U || nested == nullptr) {
            return std::nullopt;
        }
        return std::filesystem::path{nested->name}.stem().string();
    }

    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    extract_single_nested_zip(std::span<const std::uint8_t> bytes) {
        auto archive = mnemos::compression::zip_archive::open(bytes);
        if (!archive.has_value()) {
            return std::nullopt;
        }

        const mnemos::compression::zip_entry* nested = nullptr;
        std::size_t file_count = 0U;
        for (const auto& entry : archive->entries()) {
            if (!zip_entry_is_file(entry)) {
                continue;
            }
            ++file_count;
            if (ends_with_zip(entry.name)) {
                nested = &entry;
            }
        }
        if (file_count != 1U || nested == nullptr) {
            return std::nullopt;
        }
        return archive->extract(*nested);
    }

    [[nodiscard]] std::vector<std::filesystem::path> source_roots(const char* env_value) {
        std::vector<std::filesystem::path> roots;
        if (env_value == nullptr || *env_value == '\0') {
            return roots;
        }
#if defined(_WIN32)
        constexpr char separator = ';';
#else
        constexpr char separator = ':';
#endif
        std::string_view text{env_value};
        std::size_t start = 0U;
        while (start <= text.size()) {
            const std::size_t end = text.find(separator, start);
            const std::string_view part = text.substr(
                start, end == std::string_view::npos ? std::string_view::npos : end - start);
            if (!part.empty()) {
                roots.emplace_back(std::string{part});
            }
            if (end == std::string_view::npos) {
                break;
            }
            start = end + 1U;
        }
        return roots;
    }

    [[nodiscard]] std::optional<std::string> environment_value(const char* name) {
#if defined(_WIN32)
        char* raw = nullptr;
        std::size_t size = 0U;
        if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
            return std::nullopt;
        }
        std::string value{raw, size > 0U ? size - 1U : 0U};
        std::free(raw);
        return value;
#else
        const char* raw = std::getenv(name);
        return raw != nullptr ? std::optional<std::string>{raw} : std::nullopt;
#endif
    }

    [[nodiscard]] std::optional<std::string>
    identify_source(const std::filesystem::path& path,
                    const std::set<std::string, std::less<>>& known) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            const std::string set_id = path.filename().string();
            return known.contains(set_id) ? std::optional<std::string>{set_id} : std::nullopt;
        }
        if (!std::filesystem::is_regular_file(path, ec) ||
            !ends_with_zip(path.filename().string())) {
            return std::nullopt;
        }

        const std::string stem = path.stem().string();
        if (known.contains(stem)) {
            return stem;
        }

        auto bytes = mnemos::io::read_file(path.string());
        if (!bytes.has_value()) {
            return std::nullopt;
        }
        auto nested = single_nested_zip_set_id(*bytes);
        if (!nested.has_value() || !known.contains(*nested)) {
            return std::nullopt;
        }
        return nested;
    }

    [[nodiscard]] std::map<std::string, std::filesystem::path, std::less<>>
    index_source_roots(const std::vector<std::filesystem::path>& roots) {
        const auto known = embedded_set_names();
        std::map<std::string, std::filesystem::path, std::less<>> sources;

        auto maybe_add = [&](const std::filesystem::path& path) {
            auto set_id = identify_source(path, known);
            if (set_id.has_value() && sources.find(*set_id) == sources.end()) {
                sources.emplace(std::move(*set_id), path);
            }
        };

        for (const auto& root : roots) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(root, ec)) {
                maybe_add(root);
                continue;
            }
            if (!std::filesystem::is_directory(root, ec)) {
                continue;
            }

            maybe_add(root);
            std::vector<std::filesystem::path> candidates;
            for (std::filesystem::recursive_directory_iterator it{root, ec}, end; !ec && it != end;
                 it.increment(ec)) {
                const auto candidate_path = it->path();
                std::error_code entry_ec;
                if (it->is_directory(entry_ec) &&
                    candidate_path.filename().string() == "name-collisions") {
                    it.disable_recursion_pending();
                    continue;
                }
                candidates.push_back(candidate_path);
            }
            std::sort(candidates.begin(), candidates.end());
            for (const auto& path : candidates) {
                maybe_add(path);
            }
        }

        return sources;
    }

    [[nodiscard]] rom_set_decl require_embedded_decl(std::string_view set_name) {
        const std::string_view text = mnemos::manifests::irem_m62::game_manifest_toml(set_name);
        REQUIRE_FALSE(text.empty());
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, std::string{set_name});
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": " << error.message);
        }
        REQUIRE(parsed.ok());
        return std::move(*parsed.value);
    }

    [[nodiscard]] mnemos::manifests::common::rom_file_provider
    require_provider(const std::filesystem::path& path) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            return mnemos::manifests::common::make_directory_rom_provider(path.string());
        }

        auto bytes = mnemos::io::read_file(path.string());
        REQUIRE(bytes.has_value());
        if (auto nested = extract_single_nested_zip(*bytes)) {
            bytes = std::move(*nested);
        }

        auto provider = mnemos::manifests::common::make_zip_rom_provider(std::move(*bytes));
        REQUIRE(provider.has_value());
        return std::move(*provider);
    }

    [[nodiscard]] bool has_non_fill_byte(const std::vector<std::uint8_t>& bytes,
                                         std::uint8_t fill = 0xFFU) {
        return std::any_of(bytes.begin(), bytes.end(),
                           [fill](std::uint8_t byte) { return byte != fill; });
    }

    void require_loaded_region(const rom_set_image& image, std::string_view name,
                               std::size_t expected_size) {
        const auto* region = image.region(name);
        REQUIRE(region != nullptr);
        CHECK(region->size() == expected_size);
        CHECK(has_non_fill_byte(*region));
    }

    void require_m6803_reset_vector(const rom_set_image& image,
                                    std::optional<std::uint32_t> expected_pc = std::nullopt) {
        const auto* sound = image.region("soundcpu");
        REQUIRE(sound != nullptr);
        REQUIRE(sound->size() == mnemos::manifests::irem_m62::sound_rom_size);
        const std::uint32_t pc =
            (static_cast<std::uint32_t>((*sound)[mnemos::chips::cpu::m6803::reset_vector])
             << 8U) |
            (*sound)[mnemos::chips::cpu::m6803::reset_vector + 1U];
        CHECK(pc >= mnemos::manifests::irem_m62::sound_rom_base);
        CHECK(pc < mnemos::manifests::irem_m62::sound_rom_base +
                       mnemos::manifests::irem_m62::sound_rom_mapped_size);
        if (expected_pc.has_value()) {
            CHECK(pc == *expected_pc);
        }
    }

} // namespace

TEST_CASE("m62 embedded manifests cover local ROM contracts", "[m62][romset]") {
    const auto& expected = expected_contracts();
    std::set<std::string, std::less<>> names;

    for (const auto& [set_name, toml] : mnemos::manifests::irem_m62::embedded::game_manifests) {
        INFO("set=" << set_name);
        const auto contract = expected.find(set_name);
        REQUIRE(contract != expected.end());

        auto parsed =
            mnemos::manifests::common::parse_rom_set_decl(toml, "embedded:irem_m62/" +
                                                                    std::string{set_name});
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": " << error.message);
        }
        REQUIRE(parsed.ok());

        const rom_set_decl& decl = *parsed.value;
        names.emplace(decl.name);
        CHECK(decl.name == set_name);
        CHECK(decl.board == "irem_m62");
        CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::horizontal);
        CHECK_FALSE(decl.parent.has_value());
        if (contract->second.regions.empty()) {
            REQUIRE(decl.regions.size() == 1U);

            const rom_set_region* raw = find_region(decl, "raw_media");
            REQUIRE(raw != nullptr);
            CHECK(raw->size == contract->second.raw_size);
            CHECK(raw->files.size() == contract->second.file_count);
            require_region_contract(*raw);
        } else {
            REQUIRE(decl.regions.size() == contract->second.regions.size());
            for (const auto& [region_name, region_contract] : contract->second.regions) {
                const rom_set_region* region = find_region(decl, region_name);
                REQUIRE(region != nullptr);
                CHECK(region->size == region_contract.raw_size);
                CHECK(region->files.size() == region_contract.file_count);
                require_region_contract(*region);
            }
        }
    }

    CHECK(names == embedded_set_names());
}

TEST_CASE("m62 embedded manifests stay in sync with disk TOML", "[m62][romset]") {
    const std::filesystem::path games_dir{MNEMOS_IREM_M62_GAMES_DIR};
    REQUIRE_FALSE(games_dir.empty());
    REQUIRE(std::filesystem::exists(games_dir));

    std::set<std::string, std::less<>> disk_names;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator{games_dir}) {
        if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
            continue;
        }
        const std::string set_name = entry.path().stem().string();
        INFO("set=" << set_name);
        const std::string disk = read_text_file(entry.path());
        CHECK(normalized_text(std::string{
                  mnemos::manifests::irem_m62::game_manifest_toml(set_name)}) ==
              normalized_text(disk));
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(disk, entry.path().string());
        REQUIRE(parsed.ok());
        CHECK(parsed.value->name == set_name);
        CHECK(parsed.value->board == "irem_m62");
        disk_names.emplace(set_name);
    }

    CHECK(disk_names == embedded_set_names());
}

TEST_CASE("m62 local wrapper artifacts load CRC-clean through embedded manifests",
          "[m62][romset][data]") {
    const auto dir_env = environment_value("MNEMOS_M62_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M62_SET_DIR to directories containing the M62 wrapper corpus");
    }

    const auto roots = source_roots(dir_env->c_str());
    REQUIRE_FALSE(roots.empty());
    for (const auto& root : roots) {
        INFO("root=" << root.string());
        REQUIRE(std::filesystem::exists(root));
    }

    const auto indexed_sources = index_source_roots(roots);
    const auto expected_sets = embedded_set_names();
    std::vector<std::string> missing_sets;
    for (const auto& set_name : expected_sets) {
        if (indexed_sources.find(set_name) == indexed_sources.end()) {
            missing_sets.push_back(set_name);
        }
    }
    if (!missing_sets.empty()) {
        std::ostringstream missing;
        for (std::size_t i = 0; i < missing_sets.size(); ++i) {
            if (i != 0U) {
                missing << ", ";
            }
            missing << missing_sets[i];
        }
        FAIL("missing M62 artifacts: " << missing.str());
    }

    for (const auto& set_name : expected_sets) {
        INFO("set=" << set_name);
        const auto contract_it = expected_contracts().find(set_name);
        REQUIRE(contract_it != expected_contracts().end());
        const auto decl = require_embedded_decl(set_name);

        const auto source_it = indexed_sources.find(set_name);
        REQUIRE(source_it != indexed_sources.end());
        INFO("source=" << source_it->second.string());

        const auto image =
            mnemos::manifests::common::load_rom_set(decl, require_provider(source_it->second));
        for (const auto& issue : image.issues) {
            INFO(issue.file << ": " << issue.message);
        }
        CHECK(image.issues.empty());
        if (contract_it->second.regions.empty()) {
            require_loaded_region(image, "raw_media", contract_it->second.raw_size);
        } else {
            for (const auto& [region_name, region_contract] : contract_it->second.regions) {
                require_loaded_region(image, region_name, region_contract.raw_size);
            }
            if (set_name == "ldrun" || set_name == "ldrun2" || set_name == "ldrun3" ||
                set_name == "ldrun4" || set_name == "lotlot" || set_name == "spelunk2") {
                require_m6803_reset_vector(image, 0xFA00U);
            }
        }
    }
}
