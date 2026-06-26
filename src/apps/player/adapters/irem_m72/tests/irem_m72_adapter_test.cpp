#include "irem_m72_adapter.hpp"

#include "adapter_registry.hpp"
#include "crc32.hpp"
#include "file.hpp"
#include "m72_game_manifests.hpp"
#include "rom_set_toml.hpp"
#include "sha256.hpp"
#include "zip_archive.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

    using mnemos::apps::player::adapters::irem_m72::irem_m72_adapter;

    struct m72_source final {
        std::vector<std::uint8_t> bytes;
        std::string path;
    };

    // A bare V30 program image: far jump at the reset vector into a handler
    // that writes a marker into work RAM and halts.
    [[nodiscard]] std::vector<std::uint8_t> make_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m72::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        const std::vector<std::uint8_t> program{
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000 (the base map's work RAM)
            0x8EU, 0xD8U,        // MOV DS,AX
            0xB0U, 0x42U,        // MOV AL,42
            0xA2U, 0x00U, 0x00U, // MOV [0000],AL
            0xF4U,               // HLT
        };
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] const char* opt_env(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in test data path
#endif
        const char* value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        return (value != nullptr && *value != '\0') ? value : nullptr;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_decl
    require_embedded_decl(std::string_view set_name) {
        const std::string_view toml = mnemos::manifests::irem_m72::game_manifest_toml(set_name);
        REQUIRE_FALSE(toml.empty());
        const auto parsed = mnemos::manifests::common::parse_rom_set_decl(toml, "embedded");
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": " << error.message);
        }
        REQUIRE(parsed.ok());
        REQUIRE(parsed.value.has_value());
        return *parsed.value;
    }

    [[nodiscard]] bool is_directory_path(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::is_directory(path, ec);
    }

    [[nodiscard]] m72_source load_m72_source(const std::filesystem::path& path) {
        if (is_directory_path(path)) {
            return {.bytes = {}, .path = path.string()};
        }
        auto bytes = mnemos::io::read_file(path.string());
        REQUIRE(bytes.has_value());
        return {.bytes = std::move(*bytes), .path = path.string()};
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

    [[nodiscard]] std::optional<std::string>
    single_nested_zip_set_id(const std::filesystem::path& path) {
        auto bytes = mnemos::io::read_file(path.string());
        if (!bytes.has_value()) {
            return std::nullopt;
        }
        auto archive = mnemos::compression::zip_archive::open(*bytes);
        if (!archive.has_value()) {
            return std::nullopt;
        }

        const mnemos::compression::zip_entry* nested = nullptr;
        std::size_t file_count = 0U;
        for (const auto& entry : archive->entries()) {
            if (entry.name.empty() || entry.name.back() == '/') {
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

    [[nodiscard]] std::vector<std::filesystem::path> m72_source_roots(const char* env_value) {
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

    [[nodiscard]] std::map<std::string, std::filesystem::path, std::less<>>
    index_m72_source_roots(const std::vector<std::filesystem::path>& roots) {
        std::map<std::string, bool, std::less<>> known;
        for (const auto& [set_name, _] : mnemos::manifests::irem_m72::embedded::game_manifests) {
            known.emplace(std::string{set_name}, true);
        }

        struct indexed_source final {
            std::filesystem::path path;
            int rank{};
        };

        std::map<std::string, indexed_source, std::less<>> indexed;
        auto maybe_add = [&](std::string set_id, const std::filesystem::path& path, int rank) {
            if (known.find(set_id) == known.end()) {
                return;
            }
            const auto existing = indexed.find(set_id);
            if (existing == indexed.end() || rank < existing->second.rank) {
                indexed[std::move(set_id)] = indexed_source{.path = path, .rank = rank};
            }
        };

        for (const auto& root : roots) {
            std::error_code ec;
            if (!std::filesystem::is_directory(root, ec)) {
                continue;
            }

            std::vector<std::filesystem::path> candidates;
            for (std::filesystem::recursive_directory_iterator it{
                     root, std::filesystem::directory_options::skip_permission_denied, ec},
                 end;
                 !ec && it != end; it.increment(ec)) {
                if (!it->is_directory(ec) && !it->is_regular_file(ec)) {
                    continue;
                }
                candidates.push_back(it->path());
            }
            std::sort(candidates.begin(), candidates.end());

            for (const auto& path : candidates) {
                if (std::filesystem::is_directory(path, ec)) {
                    maybe_add(path.filename().string(), path, 2);
                    continue;
                }
                if (!std::filesystem::is_regular_file(path, ec) ||
                    !ends_with_zip(path.filename().string())) {
                    continue;
                }
                const std::string stem = path.stem().string();
                if (known.find(stem) != known.end()) {
                    maybe_add(stem, path, 0);
                    continue;
                }
                if (auto nested_set = single_nested_zip_set_id(path)) {
                    maybe_add(std::move(*nested_set), path, 1);
                }
            }
        }

        std::map<std::string, std::filesystem::path, std::less<>> sources;
        for (const auto& [set_id, source] : indexed) {
            sources.emplace(set_id, source.path);
        }
        return sources;
    }

    [[nodiscard]] std::vector<std::pair<std::string, std::vector<std::uint8_t>>>
    placeholder_entries_for(const mnemos::manifests::common::rom_set_decl& decl,
                            std::uint8_t fill) {
        std::map<std::string, std::size_t, std::less<>> sizes;
        for (const auto& region : decl.regions) {
            for (const auto& file : region.files) {
                const std::size_t size = file.size == 0U ? 1U : file.size;
                auto [it, inserted] = sizes.emplace(file.name, size);
                if (!inserted) {
                    it->second = std::max(it->second, size);
                }
            }
        }

        std::vector<std::pair<std::string, std::vector<std::uint8_t>>> entries;
        entries.reserve(sizes.size());
        for (const auto& [name, size] : sizes) {
            entries.emplace_back(name, std::vector<std::uint8_t>(size, fill));
        }
        return entries;
    }

    [[nodiscard]] bool
    has_only_crc_issues(const std::vector<mnemos::manifests::common::rom_load_issue>& issues) {
        return !issues.empty() && std::all_of(issues.begin(), issues.end(), [](const auto& issue) {
            return issue.message.find("crc32 mismatch") != std::string::npos;
        });
    }

    [[nodiscard]] bool frame_has_variation(const mnemos::chips::frame_buffer_view& view) {
        if (view.pixels == nullptr || view.width == 0U || view.height == 0U) {
            return false;
        }
        const std::uint32_t first = view.pixels[0];
        const std::uint32_t stride = view.effective_stride();
        for (std::uint32_t y = 0; y < view.height; ++y) {
            const std::uint32_t* row = view.pixels + y * stride;
            for (std::uint32_t x = 0; x < view.width; ++x) {
                if (row[x] != first) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t nonzero_count(std::span<const std::uint8_t> bytes) {
        return static_cast<std::size_t>(
            std::count_if(bytes.begin(), bytes.end(), [](std::uint8_t b) { return b != 0U; }));
    }

    [[nodiscard]] std::size_t distinct_pixel_count(const mnemos::chips::frame_buffer_view& view) {
        if (view.pixels == nullptr || view.width == 0U || view.height == 0U) {
            return 0U;
        }
        std::vector<std::uint32_t> pixels;
        pixels.reserve(static_cast<std::size_t>(view.width) * view.height);
        const std::uint32_t stride = view.effective_stride();
        for (std::uint32_t y = 0; y < view.height; ++y) {
            const std::uint32_t* row = view.pixels + y * stride;
            for (std::uint32_t x = 0; x < view.width; ++x) {
                pixels.push_back(row[x]);
            }
        }
        std::sort(pixels.begin(), pixels.end());
        return static_cast<std::size_t>(std::unique(pixels.begin(), pixels.end()) - pixels.begin());
    }

    [[nodiscard]] bool audio_chunk_has_nonzero_pcm(
        const mnemos::frontend_sdk::audio_chunk& chunk) noexcept {
        if (chunk.samples == nullptr || chunk.frame_count == 0U) {
            return false;
        }
        const std::uint32_t sample_count = chunk.frame_count * 2U;
        for (std::uint32_t i = 0; i < sample_count; ++i) {
            if (chunk.samples[i] != 0) {
                return true;
            }
        }
        return false;
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

    template <std::size_t N>
    void record_recent_pc(std::array<std::uint32_t, N>& pcs, std::size_t& count,
                          std::size_t& cursor, std::uint32_t pc) noexcept {
        pcs[cursor] = pc;
        cursor = (cursor + 1U) % N;
        count = std::min(count + 1U, N);
    }

    template <std::size_t N>
    [[nodiscard]] std::string recent_pc_string(const std::array<std::uint32_t, N>& pcs,
                                               std::size_t count, std::size_t cursor) {
        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t index = (cursor + N - count + i) % N;
            if (i != 0U) {
                out << ' ';
            }
            out << "0x" << std::setw(5) << pcs[index];
        }
        return out.str();
    }

    [[nodiscard]] int env_positive_int(const char* name, int fallback) {
        if (const char* value = opt_env(name)) {
            const int parsed = std::atoi(value);
            if (parsed > 0) {
                return parsed;
            }
        }
        return fallback;
    }

} // namespace

TEST_CASE("irem_m72_adapter boots a bare program through the registry", "[irem_m72][adapter]") {
    mnemos::frontend_sdk::adapter_options options{};
    options.rom = make_program();
    options.display_name = "smoke";
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 2U);
    CHECK(adapter.machine().video.frame_index() == 2U);
    CHECK(adapter.machine().work_ram[0] == 0x42U);

    const auto frame = adapter.current_frame();
    CHECK(frame.width == 384U);
    CHECK(frame.height == 256U);
    CHECK(adapter.region().frames_per_second_x1000 == 55018U);
    const auto chips = adapter.chips();
    REQUIRE(chips.size() == 6U);
    CHECK(chips[0]->metadata().part_number == "m72_video");
    CHECK(chips[1]->metadata().part_number == "v30");
    CHECK(chips[2]->metadata().part_number == "Z80");
    CHECK(chips[3]->metadata().part_number == "8259A");
    CHECK(chips[4]->metadata().part_number == "ym2151");
    CHECK(chips[5]->metadata().part_number == "dac8");
    CHECK(adapter.system_spec().size() == 3U);
    CHECK(adapter.system_spec()[1].value == "Irem M72");
    CHECK(adapter.system_spec()[2].value == "smoke");
    CHECK(adapter.set_name().empty());

    const auto& session = adapter.session_capabilities();
    REQUIRE(session.input_ports.size() == 2U);
    CHECK(session.input_ports[0].format == mnemos::frontend_sdk::input_device_format::arcade_panel);
    CHECK(session.input_ports[1].device_id == "irem_m72.panel.p2");
    CHECK(session.deterministic_frame_input);
    CHECK(session.save_state_supported);
    CHECK(session.frame_exact_save_state);

    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 1U);
    CHECK(media.media[0].id == "rom_set");
    CHECK(media.media[0].label == "smoke");
    CHECK(media.media[0].byte_count == mnemos::manifests::irem_m72::main_rom_size);
    CHECK(media.media[0].hash_algorithm == mnemos::frontend_sdk::media_hash_algorithm::crc32);
    CHECK(media.media[0].full_hash.size() == 8U);
    CHECK(media.media[0].provider_id == "irem_m72.adapter");
}

TEST_CASE("irem_m72_adapter publishes board RAM as system memory views", "[irem_m72][adapter]") {
    namespace m72 = mnemos::manifests::irem_m72;

    irem_m72_adapter adapter(make_program());

    const auto find_view =
        [&](std::string_view name) -> const mnemos::instrumentation::memory_view* {
        for (const auto* view : adapter.memory_views()) {
            if (view != nullptr && view->name() == name) {
                return view;
            }
        }
        return nullptr;
    };
    const auto expect_view = [&](std::string_view name, std::size_t size) {
        const auto* view = find_view(name);
        REQUIRE(view != nullptr);
        CHECK(view->bytes().size() == size);
    };

    CHECK(adapter.memory_views().size() == 8U);
    expect_view("work_ram", m72::work_ram_size);
    expect_view("sound_ram", m72::sound_ram_size);
    expect_view("sprite_ram", m72::sprite_ram_size);
    expect_view("palette_a", m72::palette_size);
    expect_view("palette_b", m72::palette_size);
    expect_view("vram_a", m72::vram_size);
    expect_view("vram_b", m72::vram_size);
    expect_view("mcu_shared_ram", m72::mcu_shared_ram_size);
}

TEST_CASE("irem_m72_adapter maps pads onto the board's input bytes", "[irem_m72][adapter]") {
    irem_m72_adapter adapter(make_program());

    mnemos::frontend_sdk::controller_state p1{};
    p1.up = true;
    p1.a = true;
    p1.start = true;
    p1.select = true; // coin 1
    p1.service = true;
    p1.test = true;
    adapter.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.right = true;
    p2.start = true;
    p2.mode = true; // legacy service 2 alias
    adapter.apply_input(1, p2);

    auto& machine = adapter.machine();
    // Hardware layout: up = bit 3, button 1 = bit 7; right = bit 0.
    CHECK(machine.input_p1 == static_cast<std::uint8_t>(0xFFU & ~0x08U & ~0x80U));
    CHECK(machine.input_p2 == static_cast<std::uint8_t>(0xFFU & ~0x01U));
    // start1/start2 (bits 0/1), coin1 (bit 2), service1/2 (bits 4/5),
    // and operator test (bit 6) held low.
    CHECK(machine.input_system ==
          static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x02U & ~0x04U & ~0x10U & ~0x20U & ~0x40U));

    adapter.apply_input(2, p1); // out-of-range port ignored
    CHECK(machine.input_p2 == static_cast<std::uint8_t>(0xFFU & ~0x01U));
}

TEST_CASE("irem_m72_adapter drains YM2151-clocked audio frames", "[irem_m72][adapter]") {
    irem_m72_adapter adapter(make_program());
    adapter.step_one_frame();

    const auto chunk = adapter.drain_audio();
    // The power-on frame completes at the vblank line (256 of 284 lines in),
    // so the first drain is ~916 stereo frames; steady-state frames yield
    // ~1016 (581632 master cycles -> ~65062 YM2151 clocks, one frame per 64).
    CHECK(chunk.frame_count > 900U);
    CHECK(chunk.frame_count < 1050U);
    CHECK(chunk.sample_rate == 55930U);
    REQUIRE(chunk.samples != nullptr);
    for (std::uint32_t i = 0; i < chunk.frame_count * 2U; ++i) {
        if (chunk.samples[i] != 0) {
            FAIL("expected silence with no notes keyed on");
        }
    }

    // Nothing stepped since the last drain: nothing further is due.
    CHECK(adapter.drain_audio().frame_count == 0U);
}

TEST_CASE("irem_m72_adapter mixes DAC writes at sound sample boundaries", "[irem_m72][adapter]") {
    irem_m72_adapter adapter(make_program());
    auto& machine = adapter.machine();

    machine.record_dac_write(0xC0U); // sound clock 0: affects the first sample.
    machine.sound_cpu.tick(64U);
    machine.fm.tick(64U);
    machine.record_dac_write(0x80U); // sound clock 64: next sample boundary.

    const auto first = adapter.drain_audio();
    REQUIRE(first.frame_count == 1U);
    REQUIRE(first.samples != nullptr);
    CHECK(first.samples[0] == (0xC0 - 0x80) * 64);
    CHECK(first.samples[1] == (0xC0 - 0x80) * 64);
    REQUIRE(machine.dac_write_events.size() == 1U);
    CHECK(machine.dac_write_events[0].sound_clock == 64U);

    machine.sound_cpu.tick(64U);
    machine.fm.tick(64U);
    const auto second = adapter.drain_audio();
    REQUIRE(second.frame_count == 1U);
    REQUIRE(second.samples != nullptr);
    CHECK(second.samples[0] == 0);
    CHECK(second.samples[1] == 0);
    CHECK(machine.dac_write_events.empty());
}

TEST_CASE("irem_m72_adapter applies the DIP override and reports orientation",
          "[irem_m72][adapter]") {
    mnemos::frontend_sdk::adapter_options options{};
    options.rom = make_program();
    options.dip_override = std::uint16_t{0xA5C3U};
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);
    CHECK(adapter.machine().dip_switches == 0xA5C3U);

    // Horizontal by default (R-Type); vertical games flip it from driver
    // metadata and the frontend rotates presentation.
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    adapter.set_orientation(mnemos::frontend_sdk::display_orientation::vertical);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
}

namespace {

    void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8U));
    }

    void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
        put16(out, static_cast<std::uint16_t>(v));
        put16(out, static_cast<std::uint16_t>(v >> 16U));
    }

    // Minimal STORED-method zip over the given entries (CRC fields zeroed;
    // the reader does not verify them).
    [[nodiscard]] std::vector<std::uint8_t>
    make_stored_zip(const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& entries) {
        std::vector<std::uint8_t> out;
        struct central final {
            std::string name;
            std::uint32_t size;
            std::uint32_t local_offset;
        };
        std::vector<central> directory;
        for (const auto& [name, data] : entries) {
            const auto local_offset = static_cast<std::uint32_t>(out.size());
            const auto size = static_cast<std::uint32_t>(data.size());
            put32(out, 0x04034B50U);
            put16(out, 20U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, 0U); // crc (unchecked by the reader)
            put32(out, size);
            put32(out, size);
            put16(out, static_cast<std::uint16_t>(name.size()));
            put16(out, 0U);
            out.insert(out.end(), name.begin(), name.end());
            out.insert(out.end(), data.begin(), data.end());
            directory.push_back({name, size, local_offset});
        }
        const auto cd_offset = static_cast<std::uint32_t>(out.size());
        for (const central& c : directory) {
            put32(out, 0x02014B50U);
            put16(out, 20U);
            put16(out, 20U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, 0U);
            put32(out, c.size);
            put32(out, c.size);
            put16(out, static_cast<std::uint16_t>(c.name.size()));
            put16(out, 0U);
            put16(out, 0U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, c.local_offset);
            out.insert(out.end(), c.name.begin(), c.name.end());
        }
        const auto cd_size = static_cast<std::uint32_t>(out.size()) - cd_offset;
        put32(out, 0x06054B50U);
        put16(out, 0U);
        put16(out, 0U);
        put16(out, static_cast<std::uint16_t>(directory.size()));
        put16(out, static_cast<std::uint16_t>(directory.size()));
        put32(out, cd_size);
        put32(out, cd_offset);
        put16(out, 0U);
        return out;
    }

    void write_directory_rom_set(
        const std::filesystem::path& dir,
        const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& entries) {
        REQUIRE((std::filesystem::create_directories(dir) || std::filesystem::exists(dir)));
        for (const auto& [name, data] : entries) {
            REQUIRE(mnemos::io::write_file((dir / name).string(), data));
        }
    }

    [[nodiscard]] std::vector<std::uint8_t> make_crc32_blob(std::size_t size,
                                                            std::array<std::uint8_t, 4> suffix,
                                                            std::uint32_t expected_crc) {
        REQUIRE(size >= suffix.size());
        std::vector<std::uint8_t> bytes(size, 0x00U);
        std::copy(suffix.begin(), suffix.end(), bytes.end() - static_cast<std::ptrdiff_t>(4U));
        REQUIRE(mnemos::security::cryptography::crc32(bytes) == expected_crc);
        return bytes;
    }

} // namespace

TEST_CASE("irem_m72_adapter maps soundcpu.bin from a development zip", "[irem_m72][adapter]") {
    namespace m72 = mnemos::manifests::irem_m72;

    std::vector<std::uint8_t> sound_rom(m72::sound_rom_size, 0x00U);
    // LD A,66; LD (F010),A; HALT
    const std::vector<std::uint8_t> program{0x3EU, 0x66U, 0x32U, 0x10U, 0xF0U, 0x76U};
    for (std::size_t i = 0; i < program.size(); ++i) {
        sound_rom[i] = program[i];
    }
    const auto zip = make_stored_zip({
        {"maincpu.bin", make_program()},
        {"soundcpu.bin", sound_rom},
    });

    irem_m72_adapter adapter(zip, "dev-sound-rom");
    REQUIRE(adapter.machine().sound_rom_present);
    CHECK_FALSE(adapter.machine().sound_cpu.reset_line_held());

    adapter.step_one_frame();
    CHECK(adapter.machine().sound_ram[0xF010U] == 0x66U);
}

TEST_CASE("irem_m72_adapter maps optional samples and mcu bins from a development zip",
          "[irem_m72][adapter]") {
    namespace m72 = mnemos::manifests::irem_m72;

    std::vector<std::uint8_t> sound_rom(m72::sound_rom_size, 0x00U);
    // LD A,02; OUT (80),A; LD A,00; OUT (81),A; IN A,(84); LD (F010),A; HALT
    const std::vector<std::uint8_t> sound_program{
        0x3EU, 0x02U, 0xD3U, 0x80U, 0x3EU, 0x00U, 0xD3U,
        0x81U, 0xDBU, 0x84U, 0x32U, 0x10U, 0xF0U, 0x76U,
    };
    for (std::size_t i = 0; i < sound_program.size(); ++i) {
        sound_rom[i] = sound_program[i];
    }

    std::vector<std::uint8_t> samples{0x11U, 0x22U, 0x99U, 0x44U};
    const std::vector<std::uint8_t> mcu_program{
        0x90U, 0x00U, 0x02U, // MOV DPTR,#0002
        0xE0U,               // MOVX A,@DPTR
        0x24U, 0x01U,        // ADD A,#1
        0xF0U,               // MOVX @DPTR,A
        0x80U, 0xFEU,        // SJMP $
    };
    const auto zip = make_stored_zip({
        {"maincpu.bin", make_program()},
        {"soundcpu.bin", sound_rom},
        {"samples.bin", samples},
        {"mcu.bin", mcu_program},
    });

    irem_m72_adapter adapter(zip, "dev-optional-regions");
    REQUIRE(adapter.machine().sound_rom_present);
    REQUIRE(adapter.machine().mcu_present);
    REQUIRE(adapter.chips().size() == 7U);
    CHECK(adapter.chips().back()->metadata().part_number == "mcs51");

    adapter.machine().main_to_mcu = 0x20U;
    adapter.step_one_frame();
    CHECK(adapter.machine().sound_ram[0xF010U] == 0x99U);
    CHECK(adapter.machine().mcu_to_main == 0x21U);
}

TEST_CASE("irem_m72_adapter does not schedule a missing declarative MCU dump",
          "[irem_m72][adapter]") {
    std::vector<std::uint8_t> program(mnemos::manifests::irem_m72::main_rom_size, 0xFFU);
    program[0xFFFF0U] = 0xEAU; // JMP 0000:0200
    program[0xFFFF1U] = 0x00U;
    program[0xFFFF2U] = 0x02U;
    program[0xFFFF3U] = 0x00U;
    program[0xFFFF4U] = 0x00U;
    const std::vector<std::uint8_t> body{
        0xB8U, 0x00U, 0xA0U, // MOV AX,A000
        0x8EU, 0xD8U,        // MOV DS,AX
        0xE4U, 0xC0U,        // IN AL,C0
        0xA2U, 0x10U, 0x00U, // MOV [0010],AL
        0xF4U,               // HLT
    };
    for (std::size_t i = 0; i < body.size(); ++i) {
        program[0x200U + i] = body[i];
    }

    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "bchopper"
board = "irem_m72"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "mcu"
size = 0x1000

[[region.file]]
name = "missing_i8751.bin"
offset = 0
size = 0x1000
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", program},
    });

    irem_m72_adapter adapter(zip, "bchopper-missing-mcu");

    CHECK(adapter.set_name() == "bchopper");
    CHECK_FALSE(adapter.machine().mcu_present);
    CHECK_FALSE(adapter.machine().protection_hle_present);
    REQUIRE(adapter.chips().size() == 6U);
    const auto* mcu_region = adapter.machine().roms.region("mcu");
    REQUIRE(mcu_region != nullptr);
    CHECK(mcu_region->empty());

    const auto& issues = adapter.machine().roms.issues;
    CHECK(std::any_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.file == "missing_i8751.bin" &&
               issue.message.find("missing from the ROM set") != std::string::npos;
    }));
    CHECK(std::any_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.file == "mcu" &&
               issue.message.find("disabling MCU execution") != std::string::npos;
    }));

    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0x10U] == 0xFFU);

    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 1U);
    CHECK(media.media[0].validation_issues.size() == issues.size());
    CHECK(std::any_of(media.media[0].validation_issues.begin(),
                      media.media[0].validation_issues.end(), [](const auto& issue) {
                          return issue.detail.find("disabling MCU execution") != std::string::npos;
                      }));
}

TEST_CASE("irem_m72_adapter boots the real first-game set", "[irem_m72][adapter][data]") {
    // Data-gated (never committed): MNEMOS_M72_RTYPE_SET points at the
    // authentic rtype.zip dump set or unpacked rtype directory. The adapter
    // uses the checked-in rtype.toml when the source does not carry game.toml.
    // Asserts the hardware boot path:
    // the V30 uploads the sound program and releases the Z80, and the frame
    // goes non-blank.
    const char* set_env = opt_env("MNEMOS_M72_RTYPE_SET");
    if (set_env == nullptr || *set_env == '\0') {
        SKIP("set MNEMOS_M72_RTYPE_SET to the first-game rtype.zip set or rtype directory");
    }
    auto source = load_m72_source(set_env);

    irem_m72_adapter adapter(std::move(source.bytes), "rtype", nullptr, {}, source.path);
    REQUIRE(adapter.machine().roms.issues.empty()); // CRC-verified load
    CHECK(adapter.machine().params.work_ram_base == 0x40000U);
    CHECK(adapter.machine().dip_switches == 0xFDFBU);
    CHECK(adapter.machine().sound_cpu.reset_line_held()); // parked at power-on
    // The boot-chunk reload must put a far jump at the V30 reset vector --
    // in the region, through the bus, and through the first executed
    // instruction.
    const auto& main_region = adapter.machine().roms.regions.at("maincpu");
    REQUIRE(main_region.size() == 0x100000U);
    REQUIRE(main_region[0xFFFF0U] == 0xEAU);
    REQUIRE(adapter.machine().main_bus.read8(0xFFFF0U) == 0xEAU);
    adapter.machine().main_cpu.step_instruction();
    const auto boot_regs = adapter.machine().main_cpu.cpu_registers();
    INFO("after reset-vector jump: cs=" << boot_regs.cs << " ip=" << boot_regs.ip);
    REQUIRE((static_cast<std::uint32_t>(boot_regs.cs) * 16U + boot_regs.ip) < 0x40000U);

    bool sound_released = false;
    bool frame_lit = false;
    for (int frame = 0; frame < 600 && !(sound_released && frame_lit); ++frame) {
        adapter.step_one_frame();
        sound_released = sound_released || !adapter.machine().sound_cpu.reset_line_held();
        const auto view = adapter.current_frame();
        for (std::uint32_t i = 1; i < view.width * view.height; ++i) {
            if (view.pixels[i] != view.pixels[0]) {
                frame_lit = true;
                break;
            }
        }
    }
    CHECK(sound_released);
    CHECK(frame_lit);
}

TEST_CASE("irem_m72_adapter boots a real protected M72 set", "[irem_m72][adapter][data]") {
    namespace m72 = mnemos::manifests::irem_m72;

    // Data-gated (never committed), game-agnostic protected-board check:
    // MNEMOS_M72_PROTECTED_SET points at a zip or unpacked directory of a
    // protected true-M72 set (for example bchopper, mrheli, imgfight,
    // airduelm72, dbreedm72, or dkgensanm72). The adapter uses the checked-in
    // game TOML when the source does not carry game.toml. Clone sets resolve
    // their parent zip or directory from the same directory via
    // adapter_options.rom_path. The board must CRC-load, expose either a real
    // MCU or the manifest-declared no-dump HLE profile, make the sound CPU
    // runnable, and light a frame within the warm-up.
    // MNEMOS_M72_PROTECTED_FRAMES overrides the warm-up count.
    const char* set_env = opt_env("MNEMOS_M72_PROTECTED_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_M72_PROTECTED_SET to a protected M72 set zip or directory");
    }
    auto source = load_m72_source(set_env);

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(source.bytes);
    options.display_name = "protected-m72";
    options.rom_path = source.path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);
    auto& machine = adapter.machine();

    REQUIRE(machine.roms.issues.empty()); // every declared dump present and CRC-clean
    REQUIRE_FALSE(adapter.set_name().empty());
    const auto expected_params = m72::board_params_for(adapter.set_name());
    CHECK(machine.params.work_ram_base == expected_params.work_ram_base);
    CHECK(machine.dip_switches == expected_params.dip_default);
    REQUIRE((machine.mcu_present || machine.protection_hle_present));
    if (machine.mcu_present) {
        REQUIRE(adapter.chips().size() == 7U);
        CHECK(adapter.chips().back()->metadata().part_number == "mcs51");
    } else {
        REQUIRE(machine.protection_hle_present);
        REQUIRE(adapter.chips().size() == 6U);
        REQUIRE(machine.params.protection_hle_profile.has_value());
        CHECK((*machine.params.protection_hle_profile == "irem_m72.dbreedm72_no_dump_mcu" ||
               *machine.params.protection_hle_profile == "irem_m72.dkgensanm72_no_dump_mcu"));
        CHECK_FALSE(machine.params.protection_hle_sample_triggers.empty());
        machine.main_bus.write8(m72::mcu_shared_main_base + 0x12U, 0x3CU);
        CHECK(machine.main_bus.read8(m72::mcu_shared_main_base + 0x12U) == 0x3CU);
    }

    const auto& main_region = machine.roms.regions.at("maincpu");
    REQUIRE(main_region.size() == mnemos::manifests::irem_m72::main_rom_size);
    REQUIRE(machine.main_bus.read8(0xFFFF0U) != 0xFFU);
    if (machine.sound_rom_present) {
        CHECK_FALSE(machine.sound_cpu.reset_line_held());
    } else {
        CHECK(machine.sound_cpu.reset_line_held()); // RAM-upload sound boards start parked
    }

    int warmup_frames = 600;
    if (const char* frames_env = opt_env("MNEMOS_M72_PROTECTED_FRAMES")) {
        const int parsed = std::atoi(frames_env);
        if (parsed > 0) {
            warmup_frames = parsed;
        }
    }

    std::array<std::uint32_t, 16> recent_main_pcs{};
    std::array<std::uint32_t, 16> recent_sound_pcs{};
    std::array<std::uint32_t, 64> main_window_pcs{};
    std::array<std::uint32_t, 64> restart_window_pcs{};
    std::size_t recent_main_count = 0U;
    std::size_t recent_sound_count = 0U;
    std::size_t main_window_count = 0U;
    std::size_t restart_window_count = 0U;
    std::size_t recent_main_cursor = 0U;
    std::size_t recent_sound_cursor = 0U;
    std::size_t main_window_cursor = 0U;
    std::size_t restart_window_cursor = 0U;
    std::uint64_t main_instruction_count = 0U;
    std::uint64_t reset_vector_hits = 0U;
    std::uint64_t post_probe_restarts = 0U;
    std::uint32_t max_main_pc = 0U;
    bool main_reached_post_protection_probe = false;
    if (auto* trace = machine.main_cpu.introspection().trace()) {
        trace->install([&](const mnemos::instrumentation::trace_event& event) {
            ++main_instruction_count;
            if (event.pc == 0xFFFF0U) {
                ++reset_vector_hits;
            }
            if (event.pc == 0x0200U && main_reached_post_protection_probe) {
                ++post_probe_restarts;
                restart_window_pcs = main_window_pcs;
                restart_window_count = main_window_count;
                restart_window_cursor = main_window_cursor;
            }
            max_main_pc = std::max(max_main_pc, event.pc);
            main_reached_post_protection_probe =
                main_reached_post_protection_probe || event.pc >= 0x0300U;
            record_recent_pc(main_window_pcs, main_window_count, main_window_cursor, event.pc);
            record_recent_pc(recent_main_pcs, recent_main_count, recent_main_cursor, event.pc);
        });
    }
    if (auto* trace = machine.sound_cpu.introspection().trace()) {
        trace->install([&](const mnemos::instrumentation::trace_event& event) {
            record_recent_pc(recent_sound_pcs, recent_sound_count, recent_sound_cursor, event.pc);
        });
    }

    bool sound_released = !machine.sound_cpu.reset_line_held();
    bool frame_lit = false;
    for (int frame = 0; frame < warmup_frames && !(sound_released && frame_lit); ++frame) {
        adapter.step_one_frame();
        sound_released = sound_released || !machine.sound_cpu.reset_line_held();
        if (!frame_lit) {
            const auto view = adapter.current_frame();
            for (std::uint32_t i = 1; i < view.width * view.height; ++i) {
                if (view.pixels[i] != view.pixels[0]) {
                    frame_lit = true;
                    break;
                }
            }
        }
    }
    if (auto* trace = machine.main_cpu.introspection().trace()) {
        trace->install({});
    }
    if (auto* trace = machine.sound_cpu.introspection().trace()) {
        trace->install({});
    }

    const auto main_regs = machine.main_cpu.cpu_registers();
    const auto sound_regs = machine.sound_cpu.cpu_registers();
    const auto frame = adapter.current_frame();
    INFO("set=" << adapter.set_name() << " frames=" << adapter.frames_stepped()
                << " warmup=" << warmup_frames << " sound_released=" << sound_released
                << " frame_lit=" << frame_lit
                << " frame_distinct_pixels=" << distinct_pixel_count(frame));
    INFO("main insns=" << std::dec << main_instruction_count << " max_pc=0x" << std::hex
                       << max_main_pc << " reached_0x0300=" << main_reached_post_protection_probe
                       << " reset_vector_hits=" << std::dec << reset_vector_hits
                       << " post_probe_restarts=" << post_probe_restarts);
    INFO("last post-probe restart window="
         << recent_pc_string(restart_window_pcs, restart_window_count, restart_window_cursor));
    INFO("main cs:ip=0x" << std::hex << main_regs.cs << ":0x" << main_regs.ip << " ax=0x"
                         << main_regs.ax << " bx=0x" << main_regs.bx << " cx=0x" << main_regs.cx
                         << " dx=0x" << main_regs.dx << " si=0x" << main_regs.si << " di=0x"
                         << main_regs.di << " ds=0x" << main_regs.ds << " es=0x" << main_regs.es
                         << " ss=0x" << main_regs.ss << " sp=0x" << main_regs.sp << " flags=0x"
                         << main_regs.flags << " halted=" << machine.main_cpu.halted() << " recent="
                         << recent_pc_string(recent_main_pcs, recent_main_count,
                                             recent_main_cursor));
    INFO("sound pc=0x" << std::hex << sound_regs.pc
                       << " reset=" << machine.sound_cpu.reset_line_held()
                       << " halted=" << sound_regs.halted << " recent="
                       << recent_pc_string(recent_sound_pcs, recent_sound_count,
                                           recent_sound_cursor));
    INFO("control=0x" << std::hex << static_cast<unsigned>(machine.control_register)
                      << " main_to_mcu=0x" << static_cast<unsigned>(machine.main_to_mcu)
                      << " mcu_to_main=0x" << static_cast<unsigned>(machine.mcu_to_main)
                      << " sample_address=0x" << machine.sample_address);
    INFO("nonzero work=" << std::dec << nonzero_count(machine.work_ram)
                         << " mcu_shared=" << nonzero_count(machine.mcu_shared_ram)
                         << " sound_ram=" << nonzero_count(machine.sound_ram)
                         << " sprite_ram=" << nonzero_count(machine.sprite_ram)
                         << " palette_a=" << nonzero_count(machine.palette_a)
                         << " palette_b=" << nonzero_count(machine.palette_b)
                         << " vram_a=" << nonzero_count(machine.vram_a)
                         << " vram_b=" << nonzero_count(machine.vram_b));
    CHECK(sound_released);
    CHECK(frame_lit);
}

TEST_CASE("irem_m72_adapter boots a real dumped-MCU protected M72 set",
          "[irem_m72][adapter][data]") {
    namespace m72 = mnemos::manifests::irem_m72;

    // Data-gated (never committed): MNEMOS_M72_PROTECTED_MCU_SET points at a
    // protected true-M72 set with a dumped MCU, such as nspirit.zip.
    const char* set_env = opt_env("MNEMOS_M72_PROTECTED_MCU_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_M72_PROTECTED_MCU_SET to a dumped-MCU protected M72 set");
    }
    auto source = load_m72_source(set_env);

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(source.bytes);
    options.display_name = "protected-m72-mcu";
    options.rom_path = source.path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);
    auto& machine = adapter.machine();

    REQUIRE(machine.roms.issues.empty());
    REQUIRE_FALSE(adapter.set_name().empty());
    const auto expected_params = m72::board_params_for(adapter.set_name());
    CHECK(machine.params.work_ram_base == expected_params.work_ram_base);
    CHECK(machine.dip_switches == expected_params.dip_default);
    REQUIRE(machine.mcu_present);
    CHECK_FALSE(machine.protection_hle_present);
    REQUIRE(adapter.chips().size() == 7U);
    CHECK(adapter.chips().back()->metadata().part_number == "mcs51");
    REQUIRE(machine.roms.region("mcu") != nullptr);
    CHECK_FALSE(machine.roms.region("mcu")->empty());

    const int warmup_frames = env_positive_int("MNEMOS_M72_PROTECTED_MCU_FRAMES", 900);
    bool sound_released = !machine.sound_cpu.reset_line_held();
    bool frame_lit = frame_has_variation(adapter.current_frame());
    for (int frame = 0; frame < warmup_frames && !(sound_released && frame_lit); ++frame) {
        adapter.step_one_frame();
        sound_released = sound_released || !machine.sound_cpu.reset_line_held();
        frame_lit = frame_lit || frame_has_variation(adapter.current_frame());
    }

    INFO("set=" << adapter.set_name() << " frames=" << adapter.frames_stepped()
                << " warmup=" << warmup_frames << " sound_released=" << sound_released
                << " frame_lit=" << frame_lit
                << " frame_distinct_pixels=" << distinct_pixel_count(adapter.current_frame()));
    CHECK(sound_released);
    CHECK(frame_lit);
}

TEST_CASE("irem_m72_adapter emits rendered audio for a real protected M72 set",
          "[irem_m72][adapter][data]") {
    namespace m72 = mnemos::manifests::irem_m72;

    // Data-gated (never committed): MNEMOS_M72_PROTECTED_AUDIO_SET points at a
    // protected true-M72 set with a known non-silent early runtime path, such as
    // the local dbreedm72 route. This proves the adapter's rendered audio path
    // against real media; it is not a reference-audio parity claim.
    const char* set_env = opt_env("MNEMOS_M72_PROTECTED_AUDIO_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_M72_PROTECTED_AUDIO_SET to a protected M72 audio-proof set");
    }
    auto source = load_m72_source(set_env);

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(source.bytes);
    options.display_name = "protected-m72-audio";
    options.rom_path = source.path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);
    auto& machine = adapter.machine();

    REQUIRE(machine.roms.issues.empty());
    REQUIRE_FALSE(adapter.set_name().empty());
    REQUIRE((machine.mcu_present || machine.protection_hle_present));
    if (machine.protection_hle_present) {
        REQUIRE(machine.params.protection_hle_profile.has_value());
        CHECK((*machine.params.protection_hle_profile == "irem_m72.dbreedm72_no_dump_mcu" ||
               *machine.params.protection_hle_profile == "irem_m72.dkgensanm72_no_dump_mcu"));
        CHECK_FALSE(machine.params.protection_hle_sample_triggers.empty());
        const auto expected_params = m72::board_params_for(adapter.set_name());
        CHECK(machine.params.work_ram_base == expected_params.work_ram_base);
    }

    const int proof_frames = env_positive_int("MNEMOS_M72_PROTECTED_AUDIO_FRAMES", 120);
    bool nonzero_audio = false;
    std::uint64_t total_audio_frames = 0U;
    std::uint32_t last_sample_rate = 0U;
    int first_signal_frame = -1;
    for (int frame = 0; frame < proof_frames && !nonzero_audio; ++frame) {
        mnemos::frontend_sdk::controller_state p1{};
        p1.start = frame >= 1 && frame < 3;
        p1.select = frame >= 2 && frame < 4;
        p1.service = frame >= 3 && frame < 5;
        adapter.apply_input(0, p1);
        adapter.step_one_frame();
        const auto audio = adapter.drain_audio();
        total_audio_frames += audio.frame_count;
        last_sample_rate = audio.sample_rate;
        if (audio_chunk_has_nonzero_pcm(audio)) {
            nonzero_audio = true;
            first_signal_frame = frame;
        }
    }

    INFO("set=" << adapter.set_name() << " proof_frames=" << proof_frames
                << " stepped=" << adapter.frames_stepped()
                << " total_audio_frames=" << total_audio_frames
                << " last_sample_rate=" << last_sample_rate
                << " first_signal_frame=" << first_signal_frame
                << " sample_address=0x" << std::hex << machine.sample_address
                << " dac_events=" << std::dec << machine.dac_write_events.size());
    CHECK(last_sample_rate == 55930U);
    CHECK(total_audio_frames > 0U);
    CHECK(nonzero_audio);
}

TEST_CASE("irem_m72_adapter matches optional visual/audio parity hashes for a real M72 set",
          "[irem_m72][adapter][data]") {
    struct parity_result final {
        std::string set_name;
        std::string frame_hash;
        std::string audio_hash;
        std::uint64_t audio_frames{};
        std::uint32_t sample_rate{};
    };

    const char* set_env = opt_env("MNEMOS_M72_PARITY_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_M72_PARITY_SET to a reference-captured M72 set");
    }
    const char* expected_frame_hash = opt_env("MNEMOS_M72_PARITY_FRAME_SHA256");
    const char* expected_audio_hash = opt_env("MNEMOS_M72_PARITY_AUDIO_SHA256");
    if (expected_frame_hash == nullptr && expected_audio_hash == nullptr) {
        SKIP("set MNEMOS_M72_PARITY_FRAME_SHA256 and/or MNEMOS_M72_PARITY_AUDIO_SHA256");
    }

    const int frames = env_positive_int("MNEMOS_M72_PARITY_FRAMES", 600);
    const bool collect_audio = expected_audio_hash != nullptr;

    auto run_once = [&](bool want_audio) -> parity_result {
        auto source = load_m72_source(set_env);
        mnemos::frontend_sdk::adapter_options options{};
        options.rom = std::move(source.bytes);
        options.display_name = "m72-parity";
        options.rom_path = source.path;
        auto system = mnemos::frontend_sdk::adapter_registry::instance().create(
            "irem_m72", std::move(options));
        REQUIRE(system != nullptr);
        auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);
        auto& machine = adapter.machine();
        REQUIRE(machine.roms.issues.empty());
        REQUIRE_FALSE(adapter.set_name().empty());

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
        CHECK(frame_has_variation(frame));

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
    if (expected_frame_hash != nullptr) {
        CHECK(first.frame_hash == expected_frame_hash);
    }
    if (expected_audio_hash != nullptr) {
        REQUIRE(first.audio_frames > 0U);
        CHECK(first.sample_rate == 55930U);
        CHECK(second.audio_frames == first.audio_frames);
        CHECK(second.sample_rate == first.sample_rate);
        CHECK(second.audio_hash == first.audio_hash);
        CHECK(first.audio_hash == expected_audio_hash);
    }
}

TEST_CASE("irem_m72_adapter validates a real vertical M72 set orientation",
          "[irem_m72][adapter][data]") {
    // Data-gated (never committed): MNEMOS_M72_VERTICAL_SET points at a vertical
    // true-M72 set zip or directory (for example imgfight or airduelm72). The
    // adapter uses the checked-in game TOML when the source does not carry
    // game.toml. Clone sets resolve their parent zip or directory beside it via
    // adapter_options.rom_path.
    const char* set_env = opt_env("MNEMOS_M72_VERTICAL_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_M72_VERTICAL_SET to a vertical M72 set zip or directory");
    }
    auto source = load_m72_source(set_env);

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(source.bytes);
    options.display_name = "vertical-m72";
    options.rom_path = source.path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    REQUIRE(adapter.machine().roms.issues.empty());
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    CHECK(adapter.current_frame().width == 384U);
    CHECK(adapter.current_frame().height == 256U);

    bool frame_lit = false;
    for (int frame = 0; frame < 600 && !frame_lit; ++frame) {
        adapter.step_one_frame();
        const auto view = adapter.current_frame();
        for (std::uint32_t i = 1; i < view.width * view.height; ++i) {
            if (view.pixels[i] != view.pixels[0]) {
                frame_lit = true;
                break;
            }
        }
    }
    CHECK(frame_lit);
}

TEST_CASE("irem_m72_adapter validates the checked-in true-M72 ROM roster",
          "[irem_m72][adapter][data]") {
    namespace m72 = mnemos::manifests::irem_m72;

    // Data-gated (never committed): MNEMOS_M72_SET_DIR points at one mixed
    // corpus root or a platform path-list of roots containing <set>.zip,
    // <set>\, or single-inner-set wrapper zips for each checked-in true-M72
    // manifest. Roots are walked recursively, but only checked-in M72 set IDs
    // are accepted.
    // Clone sets use the resolved source directory for parent fallback.
    // MNEMOS_M72_ROSTER_FRAMES can raise or lower the per-set warm-up window.
    const char* dir_env = opt_env("MNEMOS_M72_SET_DIR");
    if (dir_env == nullptr) {
        SKIP("set MNEMOS_M72_SET_DIR to directories containing the true-M72 zip/folder roster");
    }
    const auto roots = m72_source_roots(dir_env);
    REQUIRE_FALSE(roots.empty());
    for (const auto& root : roots) {
        INFO("root=" << root.string());
        REQUIRE(std::filesystem::is_directory(root));
    }
    const auto indexed_sources = index_m72_source_roots(roots);
    std::vector<std::string> missing_sets;
    for (const auto& [set_name_view, _] : m72::embedded::game_manifests) {
        const std::string set_name{set_name_view};
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
        FAIL("missing M72 roster artifacts: " << missing.str());
    }

    const int warmup_frames = env_positive_int("MNEMOS_M72_ROSTER_FRAMES", 600);
    for (const auto& [set_name_view, _] : m72::embedded::game_manifests) {
        const std::string set_name{set_name_view};
        INFO("set=" << set_name);
        const auto decl = require_embedded_decl(set_name);
        const auto set_path = indexed_sources.find(set_name);
        REQUIRE(set_path != indexed_sources.end());
        INFO("source=" << set_path->second.string());
        auto source = load_m72_source(set_path->second);

        mnemos::frontend_sdk::adapter_options options{};
        options.rom = std::move(source.bytes);
        options.display_name = set_name;
        options.rom_path = source.path;
        auto system = mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72",
                                                                                std::move(options));
        REQUIRE(system != nullptr);
        auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);
        auto& machine = adapter.machine();

        CHECK(adapter.set_name() == set_name);
        CHECK(machine.roms.issues.empty());
        const auto expected_params = m72::board_params_for(set_name);
        CHECK(machine.params.work_ram_base == expected_params.work_ram_base);
        CHECK(machine.dip_switches == expected_params.dip_default);
        CHECK(adapter.region().orientation ==
              (decl.orientation == mnemos::manifests::common::screen_orientation::vertical
                   ? mnemos::frontend_sdk::display_orientation::vertical
                   : mnemos::frontend_sdk::display_orientation::horizontal));
        REQUIRE(machine.roms.region("maincpu") != nullptr);
        CHECK(machine.roms.region("maincpu")->size() == m72::main_rom_size);

        const bool declares_mcu_hle =
            std::any_of(decl.hle.begin(), decl.hle.end(),
                        [](const auto& hle) { return hle.chip == "mcu" && !hle.profile.empty(); });
        const bool declares_mcu_region =
            std::any_of(decl.regions.begin(), decl.regions.end(),
                        [](const auto& region) { return region.name == "mcu"; });
        if (declares_mcu_hle || declares_mcu_region) {
            CHECK((machine.mcu_present || machine.protection_hle_present));
            if (machine.protection_hle_present) {
                CHECK_FALSE(machine.params.protection_hle_sample_triggers.empty());
            }
        }

        bool sound_released = !machine.sound_cpu.reset_line_held();
        bool frame_lit = frame_has_variation(adapter.current_frame());
        for (int frame = 0; frame < warmup_frames && !(sound_released && frame_lit); ++frame) {
            adapter.step_one_frame();
            sound_released = sound_released || !machine.sound_cpu.reset_line_held();
            frame_lit = frame_lit || frame_has_variation(adapter.current_frame());
        }
        CHECK(sound_released);
        CHECK(frame_lit);
    }
}

TEST_CASE("irem_m72_adapter loads a declarative game.toml set from a zip", "[irem_m72][adapter]") {
    // Split the working program image into even/odd halves and let the
    // declaration's stride-2 placement reassemble it.
    const auto whole = make_program();
    std::vector<std::uint8_t> low(whole.size() / 2U);
    std::vector<std::uint8_t> high(whole.size() / 2U);
    for (std::size_t i = 0; i < whole.size() / 2U; ++i) {
        low[i] = whole[i * 2U];
        high[i] = whole[i * 2U + 1U];
    }
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "synthetic"
board = "irem_m72"
orientation = "vertical"

[[dip]]
bank = "SW1"
name = "Lives"
mask = 0x0003
default = 0x0003

[[dip.option]]
label = "2"
value = 0x0002

[[dip.option]]
label = "3"
value = 0x0003

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog.lo"
offset = 0
stride = 2

[[region.file]]
name = "prog.hi"
offset = 1
stride = 2
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog.lo", low},
        {"prog.hi", high},
    });

    irem_m72_adapter adapter(zip, "synthetic");
    CHECK(adapter.set_name() == "synthetic");
    CHECK(adapter.system_spec()[2].value == "synthetic");
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    REQUIRE(adapter.dip_switches().size() == 1U);
    CHECK(adapter.dip_switches()[0].name == "Lives");
    CHECK(adapter.dip_switches()[0].default_value == 0x0003U);
    CHECK(adapter.system_spec().back().label == "DIP switches");
    CHECK(adapter.system_spec().back().value == "1");
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U); // the program ran
}

TEST_CASE("irem_m72_adapter loads a declarative game.toml set from a directory",
          "[irem_m72][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "synthetic-dir"
board = "irem_m72"
orientation = "vertical"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog.bin"
offset = 0
)";
    const auto root =
        std::filesystem::temp_directory_path() / "mnemos_irem_m72_declarative_directory";
    write_directory_rom_set(
        root, {
                  {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
                  {"prog.bin", make_program()},
              });

    irem_m72_adapter adapter({}, "synthetic-dir", nullptr, {}, root.string());

    CHECK(adapter.set_name() == "synthetic-dir");
    CHECK(adapter.system_spec()[2].value == "synthetic-dir");
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("irem_m72_adapter accepts unpacked directory filename aliases", "[irem_m72][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "synthetic-dir-alias"
board = "irem_m72"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "aa_c-v0.ic44"
offset = 0

[[region]]
name = "tiles_a"
size = 1

[[region.file]]
name = "aa_k804m.b0.ic26"
offset = 0
size = 1

[[region]]
name = "sprites"
size = 1

[[region.file]]
name = "mh_c-h0-b.ic40"
offset = 0
size = 1

[[region]]
name = "samples"
size = 1

[[region.file]]
name = "gen-v0.ic44"
offset = 0
size = 1

[[region]]
name = "tiles_b"
size = 1

[[region.file]]
name = "nin-v0.ic44"
offset = 0
size = 1
)";
    const auto root = std::filesystem::temp_directory_path() / "mnemos_irem_m72_directory_aliases";
    write_directory_rom_set(
        root, {
                  {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
                  {"aa-a-v0.rom", make_program()},
                  {"aa_k804m.a0", {0x5AU}},
                  {"c-h0-b.rom", {0x6BU}},
                  {"gen-vo.bin", {0x7CU}},
                  {"nin-v0.7a", {0x8DU}},
              });

    irem_m72_adapter adapter({}, "synthetic-dir-alias", nullptr, {}, root.string());

    CHECK(adapter.machine().roms.issues.empty());
    REQUIRE(adapter.machine().roms.region("tiles_a") != nullptr);
    CHECK(adapter.machine().roms.region("tiles_a")->front() == 0x5AU);
    REQUIRE(adapter.machine().roms.region("sprites") != nullptr);
    CHECK(adapter.machine().roms.region("sprites")->front() == 0x6BU);
    REQUIRE(adapter.machine().roms.region("samples") != nullptr);
    CHECK(adapter.machine().roms.region("samples")->front() == 0x7CU);
    REQUIRE(adapter.machine().roms.region("tiles_b") != nullptr);
    CHECK(adapter.machine().roms.region("tiles_b")->front() == 0x8DU);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("irem_m72_adapter infers a misnamed unpacked clone from program CRCs",
          "[irem_m72][adapter]") {
    const auto root = std::filesystem::temp_directory_path() / "mnemos_irem_m72_misnamed_airduel";
    const auto set_dir = root / "airduelm72";
    std::filesystem::remove_all(root);
    write_directory_rom_set(
        set_dir,
        {
            {"ad-c-h0.bin", make_crc32_blob(0x20000U, {0x97U, 0x50U, 0x37U, 0x7AU}, 0x12140276U)},
            {"ad-c-l0.bin", make_crc32_blob(0x20000U, {0x12U, 0xB1U, 0x66U, 0xE3U}, 0x4ac0b91dU)},
            {"ad-c-h3.bin", make_crc32_blob(0x20000U, {0xECU, 0x8CU, 0x6AU, 0x8EU}, 0x9f7cfca3U)},
            {"ad-c-l3.bin", make_crc32_blob(0x20000U, {0x2EU, 0x4FU, 0x10U, 0xCCU}, 0x9dd343f7U)},
        });

    irem_m72_adapter adapter({}, "airduel-misnamed", nullptr, {}, set_dir.string());

    CHECK(adapter.set_name() == "airdueljm72");
    const auto& issues = adapter.machine().roms.issues;
    CHECK(std::none_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.file == "ad_c-h0-c.ic40" || issue.file == "ad_c-l0-c.ic37";
    }));
    CHECK(std::any_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.file == "ad_c-pr-.ic1" &&
               issue.message.find("missing from the ROM set") != std::string::npos;
    }));
}

TEST_CASE("irem_m72_adapter keeps canonical M72 set selection for collection zips",
          "[irem_m72][adapter]") {
    const auto root = std::filesystem::temp_directory_path() / "mnemos_irem_m72_airduel_collection";
    std::filesystem::remove_all(root);
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    const auto set_path = root / "airduel.zip";
    const auto collection_zip = make_stored_zip({
        {"airduelm72/ad_c-h0-c.ic40",
         make_crc32_blob(0x20000U, {0x60U, 0x89U, 0x94U, 0x6BU}, 0x6467ed0fU)},
        {"airduelm72/ad_c-l0-c.ic37",
         make_crc32_blob(0x20000U, {0xEAU, 0x1FU, 0xFEU, 0x88U}, 0xb90c4ffdU)},
        {"airdueljm72/ad_c-h0-.ic40",
         make_crc32_blob(0x20000U, {0x97U, 0x50U, 0x37U, 0x7AU}, 0x12140276U)},
        {"airdueljm72/ad_c-l0-.ic37",
         make_crc32_blob(0x20000U, {0x12U, 0xB1U, 0x66U, 0xE3U}, 0x4ac0b91dU)},
        {"airdueljm72/ad_c-h3-.ic43",
         make_crc32_blob(0x20000U, {0xECU, 0x8CU, 0x6AU, 0x8EU}, 0x9f7cfca3U)},
        {"airdueljm72/ad_c-l3-.ic34",
         make_crc32_blob(0x20000U, {0x2EU, 0x4FU, 0x10U, 0xCCU}, 0x9dd343f7U)},
    });
    REQUIRE(mnemos::io::write_file(set_path.string(), collection_zip));

    irem_m72_adapter adapter(collection_zip, "airduel-collection", nullptr, {}, set_path.string());

    CHECK(adapter.set_name() == "airduelm72");
    const auto& issues = adapter.machine().roms.issues;
    CHECK(std::none_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.file == "ad_c-h0-c.ic40" || issue.file == "ad_c-l0-c.ic37" ||
               issue.file == "ad_c-h3-.ic43" || issue.file == "ad_c-l3-.ic34";
    }));
    CHECK(std::any_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.file == "ad_c-pr-c.ic1" &&
               issue.message.find("missing from the ROM set") != std::string::npos;
    }));
}

TEST_CASE("irem_m72_adapter resolves canonical M72 manifests for unpacked folder aliases",
          "[irem_m72][adapter]") {
    const auto gallop_decl = require_embedded_decl("gallopm72");
    const auto root = std::filesystem::temp_directory_path() / "mnemos_irem_m72_plain_gallop";
    const auto set_dir = root / "gallop";
    std::filesystem::remove_all(root);
    write_directory_rom_set(set_dir, placeholder_entries_for(gallop_decl, 0x45U));

    irem_m72_adapter adapter({}, "plain-gallop-folder", nullptr, {}, set_dir.string());

    CHECK(adapter.set_name() == "gallopm72");
    REQUIRE(adapter.machine().roms.region("maincpu") != nullptr);
    REQUIRE(adapter.machine().roms.region("sprites") != nullptr);
    CHECK(has_only_crc_issues(adapter.machine().roms.issues));
    CHECK(std::none_of(adapter.machine().roms.issues.begin(), adapter.machine().roms.issues.end(),
                       [](const auto& issue) {
                           return issue.message.find("no embedded manifest") != std::string::npos;
                       }));
}

TEST_CASE("irem_m72_adapter surfaces non-M72 declarative board mismatches", "[irem_m72][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "synthetic-m92"
board = "irem_m92"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    irem_m72_adapter adapter(zip, "synthetic-m92");

    CHECK(adapter.set_name().empty());
    const auto* maincpu = adapter.machine().roms.region("maincpu");
    REQUIRE(maincpu != nullptr);
    REQUIRE(maincpu->size() == mnemos::manifests::irem_m72::main_rom_size);
    CHECK((*maincpu)[0xFFFF0U] == 0xFFU); // rejected declaration did not load prog.
    REQUIRE(adapter.machine().roms.issues.size() == 1U);
    CHECK(adapter.machine().roms.issues[0].file == "game.toml");
    CHECK(adapter.machine().roms.issues[0].message.find("unsupported Irem board 'irem_m92'") !=
          std::string::npos);
}

TEST_CASE("irem_m72_adapter passes declarative MCU HLE profiles to the board",
          "[irem_m72][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "dbreedm72"
board = "irem_m72"

[[hle]]
chip = "mcu"
profile = "irem_m72.dbreedm72_no_dump_mcu"
rationale = "The set's i8751 dump is unavailable; use the declared interim profile."

[[hle.sample_trigger]]
trigger = 6
start = 0x13000

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "samples"
size = 0x13001
fill = 0x00
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    irem_m72_adapter adapter(zip, "dbreedm72-hle");
    CHECK(adapter.set_name() == "dbreedm72");
    CHECK_FALSE(adapter.machine().mcu_present);
    REQUIRE(adapter.machine().protection_hle_present);
    CHECK(adapter.machine().params.protection_hle_profile ==
          std::optional<std::string>{"irem_m72.dbreedm72_no_dump_mcu"});
    REQUIRE(adapter.machine().params.protection_hle_sample_triggers.size() == 1U);
    CHECK(adapter.machine().params.protection_hle_sample_triggers[0].trigger == 6U);
    CHECK(adapter.machine().params.protection_hle_sample_triggers[0].start == 0x13000U);

    adapter.machine().main_bus.write8(mnemos::manifests::irem_m72::mcu_shared_main_base, 0x00U);
    CHECK(adapter.machine().main_bus.read8(mnemos::manifests::irem_m72::mcu_shared_main_base) ==
          0xFFU);
}

TEST_CASE("irem_m72_adapter disables declarative MCU HLE when sample dumps are missing",
          "[irem_m72][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "dbreedm72"
board = "irem_m72"

[[hle]]
chip = "mcu"
profile = "irem_m72.dbreedm72_no_dump_mcu"
rationale = "Synthetic missing samples for loader diagnostics."

[[hle.sample_trigger]]
trigger = 6
start = 0x13000

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "samples"
size = 0x13020
fill = 0x00

[[region.file]]
name = "missing_samples.bin"
offset = 0
size = 0x13020
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    irem_m72_adapter adapter(zip, "dbreedm72-hle");

    CHECK(adapter.set_name() == "dbreedm72");
    CHECK_FALSE(adapter.machine().mcu_present);
    CHECK_FALSE(adapter.machine().protection_hle_present);
    CHECK_FALSE(adapter.machine().params.protection_hle_profile.has_value());
    CHECK(adapter.machine().params.protection_hle_sample_triggers.empty());
    CHECK(adapter.machine().main_bus.read8(mnemos::manifests::irem_m72::mcu_shared_main_base) ==
          0xFFU);

    const auto* samples = adapter.machine().roms.region("samples");
    REQUIRE(samples != nullptr);
    REQUIRE(samples->size() == 0x13020U);
    CHECK((*samples)[0x13000U] == 0x00U);

    const auto& issues = adapter.machine().roms.issues;
    REQUIRE(issues.size() == 2U);
    CHECK(std::any_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.file == "missing_samples.bin" &&
               issue.message.find("missing from the ROM set") != std::string::npos;
    }));
    CHECK(std::any_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.file == "samples" &&
               issue.message.find("disabling MCU HLE") != std::string::npos;
    }));

    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 1U);
    REQUIRE(media.media[0].validation_issues.size() == issues.size());
    CHECK(std::any_of(media.media[0].validation_issues.begin(),
                      media.media[0].validation_issues.end(), [](const auto& issue) {
                          return issue.detail.find("disabling MCU HLE") != std::string::npos;
                      }));
}

TEST_CASE("irem_m72_adapter reports out-of-range declarative MCU HLE sample triggers",
          "[irem_m72][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "dbreedm72"
board = "irem_m72"

[[hle]]
chip = "mcu"
profile = "irem_m72.dbreedm72_no_dump_mcu"
rationale = "Synthetic trigger table points outside the declared sample region."

[[hle.sample_trigger]]
trigger = 6
start = 0x13000

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0

[[region]]
name = "samples"
size = 0x100
fill = 0x00
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    irem_m72_adapter adapter(zip, "dbreedm72-hle");

    CHECK(adapter.set_name() == "dbreedm72");
    CHECK_FALSE(adapter.machine().mcu_present);
    CHECK_FALSE(adapter.machine().protection_hle_present);
    CHECK_FALSE(adapter.machine().params.protection_hle_profile.has_value());
    REQUIRE(adapter.machine().roms.issues.size() == 1U);
    CHECK(adapter.machine().roms.issues[0].file == "mcu");
    CHECK(adapter.machine().roms.issues[0].message.find("beyond samples region size") !=
          std::string::npos);
    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 1U);
    REQUIRE(media.media[0].validation_issues.size() == 1U);
    CHECK(media.media[0].validation_issues[0].detail.find("beyond samples region size") !=
          std::string::npos);
}

TEST_CASE("irem_m72_adapter reports incomplete declarative MCU HLE profiles",
          "[irem_m72][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "dbreedm72"
board = "irem_m72"

[[hle]]
chip = "mcu"
profile = "irem_m72.dbreedm72_no_dump_mcu"
rationale = "Synthetic missing trigger table for loader diagnostics."

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    irem_m72_adapter adapter(zip, "dbreedm72-hle");

    CHECK(adapter.set_name() == "dbreedm72");
    CHECK_FALSE(adapter.machine().mcu_present);
    CHECK_FALSE(adapter.machine().protection_hle_present);
    CHECK_FALSE(adapter.machine().params.protection_hle_profile.has_value());
    REQUIRE(adapter.machine().roms.issues.size() == 1U);
    CHECK(adapter.machine().roms.issues[0].file == "mcu");
    CHECK(adapter.machine().roms.issues[0].message.find("missing sample-trigger metadata") !=
          std::string::npos);
}

TEST_CASE("irem_m72_adapter reports unsupported declarative MCU HLE profiles",
          "[irem_m72][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "dbreedm72"
board = "irem_m72"

[[hle]]
chip = "mcu"
profile = "irem_m72.unknown_no_dump_mcu"
rationale = "Synthetic invalid profile for loader diagnostics."

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    irem_m72_adapter adapter(zip, "dbreedm72-invalid-hle");
    CHECK(adapter.set_name() == "dbreedm72");
    CHECK_FALSE(adapter.machine().mcu_present);
    CHECK_FALSE(adapter.machine().protection_hle_present);
    CHECK_FALSE(adapter.machine().params.protection_hle_profile.has_value());
    REQUIRE(adapter.machine().roms.issues.size() == 1U);
    CHECK(adapter.machine().roms.issues[0].file == "mcu");
    CHECK(adapter.machine().roms.issues[0].message.find("unsupported M72 MCU HLE profile") !=
          std::string::npos);
}

TEST_CASE("irem_m72_adapter whole-player save-state round-trips through runtime",
          "[irem_m72][adapter]") {
    namespace irem = mnemos::apps::player::adapters::irem_m72;

    irem::irem_m72_adapter source(make_program(), "save-source");
    source.step_one_frame();
    const auto warm_audio = source.drain_audio();
    REQUIRE(warm_audio.frame_count > 0U);

    mnemos::frontend_sdk::controller_state p1{};
    p1.up = true;
    p1.a = true;
    p1.start = true;
    p1.select = true;
    p1.service = true;
    p1.test = true;
    p1.aim_x = 123;
    p1.aim_y = 45;
    p1.trigger = true;
    source.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.right = true;
    p2.b = true;
    p2.start = true;
    p2.mode = true; // legacy service 2 alias remains serialised
    source.apply_input(1, p2);

    source.machine().work_ram[0x22U] = 0xABU;
    source.machine().sample_address = 0x3456U;
    source.machine().main_to_mcu = 0x40U;
    source.machine().mcu_to_main = 0x41U;
    source.machine().record_dac_write(0xC0U);
    source.machine().sound_cpu.tick(mnemos::chips::audio::ym2151::clocks_per_sample);
    source.machine().fm.tick(mnemos::chips::audio::ym2151::clocks_per_sample);
    const auto first_dac_audio = source.drain_audio();
    REQUIRE(first_dac_audio.frame_count == 1U);
    REQUIRE(first_dac_audio.samples != nullptr);
    CHECK(first_dac_audio.samples[0] == (0xC0 - 0x80) * 64);
    CHECK(first_dac_audio.samples[1] == (0xC0 - 0x80) * 64);
    source.machine().sound_cpu.tick(mnemos::chips::audio::ym2151::clocks_per_sample);
    source.machine().fm.tick(mnemos::chips::audio::ym2151::clocks_per_sample);

    const mnemos::runtime::save_target board_target = irem::build_save_target(source.machine());
    CHECK(board_target.manifest_id == "irem_m72");
    CHECK(board_target.manifest_rev == mnemos::manifests::irem_m72::m72_system_state_version);
    const mnemos::runtime::save_target source_target = irem::build_save_target(source);
    CHECK(source_target.manifest_id == "irem_m72.adapter");
    CHECK(source_target.manifest_rev == 3U);
    REQUIRE(source_target.components.size() == 2U);
    const std::vector<std::uint8_t> blob = mnemos::runtime::write_save_state(source_target);
    REQUIRE_FALSE(blob.empty());
    CHECK(source.save_state() == blob);

    irem::irem_m72_adapter restored(make_program(), "save-restored");
    CHECK(restored.machine().work_ram[0x22U] != 0xABU);
    mnemos::runtime::save_target restored_target = irem::build_save_target(restored);
    mnemos::runtime::save_target stale_target = restored_target;
    stale_target.manifest_rev = 2U;
    CHECK(mnemos::runtime::read_save_state(blob, stale_target).status ==
          mnemos::runtime::load_status::manifest_mismatch);
    auto mismatched_rom = make_program();
    mismatched_rom[0x200U] ^= 0x01U;
    irem::irem_m72_adapter mismatched(std::move(mismatched_rom), "save-wrong-rom");
    CHECK(mnemos::runtime::read_save_state(blob, irem::build_save_target(mismatched)).status ==
          mnemos::runtime::load_status::chunk_rejected);

    const mnemos::runtime::load_result result = restored.load_state(blob);
    REQUIRE(result.ok());

    CHECK(restored.frames_stepped() == source.frames_stepped());
    CHECK(restored.machine().work_ram[0x22U] == 0xABU);
    CHECK(restored.machine().sample_address == 0x3456U);
    CHECK(restored.machine().main_to_mcu == 0x40U);
    CHECK(restored.machine().mcu_to_main == 0x41U);
    CHECK(restored.machine().input_p1 == static_cast<std::uint8_t>(0xFFU & ~0x08U & ~0x80U));
    CHECK(restored.machine().input_p2 == static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x40U));
    CHECK(restored.machine().input_system ==
          static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x02U & ~0x04U & ~0x10U & ~0x20U & ~0x40U));
    CHECK(restored.machine().dac.level() == 0xC0U);

    const auto restored_dac_audio = restored.drain_audio();
    REQUIRE(restored_dac_audio.frame_count == 1U);
    REQUIRE(restored_dac_audio.samples != nullptr);
    CHECK(restored_dac_audio.samples[0] == (0xC0 - 0x80) * 64);
    CHECK(restored_dac_audio.samples[1] == (0xC0 - 0x80) * 64);

    restored.apply_input(0, {});
    CHECK(restored.machine().input_p1 == 0xFFU);
    CHECK(restored.machine().input_p2 == static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x40U));
    CHECK(restored.machine().input_system == static_cast<std::uint8_t>(0xFFU & ~0x02U & ~0x20U));
}

TEST_CASE("irem_m72_adapter loads v1 adapter input snapshots", "[irem_m72][adapter]") {
    namespace irem = mnemos::apps::player::adapters::irem_m72;

    irem::irem_m72_adapter legacy_source(make_program(), "legacy-input-source");

    mnemos::frontend_sdk::controller_state p1{};
    p1.mode = true;

    mnemos::frontend_sdk::controller_state p2{};
    p2.start = true;

    const auto write_i16_legacy = [](mnemos::chips::state_writer& writer, std::int16_t value) {
        writer.u16(static_cast<std::uint16_t>(static_cast<std::int32_t>(value) + 32768));
    };
    const auto write_v1_controller = [&](mnemos::chips::state_writer& writer,
                                         const mnemos::frontend_sdk::controller_state& state) {
        writer.boolean(state.up);
        writer.boolean(state.down);
        writer.boolean(state.left);
        writer.boolean(state.right);
        writer.boolean(state.start);
        writer.boolean(state.select);
        writer.boolean(state.a);
        writer.boolean(state.b);
        writer.boolean(state.c);
        writer.boolean(state.x);
        writer.boolean(state.y);
        writer.boolean(state.z);
        writer.boolean(state.mode);
        write_i16_legacy(writer, state.aim_x);
        write_i16_legacy(writer, state.aim_y);
        writer.boolean(state.trigger);
    };

    mnemos::runtime::save_target legacy_target = irem::build_save_target(legacy_source);
    REQUIRE(legacy_target.components.size() == 2U);
    legacy_target.components.pop_back();
    legacy_target.components.push_back({"adapter",
                                        [&](mnemos::chips::state_writer& writer) {
                                            writer.u32(1U); // pre-service/test adapter payload
                                            writer.u64(7U);
                                            writer.u64(0U);
                                            write_i16_legacy(writer, 0);
                                            write_v1_controller(writer, p1);
                                            write_v1_controller(writer, p2);
                                        },
                                        [](mnemos::chips::state_reader&) {}});

    const std::vector<std::uint8_t> blob = mnemos::runtime::write_save_state(legacy_target);
    REQUIRE_FALSE(blob.empty());

    irem::irem_m72_adapter restored(make_program(), "legacy-input-restored");
    const mnemos::runtime::load_result result = restored.load_state(blob);
    REQUIRE(result.ok());

    CHECK(restored.frames_stepped() == 7U);
    CHECK(restored.machine().input_system == static_cast<std::uint8_t>(0xFFU & ~0x10U & ~0x02U));
}

TEST_CASE("irem_m72_adapter resolves a declarative clone parent zip", "[irem_m72][adapter]") {
    const auto whole = make_program();
    std::vector<std::uint8_t> low(whole.size() / 2U);
    std::vector<std::uint8_t> high(whole.size() / 2U);
    for (std::size_t i = 0; i < whole.size() / 2U; ++i) {
        low[i] = whole[i * 2U];
        high[i] = whole[i * 2U + 1U];
    }

    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "mrheli"
parent = "bchopper"
board = "irem_m72"
orientation = "horizontal"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "clone.lo"
offset = 0
stride = 2

[[region.file]]
name = "parent.hi"
offset = 1
stride = 2
)";
    const std::string parent_manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "bchopper"
board = "irem_m72"

[[dip]]
bank = "SW1"
name = "Parent Lives"
mask = 0x0003
default = 0x0003

[[dip.option]]
label = "3"
value = 0x0003

[[region]]
name = "tiles_a"
size = 4

[[region.file]]
name = "parent.gfx"
offset = 0
size = 4
)";

    const auto root = std::filesystem::temp_directory_path() / "mnemos_irem_m72_parent_fallback";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));

    const std::vector<std::uint8_t> parent_gfx{0x10U, 0x11U, 0x12U, 0x13U};
    const auto parent_zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(parent_manifest.begin(), parent_manifest.end())},
        {"parent.hi", high},
        {"parent.gfx", parent_gfx},
    });
    const auto clone_zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"clone.lo", low},
    });
    const auto parent_path = (root / "bchopper.zip").string();
    const auto clone_path = (root / "mrheli.zip").string();
    REQUIRE(mnemos::io::write_file(parent_path, parent_zip));
    REQUIRE(mnemos::io::write_file(clone_path, clone_zip));

    auto clone_bytes = mnemos::io::read_file(clone_path);
    REQUIRE(clone_bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*clone_bytes);
    options.display_name = "mrheli";
    options.rom_path = clone_path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    CHECK(adapter.machine().roms.issues.empty());
    CHECK(adapter.machine().params.work_ram_base == 0xA0000U);
    REQUIRE(adapter.dip_switches().size() == 1U);
    CHECK(adapter.dip_switches()[0].name == "Parent Lives");
    REQUIRE(adapter.machine().roms.region("tiles_a") != nullptr);
    CHECK(*adapter.machine().roms.region("tiles_a") == parent_gfx);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("irem_m72_adapter resolves a declarative clone parent directory", "[irem_m72][adapter]") {
    const auto whole = make_program();
    std::vector<std::uint8_t> low(whole.size() / 2U);
    std::vector<std::uint8_t> high(whole.size() / 2U);
    for (std::size_t i = 0; i < whole.size() / 2U; ++i) {
        low[i] = whole[i * 2U];
        high[i] = whole[i * 2U + 1U];
    }

    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "mrheli"
parent = "bchopper"
board = "irem_m72"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "clone.lo"
offset = 0
stride = 2

[[region.file]]
name = "parent.hi"
offset = 1
stride = 2
)";
    const std::string parent_manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "bchopper"
board = "irem_m72"

[[region]]
name = "tiles_a"
size = 4

[[region.file]]
name = "parent.gfx"
offset = 0
size = 4
)";

    const auto root =
        std::filesystem::temp_directory_path() / "mnemos_irem_m72_parent_directory_fallback";
    const auto parent_dir = root / "bchopper";
    const auto clone_dir = root / "mrheli";
    const std::vector<std::uint8_t> parent_gfx{0x20U, 0x21U, 0x22U, 0x23U};
    write_directory_rom_set(
        parent_dir, {
                        {"game.toml",
                         std::vector<std::uint8_t>(parent_manifest.begin(), parent_manifest.end())},
                        {"parent.hi", high},
                        {"parent.gfx", parent_gfx},
                    });
    write_directory_rom_set(
        clone_dir, {
                       {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
                       {"clone.lo", low},
                   });

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = {};
    options.display_name = "mrheli";
    options.rom_path = clone_dir.string();
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    CHECK(adapter.machine().roms.issues.empty());
    REQUIRE(adapter.machine().roms.region("tiles_a") != nullptr);
    CHECK(*adapter.machine().roms.region("tiles_a") == parent_gfx);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("irem_m72_adapter resolves a declarative clone parent from supplemental media",
          "[irem_m72][adapter]") {
    const auto whole = make_program();
    std::vector<std::uint8_t> low(whole.size() / 2U);
    std::vector<std::uint8_t> high(whole.size() / 2U);
    for (std::size_t i = 0; i < whole.size() / 2U; ++i) {
        low[i] = whole[i * 2U];
        high[i] = whole[i * 2U + 1U];
    }

    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "mrheli"
parent = "bchopper"
board = "irem_m72"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "clone.lo"
offset = 0
stride = 2

[[region.file]]
name = "parent.hi"
offset = 1
stride = 2
)";
    const std::string parent_manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "bchopper"
board = "irem_m72"

[[region]]
name = "tiles_a"
size = 4

[[region.file]]
name = "parent.gfx"
offset = 0
size = 4
)";

    const auto root =
        std::filesystem::temp_directory_path() / "mnemos_irem_m72_supplemental_parent";
    const auto clone_root = root / "clone";
    const auto parent_dir = root / "supplemental" / "bchopper";
    REQUIRE(
        (std::filesystem::create_directories(clone_root) || std::filesystem::exists(clone_root)));

    const std::vector<std::uint8_t> parent_gfx{0x30U, 0x31U, 0x32U, 0x33U};
    write_directory_rom_set(
        parent_dir, {
                        {"game.toml",
                         std::vector<std::uint8_t>(parent_manifest.begin(), parent_manifest.end())},
                        {"parent.hi", high},
                        {"parent.gfx", parent_gfx},
                    });
    const auto clone_zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"clone.lo", low},
    });
    const auto clone_path = (clone_root / "mrheli.zip").string();
    REQUIRE(mnemos::io::write_file(clone_path, clone_zip));
    auto clone_bytes = mnemos::io::read_file(clone_path);
    REQUIRE(clone_bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*clone_bytes);
    options.display_name = "mrheli";
    options.rom_path = clone_path;
    options.additional_media.emplace_back();
    options.additional_media_paths.push_back(parent_dir.string());
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    CHECK(adapter.machine().roms.issues.empty());
    REQUIRE(adapter.machine().roms.region("tiles_a") != nullptr);
    CHECK(*adapter.machine().roms.region("tiles_a") == parent_gfx);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("irem_m72_adapter reports unresolved declarative clone parents as media issues",
          "[irem_m72][adapter]") {
    const std::string missing_parent = "parent_absent_for_validation";
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "mrheli"
parent = "parent_absent_for_validation"
board = "irem_m72"
orientation = "horizontal"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0
)";

    const auto clone_zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    const auto root =
        std::filesystem::temp_directory_path() / "mnemos_irem_m72_missing_parent_fallback";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    const auto clone_path = (root / "mrheli.zip").string();
    REQUIRE(mnemos::io::write_file(clone_path, clone_zip));
    auto bytes = mnemos::io::read_file(clone_path);
    REQUIRE(bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*bytes);
    options.display_name = "mrheli";
    options.rom_path = clone_path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    REQUIRE(adapter.machine().roms.issues.size() == 1U);
    CHECK(adapter.machine().roms.issues[0].file == missing_parent + ".zip");
    CHECK(adapter.machine().roms.issues[0].message.find("not found") != std::string::npos);

    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 1U);
    REQUIRE(media.media[0].validation_issues.size() == 1U);
    CHECK(media.media[0].validation_issues[0].code == "media.rom_set.load_issue");
    CHECK(media.media[0].validation_issues[0].detail.find(missing_parent + ".zip") !=
          std::string::npos);
    CHECK(media.media[0].validation_issues[0].detail.find("not found") != std::string::npos);

    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("irem_m72_adapter refuses fallback from a mismatched declarative parent zip",
          "[irem_m72][adapter]") {
    const auto whole = make_program();
    std::vector<std::uint8_t> low(whole.size() / 2U);
    std::vector<std::uint8_t> high(whole.size() / 2U);
    for (std::size_t i = 0; i < whole.size() / 2U; ++i) {
        low[i] = whole[i * 2U];
        high[i] = whole[i * 2U + 1U];
    }

    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "mrheli"
parent = "bchopper"
board = "irem_m72"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "clone.lo"
offset = 0
stride = 2

[[region.file]]
name = "parent.hi"
offset = 1
stride = 2
)";
    const std::string mismatched_parent_manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "not_bchopper"
board = "irem_m72"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "parent.hi"
offset = 1
stride = 2
)";

    const auto root =
        std::filesystem::temp_directory_path() / "mnemos_irem_m72_mismatched_parent_fallback";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    const auto parent_zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(mismatched_parent_manifest.begin(),
                                                mismatched_parent_manifest.end())},
        {"parent.hi", high},
    });
    const auto clone_zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"clone.lo", low},
    });
    const auto parent_path = (root / "bchopper.zip").string();
    const auto clone_path = (root / "mrheli.zip").string();
    REQUIRE(mnemos::io::write_file(parent_path, parent_zip));
    REQUIRE(mnemos::io::write_file(clone_path, clone_zip));

    auto clone_bytes = mnemos::io::read_file(clone_path);
    REQUIRE(clone_bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*clone_bytes);
    options.display_name = "mrheli";
    options.rom_path = clone_path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    const auto& issues = adapter.machine().roms.issues;
    CHECK(std::any_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.file == "bchopper.zip/game.toml" &&
               issue.message.find("declares set 'not_bchopper'") != std::string::npos;
    }));
    CHECK(std::any_of(issues.begin(), issues.end(), [](const auto& issue) {
        return issue.file == "parent.hi" &&
               issue.message.find("missing from the ROM set") != std::string::npos;
    }));
    const auto* maincpu = adapter.machine().roms.region("maincpu");
    REQUIRE(maincpu != nullptr);
    CHECK((*maincpu)[0] == whole[0]);
    CHECK((*maincpu)[1] == 0xFFU); // parent.hi was not accepted from the wrong set zip.

    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 1U);
    CHECK(media.media[0].validation_issues.size() == issues.size());
    CHECK(std::any_of(
        media.media[0].validation_issues.begin(), media.media[0].validation_issues.end(),
        [](const auto& issue) { return issue.detail.find("not_bchopper") != std::string::npos; }));
}

TEST_CASE("irem_m72_adapter resolves checked-in manifests for standard set zips",
          "[irem_m72][adapter]") {
    const auto rtype_decl = require_embedded_decl("rtype");
    const auto rtype_zip = make_stored_zip(placeholder_entries_for(rtype_decl, 0x11U));

    const auto root = std::filesystem::temp_directory_path() / "mnemos_irem_m72_embedded_manifest";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    const auto set_path = (root / "rtype.zip").string();
    REQUIRE(mnemos::io::write_file(set_path, rtype_zip));
    auto bytes = mnemos::io::read_file(set_path);
    REQUIRE(bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*bytes);
    options.display_name = "rtype";
    options.rom_path = set_path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    CHECK(adapter.set_name() == "rtype");
    CHECK(adapter.system_spec()[2].value == "rtype");
    CHECK(adapter.machine().params.work_ram_base == 0x40000U);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    REQUIRE(adapter.dip_switches().size() == 13U);
    CHECK(adapter.dip_switches()[0].name == "Lives");
    CHECK(adapter.machine().roms.region("maincpu") != nullptr);
    CHECK(adapter.machine().roms.region("sprites") != nullptr);
    CHECK(has_only_crc_issues(adapter.machine().roms.issues));
    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 1U);
    CHECK(media.media[0].id == "rom_set");
    CHECK(media.media[0].full_hash.size() == 8U);
    REQUIRE(media.media[0].validation_issues.size() == adapter.machine().roms.issues.size());
    CHECK(media.media[0].validation_issues[0].code == "media.rom_set.load_issue");
    CHECK(media.media[0].validation_issues[0].detail.find("crc32 mismatch") != std::string::npos);
}

TEST_CASE("irem_m72_adapter unwraps a single nested standard set zip", "[irem_m72][adapter]") {
    const auto rtype_decl = require_embedded_decl("rtype");
    const auto inner_zip = make_stored_zip(placeholder_entries_for(rtype_decl, 0x21U));
    const auto wrapper_zip = make_stored_zip({{"rtype.zip", inner_zip}});

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = wrapper_zip;
    options.display_name = "wrapped-rtype";
    options.rom_path = "collection.zip";
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    CHECK(adapter.set_name() == "rtype");
    CHECK(adapter.system_spec()[2].value == "rtype");
    CHECK(adapter.machine().params.work_ram_base == 0x40000U);
    CHECK(adapter.machine().roms.region("maincpu") != nullptr);
    CHECK(adapter.machine().roms.region("sprites") != nullptr);
    CHECK(has_only_crc_issues(adapter.machine().roms.issues));
}

TEST_CASE("irem_m72_adapter resolves a clone parent from a sibling wrapper zip",
          "[irem_m72][adapter]") {
    const auto parent_decl = require_embedded_decl("rtype");
    const auto clone_decl = require_embedded_decl("rtypej");
    const auto parent_inner = make_stored_zip(placeholder_entries_for(parent_decl, 0x41U));
    const auto clone_inner = make_stored_zip(placeholder_entries_for(clone_decl, 0x42U));

    const auto root =
        std::filesystem::temp_directory_path() / "mnemos_irem_m72_sibling_wrapper_parent";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    const auto parent_wrapper_path = (root / "R-Type_Arcade_EN.zip").string();
    const auto clone_wrapper_path = (root / "R-Type_Arcade_JA.zip").string();
    REQUIRE(mnemos::io::write_file(parent_wrapper_path,
                                   make_stored_zip({{"rtype.zip", parent_inner}})));
    REQUIRE(
        mnemos::io::write_file(clone_wrapper_path, make_stored_zip({{"rtypej.zip", clone_inner}})));
    auto clone_bytes = mnemos::io::read_file(clone_wrapper_path);
    REQUIRE(clone_bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*clone_bytes);
    options.display_name = "wrapped-rtypej";
    options.rom_path = clone_wrapper_path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    CHECK(adapter.set_name() == "rtypej");
    REQUIRE(adapter.machine().roms.region("tiles_a") != nullptr);
    CHECK(adapter.machine().roms.region("tiles_a")->front() == 0x41U);
    REQUIRE(adapter.machine().roms.region("maincpu") != nullptr);
    CHECK(adapter.machine().roms.region("maincpu")->front() == 0x42U);
    CHECK(has_only_crc_issues(adapter.machine().roms.issues));
}

TEST_CASE("irem_m72_adapter merges supplemental fragments for the same declared set",
          "[irem_m72][adapter]") {
    const auto decl = require_embedded_decl("xmultiplm72");
    const auto primary_all = placeholder_entries_for(decl, 0x51U);
    const auto supplemental_all = placeholder_entries_for(decl, 0x52U);
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> primary_entries;
    std::vector<std::pair<std::string, std::vector<std::uint8_t>>> supplemental_entries;
    for (const auto& entry : primary_all) {
        if (entry.first.rfind("xm_c-", 0U) == 0U) {
            primary_entries.push_back(entry);
        }
    }
    for (const auto& entry : supplemental_all) {
        if (entry.first.rfind("xm_c-", 0U) != 0U) {
            supplemental_entries.push_back(entry);
        }
    }

    const auto root =
        std::filesystem::temp_directory_path() / "mnemos_irem_m72_same_set_supplemental";
    const auto primary_path = root / "X-Multiply_Arcade_JA.zip";
    const auto supplemental_dir = root / "xmultiplm72";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    REQUIRE(mnemos::io::write_file(
        primary_path.string(),
        make_stored_zip({{"xmultiplm72.zip", make_stored_zip(primary_entries)}})));
    write_directory_rom_set(supplemental_dir, supplemental_entries);
    auto primary_bytes = mnemos::io::read_file(primary_path.string());
    REQUIRE(primary_bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*primary_bytes);
    options.display_name = "xmultiplm72";
    options.rom_path = primary_path.string();
    options.additional_media.emplace_back(); // directory-backed supplemental source
    options.additional_media_paths.push_back(supplemental_dir.string());
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    CHECK(adapter.set_name() == "xmultiplm72");
    REQUIRE(adapter.machine().roms.region("maincpu") != nullptr);
    CHECK(adapter.machine().roms.region("maincpu")->front() == 0x51U);
    REQUIRE(adapter.machine().roms.region("sprites") != nullptr);
    CHECK(adapter.machine().roms.region("sprites")->front() == 0x52U);
    REQUIRE(adapter.machine().roms.region("samples") != nullptr);
    CHECK(adapter.machine().roms.region("samples")->front() == 0x52U);
    CHECK(has_only_crc_issues(adapter.machine().roms.issues));
}

TEST_CASE("irem_m72 roster discovery indexes exact sets and wrapper zips", "[irem_m72][adapter]") {
    const auto rtype_decl = require_embedded_decl("rtype");
    const auto imgfight_decl = require_embedded_decl("imgfight");

    const auto root = std::filesystem::temp_directory_path() / "mnemos_irem_m72_roster_discovery";
    const auto exact_root = root / "exact";
    const auto wrapper_root = root / "wrappers";
    const auto nested_wrapper_root = wrapper_root / "i8751";
    std::error_code cleanup_ec;
    std::filesystem::remove_all(root, cleanup_ec);
    REQUIRE(
        (std::filesystem::create_directories(exact_root) || std::filesystem::exists(exact_root)));
    REQUIRE((std::filesystem::create_directories(nested_wrapper_root) ||
             std::filesystem::exists(nested_wrapper_root)));

    const auto rtype_path = exact_root / "rtype.zip";
    const auto imgfight_wrapper_path = nested_wrapper_root / "ImageFight_Arcade_EN.zip";
    REQUIRE(mnemos::io::write_file(rtype_path.string(),
                                   make_stored_zip(placeholder_entries_for(rtype_decl, 0x31U))));
    const auto imgfight_inner = make_stored_zip(placeholder_entries_for(imgfight_decl, 0x32U));
    REQUIRE(mnemos::io::write_file(imgfight_wrapper_path.string(),
                                   make_stored_zip({{"imgfight.zip", imgfight_inner}})));

    const auto indexed = index_m72_source_roots({wrapper_root, exact_root});
    REQUIRE(indexed.find("rtype") != indexed.end());
    CHECK(indexed.find("rtype")->second == rtype_path);
    REQUIRE(indexed.find("imgfight") != indexed.end());
    CHECK(indexed.find("imgfight")->second == imgfight_wrapper_path);
}

TEST_CASE("irem_m72 roster discovery prefers a set zip over a stale set directory",
          "[irem_m72][adapter]") {
    const auto nspirit_decl = require_embedded_decl("nspirit");

    const auto root =
        std::filesystem::temp_directory_path() / "mnemos_irem_m72_zip_over_directory";
    const auto stale_dir = root / "nspirit";
    const auto zip_path = root / "nspirit.zip";
    std::error_code cleanup_ec;
    std::filesystem::remove_all(root, cleanup_ec);
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    write_directory_rom_set(stale_dir, {{"placeholder.bin", {0xFFU}}});
    REQUIRE(mnemos::io::write_file(
        zip_path.string(), make_stored_zip(placeholder_entries_for(nspirit_decl, 0x33U))));

    const auto indexed = index_m72_source_roots({root});
    REQUIRE(indexed.find("nspirit") != indexed.end());
    CHECK(indexed.find("nspirit")->second == zip_path);
}

TEST_CASE("irem_m72_adapter resolves checked-in manifests for standard set directories",
          "[irem_m72][adapter]") {
    const auto rtype_decl = require_embedded_decl("rtype");

    const auto root =
        std::filesystem::temp_directory_path() / "mnemos_irem_m72_embedded_manifest_directory";
    const auto set_dir = root / "rtype";
    write_directory_rom_set(set_dir, placeholder_entries_for(rtype_decl, 0x44U));

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = {};
    options.display_name = "rtype";
    options.rom_path = set_dir.string();
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    CHECK(adapter.set_name() == "rtype");
    CHECK(adapter.system_spec()[2].value == "rtype");
    CHECK(adapter.machine().params.work_ram_base == 0x40000U);
    CHECK(adapter.machine().roms.region("maincpu") != nullptr);
    CHECK(adapter.machine().roms.region("sprites") != nullptr);
    CHECK(has_only_crc_issues(adapter.machine().roms.issues));
}

TEST_CASE("irem_m72_adapter resolves checked-in clone and parent manifests",
          "[irem_m72][adapter]") {
    const auto parent_decl = require_embedded_decl("rtype");
    const auto clone_decl = require_embedded_decl("rtypej");
    const auto parent_zip = make_stored_zip(placeholder_entries_for(parent_decl, 0x22U));
    const auto clone_zip = make_stored_zip(placeholder_entries_for(clone_decl, 0x33U));

    const auto root =
        std::filesystem::temp_directory_path() / "mnemos_irem_m72_embedded_clone_manifest";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    const auto parent_path = (root / "rtype.zip").string();
    const auto clone_path = (root / "rtypej.zip").string();
    REQUIRE(mnemos::io::write_file(parent_path, parent_zip));
    REQUIRE(mnemos::io::write_file(clone_path, clone_zip));
    auto bytes = mnemos::io::read_file(clone_path);
    REQUIRE(bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*bytes);
    options.display_name = "rtypej";
    options.rom_path = clone_path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    CHECK(adapter.set_name() == "rtypej");
    CHECK(adapter.system_spec()[2].value == "rtypej");
    CHECK(adapter.machine().params.work_ram_base == 0x40000U);
    REQUIRE(adapter.dip_switches().size() == 13U);
    CHECK(adapter.dip_switches()[0].name == "Lives");
    REQUIRE(adapter.machine().roms.region("tiles_a") != nullptr);
    CHECK(adapter.machine().roms.region("tiles_a")->front() == 0x22U);
    REQUIRE(adapter.machine().roms.region("maincpu") != nullptr);
    CHECK(adapter.machine().roms.region("maincpu")->front() == 0x33U);
    CHECK(has_only_crc_issues(adapter.machine().roms.issues));
}

TEST_CASE("irem_m72_adapter rejects a game.toml for another board", "[irem_m72][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "wrong_board"
board = "capcom_cps1"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    irem_m72_adapter adapter(zip, "wrong_board");

    const auto& main_region = adapter.machine().roms.regions.at("maincpu");
    REQUIRE(main_region.size() == mnemos::manifests::irem_m72::main_rom_size);
    CHECK(main_region[0xFFFF0U] == 0xFFU);
    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x00U);
}
