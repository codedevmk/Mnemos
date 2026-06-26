#include "irem_m75_adapter.hpp"

#include "file.hpp"
#include "m75_game_manifests.hpp"
#include "zip_archive.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

    namespace irem = mnemos::apps::player::adapters::irem_m75;
    namespace m75 = mnemos::manifests::irem_m75;
    namespace fs = std::filesystem;

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m75_program() {
        std::vector<std::uint8_t> rom(m75::main_rom_size, 0xFFU);
        const std::uint8_t program[] = {0x3EU, 0x42U, 0x32U, 0x00U, 0xE0U, 0x76U};
        std::copy(std::begin(program), std::end(program), rom.begin());
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m75_sound_dac_program() {
        std::vector<std::uint8_t> rom(m75::sound_rom_size, 0x00U);
        rom[0x0000U] = 0x3EU; // LD A,$F0
        rom[0x0001U] = 0xF0U;
        rom[0x0002U] = 0xD3U; // OUT ($82),A
        rom[0x0003U] = static_cast<std::uint8_t>(m75::z80_port_dac);
        rom[0x0004U] = 0x76U; // HALT
        return rom;
    }

    struct temp_set_dir final {
        fs::path path;

        temp_set_dir() {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() /
                   ("mnemos_m75_dac_adapter_test_" + std::to_string(stamp));
            std::error_code ec;
            fs::remove_all(path, ec);
            REQUIRE(fs::create_directories(path));
        }

        temp_set_dir(const temp_set_dir&) = delete;
        temp_set_dir& operator=(const temp_set_dir&) = delete;

        temp_set_dir(temp_set_dir&& other) noexcept : path(std::move(other.path)) {}

        temp_set_dir& operator=(temp_set_dir&& other) noexcept {
            if (this != &other) {
                cleanup();
                path = std::move(other.path);
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

    [[nodiscard]] temp_set_dir make_dac_audio_set() {
        temp_set_dir dir;
        write_text_file(dir.path / "game.toml", R"toml(
[set]
schema = "mnemos-romset/1"
name = "dacmix"
board = "irem_m75"
orientation = "horizontal"

[[region]]
name = "maincpu"
size = 0x030000

[[region.file]]
name = "maincpu.bin"
offset = 0
size = 0x030000

[[region]]
name = "soundcpu"
size = 0x010000

[[region.file]]
name = "soundcpu.bin"
offset = 0
size = 0x010000

[[region]]
name = "samples"
size = 0x010000

[[region.file]]
name = "samples.bin"
offset = 0
size = 0x010000
)toml");
        write_binary_file(dir.path / "maincpu.bin", synthetic_m75_program());
        write_binary_file(dir.path / "soundcpu.bin", synthetic_m75_sound_dac_program());
        write_binary_file(dir.path / "samples.bin",
                          std::vector<std::uint8_t>(m75::sample_rom_size, 0x80U));
        return dir;
    }

    [[nodiscard]] temp_set_dir make_dip_metadata_set() {
        temp_set_dir dir;
        write_text_file(dir.path / "game.toml", R"toml(
[set]
schema = "mnemos-romset/1"
name = "dipmeta"
board = "irem_m75"
orientation = "horizontal"

[[dip]]
bank = "SW1"
name = "Test Lives"
mask = 0x0003
default = 0x0002

[[dip.option]]
label = "2"
value = 0x0002

[[dip.option]]
label = "3"
value = 0x0003

[[dip]]
bank = "SW2"
name = "Test Flip"
mask = 0x0100
default = 0x0000

[[dip.option]]
label = "Off"
value = 0x0100

[[dip.option]]
label = "On"
value = 0x0000

[[region]]
name = "maincpu"
size = 0x030000

[[region.file]]
name = "maincpu.bin"
offset = 0
size = 0x030000

[[region]]
name = "soundcpu"
size = 0x010000

[[region.file]]
name = "soundcpu.bin"
offset = 0
size = 0x010000

[[region]]
name = "samples"
size = 0x010000

[[region.file]]
name = "samples.bin"
offset = 0
size = 0x010000
)toml");
        write_binary_file(dir.path / "maincpu.bin", synthetic_m75_program());
        write_binary_file(dir.path / "soundcpu.bin", synthetic_m75_sound_dac_program());
        write_binary_file(dir.path / "samples.bin",
                          std::vector<std::uint8_t>(m75::sample_rom_size, 0x80U));
        return dir;
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
            return std::tolower(static_cast<unsigned char>(lhs)) ==
                   std::tolower(static_cast<unsigned char>(rhs));
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

    [[nodiscard]] std::set<std::string, std::less<>> embedded_set_names() {
        std::set<std::string, std::less<>> names;
        for (const auto& [set_name, _] : m75::embedded::game_manifests) {
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
                candidates.push_back(it->path());
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

    [[nodiscard]] std::size_t
    validation_issue_count(const mnemos::frontend_sdk::media_capability_info& media) noexcept {
        std::size_t count = 0U;
        for (const auto& image : media.media) {
            count += image.validation_issues.size();
        }
        return count;
    }

    [[nodiscard]] bool spec_has(const irem::irem_m75_adapter& adapter, std::string_view label,
                                std::string_view value) {
        const auto& spec = adapter.system_spec();
        return std::any_of(spec.begin(), spec.end(), [label, value](const auto& field) {
            return field.label == label && field.value == value;
        });
    }

} // namespace

TEST_CASE("irem_m75_adapter boots a synthetic M75 program", "[irem_m75]") {
    irem::irem_m75_adapter adapter(synthetic_m75_program(), "Tiny M75");

    CHECK(adapter.region().frames_per_second_x1000 == m75::frame_rate_x1000);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    CHECK(adapter.session_capabilities().save_state_supported);
    CHECK(adapter.session_capabilities().frame_exact_save_state);
    REQUIRE(adapter.chips().size() == 5U);
    REQUIRE(adapter.memory_views().size() == 5U);

    adapter.step_one_frame();

    CHECK(adapter.frames_stepped() == 1U);
    CHECK(adapter.machine().work_ram[0] == 0x42U);
    CHECK(frame_has_nonblack(adapter.current_frame()));
    CHECK(validation_issue_count(adapter.media_capabilities()) == 0U);

    const auto state = adapter.save_state();
    CHECK_FALSE(state.empty());
}

TEST_CASE("irem_m75_adapter mixes Z80 DAC writes into drained audio", "[irem_m75]") {
    auto set = make_dac_audio_set();
    irem::irem_m75_adapter adapter({}, "DAC mix", nullptr, {}, set.path.string());

    adapter.step_one_frame();

    const auto audio = adapter.drain_audio();
    REQUIRE(audio.frame_count > 0U);
    REQUIRE(audio.samples != nullptr);
    CHECK(std::any_of(audio.samples, audio.samples + audio.frame_count * 2U,
                      [](std::int16_t sample) { return sample != 0; }));
    CHECK(adapter.machine().dac.level() == 0xF0U);
    CHECK(adapter.drain_audio().frame_count == 0U);
}

TEST_CASE("irem_m75_adapter applies manifest DIP defaults and override", "[irem_m75]") {
    const auto set = make_dip_metadata_set();
    irem::irem_m75_adapter adapter({}, "DIP metadata", nullptr, {}, set.path.string());

    CHECK(adapter.dip_switches().size() == 2U);
    CHECK(adapter.machine().dsw1 == 0xFEU);
    CHECK(adapter.machine().dsw2 == 0xFCU);
    CHECK(spec_has(adapter, "DIP switches", "2"));

    irem::irem_m75_adapter overridden({}, "DIP metadata", nullptr,
                                      static_cast<std::uint16_t>(0x1234U), set.path.string());
    CHECK(overridden.dip_switches().size() == 2U);
    CHECK(overridden.machine().dsw1 == 0x34U);
    CHECK(overridden.machine().dsw2 == 0x12U);
    CHECK(spec_has(overridden, "DIP switches", "2"));
}

TEST_CASE("irem_m75_adapter maps service and operator-test inputs to the system port",
          "[irem_m75]") {
    irem::irem_m75_adapter adapter(synthetic_m75_program(), "M75 inputs");

    mnemos::frontend_sdk::controller_state p1{};
    p1.start = true;
    p1.select = true;
    p1.service = true;
    p1.test = true;
    adapter.apply_input(0, p1);

    CHECK(adapter.machine().input_system == 0xCAU);

    p1.service = false;
    p1.test = false;
    p1.mode = true;
    adapter.apply_input(0, p1);

    CHECK(adapter.machine().input_system == 0xEAU);

    mnemos::frontend_sdk::controller_state p2{};
    p2.start = true;
    p2.select = true;
    adapter.apply_input(1, p2);

    CHECK(adapter.machine().input_system == 0xE0U);
}

TEST_CASE("irem_m75_adapter preserves adapter and board state", "[irem_m75]") {
    irem::irem_m75_adapter source(synthetic_m75_program(), "Save M75");
    mnemos::frontend_sdk::controller_state p1{};
    p1.up = true;
    p1.a = true;
    p1.start = true;
    p1.select = true;
    p1.service = true;
    p1.test = true;
    source.apply_input(0, p1);
    REQUIRE(source.machine().input_system == 0xCAU);
    source.step_one_frame();
    source.machine().palette_ram[5] = 0x44U;
    REQUIRE(source.drain_audio().frame_count > 0U);

    const auto state = source.save_state();
    irem::irem_m75_adapter restored(synthetic_m75_program(), "Save M75");
    const auto result = restored.load_state(state);
    REQUIRE(result.ok());

    CHECK(restored.frames_stepped() == 1U);
    CHECK(restored.machine().work_ram[0] == 0x42U);
    CHECK(restored.machine().palette_ram[5] == 0x44U);
    CHECK(restored.machine().input_system == 0xCAU);
    CHECK(restored.drain_audio().frame_count == 0U);
}

TEST_CASE("irem_m75_adapter validates real M75 ROM sets", "[irem_m75][data]") {
    const auto dir_env = environment_value("MNEMOS_M75_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M75_SET_DIR to directories containing the M75 zip/folder corpus");
    }

    const auto sources = index_source_roots(source_roots(dir_env->c_str()));
    const std::set<std::string, std::less<>> expected = embedded_set_names();
    for (const auto& set_id : expected) {
        INFO("set=" << set_id);
        auto found = sources.find(set_id);
        REQUIRE(found != sources.end());

        auto bytes = read_source_bytes(found->second);
        irem::irem_m75_adapter adapter(std::move(bytes), set_id, nullptr, {},
                                       found->second.string());

        CHECK(adapter.set_name() == set_id);
        CHECK(validation_issue_count(adapter.media_capabilities()) == 0U);
        CHECK(adapter.dip_switches().size() == 14U);
        CHECK(adapter.machine().dsw1 == m75::vigilant_dsw1_default);
        CHECK(adapter.machine().dsw2 == m75::vigilant_dsw2_default);
        CHECK(spec_has(adapter, "DIP switches", "14"));

        adapter.step_one_frame();
        CHECK(frame_has_nonblack(adapter.current_frame()));
        CHECK_FALSE(adapter.save_state().empty());
    }
}
