#include "file.hpp"
#include "rom_set_toml.hpp"
#include "travrusa_game_manifests.hpp"
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

#ifndef MNEMOS_IREM_TRAVRUSA_GAMES_DIR
#define MNEMOS_IREM_TRAVRUSA_GAMES_DIR ""
#endif

namespace {

    using mnemos::manifests::common::rom_file_provider;
    using mnemos::manifests::common::rom_set_decl;
    using mnemos::manifests::common::rom_set_file;
    using mnemos::manifests::common::rom_set_image;
    using mnemos::manifests::common::rom_set_region;

    struct expected_set final {
        std::size_t main_size{};
        std::optional<std::string_view> parent;
    };

    struct provider_source final {
        std::filesystem::path path;
        rom_file_provider provider;
    };

    [[nodiscard]] const std::map<std::string, expected_set, std::less<>>& expected_sets() {
        static const std::map<std::string, expected_set, std::less<>> sets{
            {"motorace", {.main_size = 0x12000U, .parent = "travrusa"}},
            {"travrusa", {.main_size = 0x10000U, .parent = std::nullopt}},
            {"travrusab", {.main_size = 0x10000U, .parent = "travrusa"}},
            {"travrusab2", {.main_size = 0x10000U, .parent = "travrusa"}},
        };
        return sets;
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

    [[nodiscard]] bool has_alias(const rom_set_region& region, std::string_view file_name,
                                 std::string_view alias) noexcept {
        for (const rom_set_file& file : region.files) {
            if (file.name != file_name) {
                continue;
            }
            return std::find(file.aliases.begin(), file.aliases.end(), alias) !=
                   file.aliases.end();
        }
        return false;
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
        for (const auto& [set_name, _] :
             mnemos::manifests::irem_travrusa::embedded::game_manifests) {
            names.emplace(std::string{set_name});
        }
        return names;
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

    [[nodiscard]] rom_set_decl require_embedded_decl(std::string_view set_name) {
        const std::string_view text =
            mnemos::manifests::irem_travrusa::game_manifest_toml(set_name);
        REQUIRE_FALSE(text.empty());
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, std::string{set_name});
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": " << error.message);
        }
        REQUIRE(parsed.ok());
        return std::move(*parsed.value);
    }

    [[nodiscard]] std::optional<provider_source>
    provider_from_zip_path(const std::filesystem::path& path) {
        auto bytes = mnemos::io::read_file(path.string());
        if (!bytes.has_value()) {
            return std::nullopt;
        }
        if (auto nested = extract_single_nested_zip(*bytes)) {
            bytes = std::move(*nested);
        }
        auto provider = mnemos::manifests::common::make_zip_rom_provider(std::move(*bytes));
        if (!provider.has_value()) {
            return std::nullopt;
        }
        return provider_source{.path = path, .provider = std::move(*provider)};
    }

    [[nodiscard]] std::vector<provider_source>
    zip_sources(const std::vector<std::filesystem::path>& roots) {
        std::vector<provider_source> sources;
        auto maybe_add = [&](const std::filesystem::path& path) {
            if (!ends_with_zip(path.filename().string())) {
                return;
            }
            if (auto source = provider_from_zip_path(path)) {
                sources.push_back(std::move(*source));
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
            std::vector<std::filesystem::path> candidates;
            for (std::filesystem::recursive_directory_iterator it{root, ec}, end; !ec && it != end;
                 it.increment(ec)) {
                if (it->is_regular_file(ec)) {
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
            std::sort(candidates.begin(), candidates.end());
            for (const auto& path : candidates) {
                maybe_add(path);
            }
        }
        return sources;
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

    [[nodiscard]] std::optional<provider_source>
    find_crc_clean_source(const rom_set_decl& decl,
                          const std::vector<provider_source>& candidates,
                          const std::optional<rom_file_provider>& fallback) {
        for (const provider_source& candidate : candidates) {
            rom_file_provider provider = candidate.provider;
            if (fallback.has_value()) {
                provider = mnemos::manifests::common::make_fallback_rom_provider(
                    std::move(provider), *fallback);
            }
            const auto image = mnemos::manifests::common::load_rom_set(decl, provider);
            if (image.issues.empty()) {
                return candidate;
            }
        }
        return std::nullopt;
    }

} // namespace

TEST_CASE("travrusa embedded manifests cover local ROM contracts", "[travrusa][romset]") {
    using mnemos::manifests::common::screen_orientation;

    std::map<std::string, rom_set_decl, std::less<>> declarations;
    for (const auto& [set_name, toml] :
         mnemos::manifests::irem_travrusa::embedded::game_manifests) {
        INFO("set=" << set_name);
        auto parsed =
            mnemos::manifests::common::parse_rom_set_decl(toml, "embedded:irem_travrusa/" +
                                                                    std::string{set_name});
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": " << error.message);
        }
        REQUIRE(parsed.ok());

        const auto expected = expected_sets().find(std::string{set_name});
        REQUIRE(expected != expected_sets().end());
        const rom_set_decl& decl = *parsed.value;
        CHECK(decl.name == set_name);
        CHECK(decl.board == "irem_travrusa");
        CHECK(decl.orientation == screen_orientation::horizontal);
        if (expected->second.parent.has_value()) {
            REQUIRE(decl.parent.has_value());
            CHECK(*decl.parent == *expected->second.parent);
        } else {
            CHECK_FALSE(decl.parent.has_value());
        }
        declarations.emplace(decl.name, std::move(*parsed.value));
    }

    CHECK(embedded_set_names() ==
          std::set<std::string, std::less<>>{"motorace", "travrusa", "travrusab",
                                             "travrusab2"});

    for (auto& [set_name, raw_decl] : declarations) {
        INFO("set=" << set_name);
        rom_set_decl decl = raw_decl;
        if (decl.parent.has_value()) {
            const auto parent_it = declarations.find(*decl.parent);
            REQUIRE(parent_it != declarations.end());
            decl =
                mnemos::manifests::common::inherit_parent_regions(parent_it->second, std::move(decl));
        }

        REQUIRE(find_region(decl, "maincpu") != nullptr);
        REQUIRE(find_region(decl, "soundcpu") != nullptr);
        REQUIRE(find_region(decl, "tiles") != nullptr);
        REQUIRE(find_region(decl, "sprites") != nullptr);
        REQUIRE(find_region(decl, "proms") != nullptr);
        CHECK(find_region(decl, "maincpu")->size == expected_sets().at(set_name).main_size);
        CHECK(find_region(decl, "soundcpu")->size == 0x8000U);
        CHECK(find_region(decl, "tiles")->size == 0x6000U);
        CHECK(find_region(decl, "sprites")->size == 0x6000U);
        CHECK(find_region(decl, "proms")->size == 0x0320U);
        CHECK(find_region(decl, "maincpu")->files.size() == 4U);
        CHECK(find_region(decl, "soundcpu")->files.size() == 1U);
        CHECK(find_region(decl, "tiles")->files.size() == 3U);
        CHECK(find_region(decl, "sprites")->files.size() == 3U);
        CHECK(find_region(decl, "proms")->files.size() == 3U);
        require_region_contract(*find_region(decl, "maincpu"));
        require_region_contract(*find_region(decl, "soundcpu"));
        require_region_contract(*find_region(decl, "tiles"));
        require_region_contract(*find_region(decl, "sprites"));
        require_region_contract(*find_region(decl, "proms"));

        if (set_name == "travrusab2") {
            CHECK(has_alias(*find_region(decl, "maincpu"), "5.2l3", "zr1-5.l3"));
            CHECK(has_alias(*find_region(decl, "soundcpu"), "4.1a1", "mr10.1a"));
            CHECK(has_alias(*find_region(decl, "proms"), "mb7052.3h2", "tbp24s10.3"));
        }
    }
}

TEST_CASE("travrusa embedded manifests stay in sync with disk TOML", "[travrusa][romset]") {
    const std::filesystem::path games_dir{MNEMOS_IREM_TRAVRUSA_GAMES_DIR};
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
                  mnemos::manifests::irem_travrusa::game_manifest_toml(set_name)}) ==
              normalized_text(disk));
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(disk, entry.path().string());
        REQUIRE(parsed.ok());
        CHECK(parsed.value->name == set_name);
        CHECK(parsed.value->board == "irem_travrusa");
        disk_names.emplace(set_name);
    }

    CHECK(disk_names == embedded_set_names());
}

TEST_CASE("travrusa local ZIP artifacts load CRC-clean through embedded manifests",
          "[travrusa][romset][data]") {
    const auto dir_env = environment_value("MNEMOS_TRAVRUSA_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_TRAVRUSA_SET_DIR to directories containing the travrusa ZIP corpus");
    }

    const auto roots = source_roots(dir_env->c_str());
    REQUIRE_FALSE(roots.empty());
    for (const auto& root : roots) {
        INFO("root=" << root.string());
        REQUIRE(std::filesystem::exists(root));
    }

    const auto candidates = zip_sources(roots);
    REQUIRE_FALSE(candidates.empty());

    const auto parent_decl = require_embedded_decl("travrusa");
    auto parent_source = find_crc_clean_source(parent_decl, candidates, std::nullopt);
    REQUIRE(parent_source.has_value());
    INFO("travrusa parent source=" << parent_source->path.string());
    CHECK(parent_source->path.filename().string() != "travrusa.zip");

    for (const auto& [set_name, expected] : expected_sets()) {
        INFO("set=" << set_name);
        rom_set_decl decl = require_embedded_decl(set_name);
        std::optional<rom_file_provider> parent_provider;
        if (decl.parent.has_value()) {
            decl = mnemos::manifests::common::inherit_parent_regions(parent_decl, std::move(decl));
            parent_provider = parent_source->provider;
        }

        auto source = find_crc_clean_source(decl, candidates, parent_provider);
        REQUIRE(source.has_value());
        INFO("source=" << source->path.string());
        rom_file_provider provider = source->provider;
        if (parent_provider.has_value()) {
            provider = mnemos::manifests::common::make_fallback_rom_provider(
                std::move(provider), *parent_provider);
        }
        const auto image = mnemos::manifests::common::load_rom_set(decl, provider);
        for (const auto& issue : image.issues) {
            INFO(issue.file << ": " << issue.message);
        }
        CHECK(image.issues.empty());
        require_loaded_region(image, "maincpu", expected.main_size);
        require_loaded_region(image, "soundcpu", 0x8000U);
        require_loaded_region(image, "tiles", 0x6000U);
        require_loaded_region(image, "sprites", 0x6000U);
        require_loaded_region(image, "proms", 0x0320U);
    }
}
