#include "irem_m15_adapter.hpp"

#include "file.hpp"
#include "m15_game_manifests.hpp"
#include "zip_archive.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

    namespace irem = mnemos::apps::player::adapters::irem_m15;
    namespace m15 = mnemos::manifests::irem_m15;

    constexpr std::uint16_t visible_probe_tile_index = 1021U;

    void poke16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
        REQUIRE(offset + 1U < bytes.size());
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    }

    [[nodiscard]] std::vector<std::uint8_t>
    make_m15_program(const std::vector<std::uint8_t>& program) {
        std::vector<std::uint8_t> rom(m15::main_rom_size, 0xFFU);
        REQUIRE(m15::program_rom_base + program.size() <= rom.size());
        std::copy(program.begin(), program.end(), rom.begin() + m15::program_rom_base);
        poke16(rom, 0xFFFCU, m15::program_rom_base);
        poke16(rom, 0xFFFEU, m15::program_rom_base);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m15_program() {
        return make_m15_program({0xA9U, 0x42U, 0x8DU, 0x00U, 0x00U, // LDA #$42 ; STA $0000
                                 0xA9U, 0x01U, 0x8DU, 0xFDU, 0x43U, // LDA #$01 ; STA $43FD
                                 0xA9U, 0x00U, 0x8DU, 0xFDU, 0x4BU, // LDA #$00 ; STA $4BFD
                                 0xA9U, 0x80U, 0x8DU, 0x0FU, 0x50U, // LDA #$80 ; STA $500F
                                 0xA9U, 0x5AU, 0x8DU, 0x00U, 0xA1U, // LDA #$5A ; STA $A100
                                 0xA9U, 0x04U, 0x8DU, 0x00U, 0xA4U, // LDA #$04 ; STA $A400
                                 0x4CU, 0x1EU, 0x10U});             // JMP $101E
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

    [[nodiscard]] std::set<std::string, std::less<>> embedded_set_names() {
        std::set<std::string, std::less<>> names;
        for (const auto& [set_name, _] : m15::embedded::game_manifests) {
            names.emplace(std::string{set_name});
        }
        return names;
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

    [[nodiscard]] std::vector<std::uint8_t> read_source_bytes(const std::filesystem::path& path) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            return {};
        }
        auto bytes = mnemos::io::read_file(path.string());
        REQUIRE(bytes.has_value());
        return std::move(*bytes);
    }

} // namespace

TEST_CASE("irem_m15_adapter boots a synthetic M15 program", "[irem_m15]") {
    irem::irem_m15_adapter adapter(synthetic_m15_program(), "Tiny M15");
    CHECK(adapter.region().frames_per_second_x1000 == m15::frame_rate_x1000);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    CHECK(adapter.current_frame().width == m15::visible_width);
    CHECK(adapter.current_frame().height == m15::visible_height);

    adapter.step_one_frame();

    CHECK(adapter.frames_stepped() == 1U);
    CHECK(adapter.machine().scratch_ram[0] == 0x42U);
    CHECK(adapter.machine().video_ram[visible_probe_tile_index] == 0x01U);
    CHECK(adapter.machine().color_ram[visible_probe_tile_index] == 0x00U);
    CHECK(adapter.machine().chargen_ram[0x0FU] == 0x80U);
    CHECK(adapter.machine().speaker_latch == 0x5AU);
    CHECK(adapter.machine().sound_latch_write_count == 1U);
    CHECK(adapter.machine().sound_bit_rise_count[6] == 1U);
    CHECK(adapter.machine().speaker_output_edge_count == 1U);
    CHECK_FALSE(adapter.machine().speaker_output_high);
    CHECK(adapter.machine().control_register == 0x04U);
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 2U);
    CHECK(framebuffer_has_nonblack(adapter.current_frame()));
    CHECK_FALSE(adapter.save_state().empty());
}

TEST_CASE("irem_m15_adapter save state restores board and adapter state", "[irem_m15]") {
    irem::irem_m15_adapter source(synthetic_m15_program(), "Tiny M15");
    source.step_one_frame();
    mnemos::frontend_sdk::controller_state p1{};
    p1.start = true;
    p1.a = true;
    p1.left = true;
    source.apply_input(0, p1);
    CHECK(source.machine().input_p1 ==
          static_cast<std::uint8_t>(m15::p1_start1_bit | m15::panel_button1_bit |
                                    m15::panel_left_bit));
    CHECK(source.machine().input_system == 0x00U);

    p1.select = true;
    source.apply_input(0, p1);
    CHECK(source.machine().input_system == m15::coin1_bit);

    const auto state = source.save_state();
    REQUIRE_FALSE(state.empty());

    irem::irem_m15_adapter restored(synthetic_m15_program(), "Tiny M15");
    const auto result = restored.load_state(state);
    CHECK(result.ok());
    CHECK(restored.frames_stepped() == source.frames_stepped());
    CHECK(restored.machine().scratch_ram[0] == 0x42U);
    CHECK(restored.machine().video_ram[visible_probe_tile_index] == 0x01U);
    CHECK(restored.machine().color_ram[visible_probe_tile_index] == 0x00U);
    CHECK(restored.machine().chargen_ram[0x0FU] == 0x80U);
    CHECK(restored.machine().speaker_latch == source.machine().speaker_latch);
    CHECK(restored.machine().sound_latch_write_count ==
          source.machine().sound_latch_write_count);
    CHECK(restored.machine().sound_bit_rise_count[6] ==
          source.machine().sound_bit_rise_count[6]);
    CHECK(restored.machine().speaker_output_edge_count ==
          source.machine().speaker_output_edge_count);
    CHECK(restored.machine().speaker_output_high == source.machine().speaker_output_high);
    CHECK(restored.machine().input_p1 == source.machine().input_p1);
    CHECK(restored.machine().input_system == source.machine().input_system);
}

TEST_CASE("irem_m15_adapter validates real M15 ROM sets", "[irem_m15][data]") {
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
        const auto source_it = indexed_sources.find(set_name);
        REQUIRE(source_it != indexed_sources.end());
        INFO("set=" << set_name);
        INFO("source=" << source_it->second.string());

        irem::irem_m15_adapter adapter(read_source_bytes(source_it->second), set_name, nullptr, {},
                                       source_it->second.string());
        REQUIRE_FALSE(adapter.media_capabilities().media.empty());
        for (const auto& issue : adapter.media_capabilities().media.front().validation_issues) {
            INFO(issue.code << ": " << issue.detail);
        }
        CHECK(adapter.media_capabilities().media.front().validation_issues.empty());
        CHECK(adapter.set_name() == set_name);

        for (std::uint32_t frame = 0; frame < 120U; ++frame) {
            adapter.step_one_frame();
        }
        CHECK(adapter.current_frame().width == m15::visible_width);
        CHECK(adapter.current_frame().height == m15::visible_height);
        CHECK(framebuffer_has_nonblack(adapter.current_frame()));
        CHECK_FALSE(adapter.save_state().empty());
    }
}
