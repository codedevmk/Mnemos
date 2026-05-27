#pragma once

#include "config.hpp" // chips::config_table

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mnemos::manifests {

    inline constexpr std::string_view schema_id = "mnemos-manifest/1";

    enum class endianness : std::uint8_t { little, big };
    enum class region_backing : std::uint8_t { ram, rom, mmio_chip, mapper };

    struct address_range final {
        std::uint32_t start{};
        std::uint32_t end{}; // inclusive
    };

    struct clock_config final {
        std::uint64_t master_hz{};
        std::uint32_t master_to_cpu_divider{};
        std::uint32_t master_to_video_divider{};
    };

    struct chip_decl final {
        std::string id;
        std::string type; // canonical chip-factory id, e.g. "mos.6510"
        std::string attached_bus;
        std::optional<address_range> mmio_range;
        // Optional [chip.config] table parsed verbatim from the manifest. The
        // builder passes this to `ichip::configure(config)` after construction,
        // before `reset(power_on)`. Empty = chip uses its defaults.
        chips::config_table config{};
    };

    struct region_decl final {
        std::string name;
        address_range range;
        region_backing backing{region_backing::ram};
        std::uint32_t size{};
        std::optional<std::string> file;   // rom backing
        std::optional<std::string> sha256; // rom backing
        bool overlay{};
    };

    struct bus_decl final {
        std::string id;
        unsigned address_bits{16};
        endianness endian{endianness::little};
        std::vector<region_decl> regions;
    };

    struct manifest final {
        std::string schema;
        std::string id;
        std::string display_name;
        std::string family;
        std::uint32_t revision{};
        clock_config clock;
        std::vector<chip_decl> chips;
        std::vector<bus_decl> buses;
    };

    // One validation/parse failure, located in the source where possible.
    struct diagnostic final {
        std::string message;
        std::string source;
        unsigned line{};
        unsigned column{};
    };

    struct load_result final {
        std::optional<manifest> value;
        std::vector<diagnostic> errors;

        [[nodiscard]] bool ok() const noexcept { return value.has_value() && errors.empty(); }
    };

    // Parse + strictly validate a manifest from TOML text. On any error, `errors`
    // is non-empty and `value` is reset.
    [[nodiscard]] load_result parse_manifest(std::string_view text,
                                             std::string_view source_name = "<manifest>");

    [[nodiscard]] load_result load_manifest_file(const std::filesystem::path& path);

} // namespace mnemos::manifests
