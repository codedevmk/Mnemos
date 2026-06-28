#include "irem_travrusa_adapter.hpp"

#include "adapter_registry.hpp"
#include "crc32.hpp"
#include "file.hpp"
#include "travrusa_game_manifests.hpp"
#include "rom_set.hpp"
#include "rom_set_toml.hpp"
#include "zip_archive.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace mnemos::apps::player::adapters::irem_travrusa {

    namespace {
        namespace fs = std::filesystem;
        namespace travrusa = mnemos::manifests::irem_travrusa;

        using mnemos::manifests::common::rom_load_issue;
        using mnemos::manifests::common::rom_set_dip_switch;
        using mnemos::manifests::common::rom_set_image;

        struct loaded_set final {
            rom_set_image image;
            std::string set_name;
            std::vector<rom_set_dip_switch> dip_switches{};
        };

        struct nested_zip_source final {
            std::vector<std::uint8_t> bytes;
            std::string entry_name;
        };

        struct provider_source final {
            mnemos::manifests::common::rom_file_provider provider;
            std::string source;
        };

        struct parent_resolution final {
            mnemos::manifests::common::rom_file_provider provider;
            std::optional<mnemos::manifests::common::rom_set_decl> decl;
            std::vector<rom_load_issue> issues;
        };

        struct supplemental_rom_source final {
            std::vector<std::uint8_t> rom;
            std::string path;
        };

        frontend_sdk::session_capability_info make_session_capabilities() {
            frontend_sdk::session_capability_info session{};
            session.input_ports = {
                {.port_index = 0U,
                 .player_slot = 1U,
                 .format = frontend_sdk::input_device_format::arcade_panel,
                 .device_id = "irem_travrusa.panel.p1",
                 .label = "Player 1 Panel"},
                {.port_index = 1U,
                 .player_slot = 2U,
                 .format = frontend_sdk::input_device_format::arcade_panel,
                 .device_id = "irem_travrusa.panel.p2",
                 .label = "Player 2 Panel"},
            };
            session.deterministic_frame_input = true;
            session.save_state_supported = true;
            session.frame_exact_save_state = true;
            session.max_input_delay_frames = 8U;
            return session;
        }

        [[nodiscard]] std::uint32_t crc32_u64(std::uint32_t crc, std::uint64_t value) noexcept {
            std::array<std::uint8_t, 8> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
            return mnemos::security::cryptography::crc32(
                std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
        }

        [[nodiscard]] std::string hex32(std::uint32_t value) {
            static constexpr char digits[] = "0123456789abcdef";
            std::string out(8U, '0');
            for (std::size_t i = 0; i < out.size(); ++i) {
                const auto shift = static_cast<unsigned>((out.size() - 1U - i) * 4U);
                out[i] = digits[(value >> shift) & 0x0FU];
            }
            return out;
        }

        [[nodiscard]] std::uint32_t crc32_string(std::uint32_t crc,
                                                 std::string_view text) noexcept {
            crc = crc32_u64(crc, text.size());
            return mnemos::security::cryptography::crc32(text, crc);
        }

        [[nodiscard]] std::uint64_t resident_image_byte_count(const rom_set_image& image) noexcept {
            std::uint64_t bytes = 0U;
            for (const auto& [_, region] : image.regions) {
                bytes += region.size();
            }
            return bytes;
        }

        [[nodiscard]] std::int16_t clamp_mix(std::int32_t value) noexcept {
            if (value > 32767) {
                return 32767;
            }
            if (value < -32768) {
                return -32768;
            }
            return static_cast<std::int16_t>(value);
        }

        [[nodiscard]] std::string resident_media_crc32(const loaded_set& set) {
            if (set.image.regions.empty()) {
                return {};
            }
            std::uint32_t crc = mnemos::security::cryptography::crc32("irem_travrusa.resident_media.v1");
            crc = crc32_string(crc, set.set_name);
            crc = crc32_u64(crc, set.image.regions.size());
            for (const auto& [name, bytes] : set.image.regions) {
                crc = crc32_string(crc, name);
                crc = crc32_u64(crc, bytes.size());
                crc = mnemos::security::cryptography::crc32(
                    std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
            }
            return hex32(crc);
        }

        [[nodiscard]] std::uint16_t
        dip_default_from_manifest(std::span<const rom_set_dip_switch> switches,
                                  std::uint16_t fallback) noexcept {
            std::uint16_t value = fallback;
            for (const auto& dip : switches) {
                value = static_cast<std::uint16_t>((value & ~dip.mask) | dip.default_value);
            }
            return value;
        }

        frontend_sdk::media_capability_info make_media_capabilities(std::string_view display_name,
                                                                    const loaded_set& set,
                                                                    std::uint64_t source_bytes) {
            const std::uint64_t resident_bytes = resident_image_byte_count(set.image);
            std::vector<frontend_sdk::media_validation_issue> validation_issues;
            validation_issues.reserve(set.image.issues.size());
            for (const rom_load_issue& issue : set.image.issues) {
                std::string detail = issue.message;
                if (!issue.file.empty()) {
                    detail = issue.file + ": " + detail;
                }
                validation_issues.push_back(
                    {.code = "media.rom_set.load_issue", .detail = std::move(detail)});
            }
            std::string full_hash = resident_media_crc32(set);
            frontend_sdk::media_capability_info media{};
            media.media.push_back(frontend_sdk::media_image_info{
                .id = "rom_set",
                .label = display_name.empty() ? std::string{"ROM set"} : std::string{display_name},
                .residency = frontend_sdk::media_residency::resident,
                .byte_count = resident_bytes == 0U ? source_bytes : resident_bytes,
                .hash_algorithm = full_hash.empty() ? frontend_sdk::media_hash_algorithm::none
                                                    : frontend_sdk::media_hash_algorithm::crc32,
                .full_hash = std::move(full_hash),
                .provider_id = "irem_travrusa.adapter",
                .revision = "loaded",
                .cache_hint = "resident",
                .validation_issues = std::move(validation_issues)});
            return media;
        }

        [[nodiscard]] bool ends_with_zip(std::string_view value) noexcept {
            constexpr std::string_view ext = ".zip";
            if (value.size() < ext.size()) {
                return false;
            }
            value.remove_prefix(value.size() - ext.size());
            for (std::size_t i = 0; i < ext.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(value[i])) != ext[i]) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool has_zip_signature(std::span<const std::uint8_t> bytes) noexcept {
            return bytes.size() >= 4U && bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 0x03U &&
                   bytes[3] == 0x04U;
        }

        [[nodiscard]] bool is_directory_path(const std::string& path) {
            if (path.empty()) {
                return false;
            }
            std::error_code ec;
            return fs::is_directory(fs::path{path}, ec);
        }

        [[nodiscard]] bool is_plain_set_id(std::string_view value) noexcept {
            if (value.empty()) {
                return false;
            }
            for (char c : value) {
                const auto u = static_cast<unsigned char>(c);
                if (std::isalnum(u) == 0 && c != '_') {
                    return false;
                }
            }
            return true;
        }

        void strip_copy_suffix(std::string& basename) {
            if (basename.size() < 4U || basename.back() != ')') {
                return;
            }
            const auto open = basename.rfind(" (");
            if (open == std::string::npos || open + 2U >= basename.size() - 1U) {
                return;
            }
            const auto first_digit = open + 2U;
            for (std::size_t i = first_digit; i + 1U < basename.size(); ++i) {
                if (std::isdigit(static_cast<unsigned char>(basename[i])) == 0) {
                    return;
                }
            }
            basename.resize(open);
        }

        [[nodiscard]] std::optional<std::string> set_id_from_rom_path(const std::string& rom_path) {
            if (rom_path.empty()) {
                return std::nullopt;
            }
            std::string normalized = rom_path;
            while (!normalized.empty() && (normalized.back() == '/' || normalized.back() == '\\')) {
                normalized.pop_back();
            }
            if (normalized.empty()) {
                return std::nullopt;
            }
            const auto slash = normalized.find_last_of("/\\");
            std::string basename =
                slash == std::string::npos ? normalized : normalized.substr(slash + 1U);
            const auto dot = basename.find_last_of('.');
            if (dot != std::string::npos) {
                basename.resize(dot);
            }
            strip_copy_suffix(basename);
            if (!is_plain_set_id(basename)) {
                return std::nullopt;
            }
            for (char& c : basename) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return basename;
        }

        [[nodiscard]] std::optional<nested_zip_source>
        unwrap_single_nested_zip(std::span<const std::uint8_t> archive_bytes) {
            auto outer = mnemos::compression::zip_archive::open(archive_bytes);
            if (!outer.has_value()) {
                return std::nullopt;
            }
            const mnemos::compression::zip_entry* nested = nullptr;
            std::size_t file_count = 0U;
            for (const mnemos::compression::zip_entry& entry : outer->entries()) {
                if (!entry.name.empty() &&
                    (entry.name.back() == '/' || entry.name.back() == '\\')) {
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
            auto inner = outer->extract(*nested);
            if (!inner.has_value() || !has_zip_signature(*inner)) {
                return std::nullopt;
            }
            return nested_zip_source{.bytes = std::move(*inner), .entry_name = nested->name};
        }

        [[nodiscard]] std::optional<provider_source>
        make_zip_provider_from_path(const fs::path& path) {
            auto bytes = mnemos::io::read_file(path.string());
            if (!bytes.has_value()) {
                return std::nullopt;
            }
            auto provider = mnemos::manifests::common::make_zip_rom_provider(std::move(*bytes));
            if (!provider.has_value()) {
                return std::nullopt;
            }
            return provider_source{.provider = std::move(*provider),
                                   .source = path.filename().string()};
        }

        [[nodiscard]] std::optional<provider_source>
        make_single_nested_zip_provider_from_path(const fs::path& path,
                                                  std::string_view expected_set) {
            auto bytes = mnemos::io::read_file(path.string());
            if (!bytes.has_value()) {
                return std::nullopt;
            }
            auto nested = unwrap_single_nested_zip(*bytes);
            if (!nested.has_value()) {
                return std::nullopt;
            }
            const fs::path nested_path{nested->entry_name};
            if (nested_path.stem().string() != expected_set) {
                return std::nullopt;
            }
            auto provider =
                mnemos::manifests::common::make_zip_rom_provider(std::move(nested->bytes));
            if (!provider.has_value()) {
                return std::nullopt;
            }
            return provider_source{.provider = std::move(*provider),
                                   .source = path.filename().string() + "/" +
                                             nested_path.filename().string()};
        }

        [[nodiscard]] std::optional<provider_source>
        find_sibling_nested_zip_parent(const fs::path& sibling_dir, std::string_view parent) {
            if (sibling_dir.empty()) {
                return std::nullopt;
            }
            std::error_code ec;
            if (!fs::is_directory(sibling_dir, ec)) {
                return std::nullopt;
            }
            for (fs::directory_iterator it{sibling_dir, ec}, end; !ec && it != end;
                 it.increment(ec)) {
                std::error_code entry_ec;
                if (!it->is_regular_file(entry_ec) ||
                    !ends_with_zip(it->path().filename().string())) {
                    continue;
                }
                if (auto nested = make_single_nested_zip_provider_from_path(it->path(), parent)) {
                    return nested;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_set_decl>
        parse_irem_decl(std::string_view text, std::string source,
                        std::optional<std::string_view> expected_set = std::nullopt,
                        std::vector<rom_load_issue>* issues = nullptr) {
            auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, source);
            if (!parsed.ok()) {
                for (const auto& error : parsed.errors) {
                    std::fprintf(stderr, "[irem_travrusa] %s:%u:%u: %s\n", error.source.c_str(),
                                 error.line, error.column, error.message.c_str());
                    if (issues != nullptr) {
                        issues->push_back(
                            {source, "invalid ROM-set declaration: " + error.message});
                    }
                }
                return std::nullopt;
            }
            if (parsed.value->board != "irem_travrusa") {
                std::fprintf(stderr, "[irem_travrusa] %s declares board '%s', expected 'irem_travrusa'\n",
                             source.c_str(), parsed.value->board.c_str());
                if (issues != nullptr) {
                    issues->push_back({source, "unsupported Irem board '" + parsed.value->board +
                                                   "' for the travrusa adapter"});
                }
                return std::nullopt;
            }
            if (expected_set.has_value() && parsed.value->name != *expected_set) {
                if (issues != nullptr) {
                    issues->push_back({source, "declares set '" + parsed.value->name +
                                                   "', expected '" + std::string{*expected_set} +
                                                   "'"});
                }
                return std::nullopt;
            }
            return std::move(*parsed.value);
        }

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_set_decl>
        embedded_decl_for_set(std::string_view set_name) {
            const std::string_view toml = travrusa::game_manifest_toml(set_name);
            if (toml.empty()) {
                return std::nullopt;
            }
            return parse_irem_decl(toml, "embedded:irem_travrusa/" + std::string(set_name) + ".toml",
                                   set_name);
        }

        [[nodiscard]] std::optional<provider_source>
        find_supplemental_parent(std::span<const supplemental_rom_source> supplemental_sources,
                                 std::string_view parent) {
            for (const supplemental_rom_source& source : supplemental_sources) {
                if (source.path.empty()) {
                    if (source.rom.empty() || !has_zip_signature(source.rom)) {
                        continue;
                    }
                    auto nested = unwrap_single_nested_zip(source.rom);
                    if (!nested.has_value() ||
                        fs::path{nested->entry_name}.stem().string() != parent) {
                        continue;
                    }
                    auto provider =
                        mnemos::manifests::common::make_zip_rom_provider(std::move(nested->bytes));
                    if (provider.has_value()) {
                        return provider_source{
                            .provider = std::move(*provider),
                            .source = fs::path{nested->entry_name}.filename().string()};
                    }
                    continue;
                }

                if (auto set_id = set_id_from_rom_path(source.path);
                    !set_id.has_value() || *set_id != parent) {
                    if (auto nested = make_single_nested_zip_provider_from_path(
                            fs::path{source.path}, parent)) {
                        return nested;
                    }
                    continue;
                }

                if (is_directory_path(source.path)) {
                    return provider_source{
                        .provider =
                            mnemos::manifests::common::make_directory_rom_provider(source.path),
                        .source = fs::path{source.path}.filename().string()};
                }
                if (auto provider = make_zip_provider_from_path(fs::path{source.path})) {
                    return provider;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] parent_resolution
        with_parent_fallback(const mnemos::manifests::common::rom_file_provider& clone,
                             const std::string& parent, const std::string& rom_path,
                             std::span<const supplemental_rom_source> supplemental_sources) {
            parent_resolution resolved;
            resolved.provider = clone;

            if (rom_path.empty()) {
                resolved.issues.push_back(
                    {parent + ".zip",
                     "set declares parent '" + parent + "' but no ROM path is known to locate it"});
                return resolved;
            }
            if (!is_plain_set_id(parent)) {
                resolved.issues.push_back(
                    {"parent", "refusing to resolve parent '" + parent + "': not a plain set id"});
                return resolved;
            }

            const fs::path source_path{rom_path};
            const fs::path sibling_dir =
                source_path.has_parent_path() ? source_path.parent_path() : fs::path{};
            const fs::path parent_zip_path = sibling_dir / (parent + ".zip");
            const fs::path parent_dir_path = sibling_dir / parent;

            std::optional<provider_source> parent_source;
            std::optional<mnemos::manifests::common::rom_set_decl> parent_decl;
            auto try_parent_source = [&](std::optional<provider_source> candidate) -> bool {
                if (!candidate.has_value()) {
                    return false;
                }
                std::vector<rom_load_issue> candidate_issues;
                std::optional<mnemos::manifests::common::rom_set_decl> candidate_decl;
                if (auto manifest_bytes = candidate->provider("game.toml")) {
                    const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                    candidate_decl = parse_irem_decl(text, candidate->source + "/game.toml",
                                                     std::string_view{parent}, &candidate_issues);
                } else {
                    candidate_decl = embedded_decl_for_set(parent);
                    if (!candidate_decl.has_value()) {
                        return false;
                    }
                }
                const rom_set_image probe =
                    mnemos::manifests::common::load_rom_set(*candidate_decl, candidate->provider);
                if (!probe.issues.empty()) {
                    return false;
                }
                parent_decl = std::move(candidate_decl);
                parent_source = std::move(candidate);
                return true;
            };
            auto try_sibling_parent_candidates = [&]() -> bool {
                if (sibling_dir.empty()) {
                    return false;
                }
                std::error_code ec;
                if (!fs::is_directory(sibling_dir, ec)) {
                    return false;
                }
                std::vector<fs::path> candidates;
                for (fs::directory_iterator it{sibling_dir, ec}, end; !ec && it != end;
                     it.increment(ec)) {
                    std::error_code entry_ec;
                    if (!it->is_regular_file(entry_ec) ||
                        !ends_with_zip(it->path().filename().string())) {
                        continue;
                    }
                    auto set_id = set_id_from_rom_path(it->path().string());
                    if (!set_id.has_value() || *set_id != parent) {
                        continue;
                    }
                    candidates.push_back(it->path());
                }
                std::sort(candidates.begin(), candidates.end());
                for (const fs::path& candidate_path : candidates) {
                    if (try_parent_source(
                            make_single_nested_zip_provider_from_path(candidate_path, parent)) ||
                        try_parent_source(make_zip_provider_from_path(candidate_path))) {
                        return true;
                    }
                }
                return false;
            };

            (void)(try_parent_source(make_single_nested_zip_provider_from_path(parent_zip_path, parent)) ||
                   try_parent_source(make_zip_provider_from_path(parent_zip_path)) ||
                   (is_directory_path(parent_dir_path.string()) &&
                    try_parent_source(provider_source{
                        .provider = mnemos::manifests::common::make_directory_rom_provider(
                            parent_dir_path.string()),
                        .source = parent_dir_path.filename().string()})) ||
                   try_sibling_parent_candidates() ||
                   try_parent_source(find_sibling_nested_zip_parent(sibling_dir, parent)) ||
                   try_parent_source(find_supplemental_parent(supplemental_sources, parent)));
            if (!parent_source.has_value()) {
                resolved.issues.push_back(
                    {parent + ".zip", "no CRC-clean parent set was found at '" +
                                          parent_zip_path.string() + "', '" +
                                          parent_dir_path.string() +
                                          "', sibling single-inner ZIPs, or supplemental media"});
                return resolved;
            }

            resolved.decl = std::move(parent_decl);
            resolved.provider = mnemos::manifests::common::make_fallback_rom_provider(
                clone, std::move(parent_source->provider));
            return resolved;
        }

        [[nodiscard]] loaded_set
        load_declared_set(mnemos::manifests::common::rom_set_decl decl,
                          mnemos::manifests::common::rom_file_provider provider,
                          const std::string& rom_path,
                          std::span<const supplemental_rom_source> supplemental_sources) {
            loaded_set result;
            std::vector<rom_load_issue> parent_issues;
            if (decl.parent.has_value()) {
                parent_resolution parent =
                    with_parent_fallback(provider, *decl.parent, rom_path, supplemental_sources);
                if (parent.decl.has_value()) {
                    decl = mnemos::manifests::common::inherit_parent_regions(*parent.decl,
                                                                             std::move(decl));
                }
                parent_issues = std::move(parent.issues);
                provider = std::move(parent.provider);
            }
            result.image = mnemos::manifests::common::load_rom_set(decl, provider);
            for (auto& issue : parent_issues) {
                result.image.issues.push_back(std::move(issue));
            }
            result.set_name = decl.name;
            result.dip_switches = decl.dips;
            for (const auto& issue : result.image.issues) {
                std::fprintf(stderr, "[irem_travrusa] %s: %s\n", issue.file.c_str(),
                             issue.message.c_str());
            }
            return result;
        }

        [[nodiscard]] loaded_set
        load_set(std::vector<std::uint8_t> rom, const std::string& rom_path,
                 std::span<const supplemental_rom_source> supplemental_sources) {
            loaded_set result;
            if (rom.empty() && is_directory_path(rom_path)) {
                auto provider = mnemos::manifests::common::make_directory_rom_provider(rom_path);
                if (auto manifest_bytes = provider("game.toml")) {
                    const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                    auto decl =
                        parse_irem_decl(text, "game.toml", std::nullopt, &result.image.issues);
                    if (!decl.has_value()) {
                        return result;
                    }
                    return load_declared_set(std::move(*decl), std::move(provider), rom_path,
                                             supplemental_sources);
                }
                if (auto set_id = set_id_from_rom_path(rom_path)) {
                    if (auto decl = embedded_decl_for_set(*set_id)) {
                        return load_declared_set(std::move(*decl), std::move(provider), rom_path,
                                                 supplemental_sources);
                    }
                }
                result.image.issues.push_back(
                    {rom_path, "directory ROM set has no game.toml and no embedded manifest"});
                return result;
            }

            std::string effective_rom_path = rom_path;
            if (has_zip_signature(rom)) {
                if (auto nested = unwrap_single_nested_zip(rom)) {
                    const fs::path nested_name = fs::path{nested->entry_name}.filename();
                    const fs::path outer_path{rom_path};
                    effective_rom_path = outer_path.has_parent_path()
                                             ? (outer_path.parent_path() / nested_name).string()
                                             : nested_name.string();
                    rom = std::move(nested->bytes);
                }
            }
            if (has_zip_signature(rom)) {
                if (auto provider =
                        mnemos::manifests::common::make_zip_rom_provider(std::move(rom))) {
                    if (auto manifest_bytes = (*provider)("game.toml")) {
                        const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                        auto decl =
                            parse_irem_decl(text, "game.toml", std::nullopt, &result.image.issues);
                        if (!decl.has_value()) {
                            return result;
                        }
                        return load_declared_set(std::move(*decl), std::move(*provider),
                                                 effective_rom_path, supplemental_sources);
                    }
                    if (auto set_id = set_id_from_rom_path(effective_rom_path)) {
                        if (auto decl = embedded_decl_for_set(*set_id)) {
                            return load_declared_set(std::move(*decl), std::move(*provider),
                                                     effective_rom_path, supplemental_sources);
                        }
                    }
                    if (auto bytes = (*provider)("maincpu.bin")) {
                        result.image.regions.emplace("maincpu", std::move(*bytes));
                    }
                }
                return result;
            }
            result.image.regions.emplace("maincpu", std::move(rom));
            return result;
        }

        [[nodiscard]] std::unique_ptr<travrusa::travrusa_system> assemble_from(loaded_set set) {
            travrusa::travrusa_board_params params = travrusa::board_params_for(set.set_name);
            const std::uint16_t fallback = static_cast<std::uint16_t>(
                params.dsw1_default | (static_cast<std::uint16_t>(params.dsw2_default) << 8U));
            const std::uint16_t dip_word = dip_default_from_manifest(set.dip_switches, fallback);
            params.dsw1_default = static_cast<std::uint8_t>(dip_word & 0x00FFU);
            params.dsw2_default = static_cast<std::uint8_t>(dip_word >> 8U);
            return travrusa::assemble_travrusa(std::move(set.image), params);
        }

        constexpr std::uint32_t irem_travrusa_adapter_state_version = 1U;
        constexpr std::uint32_t irem_travrusa_adapter_save_target_manifest_rev =
            travrusa::travrusa_system_state_version;

        void save_controller_state(chips::state_writer& writer,
                                   const frontend_sdk::controller_state& state) {
            writer.boolean(state.up);
            writer.boolean(state.down);
            writer.boolean(state.left);
            writer.boolean(state.right);
            writer.boolean(state.start);
            writer.boolean(state.select);
            writer.boolean(state.a);
            writer.boolean(state.b);
            writer.boolean(state.c);
            writer.boolean(state.service);
            writer.boolean(state.test);
        }

        [[nodiscard]] frontend_sdk::controller_state
        load_controller_state(chips::state_reader& reader) noexcept {
            frontend_sdk::controller_state state{};
            state.up = reader.boolean();
            state.down = reader.boolean();
            state.left = reader.boolean();
            state.right = reader.boolean();
            state.start = reader.boolean();
            state.select = reader.boolean();
            state.a = reader.boolean();
            state.b = reader.boolean();
            state.c = reader.boolean();
            state.service = reader.boolean();
            state.test = reader.boolean();
            return state;
        }
    } // namespace

    irem_travrusa_adapter::irem_travrusa_adapter(std::vector<std::uint8_t> rom, std::string display_name,
                                       frontend_sdk::scheduler_factory* scheduler_factory,
                                       std::optional<std::uint16_t> dip_override,
                                       std::string rom_path,
                                       std::vector<std::vector<std::uint8_t>> additional_media,
                                       std::vector<std::string> additional_media_paths)
        : session_(make_session_capabilities()) {
        (void)scheduler_factory;
        const std::uint64_t source_byte_count = rom.size();
        std::vector<supplemental_rom_source> supplemental_sources;
        supplemental_sources.reserve(additional_media.size());
        for (std::size_t i = 0; i < additional_media.size(); ++i) {
            supplemental_sources.push_back({.rom = std::move(additional_media[i]),
                                            .path = i < additional_media_paths.size()
                                                        ? std::move(additional_media_paths[i])
                                                        : std::string{}});
        }
        loaded_set set = load_set(std::move(rom), rom_path, supplemental_sources);
        media_ = make_media_capabilities(display_name, set, source_byte_count);
        loaded_set_name_ = set.set_name;
        dip_switches_ = set.dip_switches;
        sys_ = assemble_from(std::move(set));
        if (dip_override.has_value()) {
            sys_->dsw1 = static_cast<std::uint8_t>(*dip_override);
            sys_->dsw2 = static_cast<std::uint8_t>(*dip_override >> 8U);
        }
        chip_view_ = {&sys_->video, &sys_->main_cpu, &sys_->sound_cpu,
                      &sys_->ay0,   &sys_->ay1,      &sys_->msm};
        publish_memory_views();
        const std::string game_label =
            !loaded_set_name_.empty()
                ? loaded_set_name_
                : (display_name.empty() ? std::string{"unknown"} : display_name);
        spec_ = {{"System", "Arcade"}, {"Board", "Irem Traverse USA"}, {"Game", game_label}};
        if (!dip_switches_.empty()) {
            spec_.push_back({"DIP switches", std::to_string(dip_switches_.size())});
        }
    }

    void irem_travrusa_adapter::publish_memory_views() {
        auto publish = [this](std::size_t index, std::string_view name,
                              std::span<const std::uint8_t> bytes) {
            memory_view_storage_[index] =
                std::make_unique<instrumentation::span_memory_view>(name, bytes);
            system_mem_view_[index] = memory_view_storage_[index].get();
        };

        publish(0U, "video_ram", sys_->video_ram);
        publish(1U, "sprite_ram", sys_->sprite_ram);
        publish(2U, "work_ram", sys_->work_ram);
        publish(3U, "sound_ram", sys_->sound_ram);
    }

    void irem_travrusa_adapter::sync_inputs_from_ports() noexcept {
        const auto pack_player = [](const frontend_sdk::controller_state& c,
                                    bool player_two) -> std::uint8_t {
            std::uint8_t value = 0xFFU;
            if (c.right) {
                value &= static_cast<std::uint8_t>(~0x01U);
            }
            if (c.left) {
                value &= static_cast<std::uint8_t>(~0x02U);
            }
            if (player_two && c.select) {
                value &= static_cast<std::uint8_t>(~0x10U);
            }
            if (c.a) {
                value &= static_cast<std::uint8_t>(~0x20U);
            }
            if (c.b) {
                value &= static_cast<std::uint8_t>(~0x80U);
            }
            return value;
        };
        std::uint8_t system = 0xFFU;
        if (ports_[0].start) {
            system &= static_cast<std::uint8_t>(~0x01U);
        }
        if (ports_[1].start) {
            system &= static_cast<std::uint8_t>(~0x02U);
        }
        if (ports_[0].service || ports_[1].service) {
            system &= static_cast<std::uint8_t>(~0x04U);
        }
        if (ports_[0].select || ports_[0].mode) {
            system &= static_cast<std::uint8_t>(~0x08U);
        }
        if (ports_[0].test) {
            system &= static_cast<std::uint8_t>(~0x10U);
        }
        sys_->set_inputs(pack_player(ports_[0], false), pack_player(ports_[1], true), system);
    }

    void irem_travrusa_adapter::save_adapter_state(chips::state_writer& writer) const {
        writer.u32(irem_travrusa_adapter_state_version);
        writer.u64(frames_stepped_);
        for (const auto& port : ports_) {
            save_controller_state(writer, port);
        }
    }

    void irem_travrusa_adapter::load_adapter_state(chips::state_reader& reader) {
        if (reader.u32() != irem_travrusa_adapter_state_version) {
            reader.fail();
            return;
        }
        frames_stepped_ = reader.u64();
        for (auto& port : ports_) {
            port = load_controller_state(reader);
        }
        if (reader.ok()) {
            sync_inputs_from_ports();
        }
    }

    void irem_travrusa_adapter::step_one_frame() {
        sys_->run_frame();
        ++frames_stepped_;
    }

    void irem_travrusa_adapter::apply_input(int port,
                                       const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port > 1) {
            return;
        }
        ports_[static_cast<std::size_t>(port)] = state;
        sync_inputs_from_ports();
    }

    frontend_sdk::audio_chunk irem_travrusa_adapter::drain_audio() noexcept {
        const std::size_t pending =
            std::max({sys_->ay0.pending_samples(), sys_->ay1.pending_samples(),
                      sys_->msm.pending_samples()});
        if (pending == 0U) {
            return {};
        }
        audio_buf_.assign(pending * 2U, 0);
        const auto mix_chip = [this](auto& chip, std::vector<std::int16_t>& scratch) {
            const std::size_t avail = chip.pending_samples();
            if (avail == 0U) {
                return;
            }
            scratch.assign(avail * 2U, 0);
            const std::size_t drained = chip.drain_samples(scratch.data(), avail);
            const std::size_t raw_count = std::min(drained * 2U, audio_buf_.size());
            for (std::size_t i = 0; i < raw_count; ++i) {
                audio_buf_[i] = clamp_mix(static_cast<std::int32_t>(audio_buf_[i]) +
                                          static_cast<std::int32_t>(scratch[i]));
            }
        };
        mix_chip(sys_->ay0, ay0_buf_);
        mix_chip(sys_->ay1, ay1_buf_);
        mix_chip(sys_->msm, msm_buf_);
        return {.samples = audio_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(pending),
                .sample_rate = travrusa::audio_rate_hz};
    }

    std::vector<std::uint8_t> irem_travrusa_adapter::save_state() {
        return runtime::write_save_state(build_save_target(*this));
    }

    runtime::load_result irem_travrusa_adapter::load_state(std::span<const std::uint8_t> data) {
        runtime::save_target target = build_save_target(*this);
        const runtime::load_result result = runtime::read_save_state(data, target);
        if (result.ok()) {
            audio_buf_.clear();
            ay0_buf_.clear();
            ay1_buf_.clear();
            msm_buf_.clear();
        }
        return result;
    }

    void force_link() noexcept {}

    runtime::save_target build_save_target(manifests::irem_travrusa::travrusa_system& sys) {
        runtime::save_target target;
        target.manifest_id = "irem_travrusa";
        target.manifest_rev = manifests::irem_travrusa::travrusa_system_state_version;
        target.components.push_back(
            {"board", [&sys](chips::state_writer& writer) { sys.save_state(writer); },
             [&sys](chips::state_reader& reader) { sys.load_state(reader); }});
        return target;
    }

    runtime::save_target build_save_target(irem_travrusa_adapter& adapter) {
        runtime::save_target target;
        target.manifest_id = "irem_travrusa.adapter";
        target.manifest_rev = irem_travrusa_adapter_save_target_manifest_rev;
        target.components.push_back(
            {"board",
             [&adapter](chips::state_writer& writer) { adapter.machine().save_state(writer); },
             [&adapter](chips::state_reader& reader) { adapter.machine().load_state(reader); }});
        target.components.push_back(
            {"adapter",
             [&adapter](chips::state_writer& writer) { adapter.save_adapter_state(writer); },
             [&adapter](chips::state_reader& reader) { adapter.load_adapter_state(reader); }});
        return target;
    }

    namespace {
        const auto register_irem_travrusa = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "irem_travrusa",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    return std::make_unique<irem_travrusa_adapter>(
                        std::move(opts.rom), std::move(opts.display_name),
                        opts.scheduler_factory_override, opts.dip_override,
                        std::move(opts.rom_path), std::move(opts.additional_media),
                        std::move(opts.additional_media_paths));
                });
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "travrusa",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    return std::make_unique<irem_travrusa_adapter>(
                        std::move(opts.rom), std::move(opts.display_name),
                        opts.scheduler_factory_override, opts.dip_override,
                        std::move(opts.rom_path), std::move(opts.additional_media),
                        std::move(opts.additional_media_paths));
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::irem_travrusa
