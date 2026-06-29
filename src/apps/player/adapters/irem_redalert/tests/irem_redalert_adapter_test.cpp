#include "irem_redalert_adapter.hpp"

#include "file.hpp"
#include "redalert_game_manifests.hpp"

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
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

    namespace irem = mnemos::apps::player::adapters::irem_redalert;
    namespace red = mnemos::manifests::irem_redalert;

    void poke16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
        REQUIRE(offset + 1U < bytes.size());
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    }

    [[nodiscard]] std::vector<std::uint8_t> make_redalert_program() {
        std::vector<std::uint8_t> rom(red::main_rom_size, 0xFFU);
        const std::vector<std::uint8_t> program{
            0xA9U, 0x42U, 0x8DU, 0x00U, 0x00U, // LDA #$42 ; STA $0000
            0xA9U, 0x07U, 0x8DU, 0x50U, 0xC0U, // LDA #$07 ; STA $C050
            0xA9U, 0x81U, 0x8DU, 0x00U, 0x20U, // LDA #$81 ; STA $2000
            0xA9U, 0xC3U, 0x8DU, 0x00U, 0x40U, // LDA #$C3 ; STA $4000
            0xA9U, 0x00U, 0x8DU, 0x30U, 0xC0U, // LDA #$00 ; STA $C030
            0xA9U, 0x04U, 0x8DU, 0x40U, 0xC0U, // LDA #$04 ; STA $C040
            0xADU, 0x70U, 0xC0U,             // LDA $C070 ; clear IRQ
            0x4CU, 0x21U, 0x50U};             // JMP $5021
        std::copy(program.begin(), program.end(), rom.begin() + red::program_rom_base);
        poke16(rom, red::vector_mirror_source + 0x0FFCU, red::program_rom_base);
        poke16(rom, red::vector_mirror_source + 0x0FFEU, red::program_rom_base);
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

    [[nodiscard]] std::set<std::string, std::less<>> embedded_set_names() {
        std::set<std::string, std::less<>> names;
        for (const auto& [set_name, _] : red::embedded::game_manifests) {
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
        return known.contains(stem) ? std::optional<std::string>{stem} : std::nullopt;
    }

    [[nodiscard]] bool source_is_directory(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::is_directory(path, ec);
    }

    [[nodiscard]] std::map<std::string, std::filesystem::path, std::less<>>
    index_source_roots(const std::vector<std::filesystem::path>& roots) {
        const auto known = embedded_set_names();
        std::map<std::string, std::filesystem::path, std::less<>> sources;

        auto maybe_add = [&](const std::filesystem::path& path) {
            auto set_id = identify_source(path, known);
            if (!set_id.has_value()) {
                return;
            }
            const auto existing = sources.find(*set_id);
            if (existing == sources.end()) {
                sources.emplace(std::move(*set_id), path);
                return;
            }
            if (!source_is_directory(existing->second) && source_is_directory(path)) {
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

    [[nodiscard]] std::vector<std::uint8_t> read_source_bytes(const std::filesystem::path& path) {
        if (source_is_directory(path)) {
            return {};
        }
        auto bytes = mnemos::io::read_file(path.string());
        REQUIRE(bytes.has_value());
        return std::move(*bytes);
    }

} // namespace

TEST_CASE("irem_redalert_adapter boots a synthetic WW III program", "[irem_redalert]") {
    irem::irem_redalert_adapter adapter(make_redalert_program(), "Tiny WW III");
    CHECK(adapter.region().frames_per_second_x1000 == red::frame_rate_x1000);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    CHECK(adapter.current_frame().width == red::visible_width);
    CHECK(adapter.current_frame().height == red::visible_height);

    adapter.step_one_frame();

    CHECK(adapter.frames_stepped() == 1U);
    CHECK(adapter.machine().ram[0] == 0x42U);
    CHECK(adapter.machine().bitmap_ram[0] == 0x81U);
    CHECK(adapter.machine().bitmap_color_ram[0] == 0x07U);
    CHECK(adapter.machine().char_ram[0] == 0xC3U);
    CHECK(adapter.machine().audio_command == 0x00U);
    CHECK(adapter.machine().audio_command_write_count == 1U);
    CHECK(adapter.machine().speaker_output_high);
    CHECK(adapter.machine().video_control == red::video_flip_bit);
    CHECK(adapter.machine().flip_screen());
    CHECK(framebuffer_has_nonblack(adapter.current_frame()));
    CHECK_FALSE(adapter.save_state().empty());
}

TEST_CASE("irem_redalert_adapter save state restores board and adapter state",
          "[irem_redalert]") {
    irem::irem_redalert_adapter source(make_redalert_program(), "Tiny WW III");
    source.step_one_frame();
    mnemos::frontend_sdk::controller_state p1{};
    p1.start = true;
    p1.a = true;
    p1.left = true;
    source.apply_input(0, p1);
    CHECK(source.machine().key1 ==
          static_cast<std::uint8_t>(red::key1_start1_bit | red::key1_button1_bit |
                                    red::key1_left_bit));

    p1.select = true;
    source.apply_input(0, p1);
    CHECK(source.machine().coin_inputs == red::coin1_bit);

    const auto state = source.save_state();
    REQUIRE_FALSE(state.empty());

    irem::irem_redalert_adapter restored(make_redalert_program(), "Tiny WW III");
    const auto result = restored.load_state(state);
    CHECK(result.ok());
    CHECK(restored.frames_stepped() == source.frames_stepped());
    CHECK(restored.machine().ram[0] == 0x42U);
    CHECK(restored.machine().bitmap_ram[0] == 0x81U);
    CHECK(restored.machine().bitmap_color_ram[0] == 0x07U);
    CHECK(restored.machine().char_ram[0] == 0xC3U);
    CHECK(restored.machine().audio_command == source.machine().audio_command);
    CHECK(restored.machine().key1 == source.machine().key1);
    CHECK(restored.machine().coin_inputs == source.machine().coin_inputs);
}

TEST_CASE("irem_redalert_adapter drains first-pass beeper audio", "[irem_redalert]") {
    irem::irem_redalert_adapter adapter(make_redalert_program(), "Audio WW III");
    adapter.step_one_frame();

    const auto audio = adapter.drain_audio();
    CHECK(audio.frame_count > 0U);
    CHECK(audio.sample_rate == red::audio_rate_hz);
    CHECK(audio.samples != nullptr);
}

TEST_CASE("irem_redalert_adapter validates real WW III ROM set", "[irem_redalert][data]") {
    const auto dir_env = environment_value("MNEMOS_REDALERT_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_REDALERT_SET_DIR to directories containing the Red Alert/WW III corpus");
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
        FAIL("missing Red Alert artifacts: " << missing.str());
    }

    for (const auto& set_name : expected_sets) {
        const auto source_it = indexed_sources.find(set_name);
        REQUIRE(source_it != indexed_sources.end());
        INFO("set=" << set_name);
        INFO("source=" << source_it->second.string());

        irem::irem_redalert_adapter adapter(read_source_bytes(source_it->second), set_name, nullptr,
                                            {}, source_it->second.string());
        REQUIRE_FALSE(adapter.media_capabilities().media.empty());
        for (const auto& issue : adapter.media_capabilities().media.front().validation_issues) {
            INFO(issue.code << ": " << issue.detail);
        }
        CHECK(adapter.media_capabilities().media.front().validation_issues.empty());
        CHECK(adapter.set_name() == set_name);

        for (std::uint32_t frame = 0; frame < 90U; ++frame) {
            adapter.step_one_frame();
        }
        CHECK(adapter.current_frame().width == red::visible_width);
        CHECK(adapter.current_frame().height == red::visible_height);
        CHECK(framebuffer_has_nonblack(adapter.current_frame()));
        CHECK_FALSE(adapter.save_state().empty());
    }
}
