#include "file.hpp"
#include "m119_game_manifests.hpp"
#include "rom_set.hpp"
#include "rom_set_toml.hpp"

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
#include <vector>

#ifndef MNEMOS_IREM_M119_GAMES_DIR
#define MNEMOS_IREM_M119_GAMES_DIR ""
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
                               std::size_t offset, std::size_t stride, std::size_t size,
                               std::uint32_t crc32) {
        const rom_set_file* file = find_file(region, name);
        REQUIRE(file != nullptr);
        CHECK(file->offset == offset);
        CHECK(file->stride == stride);
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
        for (const auto& [set_name, _] : mnemos::manifests::irem_m119::embedded::game_manifests) {
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
        const std::string_view text = mnemos::manifests::irem_m119::game_manifest_toml(set_name);
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

} // namespace

TEST_CASE("m119 embedded manifests cover local Scumimon ROM contract", "[m119][romset]") {
    CHECK(embedded_set_names() == std::set<std::string, std::less<>>{"scumimon"});

    const auto decl = require_embedded_decl("scumimon");
    CHECK(decl.name == "scumimon");
    CHECK(decl.board == "irem_m119");
    CHECK_FALSE(decl.parent.has_value());
    CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::horizontal);
    CHECK(decl.players == 1U);
    REQUIRE(decl.sound.has_value());
    CHECK(*decl.sound == "ymz280b");
    REQUIRE(decl.regions.size() == 3U);

    const rom_set_region* maincpu = find_region(decl, "maincpu");
    REQUIRE(maincpu != nullptr);
    CHECK(maincpu->size == mnemos::manifests::irem_m119::main_rom_size);
    REQUIRE(maincpu->files.size() == 1U);
    require_file_contract(*maincpu, "scu1.a-pr00-c.ic43", 0x000000U, 1U, 0x080000U,
                          0x18c17fcfU);

    const rom_set_region* vdp = find_region(decl, "vdp");
    REQUIRE(vdp != nullptr);
    CHECK(vdp->size == mnemos::manifests::irem_m119::vdp_rom_size);
    REQUIRE(vdp->files.size() == 2U);
    require_file_contract(*vdp, "scu1.b-c0.ic4", 0x000000U, 2U, 0x200000U, 0x56402e4bU);
    require_file_contract(*vdp, "scu1.b-c1.ic2", 0x000001U, 2U, 0x200000U, 0xadff81baU);

    const rom_set_region* ymz = find_region(decl, "ymz");
    REQUIRE(ymz != nullptr);
    CHECK(ymz->size == mnemos::manifests::irem_m119::ymz_rom_size);
    REQUIRE(ymz->files.size() == 1U);
    require_file_contract(*ymz, "scu1.a-v0-.ic35", 0x000000U, 1U, 0x100000U, 0xef52018dU);
}

TEST_CASE("m119 embedded manifests stay in sync with disk TOML", "[m119][romset]") {
    const std::filesystem::path games_dir{MNEMOS_IREM_M119_GAMES_DIR};
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
                  mnemos::manifests::irem_m119::game_manifest_toml(set_name)}) ==
              normalized_text(disk));
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(disk, entry.path().string());
        REQUIRE(parsed.ok());
        CHECK(parsed.value->name == set_name);
        CHECK(parsed.value->board == "irem_m119");
        disk_names.emplace(set_name);
    }

    CHECK(disk_names == embedded_set_names());
}

TEST_CASE("m119 local Scumimon ZIP artifact loads CRC-clean through embedded manifest",
          "[m119][romset][data]") {
    const auto dir_env = environment_value("MNEMOS_M119_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M119_SET_DIR to directories containing the M119 ROM corpus");
    }

    const auto roots = source_roots(dir_env->c_str());
    REQUIRE_FALSE(roots.empty());
    for (const auto& root : roots) {
        INFO("root=" << root.string());
        REQUIRE(std::filesystem::exists(root));
    }

    const auto source = find_exact_source(roots, "scumimon");
    REQUIRE(source.has_value());
    INFO("source=" << source->string());

    const auto decl = require_embedded_decl("scumimon");
    const auto image = mnemos::manifests::common::load_rom_set(decl, require_provider(*source));
    for (const auto& issue : image.issues) {
        INFO(issue.file << ": " << issue.message);
    }
    CHECK(image.issues.empty());
    require_loaded_region(image, "maincpu", mnemos::manifests::irem_m119::main_rom_size);
    require_loaded_region(image, "vdp", mnemos::manifests::irem_m119::vdp_rom_size);
    require_loaded_region(image, "ymz", mnemos::manifests::irem_m119::ymz_rom_size);
}
