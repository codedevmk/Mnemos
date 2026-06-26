#include "irem_m52_adapter.hpp"

#include "file.hpp"
#include "m52_game_manifests.hpp"
#include "rom_set_toml.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    namespace irem = mnemos::apps::player::adapters::irem_m52;
    namespace m52 = mnemos::manifests::irem_m52;

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m52_program() {
        std::vector<std::uint8_t> rom(m52::main_rom_size, 0x00U);
        const std::uint8_t program[] = {0x3EU, 0x77U, 0x32U, 0x00U, 0x80U, 0xD3U, 0x10U, 0x3EU,
                                        0x01U, 0x32U, 0x00U, 0xD0U, 0xC3U, 0x00U, 0x00U};
        std::copy(std::begin(program), std::end(program), rom.begin());
        return rom;
    }

    [[nodiscard]] std::size_t nonblack_pixels(mnemos::chips::frame_buffer_view view) {
        std::size_t count = 0U;
        for (std::uint32_t y = 0; y < view.height; ++y) {
            for (std::uint32_t x = 0; x < view.width; ++x) {
                if (view.pixels[static_cast<std::size_t>(y) * view.stride + x] != 0U) {
                    ++count;
                }
            }
        }
        return count;
    }

    [[nodiscard]] std::optional<std::string> environment_value(const char* name) {
#ifdef _WIN32
        char* raw = nullptr;
        std::size_t size = 0;
        if (_dupenv_s(&raw, &size, name) != 0 || raw == nullptr) {
            return std::nullopt;
        }
        std::string value(raw, raw + size - 1U);
        std::free(raw);
        return value.empty() ? std::nullopt : std::optional<std::string>{std::move(value)};
#else
        if (const char* value = std::getenv(name); value != nullptr && value[0] != '\0') {
            return std::string{value};
        }
        return std::nullopt;
#endif
    }

    [[nodiscard]] std::vector<std::string> split_paths(const std::string& value) {
        std::vector<std::string> out;
        std::string current;
        for (char c : value) {
            if (c == ';') {
                if (!current.empty()) {
                    out.push_back(current);
                    current.clear();
                }
            } else {
                current.push_back(c);
            }
        }
        if (!current.empty()) {
            out.push_back(current);
        }
        return out;
    }

    [[nodiscard]] std::map<std::string, fs::path>
    find_m52_sets(const std::vector<std::string>& roots) {
        std::map<std::string, fs::path> result;
        for (const std::string& root : roots) {
            std::error_code ec;
            if (!fs::exists(root, ec)) {
                continue;
            }
            for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, ec)) {
                if (ec || !entry.is_regular_file()) {
                    continue;
                }
                std::string stem = entry.path().stem().string();
                std::transform(stem.begin(), stem.end(), stem.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (stem == "moon-patrol_arcade_en") {
                    result.try_emplace("mpatrol", entry.path());
                } else if (stem == "moon-patrol_arcade_en (1)") {
                    result.try_emplace("mpatrolw", entry.path());
                } else if (stem == "mpatrol") {
                    result.try_emplace("mpatrol", entry.path());
                } else if (stem == "mpatrolw") {
                    result.try_emplace("mpatrolw", entry.path());
                }
            }
        }
        return result;
    }
} // namespace

TEST_CASE("irem_m52_adapter boots a synthetic M52 program", "[irem_m52]") {
    irem::irem_m52_adapter adapter(synthetic_m52_program(), "Tiny M52");
    CHECK(adapter.region().frames_per_second_x1000 == m52::frame_rate_x1000);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 1U);
    CHECK(adapter.machine().video_ram[0] == 0x77U);
    CHECK(adapter.machine().scroll_regs[0] == 0x77U);
    CHECK(adapter.machine().sound_command_write_count > 0U);
    CHECK(nonblack_pixels(adapter.current_frame()) > 0U);
}

TEST_CASE("irem_m52_adapter preserves adapter and board state", "[irem_m52]") {
    irem::irem_m52_adapter source(synthetic_m52_program(), "Save M52");
    source.step_one_frame();
    mnemos::frontend_sdk::controller_state input_state{};
    input_state.up = true;
    input_state.start = true;
    input_state.a = true;
    source.apply_input(0, input_state);

    const std::vector<std::uint8_t> state = source.save_state();
    REQUIRE_FALSE(state.empty());

    irem::irem_m52_adapter restored(synthetic_m52_program(), "Save M52");
    const auto result = restored.load_state(state);
    REQUIRE(result.ok());
    CHECK(restored.frames_stepped() == source.frames_stepped());
    CHECK(restored.machine().input0 == source.machine().input0);
    CHECK(restored.machine().sound_command == source.machine().sound_command);
}

TEST_CASE("irem_m52_adapter drains mixed AY PSG samples", "[irem_m52]") {
    irem::irem_m52_adapter adapter(synthetic_m52_program(), "Audio M52");
    adapter.step_one_frame();
    CHECK(adapter.machine().ay0.volume(0) == 0x0FU);
    CHECK(adapter.machine().ay1.volume(1) == 0x0CU);
    const auto audio = adapter.drain_audio();
    CHECK(audio.sample_rate == m52::audio_rate_hz);
    CHECK(audio.frame_count > 0U);
    REQUIRE(audio.samples != nullptr);
    bool any_nonzero = false;
    for (std::uint32_t i = 0; i < audio.frame_count * 2U; ++i) {
        any_nonzero = any_nonzero || audio.samples[i] != 0;
    }
    CHECK(any_nonzero);
}

TEST_CASE("irem_m52_adapter validates real M52 ROM sets", "[irem_m52][data]") {
    const auto dir_env = environment_value("MNEMOS_M52_SET_DIR");
    if (!dir_env.has_value()) {
        SKIP("set MNEMOS_M52_SET_DIR to directories containing the M52 zip/folder corpus");
    }
    const auto found = find_m52_sets(split_paths(*dir_env));
    const auto it = found.find("mpatrol");
    if (it == found.end()) {
        SKIP("MNEMOS_M52_SET_DIR does not contain a Moon Patrol parent wrapper");
    }

    auto bytes = mnemos::io::read_file(it->second.string());
    REQUIRE(bytes.has_value());
    irem::irem_m52_adapter adapter(std::move(*bytes), "Moon Patrol", nullptr, {},
                                   it->second.string());
    REQUIRE(adapter.set_name() == "mpatrol");
    REQUIRE_FALSE(adapter.media_capabilities().media.empty());
    CHECK(adapter.media_capabilities().media.front().validation_issues.empty());

    adapter.step_one_frame();
    CHECK(nonblack_pixels(adapter.current_frame()) > 0U);
    const std::vector<std::uint8_t> state = adapter.save_state();
    REQUIRE_FALSE(state.empty());
    CHECK(adapter.load_state(state).ok());

    if (const auto clone_it = found.find("mpatrolw"); clone_it != found.end()) {
        auto clone_bytes = mnemos::io::read_file(clone_it->second.string());
        REQUIRE(clone_bytes.has_value());
        irem::irem_m52_adapter clone(std::move(*clone_bytes), "Moon Patrol (Williams)", nullptr, {},
                                     clone_it->second.string());
        REQUIRE(clone.set_name() == "mpatrolw");
        REQUIRE_FALSE(clone.media_capabilities().media.empty());
        CHECK(clone.media_capabilities().media.front().validation_issues.empty());
        clone.step_one_frame();
        CHECK(nonblack_pixels(clone.current_frame()) > 0U);
    }
}
