#pragma once

#include "chip.hpp"
#include "state.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

// Save-state container (TDS §15, ADR 0008): an uncompressed header, a zstd-
// compressed body of size-prefixed chunks (one per chip + per memory region),
// and a trailing CRC32 over everything before it. Unknown chunks are skipped at
// load for forward compatibility.
namespace mnemos::runtime {

    struct save_chip final {
        std::string id; // chunk id (the manifest chip id)
        chips::ichip* chip{};
    };

    struct save_memory final {
        std::string id;                // chunk id (e.g. "ram", "color_ram")
        std::span<std::uint8_t> bytes; // borrowed; read on save, written on load
    };

    // An arbitrary serialisable machine component that is neither an ichip nor a
    // flat memory region -- e.g. the runtime scheduler's pacing state, or a non-chip
    // peripheral (the C64 keyboard-matrix/joystick state). Both callbacks encode and
    // decode through the same chunk framing as chips, so machine state that lives
    // outside the chip set is no longer unserialisable.
    struct save_component final {
        std::string id;                                  // chunk id (unique across the target)
        std::function<void(chips::state_writer&)> save;  // read component state on save
        std::function<void(chips::state_reader&)> load;  // restore component state on load
    };

    // The pieces of a machine a save state captures. master_cycle is recorded in
    // the header; chips/memory become chunks keyed by id.
    struct save_target final {
        std::string manifest_id;
        std::uint32_t manifest_rev{};
        std::uint64_t master_cycle{};
        std::vector<save_chip> chips;
        std::vector<save_memory> memory;
        std::vector<save_component> components;
    };

    // Format version of the container itself (not the manifest).
    inline constexpr std::uint32_t save_state_format_version = 1U;

    [[nodiscard]] std::vector<std::uint8_t> write_save_state(const save_target& target);

    enum class load_status : std::uint8_t {
        ok,
        bad_magic,
        unsupported_version,
        manifest_mismatch,
        truncated,
        bad_crc,
        decompress_failed,
    };

    struct load_result final {
        load_status status{load_status::ok};
        std::uint64_t master_cycle{};

        [[nodiscard]] bool ok() const noexcept { return status == load_status::ok; }
    };

    // Validate the container (magic, version, manifest id, CRC32), decompress, and
    // restore each chunk into the matching chip/memory of `target`. Chunks with an
    // id that matches nothing are skipped.
    [[nodiscard]] load_result read_save_state(std::span<const std::uint8_t> data,
                                              const save_target& target);

} // namespace mnemos::runtime
