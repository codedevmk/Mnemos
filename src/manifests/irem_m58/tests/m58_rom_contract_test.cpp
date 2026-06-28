#include "file.hpp"
#include "m58_game_manifests.hpp"
#include "m58_system.hpp"
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
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef MNEMOS_IREM_M58_GAMES_DIR
#define MNEMOS_IREM_M58_GAMES_DIR ""
#endif

namespace {

    using mnemos::manifests::common::rom_set_decl;
    using mnemos::manifests::common::rom_set_file;
    using mnemos::manifests::common::rom_set_image;
    using mnemos::manifests::common::rom_set_region;

    struct expected_region final {
        std::size_t size{};
        std::size_t file_count{};
    };

    [[nodiscard]] const std::map<std::string, expected_region, std::less<>>&
    expected_effective_regions() {
        static const std::map<std::string, expected_region, std::less<>> regions{
            {"maincpu", {.size = 0x10000U, .file_count = 3U}},
            {"proms", {.size = 0x0520U, .file_count = 6U}},
            {"soundcpu", {.size = 0x10000U, .file_count = 4U}},
            {"sprites", {.size = 0x0c000U, .file_count = 6U}},
            {"tiles", {.size = 0x06000U, .file_count = 3U}},
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

    [[nodiscard]] std::set<std::string, std::less<>> embedded_set_names() {
        std::set<std::string, std::less<>> names;
        for (const auto& [set_name, _] : mnemos::manifests::irem_m58::embedded::game_manifests) {
            names.emplace(std::string{set_name});
        }
        return names;
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

    [[nodiscard]] bool lowercase_equals(std::string_view lhs, std::string_view rhs) {
        return lhs.size() == rhs.size() &&
               std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char l, char r) {
                   return std::tolower(static_cast<unsigned char>(l)) ==
                          std::tolower(static_cast<unsigned char>(r));
               });
    }

    [[nodiscard]] bool is_exact_set_path(const std::filesystem::path& path,
                                         std::string_view set_name) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            return lowercase_equals(path.filename().string(), set_name);
        }
        if (!std::filesystem::is_regular_file(path, ec) || path.extension() != ".zip") {
            return false;
        }
        return lowercase_equals(path.stem().string(), set_name);
    }

    [[nodiscard]] std::optional<std::filesystem::path>
    find_exact_source(const std::vector<std::filesystem::path>& roots, std::string_view set_name) {
        std::vector<std::filesystem::path> candidates;
        for (const auto& root : roots) {
            std::error_code ec;
            if (is_exact_set_path(root, set_name)) {
                candidates.push_back(root);
            }
            if (!std::filesystem::is_directory(root, ec)) {
                continue;
            }
            for (std::filesystem::recursive_directory_iterator it{root, ec}, end; !ec && it != end;
                 it.increment(ec)) {
                if (is_exact_set_path(it->path(), set_name)) {
                    const auto candidate_path = it->path();
                std::error_code entry_ec;
                if (it->is_directory(entry_ec) &&
                    candidate_path.filename().string() == "name-collisions") {
                    it.disable_recursion_pending();
                    continue;
                }
                candidates.push_back(candidate_path);
                }
            }
        }
        std::sort(candidates.begin(), candidates.end());
        if (candidates.empty()) {
            return std::nullopt;
        }
        return candidates.front();
    }

    [[nodiscard]] rom_set_decl require_embedded_decl(std::string_view set_name) {
        const std::string_view text = mnemos::manifests::irem_m58::game_manifest_toml(set_name);
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

        auto provider = mnemos::manifests::common::make_zip_rom_provider_from_path(path.string());
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

    void require_m6803_reset_vector(const rom_set_image& image, std::string_view set_name) {
        namespace m58 = mnemos::manifests::irem_m58;

        const auto* sound = image.region("soundcpu");
        REQUIRE(sound != nullptr);
        REQUIRE(sound->size() == m58::sound_rom_size);
        const auto vector = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>((*sound)[mnemos::chips::cpu::m6803::reset_vector]) << 8U) |
            (*sound)[mnemos::chips::cpu::m6803::reset_vector + 1U]);
        const auto vector32 = static_cast<std::uint32_t>(vector);
        const auto mapped_end =
            static_cast<std::uint32_t>(m58::sound_rom_base) +
            static_cast<std::uint32_t>(m58::sound_rom_mapped_size);
        CHECK(vector32 >= m58::sound_rom_base);
        CHECK(vector32 < mapped_end);
        if (set_name == "10yard") {
            CHECK(vector == 0xFA80U);
        }
    }

} // namespace

TEST_CASE("m58 embedded manifests cover local 10-Yard Fight ROM contracts",
          "[m58][romset]") {
    const auto expected_sets = embedded_set_names();
    CHECK(expected_sets ==
          std::set<std::string, std::less<>>{"10yard", "10yardj", "vs10yard", "vs10yardj"});

    std::map<std::string, rom_set_decl, std::less<>> declarations;
    for (const auto& [set_name, toml] : mnemos::manifests::irem_m58::embedded::game_manifests) {
        INFO("set=" << set_name);
        auto parsed =
            mnemos::manifests::common::parse_rom_set_decl(toml, "embedded:irem_m58/" +
                                                                    std::string{set_name});
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": " << error.message);
        }
        REQUIRE(parsed.ok());
        declarations.emplace(parsed.value->name, std::move(*parsed.value));
    }
    CHECK(declarations.size() == expected_sets.size());

    for (const auto& [set_name, decl] : declarations) {
        INFO("set=" << set_name);
        CHECK(decl.board == "irem_m58");
        CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::horizontal);
        if (set_name == "10yard") {
            CHECK_FALSE(decl.parent.has_value());
        } else {
            REQUIRE(decl.parent.has_value());
            CHECK(*decl.parent == "10yard");
        }

        rom_set_decl effective_decl = decl;
        if (decl.parent.has_value()) {
            const auto parent_it = declarations.find(*decl.parent);
            REQUIRE(parent_it != declarations.end());
            effective_decl =
                mnemos::manifests::common::inherit_parent_regions(parent_it->second, decl);
        }
        REQUIRE(effective_decl.regions.size() == expected_effective_regions().size());
        for (const auto& [region_name, expected] : expected_effective_regions()) {
            const rom_set_region* region = find_region(effective_decl, region_name);
            REQUIRE(region != nullptr);
            CHECK(region->size == expected.size);
            CHECK(region->files.size() == expected.file_count);
            require_region_contract(*region);
        }
    }
}

TEST_CASE("m58 embedded manifests stay in sync with disk TOML", "[m58][romset]") {
    const std::filesystem::path games_dir{MNEMOS_IREM_M58_GAMES_DIR};
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
                  mnemos::manifests::irem_m58::game_manifest_toml(set_name)}) ==
              normalized_text(disk));
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(disk, entry.path().string());
        REQUIRE(parsed.ok());
        CHECK(parsed.value->name == set_name);
        CHECK(parsed.value->board == "irem_m58");
        disk_names.emplace(set_name);
    }

    CHECK(disk_names == embedded_set_names());
}

TEST_CASE("m58 local ROM artifacts load CRC-clean through embedded manifests",
          "[m58][romset][data]") {
    const auto dir_env = environment_value("MNEMOS_M58_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M58_SET_DIR to directories containing the M58 ROM corpus");
    }

    const auto roots = source_roots(dir_env->c_str());
    REQUIRE_FALSE(roots.empty());
    for (const auto& root : roots) {
        INFO("root=" << root.string());
        REQUIRE(std::filesystem::exists(root));
    }

    const auto expected_sets = embedded_set_names();
    for (const auto& set_name : expected_sets) {
        INFO("set=" << set_name);
        const auto source = find_exact_source(roots, set_name);
        REQUIRE(source.has_value());
        INFO("source=" << source->string());

        const auto decl = require_embedded_decl(set_name);
        auto effective_decl = decl;
        auto provider = require_provider(*source);
        if (decl.parent.has_value()) {
            const auto parent_source = find_exact_source(roots, *decl.parent);
            REQUIRE(parent_source.has_value());
            INFO("parent_source=" << parent_source->string());
            const auto parent_decl = require_embedded_decl(*decl.parent);
            effective_decl =
                mnemos::manifests::common::inherit_parent_regions(parent_decl, effective_decl);
            provider = mnemos::manifests::common::make_fallback_rom_provider(
                std::move(provider), require_provider(*parent_source));
        }

        const auto image = mnemos::manifests::common::load_rom_set(effective_decl, provider);
        for (const auto& issue : image.issues) {
            INFO(issue.file << ": " << issue.message);
        }
        CHECK(image.issues.empty());
        for (const auto& [region_name, expected] : expected_effective_regions()) {
            require_loaded_region(image, region_name, expected.size);
        }
        require_m6803_reset_vector(image, set_name);
    }
}
