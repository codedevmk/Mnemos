#include "irem_m78_adapter.hpp"

#include "file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    namespace irem = mnemos::apps::player::adapters::irem_m78;
    namespace m78 = mnemos::manifests::irem_m78;

    struct temp_set_dir final {
        fs::path path;

        temp_set_dir() {
            const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() / ("mnemos_m78_adapter_" + std::to_string(ticks));
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

        ~temp_set_dir() {
            cleanup();
        }

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

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m78_program() {
        std::vector<std::uint8_t> rom(m78::main_rom_size, 0x00U);
        const std::vector<std::uint8_t> program{
            0x3EU, 0x42U, 0x32U, 0x00U, 0xE0U,
            0x3EU, 0x11U, 0x01U, 0x00U, 0x80U, 0xEDU, 0x79U,
            0x3EU, 0x7CU, 0x01U, 0x00U, 0x20U, 0xEDU, 0x79U,
            0xC3U, 0x00U, 0x00U};
        std::copy(program.begin(), program.end(), rom.begin());
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m78_sound_program() {
        std::vector<std::uint8_t> rom(m78::audio_rom_size, 0x00U);
        const std::vector<std::uint8_t> program{
            0xDBU, 0x80U, 0xD3U, 0x83U,
            0x3EU, 0xF0U, 0xD3U, 0x82U,
            0x76U};
        std::copy(program.begin(), program.end(), rom.begin());
        return rom;
    }

    [[nodiscard]] temp_set_dir make_audio_metadata_set() {
        temp_set_dir dir;
        write_text_file(dir.path / "game.toml", R"toml(
[set]
schema = "mnemos-romset/1"
name = "audiometa"
board = "irem_m78"
orientation = "vertical"

[[region]]
name = "maincpu"
size = 0x10000

[[region.file]]
name = "maincpu.bin"
offset = 0
size = 0x10000

[[region]]
name = "audiocpu"
size = 0x10000

[[region.file]]
name = "audiocpu.bin"
offset = 0
size = 0x10000
)toml");
        write_binary_file(dir.path / "maincpu.bin", synthetic_m78_program());
        write_binary_file(dir.path / "audiocpu.bin", synthetic_m78_sound_program());
        return dir;
    }

    [[nodiscard]] bool frame_has_nonblack(const mnemos::chips::frame_buffer_view& view) {
        for (std::uint32_t y = 0; y < view.height; ++y) {
            for (std::uint32_t x = 0; x < view.width; ++x) {
                if (view.pixels[static_cast<std::size_t>(y) * view.effective_stride() + x] !=
                    0U) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t
    validation_issue_count(const mnemos::frontend_sdk::media_capability_info& media) {
        std::size_t count = 0U;
        for (const auto& image : media.media) {
            count += image.validation_issues.size();
        }
        return count;
    }

    [[nodiscard]] bool spec_has(const irem::irem_m78_adapter& adapter, std::string_view label,
                                std::string_view value) {
        const auto& spec = adapter.system_spec();
        return std::any_of(spec.begin(), spec.end(), [label, value](const auto& field) {
            return field.label == label && field.value == value;
        });
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

    [[nodiscard]] std::vector<fs::path> source_roots(const char* env_value) {
        std::vector<fs::path> roots;
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

    [[nodiscard]] bool lowercase_equals(std::string_view lhs, std::string_view rhs) {
        return lhs.size() == rhs.size() &&
               std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char l, char r) {
                   return std::tolower(static_cast<unsigned char>(l)) ==
                          std::tolower(static_cast<unsigned char>(r));
               });
    }

    [[nodiscard]] bool is_exact_set_path(const fs::path& path, std::string_view set_name) {
        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            return lowercase_equals(path.filename().string(), set_name);
        }
        if (!fs::is_regular_file(path, ec) || !lowercase_equals(path.extension().string(), ".zip")) {
            return false;
        }
        return lowercase_equals(path.stem().string(), set_name);
    }

    [[nodiscard]] std::optional<fs::path> find_exact_source(const std::vector<fs::path>& roots,
                                                            std::string_view set_name) {
        std::vector<fs::path> candidates;
        for (const auto& root : roots) {
            std::error_code ec;
            if (is_exact_set_path(root, set_name)) {
                candidates.push_back(root);
            }
            if (!fs::is_directory(root, ec)) {
                continue;
            }
            for (fs::recursive_directory_iterator it{root, ec}, end; !ec && it != end;
                 it.increment(ec)) {
                if (is_exact_set_path(it->path(), set_name)) {
                    candidates.push_back(it->path());
                }
            }
        }
        std::sort(candidates.begin(), candidates.end());
        if (candidates.empty()) {
            return std::nullopt;
        }
        return candidates.front();
    }

    [[nodiscard]] std::vector<std::uint8_t> read_source_bytes(const fs::path& source) {
        std::error_code ec;
        if (fs::is_directory(source, ec)) {
            return {};
        }
        auto bytes = mnemos::io::read_file(source.string());
        REQUIRE(bytes.has_value());
        return std::move(*bytes);
    }

} // namespace

TEST_CASE("irem_m78_adapter boots a synthetic M78 program", "[irem_m78]") {
    irem::irem_m78_adapter adapter(synthetic_m78_program(), "Tiny M78");

    CHECK(adapter.region().frames_per_second_x1000 == m78::frame_rate_x1000);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    CHECK(adapter.session_capabilities().save_state_supported);
    CHECK(adapter.session_capabilities().frame_exact_save_state);
    REQUIRE(adapter.chips().size() == 5U);
    REQUIRE(adapter.memory_views().size() == 8U);
    CHECK(spec_has(adapter, "Board", "Irem M78"));

    adapter.step_one_frame();

    CHECK(adapter.frames_stepped() == 1U);
    CHECK(adapter.machine().work_ram[0] == 0x42U);
    CHECK(adapter.machine().tile_ram[1][0] == 0x11U);
    CHECK(adapter.machine().sound_latch == 0x7CU);
    CHECK(frame_has_nonblack(adapter.current_frame()));
    CHECK(validation_issue_count(adapter.media_capabilities()) == 0U);
    CHECK_FALSE(adapter.save_state().empty());
}

TEST_CASE("irem_m78_adapter mixes Z80 DAC writes into drained audio", "[irem_m78]") {
    auto set = make_audio_metadata_set();
    irem::irem_m78_adapter adapter({}, "DAC mix", nullptr, {}, set.path.string());

    adapter.step_one_frame();

    const auto audio = adapter.drain_audio();
    REQUIRE(audio.frame_count > 0U);
    REQUIRE(audio.samples != nullptr);
    CHECK(std::any_of(audio.samples, audio.samples + audio.frame_count * 2U,
                      [](std::int16_t sample) { return sample != 0; }));
    CHECK(adapter.machine().dac.level() == 0xF0U);
    CHECK(adapter.drain_audio().frame_count == 0U);
}

TEST_CASE("irem_m78_adapter maps BJ92 stand/hit controls and preserves save state",
          "[irem_m78]") {
    irem::irem_m78_adapter source(synthetic_m78_program(), "Save M78");
    mnemos::frontend_sdk::controller_state p1{};
    p1.start = true;
    p1.a = true;
    p1.b = true;
    p1.select = true;
    p1.service = true;
    source.apply_input(0, p1);
    REQUIRE(source.machine().input0 == 0xD8U);
    REQUIRE(source.machine().input1 == 0x23U);
    source.step_one_frame();
    source.machine().vregs[3] = 0x44U;

    const auto state = source.save_state();
    irem::irem_m78_adapter restored(synthetic_m78_program(), "Save M78");
    const auto result = restored.load_state(state);
    REQUIRE(result.ok());

    CHECK(restored.frames_stepped() == 1U);
    CHECK(restored.machine().work_ram[0] == 0x42U);
    CHECK(restored.machine().vregs[3] == 0x44U);
    CHECK(restored.machine().input0 == 0xD8U);
    CHECK(restored.machine().input1 == 0x23U);
}

TEST_CASE("irem_m78_adapter validates real BJ92 ROM set", "[irem_m78][data]") {
    const auto dir_env = environment_value("MNEMOS_M78_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M78_SET_DIR to directories containing the M78 zip/folder corpus");
    }

    const auto roots = source_roots(dir_env->c_str());
    REQUIRE_FALSE(roots.empty());
    const auto source = find_exact_source(roots, "bj92");
    REQUIRE(source.has_value());

    irem::irem_m78_adapter adapter(read_source_bytes(*source), "bj92", nullptr, {},
                                   source->string());

    CHECK(adapter.set_name() == "bj92");
    CHECK(spec_has(adapter, "Board", "Irem M78"));
    CHECK(validation_issue_count(adapter.media_capabilities()) == 0U);

    adapter.step_one_frame();
    CHECK(frame_has_nonblack(adapter.current_frame()));
    CHECK_FALSE(adapter.save_state().empty());
}
