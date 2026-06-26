#include "irem_m84_adapter.hpp"

#include "file.hpp"
#include "m81_game_manifests.hpp"
#include "m84_game_manifests.hpp"
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
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

    namespace irem = mnemos::apps::player::adapters::irem_m84;
    namespace m81 = mnemos::manifests::irem_m81;
    namespace m84 = mnemos::manifests::irem_m84;

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m84_program() {
        std::vector<std::uint8_t> rom(m84::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        const std::vector<std::uint8_t> program{0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U,
                                                0x42U, 0xA2U, 0x00U, 0x00U, 0xF4U};
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
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
        for (const auto& [set_name, _] : m84::embedded::game_manifests) {
            names.emplace(std::string{set_name});
        }
        return names;
    }

    [[nodiscard]] std::optional<std::string>
    identify_m84_source(const std::filesystem::path& path,
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

    [[nodiscard]] bool preferred_m84_source(const std::filesystem::path& candidate,
                                            const std::filesystem::path& existing) {
        std::error_code ec;
        const bool candidate_zip = std::filesystem::is_regular_file(candidate, ec) &&
                                   ends_with_zip(candidate.filename().string());
        ec.clear();
        const bool existing_zip = std::filesystem::is_regular_file(existing, ec) &&
                                  ends_with_zip(existing.filename().string());
        return candidate_zip && !existing_zip;
    }

    [[nodiscard]] std::map<std::string, std::filesystem::path, std::less<>>
    index_source_roots(const std::vector<std::filesystem::path>& roots,
                       const std::set<std::string, std::less<>>& known) {
        std::map<std::string, std::filesystem::path, std::less<>> sources;

        auto maybe_add = [&](const std::filesystem::path& path) {
            auto set_id = identify_m84_source(path, known);
            if (!set_id.has_value()) {
                return;
            }
            std::string id = std::move(*set_id);
            auto existing = sources.find(id);
            if (existing == sources.end()) {
                sources.emplace(std::move(id), path);
            } else if (preferred_m84_source(path, existing->second)) {
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
                candidates.push_back(it->path());
            }
            std::sort(candidates.begin(), candidates.end());
            for (const auto& path : candidates) {
                maybe_add(path);
            }
        }

        return sources;
    }

    [[nodiscard]] std::map<std::string, std::filesystem::path, std::less<>>
    index_m84_source_roots(const std::vector<std::filesystem::path>& roots) {
        return index_source_roots(roots, embedded_set_names());
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

} // namespace

TEST_CASE("irem_m84_adapter boots a synthetic M84 program", "[irem_m84]") {
    irem::irem_m84_adapter adapter(synthetic_m84_program(), "Tiny M84");

    CHECK(adapter.region().frames_per_second_x1000 == m84::frame_rate_x1000);
    CHECK(adapter.current_frame().width == m84::visible_width);
    CHECK(adapter.current_frame().height == m84::visible_height);
    CHECK(adapter.media_capabilities().media.size() == 1U);
    CHECK(validation_issue_count(adapter.media_capabilities()) == 0U);

    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 1U);
    CHECK(adapter.machine().work_ram[0] == 0x42U);
    CHECK(frame_has_nonblack(adapter.current_frame()));

    const auto audio = adapter.drain_audio();
    CHECK(audio.frame_count > 0U);
    CHECK(audio.sample_rate == 55930U);
    CHECK(adapter.drain_audio().frame_count == 0U);
}

TEST_CASE("irem_m84_adapter save state restores board and adapter state", "[irem_m84]") {
    irem::irem_m84_adapter source(synthetic_m84_program(), "Tiny M84");
    mnemos::frontend_sdk::controller_state p1{};
    p1.start = true;
    p1.select = true;
    p1.a = true;
    p1.service = true;
    p1.test = true;
    source.apply_input(0, p1);
    mnemos::frontend_sdk::controller_state p2{};
    p2.start = true;
    p2.mode = true; // legacy service alias
    source.apply_input(1, p2);
    CHECK(source.machine().input_system ==
          static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x02U & ~0x04U & ~0x10U &
                                    ~0x20U & ~0x40U));
    source.step_one_frame();
    REQUIRE(source.drain_audio().frame_count > 0U);

    const std::vector<std::uint8_t> state = source.save_state();
    REQUIRE_FALSE(state.empty());

    irem::irem_m84_adapter restored(synthetic_m84_program(), "Tiny M84");
    const auto result = restored.load_state(state);
    REQUIRE(result.ok());
    CHECK(restored.frames_stepped() == source.frames_stepped());
    CHECK(restored.machine().work_ram[0] == 0x42U);
    CHECK(restored.machine().input_system == source.machine().input_system);
    CHECK(restored.drain_audio().frame_count == 0U);
    restored.step_one_frame();
    CHECK(restored.frames_stepped() == source.frames_stepped() + 1U);
}

TEST_CASE("irem_m84_adapter validates real M84 ROM sets", "[irem_m84][data]") {
    const auto dir_env = environment_value("MNEMOS_M84_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M84_SET_DIR to directories containing the M84 zip/folder corpus");
    }

    const auto roots = source_roots(dir_env->c_str());
    REQUIRE_FALSE(roots.empty());
    const auto sources = index_m84_source_roots(roots);
    const std::set<std::string, std::less<>> parent_names{"hharry"};
    const auto parent_sources = index_source_roots(roots, parent_names);
    REQUIRE_FALSE(m81::game_manifest_toml("hharry").empty());
    REQUIRE(parent_sources.find("hharry") != parent_sources.end());
    const std::filesystem::path parent_path = parent_sources.at("hharry");

    const auto expected_sets = embedded_set_names();
    for (const auto& set_name : expected_sets) {
        INFO("set=" << set_name);
        REQUIRE(sources.find(set_name) != sources.end());
    }
    for (const auto& set_name : expected_sets) {
        INFO("set=" << set_name);
        const std::filesystem::path source_path = sources.at(set_name);
        std::vector<std::vector<std::uint8_t>> supplemental_roms;
        supplemental_roms.push_back(read_source_bytes(parent_path));
        std::vector<std::string> supplemental_paths{parent_path.string()};
        irem::irem_m84_adapter adapter(read_source_bytes(source_path), set_name, nullptr, {},
                                       source_path.string(), std::move(supplemental_roms),
                                       std::move(supplemental_paths));
        CHECK(adapter.set_name() == set_name);
        const bool v35_set = set_name == "gallop" || set_name == "ltswords";
        CHECK(adapter.machine().main_cpu.cpu_model() ==
              (v35_set ? mnemos::chips::cpu::v30::model::v35
                       : mnemos::chips::cpu::v30::model::v30));
        CHECK(validation_issue_count(adapter.media_capabilities()) == 0U);
        adapter.step_one_frame();
        CHECK(adapter.current_frame().width == m84::visible_width);
        CHECK(adapter.current_frame().height == m84::visible_height);
        CHECK(frame_has_nonblack(adapter.current_frame()));
        CHECK_FALSE(adapter.save_state().empty());
    }
}
