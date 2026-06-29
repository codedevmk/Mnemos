#include "file.hpp"
#include "m102_game_manifests.hpp"
#include "rom_set.hpp"
#include "rom_set_toml.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef MNEMOS_IREM_M102_GAMES_DIR
#define MNEMOS_IREM_M102_GAMES_DIR ""
#endif

namespace {

    using mnemos::manifests::common::rom_set_decl;
    using mnemos::manifests::common::rom_set_file;
    using mnemos::manifests::common::rom_set_image;
    using mnemos::manifests::common::rom_set_region;

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

    [[nodiscard]] const rom_set_file* find_file(const rom_set_region& region,
                                                std::string_view name) noexcept {
        const auto it =
            std::find_if(region.files.begin(), region.files.end(),
                         [name](const rom_set_file& file) { return file.name == name; });
        return it == region.files.end() ? nullptr : &*it;
    }

    void require_file_contract(const rom_set_region& region, std::string_view name,
                               std::size_t offset, std::size_t size, std::uint32_t crc32) {
        const rom_set_file* file = find_file(region, name);
        REQUIRE(file != nullptr);
        CHECK(file->offset == offset);
        CHECK(file->stride == 1U);
        CHECK(file->unit == 1U);
        CHECK_FALSE(file->swap);
        CHECK(file->source_offset == 0U);
        CHECK(file->length == 0U);
        CHECK(file->size == size);
        REQUIRE(file->crc32.has_value());
        CHECK(*file->crc32 == crc32);
    }

    [[nodiscard]] std::set<std::string, std::less<>> embedded_set_names() {
        std::set<std::string, std::less<>> names;
        for (const auto& [set_name, _] : mnemos::manifests::irem_m102::embedded::game_manifests) {
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
        if (!std::filesystem::is_regular_file(path, ec) ||
            !lowercase_equals(path.extension().string(), ".zip")) {
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
                const auto candidate_path = it->path();
                std::error_code entry_ec;
                if (it->is_directory(entry_ec) &&
                    candidate_path.filename().string() == "name-collisions") {
                    it.disable_recursion_pending();
                    continue;
                }
                if (is_exact_set_path(candidate_path, set_name)) {
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
        const std::string_view text = mnemos::manifests::irem_m102::game_manifest_toml(set_name);
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

    void require_filled_region(const rom_set_image& image, std::string_view name,
                               std::size_t expected_size, std::uint8_t fill) {
        const auto* region = image.region(name);
        REQUIRE(region != nullptr);
        CHECK(region->size() == expected_size);
        CHECK(std::all_of(region->begin(), region->end(),
                          [fill](std::uint8_t byte) { return byte == fill; }));
    }

    void require_main_upper_half_blank(const rom_set_image& image) {
        const auto* region = image.region("maincpu");
        REQUIRE(region != nullptr);
        REQUIRE(region->size() == mnemos::manifests::irem_m102::main_rom_size);
        REQUIRE(region->size() >= 0x020000U);
        CHECK(has_non_fill_byte(std::vector<std::uint8_t>{region->begin(), region->begin() + 0x10000}));
        CHECK(std::all_of(region->begin() + 0x10000, region->end(),
                          [](std::uint8_t byte) { return byte == 0xFFU; }));
    }

} // namespace

TEST_CASE("m102 embedded manifests cover local Hill Climber ROM contract", "[m102][romset]") {
    CHECK(embedded_set_names() == std::set<std::string, std::less<>>{"hclimber"});

    const auto decl = require_embedded_decl("hclimber");
    CHECK(decl.name == "hclimber");
    CHECK(decl.board == "irem_m102");
    CHECK_FALSE(decl.parent.has_value());
    CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::horizontal);
    CHECK(decl.players == 2U);
    REQUIRE(decl.sound.has_value());
    CHECK(*decl.sound == "ga20_manifest_only");
    REQUIRE(decl.regions.size() == 3U);

    const rom_set_region* maincpu = find_region(decl, "maincpu");
    REQUIRE(maincpu != nullptr);
    CHECK(maincpu->size == mnemos::manifests::irem_m102::main_rom_size);
    REQUIRE(maincpu->files.size() == 1U);
    require_file_contract(*maincpu, "hc-pr-c.ic23", 0x000000U, 0x020000U, 0x0b91671bU);

    const rom_set_region* ga20 = find_region(decl, "ga20");
    REQUIRE(ga20 != nullptr);
    CHECK(ga20->size == mnemos::manifests::irem_m102::ga20_rom_size);
    REQUIRE(ga20->files.size() == 2U);
    require_file_contract(*ga20, "hc-v0-.ic13", 0x000000U, 0x080000U, 0x1658e7b5U);
    require_file_contract(*ga20, "hc-v1-.ic14", 0x080000U, 0x080000U, 0xae505c2eU);

    const rom_set_region* plds = find_region(decl, "plds");
    REQUIRE(plds != nullptr);
    CHECK(plds->size == mnemos::manifests::irem_m102::pld_region_size);
    CHECK(plds->fill == 0x00U);
    CHECK(plds->files.empty());
}

TEST_CASE("m102 embedded manifests stay in sync with disk TOML", "[m102][romset]") {
    const std::filesystem::path games_dir{MNEMOS_IREM_M102_GAMES_DIR};
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
                  mnemos::manifests::irem_m102::game_manifest_toml(set_name)}) ==
              normalized_text(disk));
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(disk, entry.path().string());
        REQUIRE(parsed.ok());
        CHECK(parsed.value->name == set_name);
        CHECK(parsed.value->board == "irem_m102");
        disk_names.emplace(set_name);
    }

    CHECK(disk_names == embedded_set_names());
}

TEST_CASE("m102 local Hill Climber ZIP artifact loads CRC-clean through embedded manifest",
          "[m102][romset][data]") {
    const auto dir_env = environment_value("MNEMOS_M102_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M102_SET_DIR to directories containing the M102 ROM corpus");
    }

    const auto roots = source_roots(dir_env->c_str());
    REQUIRE_FALSE(roots.empty());
    for (const auto& root : roots) {
        INFO("root=" << root.string());
        REQUIRE(std::filesystem::exists(root));
    }

    const auto source = find_exact_source(roots, "hclimber");
    REQUIRE(source.has_value());
    INFO("source=" << source->string());

    const auto decl = require_embedded_decl("hclimber");
    const auto image = mnemos::manifests::common::load_rom_set(decl, require_provider(*source));
    for (const auto& issue : image.issues) {
        INFO(issue.file << ": " << issue.message);
    }
    CHECK(image.issues.empty());
    require_loaded_region(image, "maincpu", mnemos::manifests::irem_m102::main_rom_size);
    require_main_upper_half_blank(image);
    require_loaded_region(image, "ga20", mnemos::manifests::irem_m102::ga20_rom_size);
    require_filled_region(image, "plds", mnemos::manifests::irem_m102::pld_region_size, 0x00U);
}
