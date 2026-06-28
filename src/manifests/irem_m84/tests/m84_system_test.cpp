#include "file.hpp"
#include "m81_game_manifests.hpp"
#include "m81_system.hpp"
#include "m84_game_manifests.hpp"
#include "m84_system.hpp"
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

#ifndef MNEMOS_IREM_M84_GAMES_DIR
#define MNEMOS_IREM_M84_GAMES_DIR ""
#endif

namespace {

    using mnemos::manifests::common::rom_set_decl;
    using mnemos::manifests::common::rom_set_dip_switch;
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

    [[nodiscard]] const rom_set_dip_switch*
    find_dip(const rom_set_decl& decl, std::string_view name) noexcept {
        const auto it = std::find_if(decl.dips.begin(), decl.dips.end(),
                                     [name](const rom_set_dip_switch& dip) {
                                         return dip.name == name;
                                     });
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

    void require_boot_chunk_reload(const rom_set_region& maincpu, std::size_t mirror_offset,
                                   std::size_t mirror_file_size) {
        REQUIRE(maincpu.size == mnemos::manifests::irem_m84::main_rom_size);
        const rom_set_file* reload_lo = find_file_at(maincpu, mirror_offset);
        const rom_set_file* reload_hi = find_file_at(maincpu, mirror_offset + 1U);
        REQUIRE(reload_lo != nullptr);
        REQUIRE(reload_hi != nullptr);
        CHECK(reload_lo->stride == 2U);
        CHECK(reload_hi->stride == 2U);
        CHECK(reload_lo->size == mirror_file_size);
        CHECK(reload_hi->size == mirror_file_size);
        CHECK(reload_lo->crc32.has_value());
        CHECK(reload_hi->crc32.has_value());
    }

    void require_explicit_prom_pld_hle(const rom_set_decl& decl) {
        const auto hle =
            std::find_if(decl.hle.begin(), decl.hle.end(), [](const auto& entry) {
                return entry.chip == "irem_m84_prom_pld" &&
                       entry.profile == "irem_m84.ltswords_prom_pld_reference_defaults";
            });
        REQUIRE(hle != decl.hle.end());
        CHECK_FALSE(hle->rationale.empty());
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

    [[nodiscard]] std::set<std::string, std::less<>> embedded_m84_set_names() {
        std::set<std::string, std::less<>> names;
        for (const auto& [set_name, _] : mnemos::manifests::irem_m84::embedded::game_manifests) {
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

    [[nodiscard]] bool preferred_m84_source(const std::filesystem::path& candidate,
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
            } else if (preferred_m84_source(path, existing->second)) {
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

    [[nodiscard]] rom_set_decl require_m84_decl(std::string_view set_name) {
        const std::string_view text = mnemos::manifests::irem_m84::game_manifest_toml(set_name);
        REQUIRE_FALSE(text.empty());
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, std::string{set_name});
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": " << error.message);
        }
        REQUIRE(parsed.ok());
        return std::move(*parsed.value);
    }

    [[nodiscard]] rom_set_decl require_m81_parent_decl(std::string_view set_name) {
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
    make_m84_program(const std::vector<std::uint8_t>& program) {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m84::main_rom_size, 0xFFU);
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

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m84_program() {
        return make_m84_program(
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

TEST_CASE("m84 checked-in game manifests parse and cover local candidate corpus",
          "[m84][romset]") {
    namespace fs = std::filesystem;

    const fs::path games_dir{MNEMOS_IREM_M84_GAMES_DIR};
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

    const std::set<std::string, std::less<>> expected_names{"cosmccop", "gallop", "hharryb",
                                                            "hharryu", "ltswords"};
    std::set<std::string, std::less<>> names;
    for (const auto& [set_name, decl] : declarations) {
        INFO("set=" << set_name);
        names.insert(decl.name);
        CHECK(decl.board == "irem_m84");
        CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::horizontal);

        const rom_set_region* maincpu = find_region(decl, "maincpu");
        REQUIRE(maincpu != nullptr);
        require_region_contract(*maincpu);
        if (set_name == "hharryb") {
            REQUIRE(decl.parent.has_value());
            CHECK(*decl.parent == "hharry");
            require_boot_chunk_reload(*maincpu, 0xE0000U, 0x10000U);
            const rom_set_region* plds = find_region(decl, "plds");
            REQUIRE(plds != nullptr);
            require_region_contract(*plds);
            CHECK(plds->size == mnemos::manifests::irem_m84::hharryb_plds_size);
        } else if (set_name == "hharryu") {
            REQUIRE(decl.parent.has_value());
            CHECK(*decl.parent == "hharry");
            require_boot_chunk_reload(*maincpu, 0xE0000U, 0x10000U);
            const rom_set_region* plds = find_region(decl, "plds");
            REQUIRE(plds != nullptr);
            require_region_contract(*plds);
            CHECK(plds->size == mnemos::manifests::irem_m84::hharryu_plds_size);
        } else if (set_name == "ltswords") {
            CHECK_FALSE(decl.parent.has_value());
            require_boot_chunk_reload(*maincpu, 0xC0000U, 0x20000U);
            CHECK(find_region(decl, "soundcpu") != nullptr);
            CHECK(find_region(decl, "tiles") != nullptr);
            CHECK(find_region(decl, "sprites") != nullptr);
            CHECK(find_region(decl, "samples") != nullptr);
            CHECK(find_region(decl, "plds") == nullptr);
            require_explicit_prom_pld_hle(decl);
        } else if (set_name == "gallop") {
            CHECK_FALSE(decl.parent.has_value());
            require_boot_chunk_reload(*maincpu, 0x80000U, 0x40000U);
            CHECK(decl.dips.size() == 10U);
            const rom_set_dip_switch* lives = find_dip(decl, "Lives");
            REQUIRE(lives != nullptr);
            CHECK(lives->mask == 0x0003U);
            CHECK(lives->default_value == 0x0003U);
            const rom_set_dip_switch* coin_mode = find_dip(decl, "Coin Mode");
            REQUIRE(coin_mode != nullptr);
            CHECK(coin_mode->mask == 0x0800U);
            CHECK(coin_mode->default_value == 0x0800U);
            CHECK(raw_dip_default(decl, 0xFFFFU) == 0xF9BFU);
            CHECK(find_region(decl, "soundcpu") != nullptr);
            CHECK(find_region(decl, "tiles") != nullptr);
            CHECK(find_region(decl, "sprites") != nullptr);
            CHECK(find_region(decl, "samples") != nullptr);
            const rom_set_region* proms = find_region(decl, "proms");
            REQUIRE(proms != nullptr);
            require_region_contract(*proms);
            CHECK(proms->size == 0x0200U);
            const rom_set_region* plds = find_region(decl, "plds");
            REQUIRE(plds != nullptr);
            require_region_contract(*plds);
            CHECK(plds->size == 0x0800U);
        } else if (set_name == "cosmccop") {
            REQUIRE(decl.parent.has_value());
            CHECK(*decl.parent == "gallop");
            require_boot_chunk_reload(*maincpu, 0x80000U, 0x40000U);
            CHECK(find_region(decl, "soundcpu") != nullptr);
            CHECK(find_region(decl, "tiles") != nullptr);
            CHECK(find_region(decl, "sprites") != nullptr);
            CHECK(find_region(decl, "samples") != nullptr);
            CHECK(find_region(decl, "proms") == nullptr);
            CHECK(find_region(decl, "plds") == nullptr);
        }
    }

    CHECK(names == expected_names);
    CHECK(mnemos::manifests::irem_m84::board_params_for("cosmccop").rom_layout ==
          "v35_program_pair");
    CHECK(mnemos::manifests::irem_m84::board_params_for("cosmccop").main_cpu_model ==
          mnemos::chips::cpu::v30::model::v35);
    CHECK(mnemos::manifests::irem_m84::board_params_for("hharryb").rom_layout ==
          "bootleg_program_pair");
    CHECK(mnemos::manifests::irem_m84::board_params_for("hharryu").rom_layout ==
          "us_program_pair");
    CHECK(mnemos::manifests::irem_m84::board_params_for("ltswords").rom_layout ==
          "v35_program_pair");
    CHECK(mnemos::manifests::irem_m84::board_params_for("ltswords").main_cpu_model ==
          mnemos::chips::cpu::v30::model::v35);
    CHECK(mnemos::manifests::irem_m84::board_params_for("gallop").rom_layout ==
          "v35_program_pair");
    CHECK(mnemos::manifests::irem_m84::board_params_for("gallop").main_cpu_model ==
          mnemos::chips::cpu::v30::model::v35);
}

TEST_CASE("m84 embedded game manifests mirror the checked-in roster", "[m84][romset]") {
    using mnemos::manifests::irem_m84::embedded::game_manifests;

    CHECK(game_manifests.size() == 5U);
    CHECK_FALSE(mnemos::manifests::irem_m84::game_manifest_toml("cosmccop").empty());
    CHECK_FALSE(mnemos::manifests::irem_m84::game_manifest_toml("gallop").empty());
    CHECK_FALSE(mnemos::manifests::irem_m84::game_manifest_toml("hharryb").empty());
    CHECK_FALSE(mnemos::manifests::irem_m84::game_manifest_toml("hharryu").empty());
    CHECK_FALSE(mnemos::manifests::irem_m84::game_manifest_toml("ltswords").empty());
    CHECK(mnemos::manifests::irem_m84::game_manifest_toml("hharry").empty());
}

TEST_CASE("m84 executable board runs the Hammerin' Harry-compatible V30/Z80 frame",
          "[m84]") {
    namespace m81 = mnemos::manifests::irem_m81;
    namespace m84 = mnemos::manifests::irem_m84;

    rom_set_image image;
    image.regions.emplace("maincpu", synthetic_m84_program());
    image.regions.emplace("soundcpu", std::vector<std::uint8_t>(m81::sound_rom_size, 0x00U));
    image.regions.emplace("tiles", std::vector<std::uint8_t>(0x1000U, 0x35U));
    image.regions.emplace("sprites", std::vector<std::uint8_t>(0x1000U, 0xA7U));
    image.regions.emplace("proms", std::vector<std::uint8_t>(0x100U, 0x11U));

    auto system = m84::assemble_m84(std::move(image), m84::board_params_for("hharryb"));
    REQUIRE(system != nullptr);
    CHECK(system->main_cpu.cpu_model() == mnemos::chips::cpu::v30::model::v30);
    system->run_frame();

    CHECK(system->work_ram[0] == 0x42U);
    CHECK(system->video.frame_index() == 1U);
    CHECK(system->fm.elapsed_clocks() >= m84::sound_cycles_per_frame);
    CHECK(frame_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m84 V35 profile selects the Lightning Swords CPU model", "[m84]") {
    namespace m81 = mnemos::manifests::irem_m81;
    namespace m84 = mnemos::manifests::irem_m84;

    rom_set_image image;
    image.regions.emplace("maincpu", synthetic_m84_program());
    image.regions.emplace("soundcpu", std::vector<std::uint8_t>(m81::sound_rom_size, 0x00U));
    image.regions.emplace("tiles", std::vector<std::uint8_t>(0x1000U, 0x35U));
    image.regions.emplace("sprites", std::vector<std::uint8_t>(0x1000U, 0xA7U));

    auto system = m84::assemble_m84(std::move(image), m84::board_params_for("ltswords"));
    REQUIRE(system != nullptr);
    CHECK(system->main_cpu.cpu_model() == mnemos::chips::cpu::v30::model::v35);
    system->run_frame();
    CHECK(system->video.frame_index() == 1U);
    CHECK(frame_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m84 compatibility core exposes the shared KNA91 palette bus", "[m84]") {
    namespace m81 = mnemos::manifests::irem_m81;
    namespace m84 = mnemos::manifests::irem_m84;

    rom_set_image image;
    image.regions.emplace("maincpu", synthetic_m84_program());

    auto system = m84::assemble_m84(std::move(image), m84::board_params_for("hharryb"));
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

TEST_CASE("m84 save state rejects a different M84 program-layout profile", "[m84]") {
    namespace m81 = mnemos::manifests::irem_m81;
    namespace m84 = mnemos::manifests::irem_m84;

    rom_set_image image;
    image.regions.emplace("maincpu", synthetic_m84_program());
    image.regions.emplace("soundcpu", std::vector<std::uint8_t>(m81::sound_rom_size, 0x00U));
    image.regions.emplace("tiles", std::vector<std::uint8_t>(0x1000U, 0x35U));
    image.regions.emplace("sprites", std::vector<std::uint8_t>(0x1000U, 0xA7U));
    image.regions.emplace("proms", std::vector<std::uint8_t>(0x100U, 0x11U));

    auto source = m84::assemble_m84(image, m84::board_params_for("hharryb"));
    REQUIRE(source != nullptr);
    source->run_frame();

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto same_layout = m84::assemble_m84(image, m84::board_params_for("hharryb"));
    REQUIRE(same_layout != nullptr);
    mnemos::chips::state_reader same_reader(snapshot);
    same_layout->load_state(same_reader);
    CHECK(same_reader.ok());

    auto different_layout = m84::assemble_m84(image, m84::board_params_for("hharryu"));
    REQUIRE(different_layout != nullptr);
    mnemos::chips::state_reader different_reader(snapshot);
    different_layout->load_state(different_reader);
    CHECK_FALSE(different_reader.ok());

    auto different_cpu = m84::assemble_m84(std::move(image), m84::board_params_for("ltswords"));
    REQUIRE(different_cpu != nullptr);
    mnemos::chips::state_reader cpu_reader(snapshot);
    different_cpu->load_state(cpu_reader);
    CHECK_FALSE(cpu_reader.ok());
}

TEST_CASE("m84 local split artifacts load CRC-clean with the M81 hharry parent",
          "[m84][romset][data]") {
    namespace m81 = mnemos::manifests::irem_m81;
    namespace m84 = mnemos::manifests::irem_m84;

    const auto dir_env = environment_value("MNEMOS_M84_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M84_SET_DIR to M84 roots plus the M81 hharry parent root");
    }

    const auto roots = source_roots(dir_env->c_str());
    REQUIRE_FALSE(roots.empty());
    for (const auto& root : roots) {
        INFO("root=" << root.string());
        REQUIRE(std::filesystem::exists(root));
    }

    const auto indexed_sources = index_source_roots(roots, embedded_m84_set_names());
    const std::set<std::string, std::less<>> m81_parent_names{"hharry"};
    const auto indexed_m81_parent_sources = index_source_roots(roots, m81_parent_names);
    REQUIRE_FALSE(m81::game_manifest_toml("hharry").empty());

    const auto expected_sets = embedded_m84_set_names();
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
        FAIL("missing M84 artifacts: " << missing.str());
    }

    for (const auto& set_name : expected_sets) {
        INFO("set=" << set_name);
        auto raw_decl = require_m84_decl(set_name);
        const auto source_it = indexed_sources.find(set_name);
        REQUIRE(source_it != indexed_sources.end());
        INFO("source=" << source_it->second.string());
        auto effective_decl = std::move(raw_decl);
        auto provider = require_provider(source_it->second);
        if (effective_decl.parent.has_value()) {
            const bool m81_parent = *effective_decl.parent == "hharry";
            const auto parent_source = m81_parent
                                           ? indexed_m81_parent_sources.find(*effective_decl.parent)
                                           : indexed_sources.find(*effective_decl.parent);
            if (m81_parent) {
                REQUIRE(parent_source != indexed_m81_parent_sources.end());
            } else {
                REQUIRE(parent_source != indexed_sources.end());
            }
            INFO("parent=" << parent_source->second.string());
            const auto parent_decl = m81_parent ? require_m81_parent_decl(*effective_decl.parent)
                                                : require_m84_decl(*effective_decl.parent);
            effective_decl = mnemos::manifests::common::inherit_parent_regions(parent_decl,
                                                                               std::move(effective_decl));
            provider = mnemos::manifests::common::make_fallback_rom_provider(
                std::move(provider), require_provider(parent_source->second));
        } else if (set_name == "ltswords") {
            require_explicit_prom_pld_hle(effective_decl);
        }

        const auto image = mnemos::manifests::common::load_rom_set(effective_decl, provider);
        for (const auto& issue : image.issues) {
            INFO(issue.file << ": " << issue.message);
        }
        CHECK(image.issues.empty());

        require_loaded_region(image, "maincpu", m84::main_rom_size);
        require_loaded_region(image, "soundcpu", m81::sound_rom_size);
        require_loaded_region(image, "samples", m81::sample_rom_size);
        if (const auto* plds = find_region(effective_decl, "plds")) {
            require_loaded_region(image, "plds", plds->size);
        } else {
            CHECK(set_name == "ltswords");
        }
        const auto* tiles = find_region(effective_decl, "tiles");
        const auto* sprites = find_region(effective_decl, "sprites");
        const auto* proms = find_region(effective_decl, "proms");
        REQUIRE(tiles != nullptr);
        REQUIRE(sprites != nullptr);
        require_loaded_region(image, "tiles", tiles->size);
        require_loaded_region(image, "sprites", sprites->size);
        if (proms != nullptr) {
            require_loaded_region(image, "proms", proms->size);
        } else {
            CHECK(set_name == "ltswords");
            require_explicit_prom_pld_hle(effective_decl);
        }
    }
}
