#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace mnemos::topology {
    class bus;
}

namespace mnemos::manifests::genesis {

    // External cartridge RAM (battery-backed SRAM) as declared in the ROM header.
    // The address window is taken verbatim from the header (24-bit, sanitised for
    // the well-known garbage values); the backing store is a flat byte buffer
    // indexed by (address - start). The 8-bit SRAM chips are wired to one byte
    // lane, so a game only ever touches the odd (or even) addresses in the window
    // -- the off-lane cells simply stay at their power-on value, which is why the
    // backing buffer spans the whole window rather than packing one lane.
    struct cart_sram {
        std::uint32_t start{}; // first byte address (e.g. $200000 or $200001)
        std::uint32_t end{};   // last byte address, inclusive

        // Window size in bytes -- one backing cell per address in [start, end].
        [[nodiscard]] std::size_t byte_count() const noexcept {
            return static_cast<std::size_t>(end - start) + 1U;
        }
    };

    // Parse the Sega cartridge header's external-RAM fields:
    //   $1B0-$1B1 = "RA" signature, $1B4/$1B8 = start/end address (BE32).
    // The addresses are masked to the 24-bit bus and sanitised the way real
    // hardware/headers force them: a $FF0000 start (work-RAM mislabelled as
    // backup) disables SRAM; a start at/above $800000 (off the cartridge window)
    // is relocated to the canonical 64 KiB at $200000; an inverted or >64 KiB
    // range is clamped to 64 KiB. Returns nullopt when no usable SRAM is declared
    // (signature absent, header too short, or the range disables itself).
    [[nodiscard]] std::optional<cart_sram>
    parse_cart_sram(std::span<const std::uint8_t> rom) noexcept;

    // Live cartridge battery-RAM state: the parsed window, its backing bytes, and
    // the $A130F1 control latch. The bus's SRAM handlers borrow this, so whatever
    // owns it must outlive the bus it is wired onto.
    struct cart_sram_runtime final {
        std::optional<cart_sram> info;  // nullopt when the cart declares no SRAM
        std::vector<std::uint8_t> data; // backing bytes (info->byte_count() of them)
        // $A130F1 bit 0: SRAM mapped over the cartridge window. Powers on mapped
        // only when the window sits ABOVE the ROM image; carts whose SRAM overlaps
        // ROM (>2 MB images) boot ROM-visible and map SRAM in on demand.
        bool enabled{true};
        bool write_protect{false}; // $A130F1 bit 1: writes ignored while set
    };

    // Parse `rom`'s SRAM header and, if present, map the backing store onto `bus`
    // as a priority-1 region above the cartridge ROM, plus the $A130F1 control
    // latch. State is stored in `out` (whose `data` the handlers borrow, so `out`
    // must outlive `bus`). No-op when the cart declares no usable SRAM. Shared by
    // build_genesis_runtime and assemble_genesis so both wire SRAM identically.
    void wire_cart_sram(topology::bus& bus, cart_sram_runtime& out,
                        std::span<const std::uint8_t> rom);

} // namespace mnemos::manifests::genesis
