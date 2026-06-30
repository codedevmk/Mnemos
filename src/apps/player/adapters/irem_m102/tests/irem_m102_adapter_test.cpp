#include "irem_m102_adapter.hpp"

#include "file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

    namespace fs = std::filesystem;
    namespace irem = mnemos::apps::player::adapters::irem_m102;
    namespace m102 = mnemos::manifests::irem_m102;

    [[nodiscard]] std::vector<std::uint8_t> synthetic_program() {
        std::vector<std::uint8_t> rom(m102::main_rom_size, 0x00U);
        const std::vector<std::uint8_t> program{
            0x3EU, 0x42U, 0x32U, 0x00U, 0xE0U, // LD A,$42 ; LD ($E000),A
            0x3EU, 0x81U, 0x32U, 0x00U, 0xC0U, // LD A,$81 ; LD ($C000),A
            0x3EU, 0x24U, 0x32U, 0x00U, 0xD0U, // LD A,$24 ; LD ($D000),A
            0x3EU, 0x02U, 0xD3U, 0x40U,
            0x3EU, 0x01U, 0xD3U, 0x00U,
            0x3EU, 0x00U, 0xD3U, 0x01U,
            0x3EU, 0x08U, 0xD3U, 0x02U,
            0x3EU, 0x00U, 0xD3U, 0x03U,
            0x3EU, 0x10U, 0xD3U, 0x04U,
            0x3EU, 0xFFU, 0xD3U, 0x05U,
            0x3EU, 0x02U, 0xD3U, 0x06U,
            0x3EU, 0x5AU, 0xD3U, 0x50U,
            0xC3U, 0x31U, 0x00U};
        std::copy(program.begin(), program.end(), rom.begin());
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

    [[nodiscard]] std::size_t
    validation_issue_count(const mnemos::frontend_sdk::media_capability_info& media) {
        std::size_t count = 0U;
        for (const auto& image : media.media) {
            count += image.validation_issues.size();
        }
        return count;
    }

    [[nodiscard]] bool spec_has(const irem::irem_m102_adapter& adapter, std::string_view label,
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
                const auto candidate_path = it->path();
                std::error_code entry_ec;
                if (it->is_directory(entry_ec) &&
                    candidate_path.filename().string() == "name-collisions") {
                    it.disable_recursion_pending();
                    continue;
                }
                if (is_exact_set_path(candidate_path, set_name)) {
                    candidates.push_back(candidate_path);
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

TEST_CASE("irem_m102_adapter boots a synthetic Hill Climber-class program", "[irem_m102]") {
    irem::irem_m102_adapter adapter(synthetic_program(), "Tiny M102");

    CHECK(adapter.region().frames_per_second_x1000 == m102::frame_rate_x1000);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    CHECK(adapter.current_frame().width == m102::visible_width);
    CHECK(adapter.current_frame().height == m102::visible_height);
    CHECK(adapter.session_capabilities().save_state_supported);
    CHECK(adapter.session_capabilities().frame_exact_save_state);
    REQUIRE(adapter.chips().size() == 3U);
    REQUIRE(adapter.memory_views().size() == 3U);
    CHECK(spec_has(adapter, "Board", "Irem M102"));

    adapter.step_one_frame();

    CHECK(adapter.frames_stepped() == 1U);
    CHECK(adapter.machine().work_ram[0] == 0x42U);
    CHECK(adapter.machine().video_ram[0] == 0x81U);
    CHECK(adapter.machine().medal_ram[0] == 0x24U);
    CHECK(adapter.machine().bank_select == 0x02U);
    CHECK(adapter.machine().output_latch == 0x5AU);
    CHECK(adapter.machine().ga20_key_on_count > 0U);
    CHECK(framebuffer_has_nonblack(adapter.current_frame()));
    CHECK(validation_issue_count(adapter.media_capabilities()) == 0U);
    CHECK_FALSE(adapter.save_state().empty());
}

TEST_CASE("irem_m102_adapter maps medal controls and preserves save state", "[irem_m102]") {
    irem::irem_m102_adapter source(synthetic_program(), "Save M102");
    mnemos::frontend_sdk::controller_state p1{};
    p1.start = true;
    p1.select = true;
    p1.a = true;
    p1.b = true;
    p1.c = true;
    p1.service = true;
    source.apply_input(0, p1);
    CHECK(source.machine().input0 ==
          static_cast<std::uint8_t>(m102::input0_default & ~m102::input_coin1_bit &
                                    ~m102::input_service_bit & ~m102::input_start1_bit));
    CHECK(source.machine().input1 ==
          static_cast<std::uint8_t>(m102::input1_default & ~m102::input_button1_bit &
                                    ~m102::input_button2_bit & ~m102::input_button3_bit));
    source.step_one_frame();
    source.machine().video_ram[3] = 0x44U;

    const auto state = source.save_state();
    irem::irem_m102_adapter restored(synthetic_program(), "Save M102");
    const auto result = restored.load_state(state);
    REQUIRE(result.ok());

    CHECK(restored.frames_stepped() == 1U);
    CHECK(restored.machine().work_ram[0] == 0x42U);
    CHECK(restored.machine().video_ram[3] == 0x44U);
    CHECK(restored.machine().input0 == source.machine().input0);
    CHECK(restored.machine().input1 == source.machine().input1);
}

TEST_CASE("irem_m102_adapter drains GA20 first-pass audio", "[irem_m102]") {
    irem::irem_m102_adapter adapter(synthetic_program(), "Audio M102");
    adapter.step_one_frame();

    const auto audio = adapter.drain_audio();
    REQUIRE(audio.frame_count > 0U);
    REQUIRE(audio.samples != nullptr);
    CHECK(audio.sample_rate == m102::audio_rate_hz);
    CHECK(std::any_of(audio.samples, audio.samples + audio.frame_count * 2U,
                      [](std::int16_t sample) { return sample != 0; }));
    CHECK(adapter.drain_audio().frame_count == 0U);
}

TEST_CASE("irem_m102_adapter validates real Hill Climber ROM set", "[irem_m102][data]") {
    const auto dir_env = environment_value("MNEMOS_M102_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M102_SET_DIR to directories containing the M102 zip/folder corpus");
    }

    const auto roots = source_roots(dir_env->c_str());
    REQUIRE_FALSE(roots.empty());
    const auto source = find_exact_source(roots, "hclimber");
    REQUIRE(source.has_value());

    irem::irem_m102_adapter adapter(read_source_bytes(*source), "hclimber", nullptr, {},
                                    source->string());

    CHECK(adapter.set_name() == "hclimber");
    CHECK(spec_has(adapter, "Board", "Irem M102"));
    CHECK(validation_issue_count(adapter.media_capabilities()) == 0U);

    for (std::uint32_t frame = 0; frame < 90U; ++frame) {
        adapter.step_one_frame();
    }
    CHECK(adapter.current_frame().width == m102::visible_width);
    CHECK(adapter.current_frame().height == m102::visible_height);
    CHECK(framebuffer_has_nonblack(adapter.current_frame()));
    CHECK_FALSE(adapter.save_state().empty());
}
