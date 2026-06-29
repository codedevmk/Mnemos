#include "file.hpp"
#include "m82_game_manifests.hpp"
#include "m82_system.hpp"
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

#ifndef MNEMOS_IREM_M82_GAMES_DIR
#define MNEMOS_IREM_M82_GAMES_DIR ""
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

    void require_reset_vector_reload(const rom_set_region& maincpu) {
        REQUIRE(maincpu.size == mnemos::manifests::irem_m82::main_rom_size);
        const rom_set_file* reload_lo = find_file_at(maincpu, 0xC0000U);
        const rom_set_file* reload_hi = find_file_at(maincpu, 0xC0001U);
        std::uint32_t expected_size = 0x20000U;
        if (reload_lo == nullptr || reload_hi == nullptr) {
            reload_lo = find_file_at(maincpu, 0xE0000U);
            reload_hi = find_file_at(maincpu, 0xE0001U);
            expected_size = 0x10000U;
        }
        REQUIRE(reload_lo != nullptr);
        REQUIRE(reload_hi != nullptr);
        CHECK(reload_lo->stride == 2U);
        CHECK(reload_hi->stride == 2U);
        CHECK(reload_lo->size == expected_size);
        CHECK(reload_hi->size == expected_size);
        CHECK(reload_lo->crc32.has_value());
        CHECK(reload_hi->crc32.has_value());
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
        for (const auto& [set_name, _] : mnemos::manifests::irem_m82::embedded::game_manifests) {
            names.emplace(std::string{set_name});
        }
        return names;
    }

    struct identified_m82_source final {
        std::string set_id;
        std::uint8_t rank{};
    };

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

    [[nodiscard]] std::vector<std::filesystem::path> m82_source_roots(const char* env_value) {
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

    [[nodiscard]] std::optional<identified_m82_source>
    identify_m82_source(const std::filesystem::path& path,
                        const std::set<std::string, std::less<>>& known) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            const std::string set_id = path.filename().string();
            if (!known.contains(set_id)) {
                return std::nullopt;
            }
            return identified_m82_source{.set_id = set_id,
                                         .rank = set_id == "dkgensanm82" ? 0U : 2U};
        }
        if (!std::filesystem::is_regular_file(path, ec) ||
            !ends_with_zip(path.filename().string())) {
            return std::nullopt;
        }

        const std::string stem = path.stem().string();
        if (known.contains(stem)) {
            return identified_m82_source{.set_id = stem, .rank = 1U};
        }

        auto bytes = mnemos::io::read_file(path.string());
        if (!bytes.has_value()) {
            return std::nullopt;
        }
        auto nested = single_nested_zip_set_id(*bytes);
        if (!nested.has_value() || !known.contains(*nested)) {
            return std::nullopt;
        }
        return identified_m82_source{.set_id = std::move(*nested), .rank = 0U};
    }

    [[nodiscard]] std::map<std::string, std::filesystem::path, std::less<>>
    index_m82_source_roots(const std::vector<std::filesystem::path>& roots) {
        const auto known = embedded_set_names();
        std::map<std::string, std::filesystem::path, std::less<>> sources;
        std::map<std::string, std::uint8_t, std::less<>> ranks;

        auto maybe_add = [&](const std::filesystem::path& path) {
            auto identified = identify_m82_source(path, known);
            if (!identified.has_value()) {
                return;
            }
            const auto rank_it = ranks.find(identified->set_id);
            if (rank_it == ranks.end() || identified->rank < rank_it->second) {
                ranks[identified->set_id] = identified->rank;
                sources[identified->set_id] = path;
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
        const std::string_view text = mnemos::manifests::irem_m82::game_manifest_toml(set_name);
        REQUIRE_FALSE(text.empty());
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, std::string{set_name});
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": " << error.message);
        }
        REQUIRE(parsed.ok());
        return std::move(*parsed.value);
    }

    [[nodiscard]] mnemos::manifests::common::rom_file_provider
    require_m82_provider(const std::filesystem::path& path) {
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
    make_m82_program(const std::vector<std::uint8_t>& program) {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m82::main_rom_size, 0xFFU);
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

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m82_program() {
        return make_m82_program(
            {0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U, 0x42U, 0xA2U, 0x00U, 0x00U, 0xF4U});
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m82_sound_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m82::sound_rom_size, 0x00U);
        rom[0x0000U] = 0x3EU; // LD A,$F0
        rom[0x0001U] = 0xF0U;
        rom[0x0002U] = 0xD3U; // OUT ($82),A
        rom[0x0003U] =
            static_cast<std::uint8_t>(mnemos::manifests::irem_m82::z80_port_dac);
        rom[0x0004U] = 0x76U; // HALT
        return rom;
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

    [[nodiscard]] std::vector<std::uint8_t> make_tiles(const std::vector<std::uint8_t>& values) {
        const std::size_t plane_size = values.size() * 8U;
        std::vector<std::uint8_t> tiles(plane_size * 4U, 0U);
        for (std::size_t tile = 0; tile < values.size(); ++tile) {
            for (std::uint32_t plane = 0; plane < 4U; ++plane) {
                if (((values[tile] >> plane) & 1U) != 0U) {
                    for (std::size_t row = 0; row < 8U; ++row) {
                        tiles[plane * plane_size + tile * 8U + row] = 0xFFU;
                    }
                }
            }
        }
        return tiles;
    }

    [[nodiscard]] std::vector<std::uint8_t> make_sprites(const std::vector<std::uint8_t>& values) {
        const std::size_t plane_size = values.size() * 32U;
        std::vector<std::uint8_t> sprites(plane_size * 4U, 0U);
        for (std::size_t cell = 0; cell < values.size(); ++cell) {
            for (std::uint32_t plane = 0; plane < 4U; ++plane) {
                if (((values[cell] >> plane) & 1U) != 0U) {
                    for (std::size_t byte = 0; byte < 32U; ++byte) {
                        sprites[plane * plane_size + cell * 32U + byte] = 0xFFU;
                    }
                }
            }
        }
        return sprites;
    }

    [[nodiscard]] std::vector<std::uint8_t> make_palette(std::size_t index, std::uint8_t r5,
                                                         std::uint8_t g5, std::uint8_t b5) {
        std::vector<std::uint8_t> palette(mnemos::manifests::irem_m82::palette_ram_size, 0U);
        palette[index * 2U] = r5;
        palette[index * 2U + 0x400U] = g5;
        palette[index * 2U + 0x800U] = b5;
        return palette;
    }

    void set_sprite(std::vector<std::uint8_t>& ram, std::int32_t vis_x, std::int32_t vis_y,
                    std::uint16_t code, std::uint8_t color) {
        const auto y9 = static_cast<std::uint16_t>((384 - vis_y - 16) & 0x1FFU);
        const auto x10 = static_cast<std::uint16_t>((vis_x + 320) & 0x3FFU);
        ram[0] = static_cast<std::uint8_t>(y9);
        ram[1] = static_cast<std::uint8_t>(y9 >> 8U);
        ram[2] = static_cast<std::uint8_t>(code);
        ram[3] = static_cast<std::uint8_t>(code >> 8U);
        ram[4] = color;
        ram[5] = 0U;
        ram[6] = static_cast<std::uint8_t>(x10);
        ram[7] = static_cast<std::uint8_t>(x10 >> 8U);
    }

} // namespace

TEST_CASE("m82 checked-in game manifests parse and cover local M82 corpus", "[m82][romset]") {
    namespace fs = std::filesystem;

    const fs::path games_dir{MNEMOS_IREM_M82_GAMES_DIR};
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
        std::string set_name = decl.name;
        declarations.emplace(std::move(set_name), std::move(*parsed.value));
    }

    const std::set<std::string, std::less<>> expected_names{
        "airduel", "airduelu", "dkgensanm82", "majtitle", "majtitlej",
        "rtype2",  "rtype2j",  "rtype2jc",  "rtype2m82b"};
    std::set<std::string, std::less<>> names;
    for (const auto& [set_name, raw_decl] : declarations) {
        INFO("set=" << set_name);
        rom_set_decl decl = raw_decl;
        if (decl.parent.has_value()) {
            const auto parent_it = declarations.find(*decl.parent);
            REQUIRE(parent_it != declarations.end());
            decl = mnemos::manifests::common::inherit_parent_regions(parent_it->second,
                                                                     std::move(decl));
        }
        names.insert(decl.name);
        CHECK(decl.board == "irem_m82");

        const rom_set_region* maincpu = find_region(decl, "maincpu");
        REQUIRE(maincpu != nullptr);
        require_region_contract(*maincpu);
        require_reset_vector_reload(*maincpu);

        REQUIRE(find_region(decl, "soundcpu") != nullptr);
        CHECK(find_region(decl, "soundcpu")->size == mnemos::manifests::irem_m82::sound_rom_size);
        require_region_contract(*find_region(decl, "soundcpu"));

        REQUIRE(find_region(decl, "samples") != nullptr);
        CHECK(find_region(decl, "samples")->size == mnemos::manifests::irem_m82::sample_rom_size);
        require_region_contract(*find_region(decl, "samples"));

        REQUIRE(find_region(decl, "tiles") != nullptr);
        REQUIRE(find_region(decl, "sprites") != nullptr);
        REQUIRE(find_region(decl, "proms") != nullptr);
        require_region_contract(*find_region(decl, "tiles"));
        require_region_contract(*find_region(decl, "sprites"));
        require_region_contract(*find_region(decl, "proms"));
        if (const rom_set_region* backgrounds = find_region(decl, "backgrounds")) {
            require_region_contract(*backgrounds);
        }
    }

    CHECK(names == expected_names);
    CHECK(declarations.at("airduel").orientation ==
          mnemos::manifests::common::screen_orientation::vertical);
    REQUIRE(declarations.at("airduelu").parent.has_value());
    CHECK(*declarations.at("airduelu").parent == "airduel");
    CHECK(declarations.at("airduelu").orientation ==
          mnemos::manifests::common::screen_orientation::vertical);
    REQUIRE(declarations.at("majtitlej").parent.has_value());
    CHECK(*declarations.at("majtitlej").parent == "majtitle");
    REQUIRE(declarations.at("rtype2j").parent.has_value());
    CHECK(*declarations.at("rtype2j").parent == "rtype2");
    REQUIRE(declarations.at("rtype2jc").parent.has_value());
    CHECK(*declarations.at("rtype2jc").parent == "rtype2");
    REQUIRE(declarations.at("rtype2m82b").parent.has_value());
    CHECK(*declarations.at("rtype2m82b").parent == "rtype2");

    CHECK_FALSE(mnemos::manifests::irem_m82::board_params_for("rtype2").bootleg_layout);
    CHECK_FALSE(mnemos::manifests::irem_m82::board_params_for("majtitle").bootleg_layout);
    CHECK(mnemos::manifests::irem_m82::board_params_for("rtype2m82b").bootleg_layout);
}

TEST_CASE("m82 embedded game manifests mirror the checked-in roster", "[m82][romset]") {
    using mnemos::manifests::irem_m82::embedded::game_manifests;

    CHECK(game_manifests.size() == 9U);
    CHECK_FALSE(mnemos::manifests::irem_m82::game_manifest_toml("airduel").empty());
    CHECK_FALSE(mnemos::manifests::irem_m82::game_manifest_toml("airduelu").empty());
    CHECK_FALSE(mnemos::manifests::irem_m82::game_manifest_toml("dkgensanm82").empty());
    CHECK_FALSE(mnemos::manifests::irem_m82::game_manifest_toml("majtitle").empty());
    CHECK_FALSE(mnemos::manifests::irem_m82::game_manifest_toml("majtitlej").empty());
    CHECK_FALSE(mnemos::manifests::irem_m82::game_manifest_toml("rtype2").empty());
    CHECK_FALSE(mnemos::manifests::irem_m82::game_manifest_toml("rtype2j").empty());
    CHECK_FALSE(mnemos::manifests::irem_m82::game_manifest_toml("rtype2jc").empty());
    CHECK_FALSE(mnemos::manifests::irem_m82::game_manifest_toml("rtype2m82b").empty());
    CHECK(mnemos::manifests::irem_m82::game_manifest_toml("rtype").empty());
}

TEST_CASE("m82 executable board runs a V30/Z80/YM frame", "[m82]") {
    namespace m82 = mnemos::manifests::irem_m82;

    rom_set_image image;
    image.regions.emplace("maincpu", synthetic_m82_program());
    image.regions.emplace("soundcpu", synthetic_m82_sound_program());
    image.regions.emplace("tiles", std::vector<std::uint8_t>(0x1000U, 0x35U));
    image.regions.emplace("sprites", std::vector<std::uint8_t>(0x1000U, 0xA7U));
    image.regions.emplace("proms", std::vector<std::uint8_t>(0x100U, 0x11U));

    auto system = m82::assemble_m82(std::move(image), m82::board_params_for("rtype2"));
    REQUIRE(system != nullptr);
    system->run_frame();

    CHECK(system->work_ram[0] == 0x42U);
    CHECK(system->video.frame_index() == 1U);
    CHECK(system->fm.elapsed_clocks() >= m82::sound_cycles_per_frame);
    REQUIRE(system->dac_write_events.size() == 1U);
    CHECK(system->dac_write_events[0].sound_clock > 0U);
    CHECK(system->dac_write_events[0].sound_clock < m82::sound_cycles_per_frame);
    CHECK(system->dac.level() == 0xF0U);
    CHECK(frame_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m82 raster compare interrupts the V30 through the 8259", "[m82]") {
    namespace m82 = mnemos::manifests::irem_m82;

    rom_set_image image;
    image.regions.emplace("maincpu", make_m82_program({
                                         0xB0U, 0x13U, // MOV AL,13    (ICW1: edge, single, ICW4)
                                         0xE6U, 0x40U, // OUT 40,AL
                                         0xB0U, 0x20U, // MOV AL,20    (ICW2: vector base)
                                         0xE6U, 0x42U, // OUT 42,AL
                                         0xB0U, 0x01U, // MOV AL,01    (ICW4: 8086 mode)
                                         0xE6U, 0x42U, // OUT 42,AL
                                         0xB0U, 0xFBU, // MOV AL,FB    (OCW1: only IR2 unmasked)
                                         0xE6U, 0x42U, // OUT 42,AL
                                         0xB0U, 0x50U, // MOV AL,50    (line = 0x150 - 128 = 208)
                                         0xE6U, 0x06U, // OUT 06,AL
                                         0xB0U, 0x01U, // MOV AL,01
                                         0xE6U, 0x07U, // OUT 07,AL
                                         0xB8U, 0x00U, 0xA0U, // MOV AX,A000
                                         0x8EU, 0xD0U,        // MOV SS,AX
                                         0xBCU, 0x00U, 0x10U, // MOV SP,1000
                                         0xFBU,               // STI
                                         0xF4U                // HLT
                                     }));
    auto& main = image.regions["maincpu"];
    // IVT[0x22] -> 0040:0008 (physical 0x408).
    main[0x88U] = 0x08U;
    main[0x89U] = 0x00U;
    main[0x8AU] = 0x40U;
    main[0x8BU] = 0x00U;
    const std::vector<std::uint8_t> handler{0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U,
                                            0x82U, 0xA2U, 0x22U, 0x00U, 0xF4U};
    for (std::size_t i = 0; i < handler.size(); ++i) {
        main[0x408U + i] = handler[i];
    }
    image.regions.emplace("soundcpu", std::vector<std::uint8_t>(m82::sound_rom_size, 0x00U));

    auto system = m82::assemble_m82(std::move(image), m82::board_params_for("rtype2"));
    REQUIRE(system != nullptr);
    system->run_frame();

    CHECK(system->work_ram[0x22U] == 0x82U);
    CHECK(system->pic.vector_base() == 0x20U);
    CHECK(system->pic.isr() == 0x04U);
}

TEST_CASE("m82 composes visible scanlines before raster-time palette writes", "[m82]") {
    namespace m82 = mnemos::manifests::irem_m82;

    rom_set_image image;
    image.regions.emplace("maincpu", make_m82_program({
                                         0xB0U, 0x13U,        // MOV AL,13    (ICW1)
                                         0xE6U, 0x40U,        // OUT 40,AL
                                         0xB0U, 0x20U,        // MOV AL,20    (ICW2)
                                         0xE6U, 0x42U,        // OUT 42,AL
                                         0xB0U, 0x01U,        // MOV AL,01    (ICW4)
                                         0xE6U, 0x42U,        // OUT 42,AL
                                         0xB0U, 0xFBU,        // MOV AL,FB    (only IR2 unmasked)
                                         0xE6U, 0x42U,        // OUT 42,AL
                                         0xB0U, 0x50U,        // MOV AL,50    (line 208)
                                         0xE6U, 0x06U,        // OUT 06,AL
                                         0xB0U, 0x01U,        // MOV AL,01
                                         0xE6U, 0x07U,        // OUT 07,AL
                                         0xB8U, 0x00U, 0xA0U, // MOV AX,A000
                                         0x8EU, 0xD0U,        // MOV SS,AX
                                         0xBCU, 0x00U, 0x10U, // MOV SP,1000
                                         0xFBU,               // STI
                                         0xF4U                // HLT
                                     }));
    auto& main = image.regions["maincpu"];
    main[0x88U] = 0x08U;
    main[0x89U] = 0x00U;
    main[0x8AU] = 0x40U;
    main[0x8BU] = 0x00U;
    const std::vector<std::uint8_t> handler{
        0xB8U, 0x00U, 0xC8U, // MOV AX,C800
        0x8EU, 0xD8U,        // MOV DS,AX
        0xB0U, 0x00U,        // MOV AL,00
        0xA2U, 0x2AU, 0x00U, // MOV [002A],AL
        0xB0U, 0x1FU,        // MOV AL,1F
        0xA2U, 0x2AU, 0x04U, // MOV [042A],AL
        0xF4U                // HLT
    };
    for (std::size_t i = 0; i < handler.size(); ++i) {
        main[0x408U + i] = handler[i];
    }
    image.regions.emplace("soundcpu", std::vector<std::uint8_t>(m82::sound_rom_size, 0x00U));
    image.regions.emplace("tiles", make_tiles({5U}));

    auto system = m82::assemble_m82(std::move(image), m82::board_params_for("rtype2"));
    REQUIRE(system != nullptr);
    system->video.set_scroll(512U - 64U, 0U);
    for (std::size_t row = 0; row < 64U; ++row) {
        system->vram[row * 64U * 4U + 2U] = 1U;
    }
    system->palette_ram[0x2AU] = 0x1FU;

    system->run_frame();

    const auto frame = system->video.framebuffer();
    CHECK(frame.pixels[207U * frame.effective_stride()] == 0x00FF0000U);
    CHECK(frame.pixels[209U * frame.effective_stride()] == 0x0000FF00U);
    CHECK(system->palette_ram[0x2AU] == 0x00U);
    CHECK(system->palette_ram[0x42AU] == 0x1FU);
    CHECK(system->pic.isr() == 0x04U);
}

TEST_CASE("m82 palette bus mirrors KNA91 low-byte access", "[m82]") {
    namespace m82 = mnemos::manifests::irem_m82;

    rom_set_image image;
    image.regions.emplace("maincpu", synthetic_m82_program());

    auto system = m82::assemble_m82(std::move(image), m82::board_params_for("rtype2"));
    REQUIRE(system != nullptr);

    system->main_bus.write8(m82::palette_ram_base + 0x000U, 0xE7U);
    CHECK(system->palette_ram[0x000U] == 0x07U);
    CHECK(system->main_bus.read8(m82::palette_ram_base + 0x000U) == 0xE7U);
    CHECK(system->main_bus.read8(m82::palette_ram_base + 0x001U) == 0xFFU);

    system->main_bus.write8(m82::palette_ram_base + 0x200U, 0x3FU);
    CHECK(system->palette_ram[0x000U] == 0x1FU);
    CHECK(system->palette_ram[0x200U] == 0x00U);
    CHECK(system->main_bus.read8(m82::palette_ram_base + 0x200U) == 0xFFU);

    system->main_bus.write8(m82::palette_ram_base + 0x201U, 0x00U);
    CHECK(system->palette_ram[0x001U] == 0x00U);
    CHECK(system->main_bus.read8(m82::palette_ram_base + 0x201U) == 0xFFU);

    system->main_bus.write8(m82::palette_ram_base + 0x600U, 0x12U);
    CHECK(system->palette_ram[0x400U] == 0x12U);
    CHECK(system->palette_ram[0x600U] == 0x00U);
    CHECK(system->main_bus.read8(m82::palette_ram_base + 0x400U) == 0xF2U);

    system->main_bus.write16_le(m82::palette_ram_base + 0xA00U, 0x001AU);
    CHECK(system->palette_ram[0x800U] == 0x1AU);
    CHECK(system->palette_ram[0xA00U] == 0x00U);
    CHECK(system->main_bus.read16_le(m82::palette_ram_base + 0x800U) == 0xFFFAU);
}

TEST_CASE("m82 video renders VRAM-backed tile rows with rowscroll", "[m82][video]") {
    namespace m82 = mnemos::manifests::irem_m82;

    m82::m82_video video;
    const auto tiles = make_tiles({5U, 3U});
    std::vector<std::uint8_t> vram(64U * 64U * 4U, 0U);
    vram[4U] = 1U; // map cell (1,0): tile code 1
    auto palette = make_palette(5U, 0x1FU, 0U, 0U);
    auto green = make_palette(3U, 0U, 0x1FU, 0U);
    for (std::size_t i = 0; i < palette.size(); ++i) {
        palette[i] |= green[i];
    }
    std::vector<std::uint8_t> rowscroll(m82::rowscroll_ram_size, 0U);
    rowscroll[2U] = 8U; // line 1 samples the next tile column

    video.set_scroll(512U - 64U, 0U);
    video.compose(tiles, std::span<const std::uint8_t>{}, std::span<const std::uint8_t>{},
                  std::span<const std::uint8_t>{}, vram, rowscroll, palette, false);

    const auto frame = video.framebuffer();
    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[frame.effective_stride()] == 0x0000FF00U);
}

TEST_CASE("m82 video renders a dedicated background graphics region", "[m82][video]") {
    namespace m82 = mnemos::manifests::irem_m82;

    constexpr std::size_t tilemap_bytes = 64U * 64U * 4U;
    const auto backgrounds = make_tiles({5U});
    std::vector<std::uint8_t> vram(tilemap_bytes * 2U, 0U);
    vram[tilemap_bytes + 1U] = 0x40U;
    const auto palette = make_palette(5U, 0x1FU, 0U, 0U);

    m82::m82_video video;
    video.set_scroll(512U - 64U, 0U);
    video.compose(std::span<const std::uint8_t>{}, backgrounds, std::span<const std::uint8_t>{},
                  std::span<const std::uint8_t>{}, vram, std::span<const std::uint8_t>{}, palette,
                  false);

    CHECK(video.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("m82 video tile priority groups arbitrate front pens against sprites", "[m82][video]") {
    namespace m82 = mnemos::manifests::irem_m82;

    constexpr std::size_t tilemap_bytes = 64U * 64U * 4U;
    const auto tiles = make_tiles({0U, 3U});
    const auto sprites = make_sprites({1U});
    std::vector<std::uint8_t> vram(tilemap_bytes * 2U, 0U);
    std::vector<std::uint8_t> sprite_ram(m82::sprite_ram_size, 0U);
    std::vector<std::uint8_t> palette(m82::palette_ram_size, 0U);
    palette[1U * 2U + 0x800U] = 0x1FU;
    palette[3U * 2U + 0x400U] = 0x1FU;

    vram[0U] = 1U;
    set_sprite(sprite_ram, 0, 0, 0U, 0U);

    m82::m82_video video;
    video.set_scroll(512U - 64U, 0U);
    video.latch_sprites(sprite_ram);

    SECTION("group 0 front pens render below sprites") {
        vram[2U] = 0x00U;
        video.compose(tiles, std::span<const std::uint8_t>{}, sprites,
                      std::span<const std::uint8_t>{}, vram, std::span<const std::uint8_t>{},
                      palette, false);

        CHECK(video.framebuffer().pixels[0] == 0x000000FFU);
    }

    SECTION("group 2 front pens render above sprites") {
        vram[2U] = 0x80U;
        video.compose(tiles, std::span<const std::uint8_t>{}, sprites,
                      std::span<const std::uint8_t>{}, vram, std::span<const std::uint8_t>{},
                      palette, false);

        CHECK(video.framebuffer().pixels[0] == 0x0000FF00U);
    }
}

TEST_CASE("m82 video renders latched sprite RAM and preserves it in state", "[m82][video]") {
    namespace m82 = mnemos::manifests::irem_m82;

    const auto sprites = make_sprites({1U});
    const auto palette = make_palette(1U, 0U, 0U, 0x1FU);
    std::vector<std::uint8_t> sprite_ram(m82::sprite_ram_size, 0U);
    set_sprite(sprite_ram, 0, 0, 0U, 0U);

    m82::m82_video video;
    video.latch_sprites(sprite_ram);
    video.compose(std::span<const std::uint8_t>{}, std::span<const std::uint8_t>{}, sprites,
                  std::span<const std::uint8_t>{}, std::span<const std::uint8_t>{},
                  std::span<const std::uint8_t>{}, palette, false);
    CHECK(video.framebuffer().pixels[0] == 0x000000FFU);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    video.save_state(writer);

    m82::m82_video restored;
    mnemos::chips::state_reader reader(snapshot);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    restored.compose(std::span<const std::uint8_t>{}, std::span<const std::uint8_t>{}, sprites,
                     std::span<const std::uint8_t>{}, std::span<const std::uint8_t>{},
                     std::span<const std::uint8_t>{}, palette, false);
    CHECK(restored.framebuffer().pixels[0] == 0x000000FFU);
}

TEST_CASE("m82 local artifacts load CRC-clean through embedded manifests", "[m82][romset][data]") {
    namespace m82 = mnemos::manifests::irem_m82;

    // Artifact-only gate: this validates local M82 dump composition and
    // parent fallback, but it is not executable M82 board proof.
    const auto dir_env = environment_value("MNEMOS_M82_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M82_SET_DIR to directories containing the M82 zip/folder corpus");
    }

    const auto roots = m82_source_roots(dir_env->c_str());
    REQUIRE_FALSE(roots.empty());
    for (const auto& root : roots) {
        INFO("root=" << root.string());
        REQUIRE(std::filesystem::exists(root));
    }

    const auto indexed_sources = index_m82_source_roots(roots);
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
        FAIL("missing M82 artifacts: " << missing.str());
    }

    for (const auto& set_name : expected_sets) {
        INFO("set=" << set_name);
        auto raw_decl = require_embedded_decl(set_name);
        const auto source_it = indexed_sources.find(set_name);
        REQUIRE(source_it != indexed_sources.end());
        INFO("source=" << source_it->second.string());

        auto effective_decl = raw_decl;
        auto provider = require_m82_provider(source_it->second);
        if (raw_decl.parent.has_value()) {
            const auto parent_source = indexed_sources.find(*raw_decl.parent);
            REQUIRE(parent_source != indexed_sources.end());
            INFO("parent=" << *raw_decl.parent << " source=" << parent_source->second.string());
            const auto parent_decl = require_embedded_decl(*raw_decl.parent);
            effective_decl =
                mnemos::manifests::common::inherit_parent_regions(parent_decl, std::move(raw_decl));
            provider = mnemos::manifests::common::make_fallback_rom_provider(
                std::move(provider), require_m82_provider(parent_source->second));
        }

        const auto image = mnemos::manifests::common::load_rom_set(effective_decl, provider);
        for (const auto& issue : image.issues) {
            INFO(issue.file << ": " << issue.message);
        }
        CHECK(image.issues.empty());

        require_loaded_region(image, "maincpu", m82::main_rom_size);
        require_loaded_region(image, "soundcpu", m82::sound_rom_size);
        require_loaded_region(image, "samples", m82::sample_rom_size);
        const auto* tiles = find_region(effective_decl, "tiles");
        const auto* sprites = find_region(effective_decl, "sprites");
        const auto* proms = find_region(effective_decl, "proms");
        const auto* backgrounds = find_region(effective_decl, "backgrounds");
        REQUIRE(tiles != nullptr);
        REQUIRE(sprites != nullptr);
        REQUIRE(proms != nullptr);
        require_loaded_region(image, "tiles", tiles->size);
        require_loaded_region(image, "sprites", sprites->size);
        require_loaded_region(image, "proms", proms->size);
        if (backgrounds != nullptr) {
            require_loaded_region(image, "backgrounds", backgrounds->size);
        }

        const auto* maincpu = image.region("maincpu");
        REQUIRE(maincpu != nullptr);
        REQUIRE(maincpu->size() == m82::main_rom_size);
        const auto* maincpu_decl = find_region(effective_decl, "maincpu");
        REQUIRE(maincpu_decl != nullptr);
        const bool reloads_at_c0000 = find_file_at(*maincpu_decl, 0xC0000U) != nullptr &&
                                      find_file_at(*maincpu_decl, 0xC0001U) != nullptr;
        const std::size_t source_start = reloads_at_c0000 ? 0x40000U : 0x60000U;
        const std::size_t reload_bytes = reloads_at_c0000 ? 0x40000U : 0x20000U;
        const std::size_t reload_start = reloads_at_c0000 ? 0xC0000U : 0xE0000U;
        CHECK(std::equal(maincpu->begin() + source_start,
                         maincpu->begin() + source_start + reload_bytes,
                         maincpu->begin() + reload_start));
    }
}
