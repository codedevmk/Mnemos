#include "rom_loader.hpp"

#include "file.hpp"
#include "inflate.hpp"
#include "zip_archive.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <system_error>

namespace mnemos::apps::player::adapters {

    namespace {
        constexpr std::uint64_t max_gzip_entry_size = 256ULL * 1024U * 1024U;
        constexpr std::uint64_t max_gzip_expansion = 1032U;

        [[nodiscard]] bool is_zip_signature(std::span<const std::uint8_t> bytes) noexcept {
            return bytes.size() >= 4U && bytes[0] == 'P' && bytes[1] == 'K' && bytes[2] == 0x03U &&
                   bytes[3] == 0x04U;
        }

        [[nodiscard]] bool is_gzip_signature(std::span<const std::uint8_t> bytes) noexcept {
            return bytes.size() >= 10U && bytes[0] == 0x1FU && bytes[1] == 0x8BU &&
                   bytes[2] == 0x08U;
        }

        [[nodiscard]] std::uint16_t rd_le16(std::span<const std::uint8_t> bytes,
                                            std::size_t offset) noexcept {
            return static_cast<std::uint16_t>(
                bytes[offset] | (static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U));
        }

        [[nodiscard]] std::uint32_t rd_le32(std::span<const std::uint8_t> bytes,
                                            std::size_t offset) noexcept {
            return static_cast<std::uint32_t>(bytes[offset]) |
                   (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
                   (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
                   (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
        }

        [[nodiscard]] std::string lowercase_ascii(std::string_view value) {
            std::string out;
            out.reserve(value.size());
            for (char ch : value) {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            return out;
        }

        [[nodiscard]] std::string lowercase_extension(std::string_view name) {
            const std::size_t slash = name.find_last_of("/\\");
            const std::size_t begin = slash == std::string_view::npos ? 0U : slash + 1U;
            const std::size_t dot = name.find_last_of('.');
            if (dot == std::string_view::npos || dot < begin) {
                return {};
            }
            return lowercase_ascii(name.substr(dot));
        }

        [[nodiscard]] bool extension_matches(std::string_view name,
                                             std::span<const std::string_view> extensions) {
            const std::string ext = lowercase_extension(name);
            return std::any_of(extensions.begin(), extensions.end(), [&](std::string_view wanted) {
                return ext == lowercase_ascii(wanted);
            });
        }

        [[nodiscard]] bool zip_entry_is_directory(const compression::zip_entry& entry) noexcept {
            return !entry.name.empty() && entry.name.back() == '/';
        }

        [[nodiscard]] std::string filename_without_extension(std::string_view name) {
            const std::size_t slash = name.find_last_of("/\\");
            const std::size_t begin = slash == std::string_view::npos ? 0U : slash + 1U;
            const std::size_t dot = name.find_last_of('.');
            const std::size_t end =
                dot == std::string_view::npos || dot < begin ? name.size() : dot;
            return std::string{name.substr(begin, end - begin)};
        }

        void trim_in_place(std::string& value) {
            const auto not_space = [](unsigned char ch) {
                return std::isspace(ch) == 0 && ch != '_' && ch != '-';
            };
            const auto first =
                std::find_if(value.begin(), value.end(), [&](char ch) { return not_space(ch); });
            const auto last = std::find_if(value.rbegin(), value.rend(), [&](char ch) {
                                  return not_space(ch);
                              }).base();
            if (first >= last) {
                value.clear();
                return;
            }
            value = std::string{first, last};
        }

        struct disk_marker final {
            std::size_t begin{};
            std::size_t end{};
            std::uint32_t number{};
            std::uint32_t total{};
            bool present{};
        };

        void skip_disk_marker_separators(std::string_view text, std::size_t& offset) noexcept {
            while (offset < text.size()) {
                const char ch = text[offset];
                if (std::isspace(static_cast<unsigned char>(ch)) == 0 && ch != '_' && ch != '-') {
                    break;
                }
                ++offset;
            }
        }

        [[nodiscard]] bool parse_decimal_at(std::string_view text, std::size_t& offset,
                                            std::uint32_t& out) noexcept {
            if (offset >= text.size() ||
                std::isdigit(static_cast<unsigned char>(text[offset])) == 0) {
                return false;
            }
            std::uint32_t value = 0U;
            while (offset < text.size() &&
                   std::isdigit(static_cast<unsigned char>(text[offset])) != 0) {
                value = value * 10U + static_cast<std::uint32_t>(text[offset] - '0');
                ++offset;
            }
            out = value;
            return true;
        }

        [[nodiscard]] std::optional<disk_marker> find_disk_marker(std::string_view name) {
            const std::string stem = lowercase_ascii(filename_without_extension(name));
            std::size_t search = 0U;
            while (search < stem.size()) {
                const std::size_t disk = stem.find("disk", search);
                if (disk == std::string::npos) {
                    return std::nullopt;
                }
                std::size_t offset = disk + 4U;
                skip_disk_marker_separators(stem, offset);

                std::uint32_t number = 0U;
                std::uint32_t total = 0U;
                if (!parse_decimal_at(stem, offset, number)) {
                    search = disk + 4U;
                    continue;
                }
                skip_disk_marker_separators(stem, offset);
                if (offset + 2U > stem.size() || stem.substr(offset, 2U) != "of") {
                    search = disk + 4U;
                    continue;
                }
                offset += 2U;
                skip_disk_marker_separators(stem, offset);
                if (!parse_decimal_at(stem, offset, total) || number == 0U || total == 0U ||
                    number > total || total > 32U) {
                    search = disk + 4U;
                    continue;
                }

                std::size_t begin = disk;
                std::size_t end = offset;
                if (begin > 0U && end < stem.size()) {
                    const char before = stem[begin - 1U];
                    const char after = stem[end];
                    if ((before == '(' && after == ')') || (before == '[' && after == ']')) {
                        --begin;
                        ++end;
                    }
                }
                return disk_marker{
                    .begin = begin, .end = end, .number = number, .total = total, .present = true};
            }
            return std::nullopt;
        }

        [[nodiscard]] std::string nested_disk_group_key(std::string_view name) {
            std::string stem = lowercase_ascii(filename_without_extension(name));
            if (auto marker = find_disk_marker(name); marker.has_value()) {
                stem.erase(marker->begin, marker->end - marker->begin);
            }
            trim_in_place(stem);
            return stem;
        }

        struct nested_zip_media final {
            std::size_t outer_index{};
            std::string group_key;
            std::uint32_t disk_number{};
            std::uint32_t disk_total{};
            bool has_disk_marker{};
            std::vector<loaded_rom> media;
        };

        [[nodiscard]] std::optional<std::vector<loaded_rom>>
        choose_nested_zip_media(std::vector<nested_zip_media>& groups) {
            if (groups.empty()) {
                return std::nullopt;
            }

            struct complete_candidate final {
                std::size_t first_index{};
                std::vector<loaded_rom> media;
            };
            std::vector<complete_candidate> complete;
            for (const auto& seed : groups) {
                if (!seed.has_disk_marker || seed.disk_total <= 1U) {
                    continue;
                }

                std::vector<const nested_zip_media*> sequence(seed.disk_total + 1U, nullptr);
                bool duplicate = false;
                std::size_t first_index = std::numeric_limits<std::size_t>::max();
                for (const auto& group : groups) {
                    if (!group.has_disk_marker || group.disk_total != seed.disk_total ||
                        group.group_key != seed.group_key) {
                        continue;
                    }
                    if (sequence[group.disk_number] != nullptr) {
                        duplicate = true;
                        break;
                    }
                    sequence[group.disk_number] = &group;
                    first_index = std::min(first_index, group.outer_index);
                }
                if (duplicate) {
                    continue;
                }

                bool complete_set = true;
                for (std::uint32_t disk = 1U; disk <= seed.disk_total; ++disk) {
                    if (sequence[disk] == nullptr) {
                        complete_set = false;
                        break;
                    }
                }
                if (!complete_set) {
                    continue;
                }

                std::vector<loaded_rom> media;
                for (std::uint32_t disk = 1U; disk <= seed.disk_total; ++disk) {
                    for (const auto& entry : sequence[disk]->media) {
                        media.push_back(loaded_rom{.bytes = entry.bytes, .name = entry.name});
                    }
                }
                complete.push_back(
                    complete_candidate{.first_index = first_index, .media = std::move(media)});
            }

            if (!complete.empty()) {
                const auto best = std::min_element(
                    complete.begin(), complete.end(),
                    [](const auto& a, const auto& b) { return a.first_index < b.first_index; });
                return std::move(best->media);
            }

            const auto first_usable =
                std::find_if(groups.begin(), groups.end(), [](const nested_zip_media& group) {
                    return !group.has_disk_marker || group.disk_number == 1U;
                });
            if (first_usable != groups.end()) {
                return std::move(first_usable->media);
            }
            return std::move(groups.front().media);
        }

        [[nodiscard]] std::optional<std::vector<loaded_rom>>
        choose_complete_direct_disk_sequence(std::vector<loaded_rom>& media) {
            struct marked_entry final {
                std::size_t index{};
                std::string group_key;
                std::uint32_t disk_number{};
                std::uint32_t disk_total{};
            };

            std::vector<marked_entry> marked;
            for (std::size_t i = 0U; i < media.size(); ++i) {
                const auto marker = find_disk_marker(media[i].name);
                if (!marker || marker->total <= 1U) {
                    continue;
                }
                marked.push_back(marked_entry{.index = i,
                                              .group_key = nested_disk_group_key(media[i].name),
                                              .disk_number = marker->number,
                                              .disk_total = marker->total});
            }
            if (marked.empty()) {
                return std::nullopt;
            }

            struct complete_candidate final {
                std::size_t first_index{};
                std::vector<std::size_t> indexes;
            };
            std::vector<complete_candidate> complete;
            for (const auto& seed : marked) {
                std::vector<std::size_t> sequence(seed.disk_total + 1U,
                                                  std::numeric_limits<std::size_t>::max());
                bool duplicate = false;
                std::size_t first_index = std::numeric_limits<std::size_t>::max();
                for (const auto& entry : marked) {
                    if (entry.disk_total != seed.disk_total || entry.group_key != seed.group_key) {
                        continue;
                    }
                    if (sequence[entry.disk_number] != std::numeric_limits<std::size_t>::max()) {
                        duplicate = true;
                        break;
                    }
                    sequence[entry.disk_number] = entry.index;
                    first_index = std::min(first_index, entry.index);
                }
                if (duplicate) {
                    continue;
                }

                bool complete_set = true;
                std::vector<std::size_t> indexes;
                indexes.reserve(seed.disk_total);
                for (std::uint32_t disk = 1U; disk <= seed.disk_total; ++disk) {
                    const std::size_t index = sequence[disk];
                    if (index == std::numeric_limits<std::size_t>::max()) {
                        complete_set = false;
                        break;
                    }
                    indexes.push_back(index);
                }
                if (complete_set) {
                    complete.push_back(complete_candidate{.first_index = first_index,
                                                          .indexes = std::move(indexes)});
                }
            }
            if (complete.empty()) {
                return std::nullopt;
            }

            const auto best = std::min_element(
                complete.begin(), complete.end(),
                [](const auto& a, const auto& b) { return a.first_index < b.first_index; });
            std::vector<loaded_rom> ordered;
            ordered.reserve(best->indexes.size());
            for (const std::size_t index : best->indexes) {
                ordered.push_back(std::move(media[index]));
            }
            return ordered;
        }

        [[nodiscard]] std::optional<std::string>
        read_gzip_zero_string(std::span<const std::uint8_t> bytes, std::size_t& offset) {
            const std::size_t start = offset;
            while (offset < bytes.size() && bytes[offset] != 0U) {
                ++offset;
            }
            if (offset >= bytes.size()) {
                return std::nullopt;
            }
            std::string out(reinterpret_cast<const char*>(bytes.data() + start), offset - start);
            ++offset;
            return out;
        }

        [[nodiscard]] std::optional<loaded_rom> decode_gzip_or_raw(std::vector<std::uint8_t> bytes,
                                                                   std::string name) {
            if (!is_gzip_signature(bytes)) {
                return loaded_rom{.bytes = std::move(bytes), .name = std::move(name)};
            }

            const std::span<const std::uint8_t> source(bytes);
            const std::uint8_t flags = source[3U];
            if ((flags & 0xE0U) != 0U || source.size() < 18U) {
                return std::nullopt;
            }

            std::size_t offset = 10U;
            if ((flags & 0x04U) != 0U) {
                if (offset + 2U > source.size()) {
                    return std::nullopt;
                }
                const std::uint16_t extra_len = rd_le16(source, offset);
                offset += 2U;
                if (offset + extra_len > source.size()) {
                    return std::nullopt;
                }
                offset += extra_len;
            }

            if ((flags & 0x08U) != 0U) {
                auto original_name = read_gzip_zero_string(source, offset);
                if (!original_name) {
                    return std::nullopt;
                }
                if (!original_name->empty()) {
                    name = std::move(*original_name);
                }
            }

            if ((flags & 0x10U) != 0U && !read_gzip_zero_string(source, offset)) {
                return std::nullopt;
            }

            if ((flags & 0x02U) != 0U) {
                if (offset + 2U > source.size()) {
                    return std::nullopt;
                }
                offset += 2U;
            }

            if (offset + 8U > source.size()) {
                return std::nullopt;
            }
            const std::size_t trailer = source.size() - 8U;
            if (offset > trailer) {
                return std::nullopt;
            }

            const std::uint32_t expected_size = rd_le32(source, source.size() - 4U);
            const std::size_t compressed_size = trailer - offset;
            if (expected_size > max_gzip_entry_size ||
                expected_size >
                    (static_cast<std::uint64_t>(compressed_size) + 64U) * max_gzip_expansion) {
                return std::nullopt;
            }

            std::vector<std::uint8_t> out(expected_size);
            std::size_t consumed = 0U;
            const auto written =
                compression::inflate_raw(source.subspan(offset, compressed_size), out, consumed);
            if (!written || *written != out.size() || consumed != compressed_size) {
                return std::nullopt;
            }
            return loaded_rom{.bytes = std::move(out), .name = std::move(name)};
        }

        [[nodiscard]] std::optional<std::vector<loaded_rom>>
        load_zip_entries_by_extension(const compression::zip_archive& archive,
                                      std::span<const std::string_view> extensions,
                                      unsigned depth) {
            using mnemos::compression::zip_method;
            constexpr unsigned max_nested_zip_depth = 2U;

            std::vector<loaded_rom> direct;
            for (const auto& entry : archive.entries()) {
                if (zip_entry_is_directory(entry) || !extension_matches(entry.name, extensions)) {
                    continue;
                }
                if (entry.method == zip_method::unsupported || entry.uncompressed_size == 0U) {
                    return std::nullopt;
                }
                auto extracted = archive.extract(entry);
                if (!extracted) {
                    return std::nullopt;
                }
                auto loaded = decode_gzip_or_raw(std::move(*extracted), entry.name);
                if (!loaded) {
                    return std::nullopt;
                }
                direct.push_back(std::move(*loaded));
            }
            if (!direct.empty()) {
                if (auto complete = choose_complete_direct_disk_sequence(direct);
                    complete.has_value()) {
                    return complete;
                }
                return direct;
            }

            if (depth >= max_nested_zip_depth) {
                return std::nullopt;
            }

            std::vector<nested_zip_media> nested;
            std::size_t outer_index = 0U;
            for (const auto& entry : archive.entries()) {
                const std::size_t current_index = outer_index++;
                if (zip_entry_is_directory(entry) || lowercase_extension(entry.name) != ".zip" ||
                    entry.method == zip_method::unsupported || entry.uncompressed_size == 0U) {
                    continue;
                }
                auto extracted = archive.extract(entry);
                if (!extracted) {
                    return std::nullopt;
                }
                auto inner_archive = compression::zip_archive::open(*extracted);
                if (!inner_archive) {
                    continue;
                }
                auto inner = load_zip_entries_by_extension(*inner_archive, extensions, depth + 1U);
                if (!inner || inner->empty()) {
                    continue;
                }
                const auto marker = find_disk_marker(entry.name);
                nested.push_back(nested_zip_media{.outer_index = current_index,
                                                  .group_key = nested_disk_group_key(entry.name),
                                                  .disk_number = marker ? marker->number : 0U,
                                                  .disk_total = marker ? marker->total : 0U,
                                                  .has_disk_marker = marker.has_value(),
                                                  .media = std::move(*inner)});
            }
            return choose_nested_zip_media(nested);
        }
    } // namespace

    std::optional<loaded_rom> load_rom(const std::string& path) {
        using mnemos::compression::zip_archive;
        using mnemos::compression::zip_entry;
        using mnemos::compression::zip_method;

        auto bytes = mnemos::io::read_file(path);
        if (!bytes) {
            return std::nullopt;
        }
        // ZIP local-file-header magic "PK\3\4". A bare console ROM never starts
        // with it, so the signature reliably distinguishes archive from image.
        if (!is_zip_signature(*bytes)) {
            return decode_gzip_or_raw(std::move(*bytes), path);
        }

        const auto archive = zip_archive::open(*bytes);
        if (!archive) {
            return std::nullopt;
        }
        const zip_entry* best = nullptr;
        for (const zip_entry& e : archive->entries()) {
            const bool is_dir = !e.name.empty() && e.name.back() == '/';
            if (e.method == zip_method::unsupported || e.uncompressed_size == 0U || is_dir) {
                continue;
            }
            if (best == nullptr || e.uncompressed_size > best->uncompressed_size) {
                best = &e;
            }
        }
        if (best == nullptr) {
            return std::nullopt;
        }
        auto extracted = archive->extract(*best);
        if (!extracted) {
            return std::nullopt;
        }
        return decode_gzip_or_raw(std::move(*extracted), best->name);
    }

    std::optional<std::vector<loaded_rom>>
    load_rom_entries_by_extension(const std::string& path,
                                  std::span<const std::string_view> extensions) {
        using mnemos::compression::zip_archive;

        auto bytes = mnemos::io::read_file(path);
        if (!bytes) {
            return std::nullopt;
        }
        if (!is_zip_signature(*bytes)) {
            if (!extension_matches(path, extensions)) {
                return std::nullopt;
            }
            auto loaded = decode_gzip_or_raw(std::move(*bytes), path);
            if (!loaded) {
                return std::nullopt;
            }
            std::vector<loaded_rom> out;
            out.push_back(std::move(*loaded));
            return out;
        }

        const auto archive = zip_archive::open(*bytes);
        if (!archive) {
            return std::nullopt;
        }
        return load_zip_entries_by_extension(*archive, extensions, 0U);
    }

    std::optional<loaded_rom> load_rom_verbatim(const std::string& path) {
        std::error_code ec;
        if (std::filesystem::is_directory(std::filesystem::path{path}, ec)) {
            return loaded_rom{.bytes = {}, .name = path, .directory_source = true};
        }
        auto bytes = mnemos::io::read_file(path);
        if (!bytes) {
            return std::nullopt;
        }
        return loaded_rom{.bytes = std::move(*bytes), .name = path};
    }

    std::string clean_rom_name(const std::string& path) {
        const auto slash = path.find_last_of("/\\");
        const auto begin = slash == std::string::npos ? 0U : slash + 1U;
        const auto dot = path.find_last_of('.');
        const auto end = (dot == std::string::npos || dot < begin) ? path.size() : dot;
        return path.substr(begin, end - begin);
    }

} // namespace mnemos::apps::player::adapters
