#include "irem_travrusa_adapter.hpp"

#include "file.hpp"
#include "travrusa_game_manifests.hpp"
#include "rom_set_toml.hpp"
#include "sha256.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
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
    namespace irem = mnemos::apps::player::adapters::irem_travrusa;
    namespace travrusa = mnemos::manifests::irem_travrusa;

    struct temp_set_dir final {
        fs::path path;

        temp_set_dir() {
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() / ("mnemos_travrusa_adapter_" + std::to_string(ticks));
            REQUIRE(fs::create_directories(path));
        }

        temp_set_dir(const temp_set_dir&) = delete;
        temp_set_dir& operator=(const temp_set_dir&) = delete;

        temp_set_dir(temp_set_dir&& other) noexcept : path(std::move(other.path)) {
            other.path.clear();
        }

        temp_set_dir& operator=(temp_set_dir&& other) noexcept {
            if (this != &other) {
                cleanup();
                path = std::move(other.path);
                other.path.clear();
            }
            return *this;
        }

        ~temp_set_dir() { cleanup(); }

        void cleanup() noexcept {
            if (path.empty()) {
                return;
            }
            std::error_code ec;
            fs::remove_all(path, ec);
            path.clear();
        }
    };

    void write_binary_file(const fs::path& path, std::span<const std::uint8_t> bytes) {
        std::ofstream file(path, std::ios::binary);
        REQUIRE(file.good());
        file.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        REQUIRE(file.good());
    }

    void write_text_file(const fs::path& path, std::string_view text) {
        std::ofstream file(path);
        REQUIRE(file.good());
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        REQUIRE(file.good());
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_travrusa_program() {
        std::vector<std::uint8_t> rom(travrusa::main_rom_size, 0x00U);
        const std::uint8_t program[] = {
            0x3EU, 0x77U, 0x32U, 0x00U, 0x80U, // LD A,$77 ; LD ($8000),A
            0x3EU, 0x2AU, 0x32U, 0x00U, 0x90U, // LD A,$2A ; LD ($9000),A
            0x3EU, 0x01U, 0x32U, 0x00U, 0xA0U, // LD A,$01 ; LD ($A000),A
            0x3EU, 0x01U, 0x32U, 0x00U, 0xD0U, // LD A,$01 ; LD ($D000),A
            0xC3U, 0x00U, 0x00U};
        std::copy(std::begin(program), std::end(program), rom.begin());
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_travrusa_sound_program() {
        std::vector<std::uint8_t> rom(travrusa::sound_rom_size, 0x00U);
        const std::uint8_t program[] = {
            0xDBU, 0x02U, // IN A,($02)
            0xD3U, 0x06U, // OUT ($06),A
            0x3EU, 0x07U, 0xD3U, 0x00U, 0x3EU, 0xFEU, 0xD3U, 0x01U, 0x3EU, 0x08U, 0xD3U,
            0x00U, 0x3EU, 0x0FU, 0xD3U, 0x01U, 0x3EU, 0x00U, 0xD3U, 0x00U, 0x3EU, 0x1FU,
            0xD3U, 0x01U, 0x3EU, 0x07U, 0xD3U, 0x04U, 0x3EU, 0xFDU, 0xD3U, 0x05U, 0x3EU,
            0x09U, 0xD3U, 0x04U, 0x3EU, 0x0CU, 0xD3U, 0x05U, 0x3EU, 0x02U, 0xD3U, 0x04U,
            0x3EU, 0x23U, 0xD3U, 0x05U, 0x3EU, 0x07U, 0xD3U, 0x08U, 0xC3U, 0x00U, 0x00U};
        std::copy(std::begin(program), std::end(program), rom.begin());
        return rom;
    }

    [[nodiscard]] temp_set_dir make_audio_metadata_set() {
        temp_set_dir dir;
        write_text_file(dir.path / "game.toml", R"toml(
[set]
schema = "mnemos-romset/1"
name = "audiometa"
board = "irem_travrusa"
orientation = "horizontal"

[[region]]
name = "maincpu"
size = 0x10000

[[region.file]]
name = "maincpu.bin"
offset = 0
size = 0x10000

[[region]]
name = "soundcpu"
size = 0x10000

[[region.file]]
name = "soundcpu.bin"
offset = 0
size = 0x10000
)toml");
        write_binary_file(dir.path / "maincpu.bin", synthetic_travrusa_program());
        write_binary_file(dir.path / "soundcpu.bin", synthetic_travrusa_sound_program());
        return dir;
    }

    [[nodiscard]] temp_set_dir make_dip_metadata_set() {
        temp_set_dir dir;
        write_text_file(dir.path / "game.toml", R"toml(
[set]
schema = "mnemos-romset/1"
name = "dipmeta"
board = "irem_travrusa"
orientation = "horizontal"

[[dip]]
bank = "SW1"
name = "Test Cars"
mask = 0x0003
default = 0x0003

[[dip.option]]
label = "2"
value = 0x0003

[[dip.option]]
label = "4"
value = 0x0001

[[dip]]
bank = "SW2"
name = "Test Flip"
mask = 0x0100
default = 0x0100

[[dip.option]]
label = "No"
value = 0x0000

[[dip.option]]
label = "Yes"
value = 0x0100

[[region]]
name = "maincpu"
size = 0x10000

[[region.file]]
name = "maincpu.bin"
offset = 0
size = 0x10000
)toml");
        write_binary_file(dir.path / "maincpu.bin", synthetic_travrusa_program());
        return dir;
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

    [[nodiscard]] std::string sha256_hex(std::span<const std::uint8_t> bytes) {
        return mnemos::security::cryptography::sha256(bytes).hex();
    }

    [[nodiscard]] std::string
    hash_framebuffer_rgba(const mnemos::chips::frame_buffer_view& view) {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(static_cast<std::size_t>(view.width) * view.height * 4U);
        const std::uint32_t stride = view.effective_stride();
        for (std::uint32_t y = 0; y < view.height; ++y) {
            const std::uint32_t* row =
                view.pixels != nullptr ? view.pixels + static_cast<std::size_t>(y) * stride
                                       : nullptr;
            for (std::uint32_t x = 0; x < view.width; ++x) {
                const std::uint32_t p = row != nullptr ? row[x] : 0U;
                bytes.push_back(static_cast<std::uint8_t>((p >> 16U) & 0xFFU));
                bytes.push_back(static_cast<std::uint8_t>((p >> 8U) & 0xFFU));
                bytes.push_back(static_cast<std::uint8_t>(p & 0xFFU));
                bytes.push_back(0xFFU);
            }
        }
        return sha256_hex(bytes);
    }

    [[nodiscard]] std::string hash_audio_pcm_s16le(std::span<const std::int16_t> samples) {
        std::vector<std::uint8_t> bytes;
        bytes.reserve(samples.size() * 2U);
        for (const std::int16_t sample : samples) {
            const auto value = static_cast<std::uint16_t>(sample);
            bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
            bytes.push_back(static_cast<std::uint8_t>(value >> 8U));
        }
        return sha256_hex(bytes);
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

    [[nodiscard]] int env_positive_int(const char* name, int fallback) {
        const auto value = environment_value(name);
        if (!value.has_value()) {
            return fallback;
        }
        char* end = nullptr;
        const long parsed = std::strtol(value->c_str(), &end, 10);
        if (end == value->c_str() || parsed <= 0L) {
            return fallback;
        }
        return static_cast<int>(parsed);
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

    void strip_copy_suffix(std::string& stem) {
        if (stem.size() < 4U || stem.back() != ')') {
            return;
        }
        const auto open = stem.rfind(" (");
        if (open == std::string::npos || open + 2U >= stem.size() - 1U) {
            return;
        }
        for (std::size_t i = open + 2U; i + 1U < stem.size(); ++i) {
            if (std::isdigit(static_cast<unsigned char>(stem[i])) == 0) {
                return;
            }
        }
        stem.resize(open);
    }

    [[nodiscard]] std::optional<std::string> canonical_travrusa_set_id(fs::path path) {
        std::string stem = path.stem().string();
        std::transform(stem.begin(), stem.end(), stem.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        strip_copy_suffix(stem);
        if (stem == "travrusa" || stem == "motorace" || stem == "travrusab" ||
            stem == "travrusab2") {
            return stem;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::map<std::string, fs::path>
    find_travrusa_sets(const std::vector<std::string>& roots) {
        std::map<std::string, fs::path> result;
        for (const std::string& root : roots) {
            std::error_code ec;
            if (!fs::exists(root, ec)) {
                continue;
            }
            std::vector<fs::path> candidates;
            for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, ec)) {
                if (ec || !entry.is_regular_file()) {
                    continue;
                }
                candidates.push_back(entry.path());
            }
            std::sort(candidates.begin(), candidates.end());
            for (const fs::path& path : candidates) {
                if (path.extension() != ".zip") {
                    continue;
                }
                auto set_id = canonical_travrusa_set_id(path);
                if (!set_id.has_value()) {
                    continue;
                }
                if (*set_id == "travrusa" && path.stem().string().find(" (") != std::string::npos) {
                    result[*set_id] = path;
                } else {
                    result.try_emplace(*set_id, path);
                }
            }
        }
        return result;
    }

    [[nodiscard]] bool spec_has(const irem::irem_travrusa_adapter& adapter, std::string_view label,
                                std::string_view value) {
        const auto& spec = adapter.system_spec();
        return std::any_of(spec.begin(), spec.end(), [label, value](const auto& field) {
            return field.label == label && field.value == value;
        });
    }
} // namespace

TEST_CASE("irem_travrusa_adapter boots a synthetic travrusa program", "[irem_travrusa]") {
    irem::irem_travrusa_adapter adapter(synthetic_travrusa_program(), "Tiny travrusa");
    CHECK(adapter.region().frames_per_second_x1000 == travrusa::frame_rate_x1000);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 1U);
    CHECK(adapter.machine().video_ram[0] == 0x77U);
    CHECK(adapter.machine().scroll_x_low == 0x2AU);
    CHECK(adapter.machine().scroll_x_high == 0x01U);
    CHECK(adapter.machine().scroll_x() == 0x012AU);
    CHECK(adapter.machine().sound_command_write_count > 0U);
    CHECK(adapter.machine().sound_cpu.elapsed_cycles() > 0U);
    CHECK(irem::build_save_target(adapter).manifest_rev == travrusa::travrusa_system_state_version);
    REQUIRE(adapter.chips().size() == 6U);
    CHECK(adapter.chips()[2]->metadata().part_number == "Z80");
    CHECK(adapter.chips()[5]->metadata().part_number == "MSM5205");
    CHECK(adapter.machine().msm.vclk_count() > 0U);
    CHECK(nonblack_pixels(adapter.current_frame()) > 0U);
}

TEST_CASE("irem_travrusa_adapter applies manifest DIP defaults and override", "[irem_travrusa]") {
    const auto set = make_dip_metadata_set();
    irem::irem_travrusa_adapter adapter({}, "DIP metadata", nullptr, {}, set.path.string());

    CHECK(adapter.dip_switches().size() == 2U);
    CHECK((adapter.machine().dsw1 & 0x03U) == 0x03U);
    CHECK((adapter.machine().dsw2 & 0x01U) == 0x01U);
    CHECK(spec_has(adapter, "DIP switches", "2"));

    irem::irem_travrusa_adapter overridden({}, "DIP metadata", nullptr,
                                      static_cast<std::uint16_t>(0x1234U), set.path.string());
    CHECK(overridden.dip_switches().size() == 2U);
    CHECK(overridden.machine().dsw1 == 0x34U);
    CHECK(overridden.machine().dsw2 == 0x12U);
    CHECK(spec_has(overridden, "DIP switches", "2"));
}

TEST_CASE("irem_travrusa_adapter maps service and operator-test inputs to the system port",
          "[irem_travrusa]") {
    irem::irem_travrusa_adapter adapter(synthetic_travrusa_program(), "travrusa inputs");

    mnemos::frontend_sdk::controller_state p1{};
    p1.start = true;
    p1.select = true;
    p1.service = true;
    p1.test = true;
    adapter.apply_input(0, p1);

    CHECK(adapter.machine().input_system == 0xE2U);

    p1.service = false;
    p1.test = false;
    p1.mode = true;
    adapter.apply_input(0, p1);

    CHECK(adapter.machine().input_system == 0xF6U);

    mnemos::frontend_sdk::controller_state p2{};
    p2.start = true;
    p2.select = true;
    adapter.apply_input(1, p2);

    CHECK(adapter.machine().input_system == 0xF4U);
    CHECK((adapter.machine().input_p2 & 0x10U) == 0x00U);
}

TEST_CASE("irem_travrusa_adapter preserves adapter and board state", "[irem_travrusa]") {
    irem::irem_travrusa_adapter source(synthetic_travrusa_program(), "Save travrusa");
    source.step_one_frame();
    mnemos::frontend_sdk::controller_state input_state{};
    input_state.up = true;
    input_state.start = true;
    input_state.select = true;
    input_state.a = true;
    input_state.service = true;
    input_state.test = true;
    source.apply_input(0, input_state);
    REQUIRE(source.machine().input_system == 0xE2U);

    const std::vector<std::uint8_t> state = source.save_state();
    REQUIRE_FALSE(state.empty());

    irem::irem_travrusa_adapter restored(synthetic_travrusa_program(), "Save travrusa");
    const auto result = restored.load_state(state);
    REQUIRE(result.ok());
    CHECK(restored.frames_stepped() == source.frames_stepped());
    CHECK(restored.machine().input_p1 == source.machine().input_p1);
    CHECK(restored.machine().input_system == 0xE2U);
    CHECK(restored.machine().sound_command == source.machine().sound_command);
}

TEST_CASE("irem_travrusa_adapter drains mixed AY PSG samples", "[irem_travrusa]") {
    const auto set = make_audio_metadata_set();
    irem::irem_travrusa_adapter adapter({}, "Audio travrusa", nullptr, {}, set.path.string());
    adapter.step_one_frame();
    CHECK(adapter.machine().ay0.volume(0) == 0x0FU);
    CHECK(adapter.machine().ay1.volume(1) == 0x0CU);
    CHECK(adapter.machine().msm.pending_samples() > 0U);
    const auto audio = adapter.drain_audio();
    CHECK(audio.sample_rate == travrusa::audio_rate_hz);
    CHECK(audio.frame_count > 0U);
    REQUIRE(audio.samples != nullptr);
    CHECK(adapter.machine().msm.pending_samples() == 0U);
    bool any_nonzero = false;
    for (std::uint32_t i = 0; i < audio.frame_count * 2U; ++i) {
        any_nonzero = any_nonzero || audio.samples[i] != 0;
    }
    CHECK(any_nonzero);
}

TEST_CASE("irem_travrusa_adapter validates real travrusa ROM sets", "[irem_travrusa][data]") {
    const auto dir_env = environment_value("MNEMOS_TRAVRUSA_SET_DIR");
    if (!dir_env.has_value()) {
        SKIP("set MNEMOS_TRAVRUSA_SET_DIR to directories containing the travrusa zip/folder corpus");
    }
    const auto found = find_travrusa_sets(split_paths(*dir_env));
    const auto parent_it = found.find("travrusa");
    if (parent_it == found.end()) {
        SKIP("MNEMOS_TRAVRUSA_SET_DIR does not contain a Traverse USA parent wrapper");
    }

    const std::array<std::string_view, 4> expected_sets{
        "travrusa", "travrusab", "travrusab2", "motorace"};
    for (const std::string_view set_name : expected_sets) {
        const auto it = found.find(std::string{set_name});
        if (it == found.end()) {
            if (set_name == "motorace") {
                continue;
            }
            FAIL_CHECK("MNEMOS_TRAVRUSA_SET_DIR is missing " << set_name);
            continue;
        }
        auto bytes = mnemos::io::read_file(it->second.string());
        REQUIRE(bytes.has_value());
        irem::irem_travrusa_adapter adapter(std::move(*bytes), std::string{set_name}, nullptr, {},
                                            it->second.string());
        REQUIRE(adapter.set_name() == set_name);
        REQUIRE_FALSE(adapter.media_capabilities().media.empty());
        CHECK(adapter.media_capabilities().media.front().validation_issues.empty());
        CHECK(adapter.machine().dsw1 == travrusa::travrusa_dsw1_default);
        CHECK(adapter.machine().dsw2 == travrusa::travrusa_dsw2_default);

        adapter.step_one_frame();
        CHECK(nonblack_pixels(adapter.current_frame()) > 0U);
        const std::vector<std::uint8_t> state = adapter.save_state();
        REQUIRE_FALSE(state.empty());
        CHECK(adapter.load_state(state).ok());
    }
}

TEST_CASE("irem_travrusa_adapter matches optional visual/audio parity hashes for a real travrusa set",
          "[irem_travrusa][data]") {
    struct parity_result final {
        std::string set_name;
        std::string frame_hash;
        std::string audio_hash;
        std::uint64_t audio_frames{};
        std::uint32_t sample_rate{};
    };

    const auto set_env = environment_value("MNEMOS_TRAVRUSA_PARITY_SET");
    if (!set_env.has_value()) {
        SKIP("set MNEMOS_TRAVRUSA_PARITY_SET to a reference-captured travrusa set");
    }
    const auto expected_frame_hash = environment_value("MNEMOS_TRAVRUSA_PARITY_FRAME_SHA256");
    const auto expected_audio_hash = environment_value("MNEMOS_TRAVRUSA_PARITY_AUDIO_SHA256");
    if (!expected_frame_hash.has_value() && !expected_audio_hash.has_value()) {
        SKIP("set MNEMOS_TRAVRUSA_PARITY_FRAME_SHA256 and/or MNEMOS_TRAVRUSA_PARITY_AUDIO_SHA256");
    }

    const int frames = env_positive_int("MNEMOS_TRAVRUSA_PARITY_FRAMES", 600);
    const bool collect_audio = expected_audio_hash.has_value();

    auto run_once = [&](bool want_audio) -> parity_result {
        auto bytes = mnemos::io::read_file(*set_env);
        REQUIRE(bytes.has_value());
        irem::irem_travrusa_adapter adapter(std::move(*bytes), "travrusa-parity", nullptr, {},
                                       *set_env);
        REQUIRE_FALSE(adapter.set_name().empty());
        REQUIRE_FALSE(adapter.media_capabilities().media.empty());
        REQUIRE(adapter.media_capabilities().media.front().validation_issues.empty());

        std::vector<std::int16_t> audio_samples;
        std::uint32_t sample_rate = 0U;
        std::uint64_t audio_frames = 0U;
        for (int frame = 0; frame < frames; ++frame) {
            adapter.step_one_frame();
            if (want_audio) {
                const auto audio = adapter.drain_audio();
                sample_rate = audio.sample_rate;
                audio_frames += audio.frame_count;
                if (audio.samples != nullptr && audio.frame_count != 0U) {
                    const auto* begin = audio.samples;
                    const auto* end = begin + static_cast<std::size_t>(audio.frame_count) * 2U;
                    audio_samples.insert(audio_samples.end(), begin, end);
                }
            }
        }

        const auto frame = adapter.current_frame();
        CHECK(nonblack_pixels(frame) > 0U);

        parity_result result{};
        result.set_name = adapter.set_name();
        result.frame_hash = hash_framebuffer_rgba(frame);
        if (want_audio) {
            result.audio_hash = hash_audio_pcm_s16le(audio_samples);
            result.audio_frames = audio_frames;
            result.sample_rate = sample_rate;
        }
        return result;
    };

    const parity_result first = run_once(collect_audio);
    const parity_result second = run_once(collect_audio);

    INFO("set=" << first.set_name << " frames=" << frames
                << " frame_sha256=" << first.frame_hash
                << " audio_sha256=" << first.audio_hash
                << " audio_frames=" << first.audio_frames
                << " sample_rate=" << first.sample_rate);
    CHECK(second.set_name == first.set_name);
    CHECK(second.frame_hash == first.frame_hash);
    if (expected_frame_hash.has_value()) {
        CHECK(first.frame_hash == *expected_frame_hash);
    }
    if (expected_audio_hash.has_value()) {
        REQUIRE(first.audio_frames > 0U);
        CHECK(first.sample_rate == travrusa::audio_rate_hz);
        CHECK(second.audio_frames == first.audio_frames);
        CHECK(second.sample_rate == first.sample_rate);
        CHECK(second.audio_hash == first.audio_hash);
        CHECK(first.audio_hash == *expected_audio_hash);
    }
}
