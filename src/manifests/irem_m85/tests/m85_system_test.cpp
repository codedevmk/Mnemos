#include "file.hpp"
#include "m81_system.hpp"
#include "m85_game_manifests.hpp"
#include "m85_system.hpp"
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

#ifndef MNEMOS_IREM_M85_GAMES_DIR
#define MNEMOS_IREM_M85_GAMES_DIR ""
#endif

namespace {

    namespace m81 = mnemos::manifests::irem_m81;
    namespace m85 = mnemos::manifests::irem_m85;

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

    [[nodiscard]] const rom_set_file* find_file_at(const rom_set_region& region,
                                                   std::size_t offset) noexcept {
        const auto it =
            std::find_if(region.files.begin(), region.files.end(),
                         [offset](const rom_set_file& file) { return file.offset == offset; });
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
        REQUIRE(maincpu.size == m85::main_rom_size);
        const rom_set_file* reload_lo = find_file_at(maincpu, 0xC0000U);
        const rom_set_file* reload_hi = find_file_at(maincpu, 0xC0001U);
        REQUIRE(reload_lo != nullptr);
        REQUIRE(reload_hi != nullptr);
        CHECK(reload_lo->stride == 2U);
        CHECK(reload_hi->stride == 2U);
        CHECK(reload_lo->size == 0x20000U);
        CHECK(reload_hi->size == 0x20000U);
        CHECK(reload_lo->crc32 == 0x5b07b087U);
        CHECK(reload_hi->crc32 == 0xf6c82f48U);
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

    [[nodiscard]] std::set<std::string, std::less<>> embedded_m85_set_names() {
        std::set<std::string, std::less<>> names;
        for (const auto& [set_name, _] : m85::embedded::game_manifests) {
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

    [[nodiscard]] bool preferred_zip_source(const std::filesystem::path& candidate,
                                            const std::filesystem::path& existing) {
        std::error_code ec;
        const bool candidate_zip = std::filesystem::is_regular_file(candidate, ec) &&
                                   ends_with_zip(candidate.filename().string());
        ec.clear();
        const bool existing_zip = std::filesystem::is_regular_file(existing, ec) &&
                                  ends_with_zip(existing.filename().string());
        return candidate_zip && !existing_zip;
    }

    [[nodiscard]] std::map<std::string, std::filesystem::path, std::less<>>
    index_source_roots(const std::vector<std::filesystem::path>& roots,
                       const std::set<std::string, std::less<>>& known) {
        std::map<std::string, std::filesystem::path, std::less<>> sources;

        auto maybe_add = [&](const std::filesystem::path& path) {
            auto set_id = identify_source(path, known);
            if (!set_id.has_value()) {
                return;
            }
            std::string id = std::move(*set_id);
            auto existing = sources.find(id);
            if (existing == sources.end()) {
                sources.emplace(std::move(id), path);
            } else if (preferred_zip_source(path, existing->second)) {
                existing->second = path;
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

    [[nodiscard]] rom_set_decl require_m85_decl(std::string_view set_name) {
        const std::string_view text = m85::game_manifest_toml(set_name);
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
    make_m85_program(const std::vector<std::uint8_t>& program) {
        std::vector<std::uint8_t> rom(m85::main_rom_size, 0xFFU);
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

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m85_program() {
        return make_m85_program(
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

TEST_CASE("m85 checked-in game manifests parse and cover Pound for Pound", "[m85][romset]") {
    namespace fs = std::filesystem;

    const fs::path games_dir{MNEMOS_IREM_M85_GAMES_DIR};
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

        declarations.emplace(parsed.value->name, std::move(*parsed.value));
    }

    const std::set<std::string, std::less<>> expected_names{"poundfor", "poundforj"};
    std::set<std::string, std::less<>> names;
    for (const auto& [set_name, decl] : declarations) {
        INFO("set=" << set_name);
        names.insert(decl.name);
        CHECK(decl.board == "irem_m85");
        CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::horizontal);

        const rom_set_region* maincpu = find_region(decl, "maincpu");
        REQUIRE(maincpu != nullptr);
        require_region_contract(*maincpu);
        require_boot_chunk_reload(*maincpu);

        const rom_set_file* first_hi = find_file_at(*maincpu, 0x00001U);
        const rom_set_file* first_lo = find_file_at(*maincpu, 0x00000U);
        REQUIRE(first_hi != nullptr);
        REQUIRE(first_lo != nullptr);
        if (set_name == "poundfor") {
            CHECK_FALSE(decl.parent.has_value());
            CHECK(first_hi->name == "pp-a-h0-b.9e");
            CHECK(first_hi->crc32 == 0x50d4a2d8U);
            CHECK(first_lo->name == "pp-a-l0-b.9d");
            CHECK(first_lo->crc32 == 0xbd997942U);
            CHECK(find_region(decl, "soundcpu") != nullptr);
            CHECK(find_region(decl, "samples") != nullptr);
            CHECK(find_region(decl, "tiles") != nullptr);
            CHECK(find_region(decl, "sprites") != nullptr);
            const rom_set_region* plds = find_region(decl, "plds");
            REQUIRE(plds != nullptr);
            require_region_contract(*plds);
            CHECK(plds->size == m85::plds_size);
        } else {
            REQUIRE(decl.parent.has_value());
            CHECK(*decl.parent == "poundfor");
            CHECK(first_hi->name == "pp-a-h0-.9e");
            CHECK(first_hi->crc32 == 0xf0165e3bU);
            CHECK(first_lo->name == "pp-a-l0-.9d");
            CHECK(first_lo->crc32 == 0xf954f99fU);
            CHECK(find_region(decl, "soundcpu") == nullptr);
            CHECK(find_region(decl, "samples") == nullptr);
            CHECK(find_region(decl, "tiles") == nullptr);
            CHECK(find_region(decl, "sprites") == nullptr);
            CHECK(find_region(decl, "plds") == nullptr);
        }
    }

    CHECK(names == expected_names);
    CHECK(m85::board_params_for("poundfor").rom_layout == "m85_program_pair");
    CHECK(m85::board_params_for("poundfor").main_cpu_model == mnemos::chips::cpu::v30::model::v30);
    CHECK(m85::board_params_for("poundforj").rom_layout == "m85_program_pair");
}

TEST_CASE("m85 embedded game manifests mirror the checked-in roster", "[m85][romset]") {
    using mnemos::manifests::irem_m85::embedded::game_manifests;

    CHECK(game_manifests.size() == 2U);
    CHECK_FALSE(m85::game_manifest_toml("poundfor").empty());
    CHECK_FALSE(m85::game_manifest_toml("poundforj").empty());
    CHECK(m85::game_manifest_toml("hharry").empty());
}

TEST_CASE("m85 executable board runs the Pound for Pound-compatible V30/Z80 frame", "[m85]") {
    rom_set_image image;
    image.regions.emplace("maincpu", synthetic_m85_program());
    image.regions.emplace("soundcpu", std::vector<std::uint8_t>(m81::sound_rom_size, 0x00U));
    image.regions.emplace("tiles", std::vector<std::uint8_t>(0x1000U, 0x35U));
    image.regions.emplace("sprites", std::vector<std::uint8_t>(0x1000U, 0xA7U));
    image.regions.emplace("plds", std::vector<std::uint8_t>(m85::plds_size, 0x11U));

    auto system = m85::assemble_m85(std::move(image), m85::board_params_for("poundfor"));
    REQUIRE(system != nullptr);
    CHECK(system->main_cpu.cpu_model() == mnemos::chips::cpu::v30::model::v30);
    system->run_frame();

    CHECK(system->work_ram[0] == 0x42U);
    CHECK(system->video.frame_index() == 1U);
    CHECK(system->fm.elapsed_clocks() >= m85::sound_cycles_per_frame);
    CHECK(frame_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m85 compatibility core exposes the shared KNA91 palette bus", "[m85]") {
    rom_set_image image;
    image.regions.emplace("maincpu", synthetic_m85_program());

    auto system = m85::assemble_m85(std::move(image), m85::board_params_for("poundfor"));
    REQUIRE(system != nullptr);

    system->board.main_bus.write8(m81::palette_ram_base + 0x000U, 0xE7U);
    CHECK(system->palette_ram[0x000U] == 0x07U);
    CHECK(system->board.main_bus.read8(m81::palette_ram_base + 0x000U) == 0xE7U);
    CHECK(system->board.main_bus.read8(m81::palette_ram_base + 0x001U) == 0xFFU);

    system->board.main_bus.write8(m81::palette_ram_base + 0x200U, 0x3FU);
    CHECK(system->palette_ram[0x000U] == 0x1FU);
    CHECK(system->palette_ram[0x200U] == 0x00U);
    CHECK(system->board.main_bus.read8(m81::palette_ram_base + 0x200U) == 0xFFU);

    system->board.main_bus.write16_le(m81::palette_ram_base + 0xA00U, 0x001AU);
    CHECK(system->palette_ram[0x800U] == 0x1AU);
    CHECK(system->palette_ram[0xA00U] == 0x00U);
    CHECK(system->board.main_bus.read16_le(m81::palette_ram_base + 0x800U) == 0xFFFAU);
}

TEST_CASE("m85 save state preserves the M85 board profile", "[m85]") {
    rom_set_image image;
    image.regions.emplace("maincpu", synthetic_m85_program());
    image.regions.emplace("soundcpu", std::vector<std::uint8_t>(m81::sound_rom_size, 0x00U));
    image.regions.emplace("tiles", std::vector<std::uint8_t>(0x1000U, 0x35U));
    image.regions.emplace("sprites", std::vector<std::uint8_t>(0x1000U, 0xA7U));

    auto source = m85::assemble_m85(image, m85::board_params_for("poundfor"));
    REQUIRE(source != nullptr);
    source->run_frame();

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = m85::assemble_m85(std::move(image), m85::board_params_for("poundforj"));
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);
    CHECK(reader.ok());
    CHECK(restored->video.frame_index() == source->video.frame_index());
}

TEST_CASE("m85 local artifacts load CRC-clean with the Pound for Pound parent",
          "[m85][romset][data]") {
    const auto dir_env = environment_value("MNEMOS_M85_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M85_SET_DIR to directories containing the M85 zip/folder corpus");
    }

    const auto roots = source_roots(dir_env->c_str());
    REQUIRE_FALSE(roots.empty());
    for (const auto& root : roots) {
        INFO("root=" << root.string());
        REQUIRE(std::filesystem::exists(root));
    }

    const auto indexed_sources = index_source_roots(roots, embedded_m85_set_names());
    const auto expected_sets = embedded_m85_set_names();
    for (const auto& set_name : expected_sets) {
        INFO("set=" << set_name);
        REQUIRE(indexed_sources.find(set_name) != indexed_sources.end());
    }

    for (const auto& set_name : expected_sets) {
        INFO("set=" << set_name);
        auto effective_decl = require_m85_decl(set_name);
        const auto source_it = indexed_sources.find(set_name);
        REQUIRE(source_it != indexed_sources.end());
        INFO("source=" << source_it->second.string());
        auto provider = require_provider(source_it->second);
        if (effective_decl.parent.has_value()) {
            const auto parent_source = indexed_sources.find(*effective_decl.parent);
            REQUIRE(parent_source != indexed_sources.end());
            INFO("parent=" << parent_source->second.string());
            const auto parent_decl = require_m85_decl(*effective_decl.parent);
            effective_decl = mnemos::manifests::common::inherit_parent_regions(
                parent_decl, std::move(effective_decl));
            provider = mnemos::manifests::common::make_fallback_rom_provider(
                std::move(provider), require_provider(parent_source->second));
        }

        const auto image = mnemos::manifests::common::load_rom_set(effective_decl, provider);
        for (const auto& issue : image.issues) {
            INFO(issue.file << ": " << issue.message);
        }
        CHECK(image.issues.empty());

        require_loaded_region(image, "maincpu", m85::main_rom_size);
        require_loaded_region(image, "soundcpu", m81::sound_rom_size);
        require_loaded_region(image, "samples", 0x040000U);
        require_loaded_region(image, "tiles", 0x080000U);
        require_loaded_region(image, "sprites", 0x100000U);
        require_loaded_region(image, "plds", m85::plds_size);
    }
}
