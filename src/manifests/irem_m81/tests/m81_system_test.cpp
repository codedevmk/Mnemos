#include "file.hpp"
#include "m81_game_manifests.hpp"
#include "m81_system.hpp"
#include "rom_set_toml.hpp"
#include "zip_archive.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
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

#ifndef MNEMOS_IREM_M81_GAMES_DIR
#define MNEMOS_IREM_M81_GAMES_DIR ""
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

    [[nodiscard]] const rom_set_region* find_region(const rom_set_decl& decl,
                                                    std::string_view name) noexcept {
        const auto it =
            std::find_if(decl.regions.begin(), decl.regions.end(),
                         [name](const rom_set_region& region) { return region.name == name; });
        return it == decl.regions.end() ? nullptr : &*it;
    }

    [[nodiscard]] const rom_set_file* find_file_at_or_after(const rom_set_region& region,
                                                            std::size_t offset) noexcept {
        const auto it = std::find_if(
            region.files.begin(), region.files.end(),
            [offset](const rom_set_file& file) { return file.offset >= offset; });
        return it == region.files.end() ? nullptr : &*it;
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

    void require_boot_chunk_reload(const rom_set_region& maincpu) {
        REQUIRE(maincpu.size == mnemos::manifests::irem_m81::main_rom_size);
        const rom_set_file* reload = find_file_at_or_after(maincpu, 0xC0000U);
        REQUIRE(reload != nullptr);
        CHECK(reload->stride == 2U);
        CHECK(reload->crc32.has_value());
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
        for (const auto& [set_name, _] : mnemos::manifests::irem_m81::embedded::game_manifests) {
            names.emplace(std::string{set_name});
        }
        return names;
    }

    [[nodiscard]] std::optional<std::string> title_wrapped_set_id(std::string_view stem) {
        if (stem == "Dragon-Breed_Arcade_EN") {
            return std::string{"dbreed"};
        }
        if (stem == "Hammerin-Harry_Arcade_EN") {
            return std::string{"hharry"};
        }
        if (stem == "X-Multiply_Arcade_EN") {
            return std::string{"xmultipl"};
        }
        return std::nullopt;
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
        if (auto wrapped = title_wrapped_set_id(stem);
            wrapped.has_value() && known.contains(*wrapped)) {
            return wrapped;
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
                candidates.push_back(it->path());
            }
            std::sort(candidates.begin(), candidates.end());
            for (const auto& path : candidates) {
                maybe_add(path);
            }
        }

        return sources;
    }

    [[nodiscard]] rom_set_decl require_embedded_decl(std::string_view set_name) {
        const std::string_view text = mnemos::manifests::irem_m81::game_manifest_toml(set_name);
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

    [[nodiscard]] std::vector<std::uint8_t>
    make_m81_program(const std::vector<std::uint8_t>& program) {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m81::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m81_program() {
        return make_m81_program(
            {0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U, 0x42U, 0xA2U, 0x00U, 0x00U, 0xF4U});
    }

    [[nodiscard]] bool frame_has_nonblack(const mnemos::chips::frame_buffer_view& frame) {
        for (std::uint32_t y = 0; y < frame.height; ++y) {
            for (std::uint32_t x = 0; x < frame.width; ++x) {
                if (frame.pixels[static_cast<std::size_t>(y) * frame.effective_stride() + x] !=
                    0U) {
                    return true;
                }
            }
        }
        return false;
    }

} // namespace

TEST_CASE("m81 checked-in game manifests parse and cover local candidate corpus",
          "[m81][romset]") {
    namespace fs = std::filesystem;

    const fs::path games_dir{MNEMOS_IREM_M81_GAMES_DIR};
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

    const std::set<std::string, std::less<>> expected_names{"dbreed", "hharry", "xmultipl"};
    std::set<std::string, std::less<>> names;
    for (const auto& [set_name, decl] : declarations) {
        INFO("set=" << set_name);
        names.insert(decl.name);
        CHECK(decl.board == "irem_m81");
        CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::horizontal);

        const rom_set_region* maincpu = find_region(decl, "maincpu");
        REQUIRE(maincpu != nullptr);
        require_region_contract(*maincpu);
        require_boot_chunk_reload(*maincpu);

        REQUIRE(find_region(decl, "soundcpu") != nullptr);
        CHECK(find_region(decl, "soundcpu")->size == mnemos::manifests::irem_m81::sound_rom_size);
        require_region_contract(*find_region(decl, "soundcpu"));

        REQUIRE(find_region(decl, "samples") != nullptr);
        CHECK(find_region(decl, "samples")->size == mnemos::manifests::irem_m81::sample_rom_size);
        require_region_contract(*find_region(decl, "samples"));

        REQUIRE(find_region(decl, "sprites") != nullptr);
        REQUIRE(find_region(decl, "tiles") != nullptr);
        REQUIRE(find_region(decl, "proms") != nullptr);
        CHECK(find_region(decl, "proms")->size == mnemos::manifests::irem_m81::proms_size);
        require_region_contract(*find_region(decl, "sprites"));
        require_region_contract(*find_region(decl, "tiles"));
        require_region_contract(*find_region(decl, "proms"));
    }

    CHECK(names == expected_names);
    CHECK(mnemos::manifests::irem_m81::board_params_for("dbreed").rom_layout ==
          "single_boot_pair");
    CHECK(mnemos::manifests::irem_m81::board_params_for("hharry").rom_layout ==
          "small_boot_pair");
    CHECK(mnemos::manifests::irem_m81::board_params_for("xmultipl").rom_layout ==
          "large_boot_pair");
}

TEST_CASE("m81 embedded game manifests mirror the checked-in roster", "[m81][romset]") {
    using mnemos::manifests::irem_m81::embedded::game_manifests;

    CHECK(game_manifests.size() == 3U);
    CHECK_FALSE(mnemos::manifests::irem_m81::game_manifest_toml("dbreed").empty());
    CHECK_FALSE(mnemos::manifests::irem_m81::game_manifest_toml("hharry").empty());
    CHECK_FALSE(mnemos::manifests::irem_m81::game_manifest_toml("xmultipl").empty());
    CHECK(mnemos::manifests::irem_m81::game_manifest_toml("rtype2").empty());
}

TEST_CASE("m81 executable board runs a V30/Z80/YM frame", "[m81]") {
    namespace m81 = mnemos::manifests::irem_m81;

    rom_set_image image;
    image.regions.emplace("maincpu", synthetic_m81_program());
    image.regions.emplace("soundcpu", std::vector<std::uint8_t>(m81::sound_rom_size, 0x00U));
    image.regions.emplace("tiles", std::vector<std::uint8_t>(0x1000U, 0x35U));
    image.regions.emplace("sprites", std::vector<std::uint8_t>(0x1000U, 0xA7U));
    image.regions.emplace("proms", std::vector<std::uint8_t>(0x100U, 0x11U));

    auto system = m81::assemble_m81(std::move(image), m81::board_params_for("dbreed"));
    REQUIRE(system != nullptr);
    system->run_frame();

    CHECK(system->work_ram[0] == 0x42U);
    CHECK(system->video.frame_index() == 1U);
    CHECK(system->fm.elapsed_clocks() >= m81::sound_cycles_per_frame);
    CHECK(frame_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m81 save state rejects a different board layout profile", "[m81]") {
    namespace m81 = mnemos::manifests::irem_m81;

    rom_set_image image;
    image.regions.emplace("maincpu", synthetic_m81_program());
    image.regions.emplace("soundcpu", std::vector<std::uint8_t>(m81::sound_rom_size, 0x00U));
    image.regions.emplace("tiles", std::vector<std::uint8_t>(0x1000U, 0x35U));
    image.regions.emplace("sprites", std::vector<std::uint8_t>(0x1000U, 0xA7U));
    image.regions.emplace("proms", std::vector<std::uint8_t>(0x100U, 0x11U));

    auto source = m81::assemble_m81(image, m81::board_params_for("hharry"));
    REQUIRE(source != nullptr);
    source->run_frame();

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto same_layout = m81::assemble_m81(image, m81::board_params_for("hharry"));
    REQUIRE(same_layout != nullptr);
    mnemos::chips::state_reader same_reader(snapshot);
    same_layout->load_state(same_reader);
    CHECK(same_reader.ok());

    auto different_layout = m81::assemble_m81(std::move(image), m81::board_params_for("xmultipl"));
    REQUIRE(different_layout != nullptr);
    mnemos::chips::state_reader different_reader(snapshot);
    different_layout->load_state(different_reader);
    CHECK_FALSE(different_reader.ok());
}

TEST_CASE("m81 local artifacts load CRC-clean through embedded manifests",
          "[m81][romset][data]") {
    namespace m81 = mnemos::manifests::irem_m81;

    const auto dir_env = environment_value("MNEMOS_M81_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M81_SET_DIR to directories containing the M81 zip/folder corpus");
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
        FAIL("missing M81 artifacts: " << missing.str());
    }

    for (const auto& set_name : expected_sets) {
        INFO("set=" << set_name);
        const auto decl = require_embedded_decl(set_name);
        const auto source_it = indexed_sources.find(set_name);
        REQUIRE(source_it != indexed_sources.end());
        INFO("source=" << source_it->second.string());

        const auto image = mnemos::manifests::common::load_rom_set(decl, require_provider(source_it->second));
        for (const auto& issue : image.issues) {
            INFO(issue.file << ": " << issue.message);
        }
        CHECK(image.issues.empty());

        require_loaded_region(image, "maincpu", m81::main_rom_size);
        require_loaded_region(image, "soundcpu", m81::sound_rom_size);
        require_loaded_region(image, "samples", m81::sample_rom_size);
        const auto* sprites = find_region(decl, "sprites");
        const auto* tiles = find_region(decl, "tiles");
        REQUIRE(sprites != nullptr);
        REQUIRE(tiles != nullptr);
        require_loaded_region(image, "sprites", sprites->size);
        require_loaded_region(image, "tiles", tiles->size);
        require_loaded_region(image, "proms", m81::proms_size);
    }
}
