#include "file.hpp"
#include "m92_game_manifests.hpp"
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

#ifndef MNEMOS_IREM_M92_GAMES_DIR
#define MNEMOS_IREM_M92_GAMES_DIR ""
#endif

namespace {

    using mnemos::manifests::common::rom_set_decl;
    using mnemos::manifests::common::rom_set_file;
    using mnemos::manifests::common::rom_set_image;
    using mnemos::manifests::common::rom_set_region;

    struct expected_contract final {
        std::size_t tiles_size{};
        std::size_t sprites_size{};
        std::size_t samples_size{};
        std::uint8_t players{2U};
        mnemos::manifests::common::screen_orientation orientation{
            mnemos::manifests::common::screen_orientation::horizontal};
    };

    [[nodiscard]] const std::map<std::string, expected_contract, std::less<>>&
    expected_contracts() {
        static const std::map<std::string, expected_contract, std::less<>> contracts{
            {"bmaster",
             {.tiles_size = 0x100000U, .sprites_size = 0x200000U, .samples_size = 0x080000U}},
            {"crossbld",
             {.tiles_size = 0x100000U, .sprites_size = 0x200000U, .samples_size = 0x080000U}},
            {"geostorm",
             {.tiles_size = 0x200000U, .sprites_size = 0x400000U, .samples_size = 0x100000U}},
            {"gunforce",
             {.tiles_size = 0x100000U, .sprites_size = 0x100000U, .samples_size = 0x020000U}},
            {"gunforcej",
             {.tiles_size = 0x100000U, .sprites_size = 0x100000U, .samples_size = 0x020000U}},
            {"gunforceu",
             {.tiles_size = 0x100000U, .sprites_size = 0x100000U, .samples_size = 0x020000U}},
            {"gunforc2",
             {.tiles_size = 0x200000U, .sprites_size = 0x400000U, .samples_size = 0x100000U}},
            {"gunhohki",
             {.tiles_size = 0x100000U, .sprites_size = 0x200000U, .samples_size = 0x040000U}},
            {"hook",
             {.tiles_size = 0x100000U,
              .sprites_size = 0x400000U,
              .samples_size = 0x080000U,
              .players = 4U}},
            {"inthunt",
             {.tiles_size = 0x200000U, .sprites_size = 0x400000U, .samples_size = 0x080000U}},
            {"inthuntu",
             {.tiles_size = 0x200000U, .sprites_size = 0x400000U, .samples_size = 0x080000U}},
            {"lethalth",
             {.tiles_size = 0x100000U,
              .sprites_size = 0x100000U,
              .samples_size = 0x040000U,
              .orientation = mnemos::manifests::common::screen_orientation::vertical}},
            {"mysticri",
             {.tiles_size = 0x100000U, .sprites_size = 0x200000U, .samples_size = 0x040000U}},
            {"mysticrib",
             {.tiles_size = 0x100000U, .sprites_size = 0x200000U, .samples_size = 0x040000U}},
            {"nbbatman",
             {.tiles_size = 0x200000U,
              .sprites_size = 0x400000U,
              .samples_size = 0x080000U,
              .players = 4U}},
            {"nbbatmanu",
             {.tiles_size = 0x200000U,
              .sprites_size = 0x400000U,
              .samples_size = 0x080000U,
              .players = 4U}},
            {"rtypeleo",
             {.tiles_size = 0x200000U, .sprites_size = 0x400000U, .samples_size = 0x080000U}},
            {"rtypeleoj",
             {.tiles_size = 0x200000U, .sprites_size = 0x400000U, .samples_size = 0x080000U}},
            {"thndblst",
             {.tiles_size = 0x100000U,
              .sprites_size = 0x100000U,
              .samples_size = 0x040000U,
              .orientation = mnemos::manifests::common::screen_orientation::vertical}},
            {"uccops",
             {.tiles_size = 0x200000U,
              .sprites_size = 0x400000U,
              .samples_size = 0x080000U,
              .players = 3U}},
            {"uccopsar",
             {.tiles_size = 0x200000U,
              .sprites_size = 0x400000U,
              .samples_size = 0x080000U,
              .players = 3U}},
            {"uccopsj",
             {.tiles_size = 0x200000U,
              .sprites_size = 0x400000U,
              .samples_size = 0x080000U,
              .players = 3U}},
            {"uccopsu",
             {.tiles_size = 0x200000U,
              .sprites_size = 0x400000U,
              .samples_size = 0x080000U,
              .players = 3U}},
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

    void require_interleaved_region(const rom_set_region& region, std::size_t expected_size) {
        CHECK(region.size == expected_size);
        require_region_contract(region);
        REQUIRE((region.files.size() % 2U) == 0U);
        for (std::size_t i = 0; i < region.files.size(); i += 2U) {
            const rom_set_file& high = region.files[i];
            const rom_set_file& low = region.files[i + 1U];
            INFO("high=" << high.name << " low=" << low.name);
            CHECK(high.stride == 2U);
            CHECK(low.stride == 2U);
            CHECK((high.offset & 1U) == 1U);
            CHECK((low.offset & 1U) == 0U);
            CHECK((high.offset & ~std::size_t{1U}) == low.offset);
            CHECK(high.size == low.size);
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
        for (const auto& [set_name, _] : mnemos::manifests::irem_m92::embedded::game_manifests) {
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
        const std::string_view text = mnemos::manifests::irem_m92::game_manifest_toml(set_name);
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

} // namespace

TEST_CASE("m92 checked-in game manifests parse and cover local candidate corpus", "[m92][romset]") {
    namespace fs = std::filesystem;

    const fs::path games_dir{MNEMOS_IREM_M92_GAMES_DIR};
    REQUIRE_FALSE(games_dir.empty());
    REQUIRE(fs::exists(games_dir));

    std::map<std::string, rom_set_decl, std::less<>> declarations;
    for (const fs::directory_entry& entry : fs::directory_iterator(games_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
            continue;
        }
        const std::string text = read_text_file(entry.path());
        auto parsed =
            mnemos::manifests::common::parse_rom_set_decl(text, entry.path().filename().string());
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": " << error.message);
        }
        REQUIRE(parsed.ok());

        const rom_set_decl& decl = *parsed.value;
        INFO("set=" << decl.name);
        declarations.emplace(decl.name, std::move(*parsed.value));
    }

    const auto& expected = expected_contracts();
    std::set<std::string, std::less<>> names;
    for (const auto& [set_name, decl] : declarations) {
        const auto expected_it = expected.find(set_name);
        REQUIRE(expected_it != expected.end());
        const expected_contract& contract = expected_it->second;
        rom_set_decl effective_decl = decl;
        if (decl.parent.has_value()) {
            const auto parent_it = declarations.find(*decl.parent);
            REQUIRE(parent_it != declarations.end());
            effective_decl =
                mnemos::manifests::common::inherit_parent_regions(parent_it->second, decl);
        }
        const rom_set_decl& checked_decl = effective_decl;

        INFO("set=" << set_name);
        names.insert(decl.name);
        CHECK(checked_decl.board == "irem_m92");
        CHECK(checked_decl.orientation == contract.orientation);
        REQUIRE(checked_decl.sound.has_value());
        CHECK(*checked_decl.sound == "encrypted_v35");
        CHECK(checked_decl.players == contract.players);

        const rom_set_region* maincpu = find_region(checked_decl, "maincpu");
        REQUIRE(maincpu != nullptr);
        require_interleaved_region(*maincpu, mnemos::manifests::irem_m92::main_rom_size);

        const rom_set_region* soundcpu = find_region(checked_decl, "soundcpu");
        REQUIRE(soundcpu != nullptr);
        require_interleaved_region(*soundcpu, mnemos::manifests::irem_m92::sound_rom_size);

        const rom_set_region* tiles = find_region(checked_decl, "tiles");
        REQUIRE(tiles != nullptr);
        CHECK(tiles->size == contract.tiles_size);
        require_region_contract(*tiles);

        const rom_set_region* sprites = find_region(checked_decl, "sprites");
        REQUIRE(sprites != nullptr);
        CHECK(sprites->size == contract.sprites_size);
        require_region_contract(*sprites);

        const rom_set_region* samples = find_region(checked_decl, "samples");
        REQUIRE(samples != nullptr);
        CHECK(samples->size == contract.samples_size);
        require_region_contract(*samples);

        const rom_set_region* plds = find_region(checked_decl, "plds");
        REQUIRE(plds != nullptr);
        CHECK(plds->size == mnemos::manifests::irem_m92::plds_rom_size);
        require_region_contract(*plds);
    }

    CHECK(names == embedded_set_names());
}

TEST_CASE("m92 embedded game manifests mirror the checked-in roster", "[m92][romset]") {
    using mnemos::manifests::irem_m92::embedded::game_manifests;

    CHECK(game_manifests.size() == expected_contracts().size());
    for (const auto& [set_name, _] : game_manifests) {
        INFO("set=" << set_name);
        CHECK_FALSE(mnemos::manifests::irem_m92::game_manifest_toml(set_name).empty());
    }
    CHECK(mnemos::manifests::irem_m92::game_manifest_toml("firebarr").empty());
}

TEST_CASE("m92 local artifacts load CRC-clean through embedded manifests", "[m92][romset][data]") {
    const auto dir_env = environment_value("MNEMOS_M92_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M92_SET_DIR to directories containing the M92 zip/folder corpus");
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
        FAIL("missing M92 artifacts: " << missing.str());
    }

    for (const auto& set_name : expected_sets) {
        INFO("set=" << set_name);
        const auto decl = require_embedded_decl(set_name);
        auto effective_decl = decl;
        const auto contract_it = expected_contracts().find(set_name);
        REQUIRE(contract_it != expected_contracts().end());

        const auto source_it = indexed_sources.find(set_name);
        REQUIRE(source_it != indexed_sources.end());
        INFO("source=" << source_it->second.string());

        auto provider = require_provider(source_it->second);
        if (decl.parent.has_value()) {
            const auto parent_source_it = indexed_sources.find(*decl.parent);
            REQUIRE(parent_source_it != indexed_sources.end());
            INFO("parent_source=" << parent_source_it->second.string());
            const auto parent_decl = require_embedded_decl(*decl.parent);
            effective_decl =
                mnemos::manifests::common::inherit_parent_regions(parent_decl, effective_decl);
            provider = mnemos::manifests::common::make_fallback_rom_provider(
                std::move(provider), require_provider(parent_source_it->second));
        }

        const auto image = mnemos::manifests::common::load_rom_set(effective_decl, provider);
        for (const auto& issue : image.issues) {
            INFO(issue.file << ": " << issue.message);
        }
        CHECK(image.issues.empty());

        require_loaded_region(image, "maincpu", mnemos::manifests::irem_m92::main_rom_size);
        require_loaded_region(image, "soundcpu", mnemos::manifests::irem_m92::sound_rom_size);
        require_loaded_region(image, "tiles", contract_it->second.tiles_size);
        require_loaded_region(image, "sprites", contract_it->second.sprites_size);
        require_loaded_region(image, "samples", contract_it->second.samples_size);
        require_loaded_region(image, "plds", mnemos::manifests::irem_m92::plds_rom_size);
    }
}
