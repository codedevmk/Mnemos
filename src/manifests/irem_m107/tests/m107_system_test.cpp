#include "file.hpp"
#include "m107_game_manifests.hpp"
#include "m107_system.hpp"
#include "rom_set_toml.hpp"
#include "zip_archive.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef MNEMOS_IREM_M107_GAMES_DIR
#define MNEMOS_IREM_M107_GAMES_DIR ""
#endif

namespace {

    using mnemos::manifests::common::rom_set_decl;
    using mnemos::manifests::common::rom_set_dip_switch;
    using mnemos::manifests::common::rom_set_file;
    using mnemos::manifests::common::rom_set_image;
    using mnemos::manifests::common::rom_set_region;

    [[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        REQUIRE(in.good());
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }

    [[nodiscard]] const rom_set_region* find_region(const rom_set_decl& decl,
                                                    std::string_view name) noexcept {
        const auto it =
            std::find_if(decl.regions.begin(), decl.regions.end(),
                         [name](const rom_set_region& region) { return region.name == name; });
        return it == decl.regions.end() ? nullptr : &*it;
    }

    [[nodiscard]] const rom_set_dip_switch* find_dip(const rom_set_decl& decl,
                                                     std::string_view name) noexcept {
        const auto it =
            std::find_if(decl.dips.begin(), decl.dips.end(),
                         [name](const rom_set_dip_switch& dip) { return dip.name == name; });
        return it == decl.dips.end() ? nullptr : &*it;
    }

    [[nodiscard]] bool is_sw1_sw2_bank(std::string_view bank) noexcept {
        return bank == "SW1" || bank == "SW2";
    }

    [[nodiscard]] bool is_sw3_bank(std::string_view bank) noexcept { return bank == "SW3"; }

    template <typename Predicate>
    [[nodiscard]] std::uint16_t raw_dip_default(const rom_set_decl& decl, std::uint16_t fallback,
                                                Predicate&& include) noexcept {
        std::uint16_t value = fallback;
        for (const auto& dip : decl.dips) {
            if (!include(dip.bank)) {
                continue;
            }
            value = static_cast<std::uint16_t>((value & ~dip.mask) | dip.default_value);
        }
        return value;
    }

    [[nodiscard]] const rom_set_file* find_file_at_or_after(const rom_set_region& region,
                                                            std::size_t offset) noexcept {
        const auto it =
            std::find_if(region.files.begin(), region.files.end(),
                         [offset](const rom_set_file& file) { return file.offset >= offset; });
        return it == region.files.end() ? nullptr : &*it;
    }

    void require_region_contract(const rom_set_region& region) {
        CHECK(region.size > 0U);
        REQUIRE_FALSE(region.files.empty());
        for (const auto& file : region.files) {
            INFO("region=" << region.name << " file=" << file.name);
            CHECK_FALSE(file.name.empty());
            CHECK(file.offset < region.size);
            CHECK(file.stride >= 1U);
            CHECK(file.unit >= 1U);
            CHECK(file.size > 0U);
            CHECK(file.crc32.has_value());
            const std::size_t source_bytes = file.length == 0U ? file.size : file.length;
            REQUIRE(source_bytes > 0U);
            const std::size_t chunks = (source_bytes + file.unit - 1U) / file.unit;
            const std::size_t last_start = file.offset + (chunks - 1U) * file.stride;
            CHECK(last_start < region.size);
        }
    }

    void require_boot_chunk_reload(const rom_set_region& maincpu) {
        REQUIRE(maincpu.size == mnemos::manifests::irem_m107::main_rom_size);
        const rom_set_file* reload = find_file_at_or_after(maincpu, 0xC0000U);
        REQUIRE(reload != nullptr);
        CHECK(reload->stride == 2U);
        CHECK(reload->crc32.has_value());
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
        for (const auto& [set_name, _] : mnemos::manifests::irem_m107::embedded::game_manifests) {
            names.emplace(std::string{set_name});
        }
        return names;
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

    [[nodiscard]] rom_set_decl require_embedded_decl(std::string_view set_name) {
        const std::string_view text = mnemos::manifests::irem_m107::game_manifest_toml(set_name);
        REQUIRE_FALSE(text.empty());
        auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, std::string{set_name});
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": " << error.message);
        }
        REQUIRE(parsed.ok());
        return std::move(*parsed.value);
    }

    [[nodiscard]] mnemos::manifests::common::rom_file_provider
    require_provider(const std::filesystem::path& path) {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec)) {
            return mnemos::manifests::common::make_directory_rom_provider(path.string());
        }

        auto bytes = mnemos::io::read_file(path.string());
        REQUIRE(bytes.has_value());
        auto provider = mnemos::manifests::common::make_zip_rom_provider(std::move(*bytes));
        REQUIRE(provider.has_value());
        return std::move(*provider);
    }

    [[nodiscard]] bool has_non_fill_byte(const std::vector<std::uint8_t>& bytes,
                                         std::uint8_t fill = 0xFFU) {
        return std::any_of(bytes.begin(), bytes.end(),
                           [fill](std::uint8_t byte) { return byte != fill; });
    }

    void require_loaded_region(const rom_set_image& image, std::string_view name,
                               std::size_t expected_size) {
        const auto* region = image.region(name);
        REQUIRE(region != nullptr);
        CHECK(region->size() == expected_size);
        CHECK(has_non_fill_byte(*region));
    }

    [[nodiscard]] std::vector<std::uint8_t>
    make_m107_program(const std::vector<std::uint8_t>& program) {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m107::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m107_program() {
        return make_m107_program(
            {0xB8U, 0x00U, 0xE0U, 0x8EU, 0xD8U, 0xB0U, 0x42U, 0xA2U, 0x00U, 0x00U, 0xF4U});
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m107_input_port_program() {
        return make_m107_program({0xB8U, 0x00U, 0xE0U, 0x8EU, 0xD8U, // MOV DS,E000
                                  0xE4U, 0x02U, 0xA2U, 0x00U, 0x00U, // IN AL,02; MOV [0],AL
                                  0xE4U, 0x03U, 0xA2U, 0x01U, 0x00U, // IN AL,03; MOV [1],AL
                                  0xE4U, 0x04U, 0xA2U, 0x02U, 0x00U, // IN AL,04; MOV [2],AL
                                  0xE4U, 0x05U, 0xA2U, 0x03U, 0x00U, // IN AL,05; MOV [3],AL
                                  0xF4U});
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m107_sound_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m107::sound_rom_size, 0xFFU);
        rom[0x1FFF0U] = 0xEAU; // JMP E000:0200 through the mirrored sound ROM window
        rom[0x1FFF1U] = 0x00U;
        rom[0x1FFF2U] = 0x02U;
        rom[0x1FFF3U] = 0x00U;
        rom[0x1FFF4U] = 0xE0U;
        rom[0x0200U] = 0xF4U;
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m107_sound_ga20_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m107::sound_rom_size, 0xFFU);
        rom[0x1FFF0U] = 0xEAU; // JMP E000:0200 through the mirrored sound ROM window
        rom[0x1FFF1U] = 0x00U;
        rom[0x1FFF2U] = 0x02U;
        rom[0x1FFF3U] = 0x00U;
        rom[0x1FFF4U] = 0xE0U;
        const std::array<std::uint8_t, 41> program = {
            0xB8U, 0x00U, 0xA8U, 0x8EU, 0xD8U, // MOV DS,A800
            0xB0U, 0x10U, 0xA2U, 0x00U, 0x00U, // GA20 ch0 start low  -> 0x100
            0xB0U, 0x00U, 0xA2U, 0x01U, 0x00U, // GA20 ch0 start high
            0xB0U, 0x40U, 0xA2U, 0x02U, 0x00U, // GA20 ch0 end low    -> 0x400
            0xB0U, 0x00U, 0xA2U, 0x03U, 0x00U, // GA20 ch0 end high
            0xB0U, 0x00U, 0xA2U, 0x04U, 0x00U, // slowest byte advance
            0xB0U, 0xF6U, 0xA2U, 0x05U, 0x00U, // audible volume
            0xB0U, 0x02U, 0xA2U, 0x06U, 0x00U, // control bit 1 = key-on
            0xF4U};                            // HLT
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x0200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m107_sound_command_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m107::sound_rom_size, 0xFFU);
        const std::size_t intp1_vector =
            static_cast<std::size_t>(mnemos::manifests::irem_m107::sound_irq_vector_command_latch) *
            4U;
        rom[intp1_vector + 0U] = 0x00U; // IVT[INTP1] -> 0000:0300
        rom[intp1_vector + 1U] = 0x03U;
        rom[intp1_vector + 2U] = 0x00U;
        rom[intp1_vector + 3U] = 0x00U;
        rom[0x1FFF0U] = 0xEAU; // JMP E000:0200 through the mirrored sound ROM window
        rom[0x1FFF1U] = 0x00U;
        rom[0x1FFF2U] = 0x02U;
        rom[0x1FFF3U] = 0x00U;
        rom[0x1FFF4U] = 0xE0U;
        const std::vector<std::uint8_t> wait_program{
            0xFBU, // STI
            0xF4U, // HLT, then wake on the command-latch IRQ
            0xF4U  // HLT again if the handler ever returns
        };
        for (std::size_t i = 0; i < wait_program.size(); ++i) {
            rom[0x0200U + i] = wait_program[i];
        }
        const std::vector<std::uint8_t> handler{
            0xB8U, 0x00U, 0xA8U, // MOV AX,A800
            0x8EU, 0xD8U,        // MOV DS,AX
            0xA0U, 0x44U, 0x00U, // MOV AL,[0044] (main sound command latch)
            0x88U, 0xC3U,        // MOV BL,AL
            0xA2U, 0x44U, 0x00U, // MOV [0044],AL (acknowledge command IRQ)
            0xA2U, 0x46U, 0x00U, // MOV [0046],AL (sound reply)
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000
            0x8EU, 0xD8U,        // MOV DS,AX
            0x8AU, 0xC3U,        // MOV AL,BL
            0xA2U, 0x00U, 0x00U, // MOV [0000],AL
            0xF4U                // HLT
        };
        for (std::size_t i = 0; i < handler.size(); ++i) {
            rom[0x0300U + i] = handler[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m107_sound_ym_irq_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m107::sound_rom_size, 0xFFU);
        const std::size_t intp0_vector =
            static_cast<std::size_t>(mnemos::manifests::irem_m107::sound_irq_vector_ym2151) * 4U;
        rom[intp0_vector + 0U] = 0x00U; // IVT[INTP0] -> 0000:0300
        rom[intp0_vector + 1U] = 0x03U;
        rom[intp0_vector + 2U] = 0x00U;
        rom[intp0_vector + 3U] = 0x00U;
        rom[0x1FFF0U] = 0xEAU; // JMP E000:0200 through the mirrored sound ROM window
        rom[0x1FFF1U] = 0x00U;
        rom[0x1FFF2U] = 0x02U;
        rom[0x1FFF3U] = 0x00U;
        rom[0x1FFF4U] = 0xE0U;
        const std::vector<std::uint8_t> wait_program{
            0xFBU, // STI
            0xF4U, // HLT, then wake on the YM2151 INTP0 IRQ
            0xF4U  // HLT again if the handler ever returns
        };
        for (std::size_t i = 0; i < wait_program.size(); ++i) {
            rom[0x0200U + i] = wait_program[i];
        }
        const std::vector<std::uint8_t> handler{
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000
            0x8EU, 0xD8U,        // MOV DS,AX
            0xB0U, 0xA5U,        // MOV AL,A5
            0xA2U, 0x01U, 0x00U, // MOV [0001],AL
            0xF4U                // HLT
        };
        for (std::size_t i = 0; i < handler.size(); ++i) {
            rom[0x0300U + i] = handler[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m107_sound_irq_priority_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m107::sound_rom_size, 0xFFU);
        const std::size_t intp0_vector =
            static_cast<std::size_t>(mnemos::manifests::irem_m107::sound_irq_vector_ym2151) * 4U;
        const std::size_t intp1_vector =
            static_cast<std::size_t>(mnemos::manifests::irem_m107::sound_irq_vector_command_latch) *
            4U;
        rom[intp0_vector + 0U] = 0x00U; // IVT[INTP0] -> 0000:0300
        rom[intp0_vector + 1U] = 0x03U;
        rom[intp0_vector + 2U] = 0x00U;
        rom[intp0_vector + 3U] = 0x00U;
        rom[intp1_vector + 0U] = 0x20U; // IVT[INTP1] -> 0000:0320
        rom[intp1_vector + 1U] = 0x03U;
        rom[intp1_vector + 2U] = 0x00U;
        rom[intp1_vector + 3U] = 0x00U;
        rom[0x1FFF0U] = 0xEAU; // JMP E000:0200 through the mirrored sound ROM window
        rom[0x1FFF1U] = 0x00U;
        rom[0x1FFF2U] = 0x02U;
        rom[0x1FFF3U] = 0x00U;
        rom[0x1FFF4U] = 0xE0U;
        const std::vector<std::uint8_t> wait_program{
            0xFBU, // STI
            0xF4U, // HLT, then wake on the selected sound IRQ
            0xF4U  // HLT again if the handler ever returns
        };
        for (std::size_t i = 0; i < wait_program.size(); ++i) {
            rom[0x0200U + i] = wait_program[i];
        }
        const std::vector<std::uint8_t> intp0_handler{
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000
            0x8EU, 0xD8U,        // MOV DS,AX
            0xB0U, 0xA0U,        // MOV AL,A0
            0xA2U, 0x02U, 0x00U, // MOV [0002],AL
            0xF4U                // HLT
        };
        const std::vector<std::uint8_t> intp1_handler{
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000
            0x8EU, 0xD8U,        // MOV DS,AX
            0xB0U, 0xC1U,        // MOV AL,C1
            0xA2U, 0x02U, 0x00U, // MOV [0002],AL
            0xF4U                // HLT
        };
        for (std::size_t i = 0; i < intp0_handler.size(); ++i) {
            rom[0x0300U + i] = intp0_handler[i];
        }
        for (std::size_t i = 0; i < intp1_handler.size(); ++i) {
            rom[0x0320U + i] = intp1_handler[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m107_sound_irq_priority_chain_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m107::sound_rom_size, 0xFFU);
        const std::size_t intp0_vector =
            static_cast<std::size_t>(mnemos::manifests::irem_m107::sound_irq_vector_ym2151) * 4U;
        const std::size_t intp1_vector =
            static_cast<std::size_t>(mnemos::manifests::irem_m107::sound_irq_vector_command_latch) *
            4U;
        rom[intp0_vector + 0U] = 0x00U; // IVT[INTP0] -> 0000:0300
        rom[intp0_vector + 1U] = 0x03U;
        rom[intp0_vector + 2U] = 0x00U;
        rom[intp0_vector + 3U] = 0x00U;
        rom[intp1_vector + 0U] = 0x40U; // IVT[INTP1] -> 0000:0340
        rom[intp1_vector + 1U] = 0x03U;
        rom[intp1_vector + 2U] = 0x00U;
        rom[intp1_vector + 3U] = 0x00U;
        rom[0x1FFF0U] = 0xEAU; // JMP E000:0200 through the mirrored sound ROM window
        rom[0x1FFF1U] = 0x00U;
        rom[0x1FFF2U] = 0x02U;
        rom[0x1FFF3U] = 0x00U;
        rom[0x1FFF4U] = 0xE0U;
        const std::vector<std::uint8_t> wait_program{
            0xFBU, // STI
            0xF4U, // HLT, then wake on the selected sound IRQ
            0xF4U  // HLT again if the handlers ever return
        };
        for (std::size_t i = 0; i < wait_program.size(); ++i) {
            rom[0x0200U + i] = wait_program[i];
        }
        const std::vector<std::uint8_t> intp0_handler{
            0xB8U, 0x00U, 0xA8U, // MOV AX,A800
            0x8EU, 0xD8U,        // MOV DS,AX
            0xB0U, 0x14U,        // MOV AL,14
            0xA2U, 0x40U, 0x00U, // MOV [0040],AL (YM2151 timer control address)
            0xB0U, 0x15U,        // MOV AL,15 (reset Timer A flag, keep Timer A/IRQ enabled)
            0xA2U, 0x41U, 0x00U, // MOV [0041],AL
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000
            0x8EU, 0xD8U,        // MOV DS,AX
            0xB0U, 0xA0U,        // MOV AL,A0
            0xA2U, 0x02U, 0x00U, // MOV [0002],AL
            0xFBU,               // STI, then let the pending lower-priority source through
            0xF4U                // HLT
        };
        const std::vector<std::uint8_t> intp1_handler{
            0xB8U, 0x00U, 0xA8U, // MOV AX,A800
            0x8EU, 0xD8U,        // MOV DS,AX
            0xA0U, 0x44U, 0x00U, // MOV AL,[0044] (main sound command latch)
            0x88U, 0xC3U,        // MOV BL,AL
            0xA2U, 0x44U, 0x00U, // MOV [0044],AL (acknowledge command IRQ)
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000
            0x8EU, 0xD8U,        // MOV DS,AX
            0x8AU, 0xC3U,        // MOV AL,BL
            0xA2U, 0x03U, 0x00U, // MOV [0003],AL
            0xF4U                // HLT
        };
        for (std::size_t i = 0; i < intp0_handler.size(); ++i) {
            rom[0x0300U + i] = intp0_handler[i];
        }
        for (std::size_t i = 0; i < intp1_handler.size(); ++i) {
            rom[0x0340U + i] = intp1_handler[i];
        }
        return rom;
    }

    [[nodiscard]] rom_set_image synthetic_m107_image() {
        rom_set_image image;
        image.regions.emplace("maincpu", synthetic_m107_program());
        image.regions.emplace("soundcpu", synthetic_m107_sound_program());
        std::vector<std::uint8_t> gfx(0x4000U, 0x00U);
        for (std::size_t i = 0; i < gfx.size(); ++i) {
            gfx[i] = static_cast<std::uint8_t>((i * 37U) & 0xFFU);
        }
        image.regions.emplace("gfx", std::move(gfx));
        image.regions.emplace("samples", std::vector<std::uint8_t>(0x1000U, 0x55U));
        image.regions.emplace("subdata", std::vector<std::uint8_t>(0x1000U, 0xA5U));
        return image;
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

} // namespace

TEST_CASE("m107 checked-in game manifests parse and cover local candidate corpus",
          "[m107][romset]") {
    namespace fs = std::filesystem;

    const fs::path games_dir{MNEMOS_IREM_M107_GAMES_DIR};
    REQUIRE_FALSE(games_dir.empty());
    REQUIRE(fs::exists(games_dir));

    std::map<std::string, rom_set_decl, std::less<>> declarations;
    for (const fs::directory_entry& entry : fs::directory_iterator(games_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".toml") {
            continue;
        }
        const std::string text = read_text_file(entry.path());
        auto parsed =
            mnemos::manifests::common::parse_rom_set_decl(text, entry.path().filename().string());
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": " << error.message);
        }
        REQUIRE(parsed.ok());

        const rom_set_decl& decl = *parsed.value;
        INFO("set=" << decl.name);
        declarations.emplace(decl.name, std::move(*parsed.value));
    }

    const std::set<std::string, std::less<>> expected_names{"airass", "firebarr"};
    std::set<std::string, std::less<>> names;
    for (const auto& [set_name, decl] : declarations) {
        INFO("set=" << set_name);
        names.insert(decl.name);
        CHECK(decl.board == "irem_m107");
        CHECK(decl.orientation == mnemos::manifests::common::screen_orientation::horizontal);

        const rom_set_region* maincpu = find_region(decl, "maincpu");
        REQUIRE(maincpu != nullptr);
        require_region_contract(*maincpu);
        require_boot_chunk_reload(*maincpu);

        REQUIRE(find_region(decl, "soundcpu") != nullptr);
        CHECK(find_region(decl, "soundcpu")->size == mnemos::manifests::irem_m107::sound_rom_size);
        require_region_contract(*find_region(decl, "soundcpu"));

        REQUIRE(find_region(decl, "gfx") != nullptr);
        require_region_contract(*find_region(decl, "gfx"));
        if (const auto* samples = find_region(decl, "samples")) {
            require_region_contract(*samples);
        }
        if (const auto* subdata = find_region(decl, "subdata")) {
            require_region_contract(*subdata);
        }

        CHECK(decl.dips.size() == 12U);
        const rom_set_dip_switch* lives = find_dip(decl, "Lives");
        REQUIRE(lives != nullptr);
        CHECK(lives->bank == "SW1");
        CHECK(lives->mask == 0x0003U);
        REQUIRE(lives->options.size() == 4U);

        const rom_set_dip_switch* coin_mode = find_dip(decl, "Coin Mode");
        REQUIRE(coin_mode != nullptr);
        CHECK(coin_mode->mask == 0x0800U);
        CHECK(coin_mode->default_value == 0x0800U);

        const rom_set_dip_switch* coinage = find_dip(decl, "Coinage");
        REQUIRE(coinage != nullptr);
        REQUIRE(coinage->condition.has_value());
        CHECK(coinage->condition->mask == 0x0800U);
        CHECK(coinage->condition->value == 0x0800U);

        const rom_set_dip_switch* rapid_fire = find_dip(decl, "Rapid Fire");
        REQUIRE(rapid_fire != nullptr);
        CHECK(rapid_fire->bank == "SW3");
        CHECK(rapid_fire->mask == 0x0C00U);
        CHECK(rapid_fire->default_value == 0x0800U);
        REQUIRE(rapid_fire->options.size() == 4U);

        const rom_set_dip_switch* continuous_play = find_dip(decl, "Continuous Play");
        REQUIRE(continuous_play != nullptr);
        CHECK(continuous_play->bank == "SW3");
        CHECK(continuous_play->mask == 0x1000U);
        CHECK(continuous_play->default_value == 0x0000U);
        REQUIRE(continuous_play->options.size() == 2U);

        CHECK(raw_dip_default(decl, 0xFFFFU, is_sw1_sw2_bank) == 0xFFBFU);
        CHECK(raw_dip_default(decl, 0xFFFFU, is_sw3_bank) == 0xEBFFU);
    }

    CHECK(names == expected_names);
    CHECK(mnemos::manifests::irem_m107::board_params_for("airass").rom_layout == "air_assault");
    CHECK(mnemos::manifests::irem_m107::board_params_for("firebarr").rom_layout == "fire_barrel");
}

TEST_CASE("m107 embedded game manifests mirror the checked-in roster", "[m107][romset]") {
    using mnemos::manifests::irem_m107::embedded::game_manifests;

    CHECK(game_manifests.size() == 2U);
    CHECK_FALSE(mnemos::manifests::irem_m107::game_manifest_toml("airass").empty());
    CHECK_FALSE(mnemos::manifests::irem_m107::game_manifest_toml("firebarr").empty());
    CHECK(mnemos::manifests::irem_m107::game_manifest_toml("rtype2").empty());
}

TEST_CASE("m107 executable board maps V-series reset and RAM", "[m107][board]") {
    namespace m107 = mnemos::manifests::irem_m107;

    auto system = m107::assemble_m107(synthetic_m107_image(), m107::board_params_for("airass"));
    REQUIRE(system != nullptr);

    CHECK(system->main_bus.read8(0xFFFF0U) == 0xEAU);
    CHECK(system->sound_bus.read8(0xFFFF0U) == 0xEAU);
    system->main_bus.write8(m107::work_ram_base, 0x12U);
    system->main_bus.write8(m107::vram_base, 0x34U);
    system->main_bus.write8(
        m107::sprite_ram_base + static_cast<std::uint32_t>(m107::sprite_ram_size - 1U), 0x56U);
    system->main_bus.write8(
        m107::palette_ram_base + static_cast<std::uint32_t>(m107::palette_ram_size - 1U), 0x78U);
    CHECK(system->work_ram[0] == 0x12U);
    CHECK(system->vram[0] == 0x34U);
    CHECK(system->sprite_ram.back() == 0x56U);
    CHECK(system->palette_ram.back() == 0x78U);

    system->sound_bus.write8(m107::sound_work_ram_base, 0x9AU);
    CHECK(system->sound_ram[0] == 0x9AU);
    system->sound_bus.write8(m107::sound_ga20_base + mnemos::chips::audio::irem_ga20::reg_start_low,
                             0x10U);
    system->sound_bus.write8(
        m107::sound_ga20_base + mnemos::chips::audio::irem_ga20::reg_start_high, 0x00U);
    system->sound_bus.write8(m107::sound_ga20_base + mnemos::chips::audio::irem_ga20::reg_end_low,
                             0x40U);
    system->sound_bus.write8(m107::sound_ga20_base + mnemos::chips::audio::irem_ga20::reg_end_high,
                             0x00U);
    system->sound_bus.write8(m107::sound_ga20_base + mnemos::chips::audio::irem_ga20::reg_rate,
                             0x00U);
    system->sound_bus.write8(m107::sound_ga20_base + mnemos::chips::audio::irem_ga20::reg_volume,
                             0xF6U);
    system->sound_bus.write8(m107::sound_ga20_base + mnemos::chips::audio::irem_ga20::reg_control,
                             mnemos::chips::audio::irem_ga20::control_key_on);
    CHECK(system->sound_bus.read8(m107::sound_ga20_base +
                                  mnemos::chips::audio::irem_ga20::reg_status) ==
          mnemos::chips::audio::irem_ga20::status_active);
    system->sound_bus.write8(m107::sound_ym2151_base, 0x20U);
    CHECK(system->ym_address == 0x20U);
    system->write_sound_latch(0x5CU);
    CHECK(system->sound_latch_pending);
    CHECK(system->sound_bus.read8(m107::sound_latch_addr) == 0x5CU);
    CHECK(system->sound_latch_pending);
    system->sound_bus.write8(m107::sound_latch_addr, 0x00U);
    CHECK_FALSE(system->sound_latch_pending);
    system->sound_bus.write8(m107::sound_reply_addr, 0x6DU);
    CHECK(system->sound_reply == 0x6DU);
    CHECK(system->sound_reply_pending);

    system->run_frame();
    CHECK(system->work_ram[0] == 0x42U);
    CHECK(frame_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m107 main CPU sees COINS_DSW3 and SW1/SW2 as separate port words",
          "[m107][board][input]") {
    namespace m107 = mnemos::manifests::irem_m107;

    auto image = synthetic_m107_image();
    image.regions["maincpu"] = synthetic_m107_input_port_program();

    m107::m107_board_params params = m107::board_params_for("airass");
    params.dip_default = 0xFFBFU;
    params.coins_dsw3_default = 0xEBFFU;

    auto system = m107::assemble_m107(std::move(image), params);
    REQUIRE(system != nullptr);
    system->set_inputs(0xFFU, 0xFFU, 0xE7U);

    system->run_frame();

    CHECK(system->work_ram[0] == 0xE7U);
    CHECK(system->work_ram[1] == 0xEBU);
    CHECK(system->work_ram[2] == 0xBFU);
    CHECK(system->work_ram[3] == 0xFFU);
}

TEST_CASE("m107 board declares V33 main and V35 sound CPU clocks", "[m107][board]") {
    namespace m107 = mnemos::manifests::irem_m107;

    auto system = m107::assemble_m107(synthetic_m107_image(), m107::board_params_for("airass"));
    REQUIRE(system != nullptr);

    CHECK(system->main_cpu.metadata().part_number == "v33");
    CHECK(system->sound_cpu.metadata().part_number == "v35");
    CHECK(m107::main_clock_hz == 14'000'000U);
    CHECK(m107::sound_cpu_clock_hz == 14'318'181U);
    CHECK(m107::main_cycles_per_frame == 254'462U);
    CHECK(m107::sound_cycles_per_frame == 260'245U);
    CHECK(m107::sound_irq_vector_ym2151 == 24U);
    CHECK(m107::sound_irq_vector_command_latch == 25U);
}

TEST_CASE("m107 sound CPU drives the Irem GA20 register window", "[m107][board][audio]") {
    namespace m107 = mnemos::manifests::irem_m107;

    auto image = synthetic_m107_image();
    image.regions["soundcpu"] = synthetic_m107_sound_ga20_program();
    image.regions["samples"] = std::vector<std::uint8_t>(0x1000U, 0x55U);

    auto system = m107::assemble_m107(std::move(image), m107::board_params_for("airass"));
    REQUIRE(system != nullptr);
    CHECK(system->pcm.metadata().part_number == "GA20");
    CHECK(system->pcm.capture_divider() == m107::pcm_capture_divider);

    system->run_frame();

    CHECK(system->pcm.read_register(mnemos::chips::audio::irem_ga20::reg_status) ==
          mnemos::chips::audio::irem_ga20::status_active);
    CHECK(system->pcm.last_sample() < 0);
}

TEST_CASE("m107 sound command latch reaches the V35 and returns a reply", "[m107][board][audio]") {
    namespace m107 = mnemos::manifests::irem_m107;

    auto image = synthetic_m107_image();
    image.regions["maincpu"] = make_m107_program({0xB0U, 0x5AU, 0xE6U, 0x00U, 0xF4U});

    SECTION("unread command remains pending") {
        image.regions["soundcpu"] = synthetic_m107_sound_program();

        auto system = m107::assemble_m107(std::move(image), m107::board_params_for("airass"));
        REQUIRE(system != nullptr);

        system->run_frame();

        CHECK(system->sound_latch == 0x5AU);
        CHECK(system->sound_latch_pending);
        CHECK_FALSE(system->sound_reply_pending);
    }

    SECTION("V35 INTP1 handler acknowledges the command latch and replies") {
        image.regions["soundcpu"] = synthetic_m107_sound_command_program();

        auto system = m107::assemble_m107(std::move(image), m107::board_params_for("airass"));
        REQUIRE(system != nullptr);

        system->run_frame();

        CHECK(system->sound_latch == 0x5AU);
        CHECK_FALSE(system->sound_latch_pending);
        CHECK(system->sound_reply == 0x5AU);
        CHECK(system->sound_reply_pending);
        CHECK(system->sound_ram[0] == 0x5AU);
    }
}

TEST_CASE("m107 YM2151 IRQ reaches the V35 INTP0 vector", "[m107][board][audio]") {
    namespace m107 = mnemos::manifests::irem_m107;

    auto image = synthetic_m107_image();
    image.regions["soundcpu"] = synthetic_m107_sound_ym_irq_program();

    auto system = m107::assemble_m107(std::move(image), m107::board_params_for("airass"));
    REQUIRE(system != nullptr);

    system->run_frame();
    CHECK(system->sound_ram[1] == 0x00U);
    CHECK(system->sound_cpu.halted());

    system->fm.write_address(0x10U);
    system->fm.write_data(0xFFU);
    system->fm.write_address(0x11U);
    system->fm.write_data(0x02U); // CLKA = 1022, overflow after 128 OPM clocks.
    system->fm.write_address(0x14U);
    system->fm.write_data(0x05U); // run Timer A + enable IRQ.
    system->fm.tick(128U);
    REQUIRE(system->fm.irq_asserted());

    system->sound_cpu.tick(256U);
    CHECK(system->sound_ram[1] == 0xA5U);
}

TEST_CASE("m107 simultaneous sound IRQs prefer YM2151 INTP0 over command INTP1",
          "[m107][board][audio]") {
    namespace m107 = mnemos::manifests::irem_m107;

    auto image = synthetic_m107_image();
    image.regions["soundcpu"] = synthetic_m107_sound_irq_priority_program();

    auto system = m107::assemble_m107(std::move(image), m107::board_params_for("airass"));
    REQUIRE(system != nullptr);

    system->run_frame();
    CHECK(system->sound_ram[2] == 0x00U);
    REQUIRE(system->sound_cpu.halted());

    system->write_sound_latch(0x44U);
    REQUIRE(system->sound_latch_pending);

    system->fm.write_address(0x10U);
    system->fm.write_data(0xFFU);
    system->fm.write_address(0x11U);
    system->fm.write_data(0x02U);
    system->fm.write_address(0x14U);
    system->fm.write_data(0x05U);
    system->fm.tick(128U);
    REQUIRE(system->fm.irq_asserted());

    system->sound_cpu.tick(256U);
    CHECK(system->sound_ram[2] == 0xA0U);
    CHECK(system->sound_latch_pending);
}

TEST_CASE("m107 pending command IRQ is serviced after YM2151 INTP0 clears",
          "[m107][board][audio]") {
    namespace m107 = mnemos::manifests::irem_m107;

    auto image = synthetic_m107_image();
    image.regions["soundcpu"] = synthetic_m107_sound_irq_priority_chain_program();

    auto system = m107::assemble_m107(std::move(image), m107::board_params_for("airass"));
    REQUIRE(system != nullptr);

    system->run_frame();
    CHECK(system->sound_ram[2] == 0x00U);
    CHECK(system->sound_ram[3] == 0x00U);
    REQUIRE(system->sound_cpu.halted());

    system->write_sound_latch(0x44U);
    REQUIRE(system->sound_latch_pending);

    system->fm.write_address(0x10U);
    system->fm.write_data(0xFFU);
    system->fm.write_address(0x11U);
    system->fm.write_data(0x02U);
    system->fm.write_address(0x14U);
    system->fm.write_data(0x05U);
    system->fm.tick(128U);
    REQUIRE(system->fm.irq_asserted());

    system->sound_cpu.tick(4096U);
    CHECK(system->sound_ram[2] == 0xA0U);
    CHECK(system->sound_ram[3] == 0x44U);
    CHECK_FALSE(system->fm.irq_asserted());
    CHECK_FALSE(system->sound_latch_pending);
    CHECK(system->sound_cpu.halted());
}

TEST_CASE("m107 save state preserves board identity and runtime state", "[m107][board]") {
    namespace m107 = mnemos::manifests::irem_m107;

    auto source = m107::assemble_m107(synthetic_m107_image(), m107::board_params_for("airass"));
    source->set_inputs(0xFEU, 0xFDU, 0xFBU);
    source->coins_dsw3 = 0xEBFFU;
    source->write_sound_latch(0x76U);
    source->write_sound_reply(0x34U);
    source->run_frame();
    source->work_ram[7] = 0x6CU;

    std::vector<std::uint8_t> state;
    mnemos::chips::state_writer writer(state);
    source->save_state(writer);
    REQUIRE_FALSE(state.empty());

    mnemos::chips::state_reader header_reader(state);
    CHECK(header_reader.u32() == m107::m107_system_state_version);

    std::vector<std::uint8_t> old_version_state = state;
    REQUIRE(old_version_state.size() >= sizeof(std::uint32_t));
    old_version_state[0] = static_cast<std::uint8_t>(m107::m107_system_state_version - 1U);
    auto old_version =
        m107::assemble_m107(synthetic_m107_image(), m107::board_params_for("airass"));
    mnemos::chips::state_reader old_reader(old_version_state);
    old_version->load_state(old_reader);
    CHECK_FALSE(old_reader.ok());

    auto restored = m107::assemble_m107(synthetic_m107_image(), m107::board_params_for("airass"));
    mnemos::chips::state_reader reader(state);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored->work_ram[0] == 0x42U);
    CHECK(restored->work_ram[7] == 0x6CU);
    CHECK(restored->input_p1 == 0xFEU);
    CHECK(restored->coins_dsw3 == 0xEBFFU);
    CHECK(restored->sound_latch == 0x76U);
    CHECK(restored->sound_latch_pending);
    CHECK(restored->sound_reply == 0x34U);
    CHECK(restored->sound_reply_pending);

    auto wrong_layout =
        m107::assemble_m107(synthetic_m107_image(), m107::board_params_for("firebarr"));
    mnemos::chips::state_reader wrong_reader(state);
    wrong_layout->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}

TEST_CASE("m107 local artifacts load CRC-clean through embedded manifests",
          "[m107][romset][data]") {
    namespace m107 = mnemos::manifests::irem_m107;

    const auto dir_env = environment_value("MNEMOS_M107_SET_DIR");
    if (!dir_env.has_value() || dir_env->empty()) {
        SKIP("set MNEMOS_M107_SET_DIR to directories containing the M107 zip/folder corpus");
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
        FAIL("missing M107 artifacts: " << missing.str());
    }

    for (const auto& set_name : expected_sets) {
        INFO("set=" << set_name);
        const auto decl = require_embedded_decl(set_name);
        const auto source_it = indexed_sources.find(set_name);
        REQUIRE(source_it != indexed_sources.end());
        INFO("source=" << source_it->second.string());

        const auto image =
            mnemos::manifests::common::load_rom_set(decl, require_provider(source_it->second));
        for (const auto& issue : image.issues) {
            INFO(issue.file << ": " << issue.message);
        }
        CHECK(image.issues.empty());

        require_loaded_region(image, "maincpu", m107::main_rom_size);
        require_loaded_region(image, "soundcpu", m107::sound_rom_size);
        const auto* gfx = find_region(decl, "gfx");
        REQUIRE(gfx != nullptr);
        require_loaded_region(image, "gfx", gfx->size);
        if (const auto* samples = find_region(decl, "samples")) {
            require_loaded_region(image, "samples", samples->size);
        }
        if (const auto* subdata = find_region(decl, "subdata")) {
            require_loaded_region(image, "subdata", subdata->size);
        }
    }
}
