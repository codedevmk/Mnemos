#include "irem_m72_adapter.hpp"

#include "adapter_registry.hpp"
#include "crc32.hpp"
#include "file.hpp"
#include "input_pack.hpp"
#include "m72_game_manifests.hpp"
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

namespace mnemos::apps::player::adapters::irem_m72 {

    namespace {

        namespace fs = std::filesystem;

        using mnemos::manifests::common::rom_load_issue;
        using mnemos::manifests::common::rom_set_image;

        struct loaded_set final {
            rom_set_image image;
            std::string set_name; // from the declaration; empty for dev formats
            frontend_sdk::display_orientation orientation{
                frontend_sdk::display_orientation::horizontal};
            std::optional<std::string> protection_hle_profile{};
            std::vector<mnemos::manifests::irem_m72::no_dump_hle_sample_trigger>
                protection_hle_sample_triggers{};
            std::vector<mnemos::manifests::common::rom_set_dip_switch> dip_switches{};
        };

        struct parent_resolution final {
            mnemos::manifests::common::rom_file_provider provider;
            std::optional<mnemos::manifests::common::rom_set_decl> decl;
            std::vector<rom_load_issue> issues;
        };

        frontend_sdk::session_capability_info make_session_capabilities() {
            frontend_sdk::session_capability_info session{};
            session.input_ports = {
                {.port_index = 0U,
                 .player_slot = 1U,
                 .format = frontend_sdk::input_device_format::arcade_panel,
                 .device_id = "irem_m72.panel.p1",
                 .label = "Player 1 Panel"},
                {.port_index = 1U,
                 .player_slot = 2U,
                 .format = frontend_sdk::input_device_format::arcade_panel,
                 .device_id = "irem_m72.panel.p2",
                 .label = "Player 2 Panel"},
            };
            session.deterministic_frame_input = true;
            session.save_state_supported = true;
            session.frame_exact_save_state = true;
            session.max_input_delay_frames = 8U;
            return session;
        }

        frontend_sdk::media_capability_info make_media_capabilities(std::string_view display_name,
                                                                    const loaded_set& set,
                                                                    std::uint64_t source_bytes);

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

        [[nodiscard]] std::uint64_t resident_image_byte_count(const rom_set_image& image) noexcept {
            std::uint64_t bytes = 0U;
            for (const auto& [_, region] : image.regions) {
                bytes += region.size();
            }
            return bytes;
        }

        [[nodiscard]] std::uint32_t crc32_string(std::uint32_t crc,
                                                 std::string_view text) noexcept {
            crc = crc32_u64(crc, text.size());
            return mnemos::security::cryptography::crc32(text, crc);
        }

        [[nodiscard]] std::string resident_media_crc32(const loaded_set& set) {
            const auto& image = set.image;
            if (image.regions.empty()) {
                return {};
            }
            std::uint32_t crc = mnemos::security::cryptography::crc32("irem_m72.resident_media.v1");
            crc = crc32_string(crc, set.set_name);
            crc = crc32_u64(crc, static_cast<std::uint64_t>(set.orientation));
            crc = crc32_u64(crc, set.protection_hle_profile.has_value() ? 1U : 0U);
            if (set.protection_hle_profile.has_value()) {
                crc = crc32_string(crc, *set.protection_hle_profile);
            }
            crc = crc32_u64(crc, set.protection_hle_sample_triggers.size());
            for (const auto& sample_trigger : set.protection_hle_sample_triggers) {
                crc = crc32_u64(crc, sample_trigger.trigger);
                crc = crc32_u64(crc, sample_trigger.start);
            }
            crc = crc32_u64(crc, image.regions.size());
            for (const auto& [name, bytes] : image.regions) {
                crc = crc32_string(crc, name);
                crc = crc32_u64(crc, bytes.size());
                crc = mnemos::security::cryptography::crc32(
                    std::span<const std::uint8_t>(bytes.data(), bytes.size()), crc);
            }
            return hex32(crc);
        }

        frontend_sdk::media_capability_info make_media_capabilities(std::string_view display_name,
                                                                    const loaded_set& set,
                                                                    std::uint64_t source_bytes) {
            const auto& image = set.image;
            const std::uint64_t resident_bytes = resident_image_byte_count(image);
            std::string full_hash = resident_media_crc32(set);
            std::vector<frontend_sdk::media_validation_issue> validation_issues;
            validation_issues.reserve(image.issues.size());
            for (const auto& issue : image.issues) {
                std::string detail = issue.message;
                if (!issue.file.empty()) {
                    detail = issue.file + ": " + detail;
                }
                validation_issues.push_back(
                    {.code = "media.rom_set.load_issue", .detail = std::move(detail)});
            }
            frontend_sdk::media_capability_info media{};
            media.media.push_back(frontend_sdk::media_image_info{
                .id = "rom_set",
                .label = display_name.empty() ? std::string{"ROM set"} : std::string{display_name},
                .residency = frontend_sdk::media_residency::resident,
                .byte_count = resident_bytes == 0U ? source_bytes : resident_bytes,
                .hash_algorithm = full_hash.empty() ? frontend_sdk::media_hash_algorithm::none
                                                    : frontend_sdk::media_hash_algorithm::crc32,
                .full_hash = std::move(full_hash),
                .provider_id = "irem_m72.adapter",
                .revision = "loaded",
                .cache_hint = "resident",
                .validation_issues = std::move(validation_issues)});
            return media;
        }

        [[nodiscard]] frontend_sdk::display_orientation
        to_display_orientation(mnemos::manifests::common::screen_orientation orientation) noexcept {
            return orientation == mnemos::manifests::common::screen_orientation::vertical
                       ? frontend_sdk::display_orientation::vertical
                       : frontend_sdk::display_orientation::horizontal;
        }

        [[nodiscard]] const mnemos::manifests::common::rom_set_hle_decl*
        declared_mcu_hle(const mnemos::manifests::common::rom_set_decl& decl) noexcept {
            for (const auto& hle : decl.hle) {
                if (hle.chip == "mcu") {
                    return &hle;
                }
            }
            return nullptr;
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

        [[nodiscard]] std::optional<std::string>
        canonical_set_id_from_basename(std::string basename) {
            auto normalize_plain = [](std::string value) -> std::optional<std::string> {
                if (!is_plain_set_id(value)) {
                    return std::nullopt;
                }
                for (char& c : value) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                return value;
            };

            if (auto plain = normalize_plain(basename)) {
                return plain;
            }

            if (basename.size() < 5U || basename.back() != ')') {
                return std::nullopt;
            }
            const std::size_t open = basename.rfind(" (");
            if (open == std::string::npos || open + 2U >= basename.size() - 1U) {
                return std::nullopt;
            }
            for (std::size_t i = open + 2U; i + 1U < basename.size(); ++i) {
                if (std::isdigit(static_cast<unsigned char>(basename[i])) == 0) {
                    return std::nullopt;
                }
            }
            basename.resize(open);
            return normalize_plain(std::move(basename));
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

        void push_unique(std::vector<std::string>& values, std::string value) {
            if (value.empty()) {
                return;
            }
            if (std::find(values.begin(), values.end(), value) == values.end()) {
                values.push_back(std::move(value));
            }
        }

        struct nested_zip_source final {
            std::vector<std::uint8_t> bytes;
            std::string entry_name;
        };

        struct nested_zip_provider final {
            mnemos::manifests::common::rom_file_provider provider;
            std::string source;
        };

        struct supplemental_rom_source final {
            std::vector<std::uint8_t> rom;
            std::string path;
        };

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_file_provider>
        make_m72_zip_rom_provider(std::vector<std::uint8_t> archive);

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_file_provider>
        make_m72_zip_rom_provider_from_path(const std::string& path, bool* unreadable_zip);

        [[nodiscard]] mnemos::manifests::common::rom_file_provider
        make_m72_directory_rom_provider(std::string directory);

        [[nodiscard]] std::optional<std::string> set_id_from_rom_path(const std::string& rom_path);

        [[nodiscard]] bool is_embedded_m72_set_id(std::string_view set_name);

        [[nodiscard]] bool ends_with_m72_suffix(std::string_view value) noexcept;

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

        [[nodiscard]] std::optional<nested_zip_provider>
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
            auto provider = make_m72_zip_rom_provider(std::move(nested->bytes));
            if (!provider.has_value()) {
                return std::nullopt;
            }
            return nested_zip_provider{.provider = std::move(*provider),
                                       .source = path.filename().string() + "/" +
                                                 nested_path.filename().string()};
        }

        [[nodiscard]] std::optional<nested_zip_provider>
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

        [[nodiscard]] std::optional<nested_zip_provider>
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
                    auto provider = make_m72_zip_rom_provider(std::move(nested->bytes));
                    if (provider.has_value()) {
                        return nested_zip_provider{
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
                    return nested_zip_provider{.provider =
                                                   make_m72_directory_rom_provider(source.path),
                                               .source = fs::path{source.path}.filename().string()};
                }
                bool unreadable_zip = false;
                if (auto provider =
                        make_m72_zip_rom_provider_from_path(source.path, &unreadable_zip)) {
                    return nested_zip_provider{.provider = std::move(*provider),
                                               .source = fs::path{source.path}.filename().string()};
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] bool is_decimal(std::string_view value) noexcept {
            if (value.empty()) {
                return false;
            }
            return std::all_of(value.begin(), value.end(), [](char c) {
                return std::isdigit(static_cast<unsigned char>(c)) != 0;
            });
        }

        [[nodiscard]] bool is_location_suffix(std::string_view suffix) noexcept {
            if (suffix == "bin" || suffix == "rom") {
                return true;
            }
            if (suffix.size() > 2U && suffix[0] == 'i' && suffix[1] == 'c' &&
                is_decimal(suffix.substr(2U))) {
                return true;
            }
            if (suffix.size() > 1U &&
                std::isalpha(static_cast<unsigned char>(suffix.back())) != 0 &&
                is_decimal(suffix.substr(0U, suffix.size() - 1U))) {
                return true;
            }
            return false;
        }

        [[nodiscard]] std::string directory_filename_key(std::string_view name) {
            const std::size_t slash = name.find_last_of("/\\");
            std::string key = slash == std::string_view::npos
                                  ? std::string{name}
                                  : std::string{name.substr(slash + 1U)};
            for (char& c : key) {
                if (c == '_') {
                    c = '-';
                } else {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
            }
            const std::size_t dot = key.find_last_of('.');
            if (dot != std::string::npos &&
                is_location_suffix(std::string_view{key}.substr(dot + 1U))) {
                key.resize(dot);
            }
            while (!key.empty() && (key.back() == '-' || key.back() == '_')) {
                key.pop_back();
            }
            return key;
        }

        [[nodiscard]] std::string normalized_zip_path(std::string_view name) {
            std::string normalized{name};
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            return normalized;
        }

        [[nodiscard]] std::optional<std::string>
        collection_relative_path(std::string_view entry_name, std::string_view prefix) {
            if (prefix.empty()) {
                return std::nullopt;
            }
            const std::string normalized = normalized_zip_path(entry_name);
            if (normalized.size() <= prefix.size() ||
                normalized.compare(0U, prefix.size(), prefix) != 0 ||
                normalized[prefix.size()] != '/') {
                return std::nullopt;
            }
            std::string relative = normalized.substr(prefix.size() + 1U);
            if (relative.empty() || relative.back() == '/') {
                return std::nullopt;
            }
            return relative;
        }

        [[nodiscard]] std::string replace_board_token(std::string value, char from, char to) {
            const std::string needle = std::string{"-"} + from + "-";
            const std::size_t pos = value.find(needle);
            if (pos == std::string::npos) {
                return {};
            }
            value[pos + 1U] = to;
            return value;
        }

        [[nodiscard]] std::string replace_dot_number_token(std::string value, char from, char to) {
            const std::string needle = std::string{"."} + from;
            const std::size_t pos = value.find(needle);
            if (pos == std::string::npos || pos + 2U >= value.size() ||
                std::isdigit(static_cast<unsigned char>(value[pos + 2U])) == 0) {
                return {};
            }
            value[pos + 1U] = to;
            return value;
        }

        [[nodiscard]] std::vector<std::string> directory_filename_keys(std::string_view name) {
            std::vector<std::string> keys;
            const std::string base = directory_filename_key(name);
            push_unique(keys, base);
            if (base.size() > 2U && base[base.size() - 2U] == '-' &&
                std::isalpha(static_cast<unsigned char>(base.back())) != 0) {
                push_unique(keys, base.substr(0U, base.size() - 2U));
            }
            const std::size_t prefix_count = keys.size();
            for (std::size_t i = 0; i < prefix_count; ++i) {
                if (const std::size_t dash = keys[i].find('-');
                    dash != std::string::npos && dash + 1U < keys[i].size()) {
                    push_unique(keys, keys[i].substr(dash + 1U));
                }
            }
            const std::size_t board_count = keys.size();
            for (std::size_t i = 0; i < board_count; ++i) {
                push_unique(keys, replace_board_token(keys[i], 'b', 'a'));
                push_unique(keys, replace_board_token(keys[i], 'c', 'a'));
                push_unique(keys, replace_dot_number_token(keys[i], 'b', 'a'));
            }
            const std::size_t vo_count = keys.size();
            for (std::size_t i = 0; i < vo_count; ++i) {
                if (const std::size_t pos = keys[i].find("-vo"); pos != std::string::npos) {
                    std::string normalized = keys[i];
                    normalized.replace(pos, 3U, "-v0");
                    push_unique(keys, std::move(normalized));
                }
                if (const std::size_t pos = keys[i].find("-v0"); pos != std::string::npos) {
                    std::string normalized = keys[i];
                    normalized.replace(pos, 3U, "-vo");
                    push_unique(keys, std::move(normalized));
                }
            }
            return keys;
        }

        [[nodiscard]] bool zip_has_collection_prefix(std::span<const std::uint8_t> archive,
                                                     std::string_view prefix) {
            if (prefix.empty()) {
                return false;
            }
            auto opened = mnemos::compression::zip_archive::open(archive);
            if (!opened.has_value()) {
                return false;
            }
            for (const auto& entry : opened->entries()) {
                if (collection_relative_path(entry.name, prefix).has_value()) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] std::optional<std::string>
        preferred_collection_prefix(std::string_view set_id,
                                    std::span<const std::uint8_t> archive) {
            if (set_id.empty()) {
                return std::nullopt;
            }
            if (is_embedded_m72_set_id(set_id) && zip_has_collection_prefix(archive, set_id)) {
                return std::string{set_id};
            }
            if (!ends_with_m72_suffix(set_id)) {
                const std::string suffixed = std::string{set_id} + "m72";
                if (is_embedded_m72_set_id(suffixed) &&
                    zip_has_collection_prefix(archive, suffixed)) {
                    return suffixed;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_file_provider>
        make_m72_zip_rom_provider_with_collection_prefix(std::vector<std::uint8_t> archive,
                                                         std::string preferred_prefix) {
            auto bytes = std::make_shared<std::vector<std::uint8_t>>(std::move(archive));
            auto opened = mnemos::compression::zip_archive::open(*bytes);
            if (!opened.has_value()) {
                return std::nullopt;
            }
            auto zip = std::make_shared<mnemos::compression::zip_archive>(std::move(*opened));
            return mnemos::manifests::common::rom_file_provider{
                [bytes, zip, preferred_prefix = std::move(preferred_prefix)](
                    std::string_view name) -> std::optional<std::vector<std::uint8_t>> {
                    const auto try_entries =
                        [&](bool scoped) -> std::optional<std::vector<std::uint8_t>> {
                        const auto wanted = directory_filename_keys(name);
                        for (const auto& entry : zip->entries()) {
                            std::string candidate_name;
                            if (scoped) {
                                auto relative =
                                    collection_relative_path(entry.name, preferred_prefix);
                                if (!relative.has_value()) {
                                    continue;
                                }
                                candidate_name = std::move(*relative);
                            } else {
                                candidate_name = entry.name;
                            }
                            if (candidate_name == name) {
                                return zip->extract(entry);
                            }
                        }
                        for (const auto& entry : zip->entries()) {
                            std::string candidate_name;
                            if (scoped) {
                                auto relative =
                                    collection_relative_path(entry.name, preferred_prefix);
                                if (!relative.has_value()) {
                                    continue;
                                }
                                candidate_name = std::move(*relative);
                            } else {
                                if (entry.name.empty() ||
                                    (entry.name.back() == '/' || entry.name.back() == '\\')) {
                                    continue;
                                }
                                candidate_name = entry.name;
                            }
                            const std::string key = directory_filename_key(candidate_name);
                            if (std::find(wanted.begin(), wanted.end(), key) != wanted.end()) {
                                return zip->extract(entry);
                            }
                        }
                        return std::nullopt;
                    };

                    if (!preferred_prefix.empty()) {
                        if (auto scoped = try_entries(true)) {
                            return scoped;
                        }
                    }
                    return try_entries(false);
                }};
        }

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_file_provider>
        make_m72_zip_rom_provider(std::vector<std::uint8_t> archive) {
            return make_m72_zip_rom_provider_with_collection_prefix(std::move(archive), {});
        }

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_file_provider>
        make_m72_zip_rom_provider_from_path(const std::string& path, bool* unreadable_zip) {
            if (unreadable_zip != nullptr) {
                *unreadable_zip = false;
            }
            auto bytes = mnemos::io::read_file(path);
            if (!bytes.has_value()) {
                return std::nullopt;
            }
            auto provider = make_m72_zip_rom_provider(std::move(*bytes));
            if (!provider.has_value() && unreadable_zip != nullptr) {
                *unreadable_zip = true;
            }
            return provider;
        }

        [[nodiscard]] mnemos::manifests::common::rom_file_provider
        make_m72_directory_rom_provider(std::string directory) {
            return [dir = std::move(directory)](
                       std::string_view name) -> std::optional<std::vector<std::uint8_t>> {
                if (auto bytes =
                        mnemos::manifests::common::make_directory_rom_provider(dir)(name)) {
                    return bytes;
                }
                const auto wanted = directory_filename_keys(name);
                std::error_code ec;
                for (const auto& entry : fs::directory_iterator(fs::path{dir}, ec)) {
                    std::error_code file_ec;
                    if (ec || !entry.is_regular_file(file_ec)) {
                        continue;
                    }
                    const std::string key =
                        directory_filename_key(entry.path().filename().string());
                    if (std::find(wanted.begin(), wanted.end(), key) != wanted.end()) {
                        return mnemos::io::read_file(entry.path().string());
                    }
                }
                return std::nullopt;
            };
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
            return canonical_set_id_from_basename(std::move(basename));
        }

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_set_decl>
        parse_irem_decl(std::string_view text, std::string source,
                        std::optional<std::string_view> expected_set = std::nullopt,
                        std::vector<mnemos::manifests::common::rom_load_issue>* issues = nullptr) {
            auto parsed = mnemos::manifests::common::parse_rom_set_decl(text, source);
            if (!parsed.ok()) {
                for (const auto& error : parsed.errors) {
                    std::fprintf(stderr, "[irem_m72] %s:%u:%u: %s\n", error.source.c_str(),
                                 error.line, error.column, error.message.c_str());
                    if (issues != nullptr) {
                        issues->push_back(
                            {source, "invalid ROM-set declaration: " + error.message});
                    }
                }
                return std::nullopt;
            }
            if (parsed.value->board != "irem_m72") {
                std::fprintf(stderr, "[irem_m72] %s declares board '%s', expected 'irem_m72'\n",
                             source.c_str(), parsed.value->board.c_str());
                if (issues != nullptr) {
                    issues->push_back({source, "unsupported Irem board '" + parsed.value->board +
                                                   "' for the M72 adapter"});
                }
                return std::nullopt;
            }
            if (expected_set.has_value() && parsed.value->name != *expected_set) {
                std::fprintf(stderr, "[irem_m72] %s declares set '%s', expected '%.*s'\n",
                             source.c_str(), parsed.value->name.c_str(),
                             static_cast<int>(expected_set->size()), expected_set->data());
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
            const std::string_view toml = mnemos::manifests::irem_m72::game_manifest_toml(set_name);
            if (toml.empty()) {
                return std::nullopt;
            }
            return parse_irem_decl(toml, "embedded:irem_m72/" + std::string(set_name) + ".toml",
                                   set_name);
        }

        struct embedded_identity_score final {
            std::uint32_t exact{};
            std::uint32_t present{};
            std::uint32_t mismatch{};
            std::uint32_t missing{};
        };

        [[nodiscard]] embedded_identity_score
        score_decl_identity(const mnemos::manifests::common::rom_set_decl& decl,
                            const mnemos::manifests::common::rom_file_provider& provider) {
            embedded_identity_score score{};
            if (!provider) {
                return score;
            }
            for (const auto& region : decl.regions) {
                if (region.name != "maincpu") {
                    continue;
                }
                for (const auto& file : region.files) {
                    auto data = provider(file.name);
                    if (!data.has_value()) {
                        ++score.missing;
                        continue;
                    }
                    ++score.present;
                    bool matches = true;
                    if (file.size != 0U && data->size() != file.size) {
                        matches = false;
                    }
                    if (file.crc32.has_value() &&
                        mnemos::security::cryptography::crc32(*data) != *file.crc32) {
                        matches = false;
                    }
                    if (matches) {
                        ++score.exact;
                    } else {
                        ++score.mismatch;
                    }
                }
            }
            return score;
        }

        [[nodiscard]] bool identity_score_better(const embedded_identity_score& lhs,
                                                 const embedded_identity_score& rhs) noexcept {
            if (lhs.exact != rhs.exact) {
                return lhs.exact > rhs.exact;
            }
            if (lhs.mismatch != rhs.mismatch) {
                return lhs.mismatch < rhs.mismatch;
            }
            if (lhs.present != rhs.present) {
                return lhs.present > rhs.present;
            }
            return lhs.missing < rhs.missing;
        }

        [[nodiscard]] bool ends_with_m72_suffix(std::string_view value) noexcept {
            constexpr std::string_view suffix = "m72";
            if (value.size() < suffix.size()) {
                return false;
            }
            return value.substr(value.size() - suffix.size()) == suffix;
        }

        [[nodiscard]] std::optional<mnemos::manifests::common::rom_set_decl>
        embedded_decl_for_source(std::string_view hinted_set,
                                 const mnemos::manifests::common::rom_file_provider& provider) {
            if (!ends_with_m72_suffix(hinted_set)) {
                const std::string suffixed_set = std::string{hinted_set} + "m72";
                if (auto suffixed = embedded_decl_for_set(suffixed_set)) {
                    const embedded_identity_score suffixed_score =
                        score_decl_identity(*suffixed, provider);
                    if (suffixed_score.exact > 0U) {
                        std::fprintf(stderr,
                                     "[irem_m72] ROM source id '%.*s' matched embedded set '%s' "
                                     "by canonical M72 suffix\n",
                                     static_cast<int>(hinted_set.size()), hinted_set.data(),
                                     suffixed_set.c_str());
                        return suffixed;
                    }
                }
            }

            std::vector<std::string> seeded_names;
            auto add_seed = [&](std::string name) {
                if (std::find(seeded_names.begin(), seeded_names.end(), name) ==
                    seeded_names.end()) {
                    seeded_names.push_back(std::move(name));
                }
            };
            add_seed(std::string{hinted_set});
            if (!ends_with_m72_suffix(hinted_set)) {
                add_seed(std::string{hinted_set} + "m72");
            }

            std::optional<mnemos::manifests::common::rom_set_decl> best;
            embedded_identity_score best_score{};
            std::string best_name;

            auto consider = [&](std::string_view set_name, bool require_exact_match) {
                auto candidate = embedded_decl_for_set(set_name);
                if (!candidate.has_value()) {
                    return;
                }
                const embedded_identity_score candidate_score =
                    score_decl_identity(*candidate, provider);
                if (require_exact_match && candidate_score.exact == 0U) {
                    return;
                }
                if (!best.has_value() || identity_score_better(candidate_score, best_score)) {
                    best = std::move(candidate);
                    best_score = candidate_score;
                    best_name = std::string{set_name};
                }
            };

            for (const auto& set_name : seeded_names) {
                consider(set_name, false);
            }

            for (const auto& [set_name, _] :
                 mnemos::manifests::irem_m72::embedded::game_manifests) {
                if (std::find(seeded_names.begin(), seeded_names.end(), set_name) !=
                    seeded_names.end()) {
                    continue;
                }
                auto candidate = embedded_decl_for_set(set_name);
                if (!candidate.has_value()) {
                    continue;
                }
                const embedded_identity_score candidate_score =
                    score_decl_identity(*candidate, provider);
                if ((!best.has_value() && candidate_score.exact > 0U) ||
                    (best.has_value() && candidate_score.exact > best_score.exact &&
                     identity_score_better(candidate_score, best_score))) {
                    best = std::move(candidate);
                    best_score = candidate_score;
                    best_name = set_name;
                }
            }

            if (!best.has_value()) {
                return std::nullopt;
            }

            if (best_name != hinted_set) {
                const char* reason =
                    best_score.exact > 0U ? "maincpu CRCs" : "canonical M72 suffix";
                std::fprintf(stderr,
                             "[irem_m72] ROM source id '%.*s' matched embedded set '%.*s' by "
                             "%s\n",
                             static_cast<int>(hinted_set.size()), hinted_set.data(),
                             static_cast<int>(best_name.size()), best_name.data(), reason);
            }
            return best;
        }

        [[nodiscard]] bool is_embedded_m72_set_id(std::string_view set_name) {
            return !mnemos::manifests::irem_m72::game_manifest_toml(set_name).empty();
        }

        [[nodiscard]] bool
        file_bytes_match_decl(const mnemos::manifests::common::rom_set_file& file,
                              std::span<const std::uint8_t> data) {
            if (file.size != 0U && data.size() != file.size) {
                return false;
            }
            if (file.crc32.has_value() &&
                mnemos::security::cryptography::crc32(data) != *file.crc32) {
                return false;
            }
            return true;
        }

        [[nodiscard]] bool
        provider_contributes_to_decl(const mnemos::manifests::common::rom_set_decl& decl,
                                     const mnemos::manifests::common::rom_file_provider& provider) {
            if (!provider) {
                return false;
            }
            const auto file_matches = [&](const mnemos::manifests::common::rom_set_file& file,
                                          std::string_view name) {
                if (auto data = provider(name)) {
                    return file_bytes_match_decl(file, *data);
                }
                return false;
            };
            for (const auto& region : decl.regions) {
                for (const auto& file : region.files) {
                    if (file_matches(file, file.name)) {
                        return true;
                    }
                    for (const std::string& alias : file.aliases) {
                        if (file_matches(file, alias)) {
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        [[nodiscard]] std::optional<nested_zip_provider>
        make_supplemental_provider_for_decl(const mnemos::manifests::common::rom_set_decl& decl,
                                            supplemental_rom_source source,
                                            std::vector<rom_load_issue>& issues) {
            mnemos::manifests::common::rom_file_provider provider;
            std::string effective_path = source.path;
            std::string source_label =
                source.path.empty() ? std::string{"supplemental ROM set"} : source.path;

            if (source.rom.empty() && is_directory_path(source.path)) {
                provider = make_m72_directory_rom_provider(source.path);
            } else {
                if (source.rom.empty()) {
                    issues.push_back({source_label, "supplemental ROM set is empty"});
                    return std::nullopt;
                }
                if (has_zip_signature(source.rom)) {
                    if (auto nested = unwrap_single_nested_zip(source.rom)) {
                        const fs::path nested_name = fs::path{nested->entry_name}.filename();
                        const fs::path outer_path{source.path};
                        effective_path = outer_path.has_parent_path()
                                             ? (outer_path.parent_path() / nested_name).string()
                                             : nested_name.string();
                        source_label += "/" + nested_name.string();
                        source.rom = std::move(nested->bytes);
                    }
                }
                if (auto zip_provider = make_m72_zip_rom_provider(std::move(source.rom))) {
                    provider = std::move(*zip_provider);
                } else {
                    issues.push_back({source_label, "supplemental ROM set is not a readable zip"});
                    return std::nullopt;
                }
            }

            const std::optional<std::string> path_set_id = set_id_from_rom_path(effective_path);

            if (auto manifest_bytes = provider("game.toml")) {
                const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                const std::string_view expected_set = path_set_id.has_value() &&
                                                              decl.parent.has_value() &&
                                                              *path_set_id == *decl.parent
                                                          ? std::string_view{*decl.parent}
                                                          : std::string_view{decl.name};
                auto parsed =
                    parse_irem_decl(text, source_label + "/game.toml", expected_set, &issues);
                if (!parsed.has_value()) {
                    return std::nullopt;
                }
                return nested_zip_provider{.provider = std::move(provider),
                                           .source = std::move(source_label)};
            }

            if (path_set_id.has_value()) {
                const std::string& set_id = *path_set_id;
                if (set_id == decl.name) {
                    return nested_zip_provider{.provider = std::move(provider),
                                               .source = std::move(source_label)};
                }
                if (decl.parent.has_value() && set_id == *decl.parent) {
                    return nested_zip_provider{.provider = std::move(provider),
                                               .source = std::move(source_label)};
                }
                if (is_embedded_m72_set_id(set_id)) {
                    issues.push_back({source_label, "supplemental ROM set id '" + set_id +
                                                        "' does not match selected set '" +
                                                        decl.name + "'"});
                    return std::nullopt;
                }
            }

            if (provider_contributes_to_decl(decl, provider)) {
                return nested_zip_provider{.provider = std::move(provider),
                                           .source = std::move(source_label)};
            }

            issues.push_back({source_label, "supplemental ROM set does not match selected set '" +
                                                decl.name + "'"});
            return std::nullopt;
        }

        [[nodiscard]] mnemos::manifests::common::rom_file_provider
        with_supplemental_fallbacks(const mnemos::manifests::common::rom_set_decl& decl,
                                    mnemos::manifests::common::rom_file_provider provider,
                                    std::span<supplemental_rom_source> supplemental_sources,
                                    std::vector<rom_load_issue>& issues) {
            for (supplemental_rom_source& source : supplemental_sources) {
                if (auto supplemental =
                        make_supplemental_provider_for_decl(decl, std::move(source), issues)) {
                    provider = mnemos::manifests::common::make_fallback_rom_provider(
                        std::move(provider), std::move(supplemental->provider));
                }
            }
            return provider;
        }

        [[nodiscard]] bool issue_is_missing_file(const rom_load_issue& issue) noexcept {
            return issue.message.find("missing from the ROM set") != std::string::npos;
        }

        [[nodiscard]] bool
        region_has_missing_file_issue(const mnemos::manifests::common::rom_set_decl& decl,
                                      std::string_view region_name,
                                      std::span<const rom_load_issue> issues) {
            for (const auto& region : decl.regions) {
                if (region.name != region_name) {
                    continue;
                }
                for (const auto& file : region.files) {
                    const auto missing = [&](const rom_load_issue& issue) {
                        return issue.file == file.name && issue_is_missing_file(issue);
                    };
                    if (std::any_of(issues.begin(), issues.end(), missing)) {
                        return true;
                    }
                }
            }
            return false;
        }

        void disable_incomplete_mcu_region(const mnemos::manifests::common::rom_set_decl& decl,
                                           mnemos::manifests::common::rom_set_image& image) {
            if (!region_has_missing_file_issue(decl, "mcu", image.issues)) {
                return;
            }
            if (auto it = image.regions.find("mcu"); it != image.regions.end()) {
                it->second.clear();
            }
            image.issues.push_back(
                {"mcu", "M72 protection MCU region is incomplete; disabling MCU execution rather "
                        "than running fill bytes"});
        }

        void disable_hle_with_incomplete_sample_region(
            const mnemos::manifests::common::rom_set_decl& decl, loaded_set& set) {
            if (!set.protection_hle_profile.has_value()) {
                return;
            }
            if (!region_has_missing_file_issue(decl, "samples", set.image.issues)) {
                return;
            }
            set.image.issues.push_back(
                {"samples", "M72 MCU HLE sample region is incomplete; disabling MCU HLE rather "
                            "than running fill bytes"});
            set.protection_hle_profile.reset();
            set.protection_hle_sample_triggers.clear();
        }

        // Resolve a clone set's parent zip or unpacked directory beside the
        // clone on disk and compose a fallback provider (clone first, then
        // parent) so shared dumps in the parent satisfy the clone declaration.
        [[nodiscard]] parent_resolution
        with_parent_fallback(const mnemos::manifests::common::rom_file_provider& clone,
                             const std::string& parent, const std::string& rom_path,
                             std::span<const supplemental_rom_source> supplemental_sources) {
            parent_resolution resolved;
            resolved.provider = clone;
            if (rom_path.empty()) {
                std::fprintf(stderr,
                             "[irem_m72] set declares parent '%s' but no path is known to "
                             "locate it; shared ROMs will be missing\n",
                             parent.c_str());
                resolved.issues.push_back(
                    {parent + ".zip",
                     "set declares parent '" + parent + "' but no ROM path is known to locate it"});
                return resolved;
            }
            if (!is_plain_set_id(parent)) {
                std::fprintf(stderr,
                             "[irem_m72] refusing to resolve parent '%s': not a plain set id\n",
                             parent.c_str());
                resolved.issues.push_back(
                    {"parent", "refusing to resolve parent '" + parent + "': not a plain set id"});
                return resolved;
            }
            const fs::path source_path{rom_path};
            const fs::path sibling_dir =
                source_path.has_parent_path() ? source_path.parent_path() : fs::path{};
            const fs::path parent_zip_path = sibling_dir / (parent + ".zip");
            const fs::path parent_dir_path = sibling_dir / parent;
            std::string parent_source = parent + ".zip";
            bool unreadable_zip = false;
            auto parent_provider =
                make_m72_zip_rom_provider_from_path(parent_zip_path.string(), &unreadable_zip);
            if (!parent_provider.has_value() && !unreadable_zip &&
                is_directory_path(parent_dir_path.string())) {
                parent_provider = make_m72_directory_rom_provider(parent_dir_path.string());
                parent_source = parent;
            }
            if (!parent_provider.has_value() && !unreadable_zip) {
                if (auto nested = find_sibling_nested_zip_parent(sibling_dir, parent)) {
                    parent_provider = std::move(nested->provider);
                    parent_source = std::move(nested->source);
                }
            }
            if (!parent_provider.has_value() && !unreadable_zip) {
                if (auto supplemental = find_supplemental_parent(supplemental_sources, parent)) {
                    parent_provider = std::move(supplemental->provider);
                    parent_source = std::move(supplemental->source);
                }
            }
            if (!parent_provider.has_value()) {
                std::fprintf(stderr, "[irem_m72] parent set %s or %s: %s\n",
                             parent_zip_path.string().c_str(), parent_dir_path.string().c_str(),
                             unreadable_zip ? "zip is not readable" : "not found");
                resolved.issues.push_back(
                    {parent + ".zip",
                     "parent set '" + parent_zip_path.string() + "' or '" +
                         parent_dir_path.string() +
                         (unreadable_zip ? "' has an unreadable zip" : "' not found")});
                return resolved;
            }
            if (auto manifest_bytes = (*parent_provider)("game.toml")) {
                const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                resolved.decl = parse_irem_decl(text, parent_source + "/game.toml",
                                                std::string_view{parent}, &resolved.issues);
                if (!resolved.decl.has_value()) {
                    return resolved;
                }
            } else {
                resolved.decl = embedded_decl_for_set(parent);
                if (!resolved.decl.has_value()) {
                    resolved.issues.push_back(
                        {parent_source + "/game.toml",
                         "parent set '" + parent + "' has no game.toml and no embedded manifest"});
                    return resolved;
                }
            }
            resolved.provider = mnemos::manifests::common::make_fallback_rom_provider(
                clone, std::move(*parent_provider));
            return resolved;
        }

        [[nodiscard]] loaded_set
        load_declared_set(mnemos::manifests::common::rom_set_decl decl,
                          mnemos::manifests::common::rom_file_provider provider,
                          const std::string& rom_path,
                          std::span<supplemental_rom_source> supplemental_sources) {
            loaded_set result;
            mnemos::manifests::common::rom_file_provider effective = std::move(provider);
            std::vector<rom_load_issue> parent_issues;
            if (decl.parent.has_value()) {
                parent_resolution parent =
                    with_parent_fallback(effective, *decl.parent, rom_path, supplemental_sources);
                if (parent.decl.has_value()) {
                    decl = mnemos::manifests::common::inherit_parent_regions(*parent.decl,
                                                                             std::move(decl));
                }
                parent_issues = std::move(parent.issues);
                effective = std::move(parent.provider);
            }
            effective = with_supplemental_fallbacks(decl, std::move(effective),
                                                    supplemental_sources, result.image.issues);
            result.image = mnemos::manifests::common::load_rom_set(decl, effective);
            for (auto& issue : parent_issues) {
                result.image.issues.push_back(std::move(issue));
            }
            disable_incomplete_mcu_region(decl, result.image);
            result.set_name = decl.name;
            result.orientation = to_display_orientation(decl.orientation);
            if (const auto* mcu_hle = declared_mcu_hle(decl)) {
                result.protection_hle_profile = mcu_hle->profile;
                result.protection_hle_sample_triggers.reserve(mcu_hle->sample_triggers.size());
                for (const auto& sample_trigger : mcu_hle->sample_triggers) {
                    result.protection_hle_sample_triggers.push_back(
                        {.trigger = sample_trigger.trigger, .start = sample_trigger.start});
                }
            }
            if (result.protection_hle_profile.has_value() &&
                !mnemos::manifests::irem_m72::supported_protection_hle_profile(
                    *result.protection_hle_profile)) {
                result.image.issues.push_back({"mcu", "unsupported M72 MCU HLE profile '" +
                                                          *result.protection_hle_profile + "'"});
                result.protection_hle_profile.reset();
                result.protection_hle_sample_triggers.clear();
            }
            disable_hle_with_incomplete_sample_region(decl, result);
            if (result.protection_hle_profile.has_value() &&
                result.protection_hle_sample_triggers.empty()) {
                result.image.issues.push_back({"mcu", "M72 MCU HLE profile '" +
                                                          *result.protection_hle_profile +
                                                          "' is missing sample-trigger metadata"});
                result.protection_hle_profile.reset();
            }
            if (result.protection_hle_profile.has_value()) {
                if (auto issue = mnemos::manifests::irem_m72::protection_hle_sample_trigger_issue(
                        result.image, result.protection_hle_sample_triggers)) {
                    result.image.issues.push_back({"mcu", std::move(*issue)});
                    result.protection_hle_profile.reset();
                    result.protection_hle_sample_triggers.clear();
                }
            }
            result.dip_switches = decl.dips;
            for (const auto& issue : result.image.issues) {
                std::fprintf(stderr, "[irem_m72] %s: %s\n", issue.file.c_str(),
                             issue.message.c_str());
            }
            return result;
        }

        // Set loader. A directory, normal .zip, or single-inner-set wrapper .zip
        // carrying a "game.toml" declaration (schema mnemos-romset/1) loads declaratively --
        // per-file placement, interleave, CRC verification -- with loader issues reported to
        // stderr; the declared set name selects the per-game board wiring. A
        // clone set can name a `parent`, whose sibling directory or zip is
        // resolved beside the clone using `rom_path` and used as a fallback for
        // shared dumps. Without a source-local manifest, the source basename
        // selects a checked-in embedded game manifest (e.g. rtype.zip or
        // rtype/ -> rtype.toml). If no declaration can be resolved, the
        // development zip format applies: region-named entries ("maincpu.bin",
        // ...) loaded whole. A bare binary is the V30 program image.
        [[nodiscard]] loaded_set load_set(std::vector<std::uint8_t> rom,
                                          const std::string& rom_path,
                                          std::span<supplemental_rom_source> supplemental_sources) {
            loaded_set result;
            if (rom.empty() && is_directory_path(rom_path)) {
                auto provider = make_m72_directory_rom_provider(rom_path);
                if (auto manifest_bytes = provider("game.toml")) {
                    const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                    auto decl =
                        parse_irem_decl(text, "game.toml", std::nullopt, &result.image.issues);
                    if (!decl.has_value()) {
                        return result; // declared but invalid: boot an issue-marked board
                    }
                    return load_declared_set(std::move(*decl), std::move(provider), rom_path,
                                             supplemental_sources);
                }
                if (auto set_id = set_id_from_rom_path(rom_path)) {
                    if (auto decl = embedded_decl_for_source(*set_id, provider)) {
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
            const bool is_zip = has_zip_signature(rom);
            if (is_zip) {
                std::optional<std::string> collection_prefix;
                if (auto set_id = set_id_from_rom_path(effective_rom_path)) {
                    collection_prefix = preferred_collection_prefix(*set_id, rom);
                }
                auto provider = collection_prefix.has_value()
                                    ? make_m72_zip_rom_provider_with_collection_prefix(
                                          std::move(rom), std::move(*collection_prefix))
                                    : make_m72_zip_rom_provider(std::move(rom));
                if (provider.has_value()) {
                    if (auto manifest_bytes = (*provider)("game.toml")) {
                        const std::string text(manifest_bytes->begin(), manifest_bytes->end());
                        auto decl =
                            parse_irem_decl(text, "game.toml", std::nullopt, &result.image.issues);
                        if (!decl.has_value()) {
                            return result; // declared but invalid: boot an issue-marked board
                        }
                        return load_declared_set(std::move(*decl), *provider, effective_rom_path,
                                                 supplemental_sources);
                    }
                    if (auto set_id = set_id_from_rom_path(effective_rom_path)) {
                        if (auto decl = embedded_decl_for_source(*set_id, *provider)) {
                            return load_declared_set(std::move(*decl), *provider,
                                                     effective_rom_path, supplemental_sources);
                        }
                    }
                    for (const char* region : {"maincpu", "soundcpu", "samples", "mcu", "tiles_a",
                                               "tiles_b", "sprites"}) {
                        if (auto bytes = (*provider)(std::string{region} + ".bin")) {
                            result.image.regions.emplace(region, std::move(*bytes));
                        }
                    }
                }
                return result;
            }
            result.image.regions.emplace("maincpu", std::move(rom));
            return result;
        }

        [[nodiscard]] std::unique_ptr<manifests::irem_m72::m72_system>
        assemble_from(loaded_set set) {
            auto params = manifests::irem_m72::board_params_for(set.set_name);
            params.protection_hle_profile = std::move(set.protection_hle_profile);
            params.protection_hle_sample_triggers = std::move(set.protection_hle_sample_triggers);
            return manifests::irem_m72::assemble_m72(std::move(set.image), std::move(params));
        }

        [[nodiscard]] std::vector<runtime::scheduled_chip>
        build_schedule(manifests::irem_m72::m72_system& sys) {
            // 32 MHz board crystal: pixel clock /4, V30 /4 (8 MHz effective).
            // The Z80 and the YM2151 share the separate 3.579545 MHz sound
            // crystal -- not an integer divider of the master, so both run on
            // the rational rate (715909 chip cycles per 6400000 master
            // cycles). Video first so the CPUs observe the advanced beam.
            std::vector<runtime::scheduled_chip> chips{
                {.chip = &sys.video, .divider = 4U},
                {.chip = &sys.main_cpu, .divider = 4U},
                {.chip = &sys.sound_cpu, .divider = 1U, .rate_num = 6400000U, .rate_den = 715909U},
                {.chip = &sys.fm, .divider = 1U, .rate_num = 6400000U, .rate_den = 715909U},
                {.chip = &sys.sample_pump,
                 .divider = manifests::irem_m72::sample_pump_master_divider}};
            if (sys.mcu_present) {
                // 8 MHz MCU crystal, 12 clocks per machine cycle: 32 MHz / 48.
                chips.push_back({.chip = &sys.mcu, .divider = 48U});
            }
            return chips;
        }

        [[nodiscard]] std::int16_t add_clamped(std::int16_t sample, std::int16_t addend) noexcept {
            const std::int32_t mixed = static_cast<std::int32_t>(sample) + addend;
            if (mixed > 32767) {
                return 32767;
            }
            if (mixed < -32768) {
                return -32768;
            }
            return static_cast<std::int16_t>(mixed);
        }

        void mix_dac_range(std::span<std::int16_t> interleaved_stereo, std::size_t first_frame,
                           std::size_t last_frame, std::int16_t dac) noexcept {
            if (dac == 0 || first_frame >= last_frame) {
                return;
            }
            const std::size_t first = first_frame * 2U;
            const std::size_t last = last_frame * 2U;
            for (std::size_t i = first; i + 1U < last; i += 2U) {
                interleaved_stereo[i] = add_clamped(interleaved_stereo[i], dac);
                interleaved_stereo[i + 1U] = add_clamped(interleaved_stereo[i + 1U], dac);
            }
        }

        // Player-adapter state format. This is deliberately separate from the
        // board schema so a board-only save cannot masquerade as a frame-exact
        // player rollback point.
        constexpr std::uint32_t irem_m72_adapter_state_version = 2U;
        constexpr std::uint32_t irem_m72_adapter_save_target_manifest_rev = 4U;

        void write_i16(chips::state_writer& writer, std::int16_t value) {
            writer.u16(static_cast<std::uint16_t>(static_cast<std::int32_t>(value) + 32768));
        }

        [[nodiscard]] std::int16_t read_i16(chips::state_reader& reader) noexcept {
            return static_cast<std::int16_t>(static_cast<std::int32_t>(reader.u16()) - 32768);
        }

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
            writer.boolean(state.x);
            writer.boolean(state.y);
            writer.boolean(state.z);
            writer.boolean(state.mode);
            writer.boolean(state.service);
            writer.boolean(state.test);
            write_i16(writer, state.aim_x);
            write_i16(writer, state.aim_y);
            writer.boolean(state.trigger);
        }

        [[nodiscard]] frontend_sdk::controller_state
        load_controller_state(chips::state_reader& reader, std::uint32_t version) noexcept {
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
            state.x = reader.boolean();
            state.y = reader.boolean();
            state.z = reader.boolean();
            state.mode = reader.boolean();
            if (version >= 2U) {
                state.service = reader.boolean();
                state.test = reader.boolean();
            }
            state.aim_x = read_i16(reader);
            state.aim_y = read_i16(reader);
            state.trigger = reader.boolean();
            return state;
        }

    } // namespace

    irem_m72_adapter::irem_m72_adapter(std::vector<std::uint8_t> rom, std::string display_name,
                                       frontend_sdk::scheduler_factory* scheduler_factory,
                                       std::optional<std::uint16_t> dip_override,
                                       std::string rom_path,
                                       std::vector<std::vector<std::uint8_t>> supplemental_roms,
                                       std::vector<std::string> supplemental_rom_paths)
        : session_(make_session_capabilities()) {
        const std::uint64_t source_byte_count = rom.size();
        std::vector<supplemental_rom_source> supplemental_sources;
        supplemental_sources.reserve(supplemental_roms.size());
        for (std::size_t i = 0; i < supplemental_roms.size(); ++i) {
            supplemental_sources.push_back({.rom = std::move(supplemental_roms[i]),
                                            .path = i < supplemental_rom_paths.size()
                                                        ? std::move(supplemental_rom_paths[i])
                                                        : std::string{}});
        }
        loaded_set set = load_set(std::move(rom), rom_path, supplemental_sources);
        media_ = make_media_capabilities(display_name, set, source_byte_count);
        orientation_ = set.orientation;
        dip_switches_ = std::move(set.dip_switches);
        loaded_set_name_ = set.set_name;
        sys_ = assemble_from(std::move(set));
        dac_mix_output_ = sys_->dac.output();
        scheduler_.emplace(
            frontend_sdk::make_scheduler(scheduler_factory, build_schedule(*sys_), &sys_->video));
        if (dip_override.has_value()) {
            sys_->dip_switches = *dip_override;
        }
        chip_view_ = {&sys_->video, &sys_->main_cpu, &sys_->sound_cpu, &sys_->pic,
                      &sys_->fm,    &sys_->dac,      &sys_->sample_pump};
        if (sys_->mcu_present) {
            chip_view_.push_back(&sys_->mcu);
        }
        publish_memory_views();
        const std::string game_label =
            !loaded_set_name_.empty()
                ? loaded_set_name_
                : (display_name.empty() ? std::string{"unknown"} : display_name);
        spec_ = {{"System", "Arcade"}, {"Board", "Irem M72"}, {"Game", game_label}};
        if (!dip_switches_.empty()) {
            spec_.push_back({"DIP switches", std::to_string(dip_switches_.size())});
        }
    }

    void irem_m72_adapter::publish_memory_views() {
        auto publish = [this](std::size_t index, std::string_view name,
                              std::span<const std::uint8_t> bytes) {
            memory_view_storage_[index] =
                std::make_unique<instrumentation::span_memory_view>(name, bytes);
            system_mem_view_[index] = memory_view_storage_[index].get();
        };

        publish(0U, "work_ram", sys_->work_ram);
        publish(1U, "sound_ram", sys_->sound_ram);
        publish(2U, "sprite_ram", sys_->sprite_ram);
        publish(3U, "palette_a", sys_->palette_a);
        publish(4U, "palette_b", sys_->palette_b);
        publish(5U, "vram_a", sys_->vram_a);
        publish(6U, "vram_b", sys_->vram_b);
        publish(7U, "mcu_shared_ram", sys_->mcu_shared_ram);
    }

    void irem_m72_adapter::sync_inputs_from_ports() noexcept {
        // Hardware bit layout (active low): joystick right/left/down/up from
        // bit 0 (the standard arcade nibble), buttons 4..1 from bit 4 -- button 1
        // is the MSB, button 2 next.
        const auto pack = [](const frontend_sdk::controller_state& c) -> std::uint8_t {
            return pack_active_low_pad(c, dpad_layout{}, {{c.a, 0x80U}, {c.b, 0x40U}});
        };
        sys_->input_p1 = pack(ports_[0]);
        sys_->input_p2 = pack(ports_[1]);

        // System byte: start1/start2/coin1/coin2 from bit 0, service1/2 at
        // bits 4/5, and operator test at bit 6. The board read path supplies
        // bit 7 as sprite-DMA-complete.
        std::uint8_t system_byte = 0xFFU;
        if (ports_[0].start) {
            system_byte &= static_cast<std::uint8_t>(~0x01U); // start 1
        }
        if (ports_[1].start) {
            system_byte &= static_cast<std::uint8_t>(~0x02U); // start 2
        }
        if (ports_[0].select) {
            system_byte &= static_cast<std::uint8_t>(~0x04U); // coin 1
        }
        if (ports_[1].select) {
            system_byte &= static_cast<std::uint8_t>(~0x08U); // coin 2
        }
        if (ports_[0].service || ports_[0].mode) {
            system_byte &= static_cast<std::uint8_t>(~0x10U); // service 1
        }
        if (ports_[1].service || ports_[1].mode) {
            system_byte &= static_cast<std::uint8_t>(~0x20U); // service 2
        }
        if (ports_[0].test || ports_[1].test) {
            system_byte &= static_cast<std::uint8_t>(~0x40U); // operator test
        }
        sys_->input_system = system_byte;
    }

    void irem_m72_adapter::save_adapter_state(chips::state_writer& writer) const {
        writer.u32(irem_m72_adapter_state_version);
        writer.u64(frames_stepped_);
        writer.u64(samples_drained_);
        write_i16(writer, dac_mix_output_);
        for (const auto& port : ports_) {
            save_controller_state(writer, port);
        }
    }

    void irem_m72_adapter::load_adapter_state(chips::state_reader& reader) {
        const std::uint32_t version = reader.u32();
        if (version == 0U || version > irem_m72_adapter_state_version) {
            reader.fail();
            return;
        }
        frames_stepped_ = reader.u64();
        samples_drained_ = reader.u64();
        dac_mix_output_ = read_i16(reader);
        for (auto& port : ports_) {
            port = load_controller_state(reader, version);
        }
        if (reader.ok()) {
            sync_inputs_from_ports();
        }
    }

    frontend_sdk::audio_chunk irem_m72_adapter::drain_audio() noexcept {
        // One stereo frame per 64 YM2151 clocks; the chip's elapsed-clock
        // counter is the sample clock, so drains never drift from emulation.
        constexpr std::uint64_t clocks_per_sample = chips::audio::ym2151::clocks_per_sample;
        const std::uint64_t start_sample = samples_drained_;
        const std::uint64_t due = sys_->fm.elapsed_clocks() / clocks_per_sample;
        const std::uint64_t pending = due - start_sample;
        samples_drained_ = due;
        if (pending == 0U) {
            return {};
        }
        audio_buf_.assign(static_cast<std::size_t>(pending) * 2U, 0);
        sys_->fm.update(audio_buf_);

        if (sys_->dac_write_events.empty()) {
            dac_mix_output_ = sys_->dac.output();
        }
        std::int16_t dac = dac_mix_output_;
        std::size_t cursor = 0U;
        for (const auto& event : sys_->dac_write_events) {
            const std::uint64_t event_sample = event.sound_clock / clocks_per_sample;
            if (event_sample < start_sample) {
                dac = event.output;
                continue;
            }
            if (event_sample >= due) {
                break;
            }
            const auto boundary = static_cast<std::size_t>(event_sample - start_sample);
            mix_dac_range(audio_buf_, cursor, boundary, dac);
            dac = event.output;
            cursor = boundary;
        }
        mix_dac_range(audio_buf_, cursor, static_cast<std::size_t>(pending), dac);
        dac_mix_output_ = dac;
        sys_->discard_dac_write_events_before(due * clocks_per_sample);

        return {.samples = audio_buf_.data(),
                .frame_count = static_cast<std::uint32_t>(pending),
                .sample_rate = 55930U}; // 3579545 / 64
    }

    void irem_m72_adapter::step_one_frame() {
        scheduler_->run_frame();
        ++frames_stepped_;
    }

    void irem_m72_adapter::apply_input(int port,
                                       const frontend_sdk::controller_state& state) noexcept {
        if (port < 0 || port > 1) {
            return; // two-player hardware
        }
        ports_[static_cast<std::size_t>(port)] = state;
        sync_inputs_from_ports();
    }

    std::vector<std::uint8_t> irem_m72_adapter::save_state() {
        return runtime::write_save_state(build_save_target(*this));
    }

    runtime::load_result irem_m72_adapter::load_state(std::span<const std::uint8_t> data) {
        runtime::save_target target = build_save_target(*this);
        const runtime::load_result result = runtime::read_save_state(data, target);
        if (result.ok()) {
            audio_buf_.clear();
        }
        return result;
    }

    void force_link() noexcept {}

    runtime::save_target build_save_target(manifests::irem_m72::m72_system& sys) {
        runtime::save_target target;
        target.manifest_id = "irem_m72";
        target.manifest_rev = manifests::irem_m72::m72_system_state_version;
        target.components.push_back(
            {"board", [&sys](chips::state_writer& writer) { sys.save_state(writer); },
             [&sys](chips::state_reader& reader) { sys.load_state(reader); }});
        return target;
    }

    runtime::save_target build_save_target(irem_m72_adapter& adapter) {
        runtime::save_target target;
        target.manifest_id = "irem_m72.adapter";
        target.manifest_rev = irem_m72_adapter_save_target_manifest_rev;
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
        const auto register_irem_m72 = [] {
            mnemos::frontend_sdk::adapter_registry::instance().register_family(
                "irem_m72",
                [](mnemos::frontend_sdk::adapter_options opts)
                    -> std::unique_ptr<mnemos::frontend_sdk::player_system> {
                    return std::make_unique<irem_m72_adapter>(
                        std::move(opts.rom), std::move(opts.display_name),
                        opts.scheduler_factory_override, opts.dip_override,
                        std::move(opts.rom_path), std::move(opts.additional_media),
                        std::move(opts.additional_media_paths));
                });
            return 0;
        }();
    } // namespace

} // namespace mnemos::apps::player::adapters::irem_m72
