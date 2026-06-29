#pragma once

// Taito G-NET package/media inspection. The local corpus stores G-NET flash
// cards as ZIP packages that contain CHD block-device images. This layer records
// the package structure and can load bounded CHD flash-card images without
// claiming board-level emulation support.

#include "chd_reader.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace mnemos::manifests::taito_gnet {

    struct gnet_chd_entry {
        std::string name;
        std::uint32_t compressed_size{};
        std::uint32_t uncompressed_size{};
        bool payload_extracted{};
        std::optional<disc::chd::chd_file_info> chd;
    };

    struct gnet_package_report {
        std::vector<std::string> archive_entries;
        std::vector<gnet_chd_entry> chd_entries;
    };

    struct gnet_flash_card_image {
        std::string name;
        disc::chd::chd_block_image_data media;
    };

    [[nodiscard]] std::optional<gnet_package_report>
    inspect_gnet_package(std::span<const std::uint8_t> package_bytes);

    [[nodiscard]] std::optional<std::vector<gnet_flash_card_image>>
    load_gnet_flash_cards(std::span<const std::uint8_t> package_bytes,
                          std::uint64_t max_card_bytes = 128ULL * 1024ULL * 1024ULL);

    [[nodiscard]] bool has_probeable_chd_media(const gnet_package_report& report) noexcept;

} // namespace mnemos::manifests::taito_gnet
