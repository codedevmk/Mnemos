#include "taito_gnet_media.hpp"

#include "zip_archive.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>

namespace mnemos::manifests::taito_gnet {
    namespace {

        [[nodiscard]] char ascii_lower(char c) noexcept {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        [[nodiscard]] bool ends_with_ascii_ci(std::string_view value,
                                              std::string_view suffix) noexcept {
            if (value.size() < suffix.size()) {
                return false;
            }
            const std::size_t offset = value.size() - suffix.size();
            for (std::size_t i = 0; i < suffix.size(); ++i) {
                if (ascii_lower(value[offset + i]) != ascii_lower(suffix[i])) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool is_chd_entry(std::string_view name) noexcept {
            return ends_with_ascii_ci(name, ".chd");
        }

    } // namespace

    std::optional<gnet_package_report>
    inspect_gnet_package(std::span<const std::uint8_t> package_bytes) {
        auto archive = compression::zip_archive::open(package_bytes);
        if (!archive) {
            return std::nullopt;
        }

        gnet_package_report report;
        report.archive_entries.reserve(archive->entries().size());

        for (const compression::zip_entry& entry : archive->entries()) {
            report.archive_entries.push_back(entry.name);
            if (!is_chd_entry(entry.name)) {
                continue;
            }

            gnet_chd_entry chd_entry;
            chd_entry.name = entry.name;
            chd_entry.compressed_size = entry.compressed_size;
            chd_entry.uncompressed_size = entry.uncompressed_size;

            if (auto payload = archive->extract(entry)) {
                chd_entry.payload_extracted = true;
                chd_entry.chd = disc::chd::probe(std::span<const std::uint8_t>{*payload});
            }

            report.chd_entries.push_back(std::move(chd_entry));
        }

        return report;
    }

    std::optional<std::vector<gnet_flash_card_image>>
    load_gnet_flash_cards(std::span<const std::uint8_t> package_bytes,
                          std::uint64_t max_card_bytes) {
        auto archive = compression::zip_archive::open(package_bytes);
        if (!archive) {
            return std::nullopt;
        }

        std::vector<gnet_flash_card_image> cards;
        for (const compression::zip_entry& entry : archive->entries()) {
            if (!is_chd_entry(entry.name)) {
                continue;
            }

            auto payload = archive->extract(entry);
            if (!payload) {
                return std::nullopt;
            }
            auto media = disc::chd::decode_block_device(
                std::span<const std::uint8_t>{*payload}, max_card_bytes);
            if (!media) {
                return std::nullopt;
            }

            cards.push_back(gnet_flash_card_image{.name = entry.name, .media = std::move(*media)});
        }

        if (cards.empty()) {
            return std::nullopt;
        }
        return cards;
    }

    bool has_probeable_chd_media(const gnet_package_report& report) noexcept {
        for (const gnet_chd_entry& entry : report.chd_entries) {
            if (entry.chd.has_value()) {
                return true;
            }
        }
        return false;
    }

} // namespace mnemos::manifests::taito_gnet
