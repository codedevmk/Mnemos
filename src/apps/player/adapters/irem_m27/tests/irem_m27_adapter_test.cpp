#include "irem_m27_adapter.hpp"

#include "file.hpp"
#include "m27_game_manifests.hpp"
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

    namespace irem = mnemos::apps::player::adapters::irem_m27;
    namespace M27 = mnemos::manifests::irem_m27;

    void poke16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
        REQUIRE(offset + 1U < bytes.size());
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    }

    [[nodiscard]] std::vector<std::uint8_t> make_m27_program() {
        std::vector<std::uint8_t> rom(M27::main_rom_size, 0xFFU);
        const std::vector<std::uint8_t> program{
            0xA9U, 0x42U, 0x8DU, 0x00U, 0x00U, // LDA #$42 ; STA $0000
            0xA9U, 0x81U, 0x8DU, 0x00U, 0x20U, // LDA #$81 ; STA $2000
            0xA9U, 0x07U, 0x8DU, 0x00U, 0x24U, // LDA #$07 ; STA $2400
            0xA9U, 0x01U, 0x8DU, 0x04U, 0x40U, // LDA #$01 ; STA $4004
            0xA9U, 0x01U, 0x8DU, 0x05U, 0x40U, // LDA #$01 ; STA $4005
            0x4CU, 0x1EU, 0x80U};             // JMP $801E
        std::copy(program.begin(), program.end(), rom.begin() + M27::program_rom_base);
        poke16(rom, 0xFFFCU, M27::program_rom_base);
        poke16(rom, 0xFFFEU, M27::program_rom_base);
        return rom;
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
        for (const auto& [set_name, _] : M27::embedded::game_manifests) {
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

TEST_CASE("irem_m27_adapter boots a synthetic M27 program", "[irem_m27]") {
    irem::irem_m27_adapter adapter(make_m27_program(), "Tiny M27");
    CHECK(adapter.region().frames_per_second_x1000 == M27::frame_rate_x1000);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    CHECK(adapter.current_frame().width == M27::visible_width);
    CHECK(adapter.current_frame().height == M27::visible_height);

    adapter.step_one_frame();

    CHECK(adapter.frames_stepped() == 1U);
    CHECK(adapter.machine().scratch_ram[0] == 0x42U);
    CHECK(adapter.machine().video_ram[0] == 0x81U);
    CHECK(adapter.machine().color_ram[0] == 0x07U);
    CHECK(adapter.machine().sound_latch == 0x01U);
    CHECK(adapter.machine().sound_latch_write_count == 1U);
    CHECK(adapter.machine().speaker_output_high);
    CHECK(adapter.machine().control_register == 0x01U);
    CHECK(adapter.machine().flip_screen);
    CHECK(framebuffer_has_nonblack(adapter.current_frame()));
    CHECK_FALSE(adapter.save_state().empty());
}

TEST_CASE("irem_m27_adapter save state restores board and adapter state", "[irem_m27]") {
    irem::irem_m27_adapter source(make_m27_program(), "Tiny M27");
    source.step_one_frame();
    mnemos::frontend_sdk::controller_state p1{};
    p1.start = true;
    p1.a = true;
    p1.left = true;
    source.apply_input(0, p1);
    CHECK(source.machine().input_p1 ==
          static_cast<std::uint8_t>(M27::panel_button1_bit | M27::panel_left_bit));
    CHECK(source.machine().input_system == M27::start1_bit);

    p1.select = true;
    source.apply_input(0, p1);
    CHECK(source.machine().input_system ==
          static_cast<std::uint8_t>(M27::start1_bit | M27::coin1_bit));

    const auto state = source.save_state();
    REQUIRE_FALSE(state.empty());

    irem::irem_m27_adapter restored(make_m27_program(), "Tiny M27");
    const auto result = restored.load_state(state);
    CHECK(result.ok());
    CHECK(restored.frames_stepped() == source.frames_stepped());
    CHECK(restored.machine().scratch_ram[0] == 0x42U);
    CHECK(restored.machine().video_ram[0] == 0x81U);
    CHECK(restored.machine().color_ram[0] == 0x07U);
    CHECK(restored.machine().sound_latch == source.machine().sound_latch);
    CHECK(restored.machine().input_p1 == source.machine().input_p1);
    CHECK(restored.machine().input_system == source.machine().input_system);
}

TEST_CASE("irem_m27_adapter drains first-pass beeper audio", "[irem_m27]") {
    irem::irem_m27_adapter adapter(make_m27_program(), "Audio M27");
    adapter.step_one_frame();

    const auto audio = adapter.drain_audio();
    CHECK(audio.frame_count > 0U);
    CHECK(audio.sample_rate == M27::audio_rate_hz);
    CHECK(audio.samples != nullptr);
}

TEST_CASE("irem_m27_adapter validates real M27 ROM sets", "[irem_m27][data]") {
    const auto dir_env = environment_value("MNEMOS_M27_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M27_SET_DIR to directories containing the M27 zip/folder corpus");
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
        FAIL("missing M27 artifacts: " << missing.str());
    }

    for (const auto& set_name : expected_sets) {
        const auto source_it = indexed_sources.find(set_name);
        REQUIRE(source_it != indexed_sources.end());
        INFO("set=" << set_name);
        INFO("source=" << source_it->second.string());

        irem::irem_m27_adapter adapter(read_source_bytes(source_it->second), set_name, nullptr, {},
                                       source_it->second.string());
        REQUIRE_FALSE(adapter.media_capabilities().media.empty());
        for (const auto& issue : adapter.media_capabilities().media.front().validation_issues) {
            INFO(issue.code << ": " << issue.detail);
        }
        CHECK(adapter.media_capabilities().media.front().validation_issues.empty());
        CHECK(adapter.set_name() == set_name);

        for (std::uint32_t frame = 0; frame < 90U; ++frame) {
            adapter.step_one_frame();
        }
        CHECK(adapter.current_frame().width == M27::visible_width);
        CHECK(adapter.current_frame().height == M27::visible_height);
        CHECK(framebuffer_has_nonblack(adapter.current_frame()));
        CHECK_FALSE(adapter.save_state().empty());
    }
}
