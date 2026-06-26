#include "file.hpp"
#include "m15_game_manifests.hpp"
#include "m15_system.hpp"
#include "rom_set_toml.hpp"
#include "zip_archive.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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

#ifndef MNEMOS_IREM_M15_GAMES_DIR
#define MNEMOS_IREM_M15_GAMES_DIR ""
#endif

namespace {

    using mnemos::manifests::common::rom_set_decl;
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
        for (const auto& [set_name, _] : mnemos::manifests::irem_m15::embedded::game_manifests) {
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
        const std::string_view text = mnemos::manifests::irem_m15::game_manifest_toml(set_name);
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

    void poke16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
        REQUIRE(offset + 1U < bytes.size());
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    }

    [[nodiscard]] std::vector<std::uint8_t>
    make_m15_program(std::uint16_t origin, const std::vector<std::uint8_t>& program,
                     std::uint16_t irq_vector = 0x1000U) {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m15::main_rom_size, 0xFFU);
        REQUIRE(static_cast<std::size_t>(origin) + program.size() <= rom.size());
        std::copy(program.begin(), program.end(), rom.begin() + origin);
        poke16(rom, 0xFFFCU, origin);
        poke16(rom, 0xFFFEU, irq_vector);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m15_program() {
        return make_m15_program(0x1000U,
                                {0xA9U, 0x42U, 0x8DU, 0x00U, 0x00U, // LDA #$42 ; STA $0000
                                 0xA9U, 0x81U, 0x8DU, 0x00U, 0x40U, // LDA #$81 ; STA $4000
                                 0xA9U, 0x2AU, 0x8DU, 0x00U, 0x48U, // LDA #$2A ; STA $4800
                                 0xA9U, 0x18U, 0x8DU, 0x00U, 0x50U, // LDA #$18 ; STA $5000
                                 0xA9U, 0x5AU, 0x8DU, 0x00U, 0xA1U, // LDA #$5A ; STA $A100
                                 0xA9U, 0x04U, 0x8DU, 0x00U, 0xA4U, // LDA #$04 ; STA $A400
                                 0x4CU, 0x1EU, 0x10U},             // JMP $101E
                                0x101EU);
    }

    [[nodiscard]] rom_set_image synthetic_m15_image() {
        rom_set_image image;
        image.regions.emplace("maincpu", synthetic_m15_program());
        return image;
    }

    [[nodiscard]] rom_set_image irq_m15_image() {
        std::vector<std::uint8_t> program(0x30U, 0xEAU);
        program[0x00U] = 0x58U; // CLI
        program[0x01U] = 0xEAU; // NOP
        program[0x02U] = 0x4CU; // JMP $1001
        program[0x03U] = 0x01U;
        program[0x04U] = 0x10U;
        program[0x20U] = 0xA9U; // IRQ: LDA #$77
        program[0x21U] = 0x77U;
        program[0x22U] = 0x8DU; // STA $0002
        program[0x23U] = 0x02U;
        program[0x24U] = 0x00U;
        program[0x25U] = 0x40U; // RTI

        rom_set_image image;
        image.regions.emplace("maincpu", make_m15_program(0x1000U, program, 0x1020U));
        return image;
    }

    [[nodiscard]] bool framebuffer_has_nonblack(const mnemos::chips::frame_buffer_view& frame) {
        REQUIRE(frame.pixels != nullptr);
        REQUIRE(frame.width > 0U);
        REQUIRE(frame.height > 0U);
        const std::uint32_t stride = frame.effective_stride();
        for (std::uint32_t y = 0; y < frame.height; ++y) {
            for (std::uint32_t x = 0; x < frame.width; ++x) {
                if (frame.pixels[static_cast<std::size_t>(y) * stride + x] != 0U) {
                    return true;
                }
            }
        }
        return false;
    }

} // namespace

TEST_CASE("m15 checked-in game manifests parse and cover local candidate corpus",
          "[m15][romset]") {
    namespace fs = std::filesystem;
    namespace m15 = mnemos::manifests::irem_m15;

    const fs::path games_dir{MNEMOS_IREM_M15_GAMES_DIR};
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

    const std::set<std::string, std::less<>> expected_names{"headoni"};
    std::set<std::string, std::less<>> names;
    for (const auto& [set_name, decl] : declarations) {
        INFO("set=" << set_name);
        names.insert(decl.name);
        CHECK(decl.board == "irem_m15");
        CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::vertical);

        const rom_set_region* maincpu = find_region(decl, "maincpu");
        REQUIRE(maincpu != nullptr);
        CHECK(maincpu->size == m15::main_rom_size);
        REQUIRE(maincpu->files.size() == 7U);
        require_region_contract(*maincpu);

        const std::array<std::size_t, 7U> expected_offsets{
            0x1000U, 0x1400U, 0x1800U, 0x1C00U, 0xFC00U, 0x2000U, 0x2400U};
        for (std::size_t i = 0U; i < maincpu->files.size(); ++i) {
            CHECK(maincpu->files[i].offset == expected_offsets[i]);
        }
        CHECK(maincpu->files[3].name == "e4.9d");
        CHECK(maincpu->files[4].name == "e4.9d");
    }

    CHECK(names == expected_names);
    const auto params = m15::board_params_for("headoni");
    CHECK(params.cpu_clock_hz == m15::cpu_clock_hz);
    CHECK(params.rom_layout == "m15_headon_6502");
}

TEST_CASE("m15 embedded game manifests mirror the checked-in roster", "[m15][romset]") {
    using mnemos::manifests::irem_m15::embedded::game_manifests;

    CHECK(game_manifests.size() == 1U);
    CHECK_FALSE(mnemos::manifests::irem_m15::game_manifest_toml("headoni").empty());
    CHECK(mnemos::manifests::irem_m15::game_manifest_toml("rtype").empty());
}

TEST_CASE("m15 local artifacts load CRC-clean through embedded manifests",
          "[m15][romset][data]") {
    namespace m15 = mnemos::manifests::irem_m15;

    const auto dir_env = environment_value("MNEMOS_M15_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M15_SET_DIR to directories containing the M15 zip/folder corpus");
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
        FAIL("missing M15 artifacts: " << missing.str());
    }

    for (const auto& set_name : expected_sets) {
        INFO("set=" << set_name);
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
        require_loaded_region(image, "maincpu", m15::main_rom_size);
    }
}

TEST_CASE("m15 executable board runs MOS 6502 memory video and sound path", "[m15][board]") {
    namespace m15 = mnemos::manifests::irem_m15;

    auto system = m15::assemble_m15(synthetic_m15_image(), m15::board_params_for("headoni"));
    REQUIRE(system != nullptr);

    const auto cpu_meta = system->main_cpu.metadata();
    CHECK(cpu_meta.manufacturer == "MOS Technology");
    CHECK(cpu_meta.part_number == "6502");
    CHECK(cpu_meta.family == "6502");
    CHECK(system->main_cpu.cpu_registers().pc == m15::program_rom_base);

    system->run_frame();

    CHECK(system->scratch_ram[0] == 0x42U);
    CHECK(system->video_ram[0] == 0x81U);
    CHECK(system->color_ram[0] == 0x2AU);
    CHECK(system->chargen_ram[0] == 0x18U);
    CHECK(system->speaker_latch == 0x5AU);
    CHECK(system->control_register == 0x04U);
    CHECK(system->video.framebuffer().width == m15::visible_width);
    CHECK(system->video.framebuffer().height == m15::visible_height);
    CHECK(framebuffer_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m15 frame tick pulses the 6502 IRQ vector", "[m15][board]") {
    namespace m15 = mnemos::manifests::irem_m15;

    auto system = m15::assemble_m15(irq_m15_image(), m15::board_params_for("headoni"));
    REQUIRE(system != nullptr);

    system->run_frame();

    CHECK(system->scratch_ram[2] == 0x77U);
    CHECK(system->main_cpu.elapsed_cycles() >= m15::cpu_cycles_per_frame);
}

TEST_CASE("m15 save state preserves board identity and runtime state", "[m15][board]") {
    namespace m15 = mnemos::manifests::irem_m15;

    auto source = m15::assemble_m15(synthetic_m15_image(), m15::board_params_for("headoni"));
    REQUIRE(source != nullptr);
    source->set_inputs(0xEEU, 0xDDU, 0xCCU);
    source->run_frame();
    source->scratch_ram[3] = 0x5AU;

    std::vector<std::uint8_t> state;
    mnemos::chips::state_writer writer(state);
    source->save_state(writer);
    REQUIRE_FALSE(state.empty());

    auto restored = m15::assemble_m15(synthetic_m15_image(), m15::board_params_for("headoni"));
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(state);
    restored->load_state(reader);
    CHECK(reader.ok());
    CHECK(restored->scratch_ram[0] == 0x42U);
    CHECK(restored->scratch_ram[3] == 0x5AU);
    CHECK(restored->video_ram[0] == 0x81U);
    CHECK(restored->color_ram[0] == 0x2AU);
    CHECK(restored->chargen_ram[0] == 0x18U);
    CHECK(restored->input_p1 == 0xEEU);
    CHECK(restored->input_p2 == 0xDDU);
    CHECK(restored->input_system == 0xCCU);

    auto incompatible =
        m15::assemble_m15(synthetic_m15_image(), m15::m15_board_params{.dip_default = 0x7FU});
    REQUIRE(incompatible != nullptr);
    mnemos::chips::state_reader rejected(state);
    incompatible->load_state(rejected);
    CHECK_FALSE(rejected.ok());
}
