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
        // For backing == mapper: id of the imapper chip whose
        // read_overlay/write_overlay/overlay_active methods route this
        // region's accesses. Required for mapper backing; ignored otherwise.
        std::optional<std::string> mapper_id;
    };

    struct bus_decl final {
        std::string id;
        unsigned address_bits{16};
        endianness endian{endianness::little};
        std::vector<region_decl> regions;
    };

    // Per-tick gate. When the named predicate is registered with the builder
    // and returns false, the wrapped chip's tick is skipped that cycle. Used
    // for: Genesis Z80 stalled by 68K's BUSREQ / RESET, Genesis 68K stalled
    // during VDP DMA, etc.
    struct gate_decl final {
        std::string chip_id;   // matches an existing chip_decl.id
        std::string predicate; // key into the host-supplied predicate_table
    };

    // System-specific MMIO escape: the host registers an mmio_factory by name;
    // the manifest references it via `[[mmio_block]]`. Used for stateful MMIO
    // regions that don't fit the chip-MMIO-window, mapper-overlay, or RAM/ROM
    // models -- Genesis controller-port block at $A10000-$A1001F, Z80 BUSREQ
    // at $A11100, etc.
    struct mmio_block_decl final {
        std::string name;          // key into the host-supplied mmio_factory_table
        std::string attached_bus;  // matches an existing bus_decl.id
        address_range range;
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
        std::vector<gate_decl> gates;
        std::vector<mmio_block_decl> mmio_blocks;
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
