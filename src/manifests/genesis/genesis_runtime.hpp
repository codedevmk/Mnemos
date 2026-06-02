#pragma once

// Manifest-path counterpart of genesis_system / assemble_genesis.
//
// assemble_genesis hand-wires a Genesis into a value-member struct and stays the
// independent parity oracle. build_genesis_runtime produces the SAME machine
// through build_system + genesis_callbacks: it picks the embedded manifest by
// region, supplies the cartridge via the rom_provider, wires the chip + bus
// pointers the host callbacks close over, sets the region/version register, and
// default-plugs the controller pads.
//
// The Genesis gates the Z80 (BUSREQ/RESET) and the 68000 (VDP DMA stall), so the
// scheduler must tick the gated_chip WRAPPERS build_system produced -- not the
// inner CPUs. schedule() hands back the scheduler-view chips in canonical order
// with their tick weights, as plain ichip* pairs (the manifests tier sits below
// runtime, so it cannot name runtime::scheduled_chip -- the adapter/test convert).

#include "builder.hpp"           // mnemos::manifests::system_graph
#include "genesis_callbacks.hpp" // state + chip/bus types (+ chips::ichip via chip.hpp)
#include "genesis_cart.hpp"      // cart_sram (header SRAM descriptor)
#include "genesis_system.hpp"    // genesis_config

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace mnemos::manifests::genesis {

    // One scheduler slot: the chip to tick and its per-step weight. Uses ichip*
    // (tier 2) rather than runtime::scheduled_chip (tier 5) so this header stays
    // within the manifests tier; the adapter/test map it to scheduled_chip.
    struct scheduled_entry final {
        chips::ichip* chip{};
        std::uint32_t weight{};
    };

    // Cartridge battery-RAM (SRAM): the header-declared region, its backing bytes,
    // and the $A130F1 enable latch. The bus's SRAM handlers borrow this, so the
    // owning genesis_runtime declares it BEFORE `graph` (which destructs first).
    struct cart_sram_runtime final {
        std::optional<cart_sram> info;  // nullopt when the cart declares no SRAM
        std::vector<std::uint8_t> data; // backing bytes (info->byte_count() of them)
        bool enabled{true};             // $A130F1 bit 0; powers on accessible
    };

    // A fully wired Genesis built through the manifest path. Member ORDER is
    // load-bearing: `graph` is declared last so it destructs FIRST -- the chips
    // stop ticking (and stop dereferencing `state` / borrowing `rom`) before the
    // state and ROM they point at are torn down.
    struct genesis_runtime final {
        std::vector<std::uint8_t> rom; // cartridge feed for the rom_provider
        genesis_callbacks_state state; // chip/bus pointers + non-chip glue state
        // Scheduler-view wrappers for the gated CPUs (the gated_chip build_system
        // produced); set by build_genesis_runtime. Ungated chips schedule as
        // themselves via `state`.
        chips::ichip* cpu_sched{};
        chips::ichip* z80_sched{};
        cart_sram_runtime sram; // battery SRAM (borrowed by graph's bus handlers)
        system_graph graph;     // owns chips/buses/memory; destructs first

        [[nodiscard]] chips::cpu::m68000* cpu() const noexcept { return state.cpu; }
        [[nodiscard]] chips::cpu::z80* z80() const noexcept { return state.z80; }
        [[nodiscard]] chips::video::genesis_vdp* vdp() const noexcept { return state.vdp; }
        [[nodiscard]] chips::audio::ym2612* fm() const noexcept { return state.fm; }
        [[nodiscard]] chips::audio::sn76489* psg() const noexcept { return state.psg; }

        [[nodiscard]] peripheral::device* port_device(int port) noexcept {
            return state.port_device(port);
        }
        void set_reset_button(bool pressed) noexcept { state.set_reset_button(pressed); }

        // Scheduler view in the canonical order/weights (VDP first; the 68K and
        // Z80 are their gated wrappers). Mirrors the genesis_adapter schedule.
        [[nodiscard]] std::array<scheduled_entry, 5> schedule() const noexcept {
            return {{{state.vdp, 1U},
                     {cpu_sched, 7U},
                     {z80_sched, 15U},
                     {state.fm, 7U},
                     {state.psg, 15U}}};
        }
    };

    // Build a runnable Genesis from a cartridge image (moved in) via the manifest
    // path. Equivalent to assemble_genesis by construction -- verified
    // byte-for-byte in genesis_manifest_parity_test.
    [[nodiscard]] std::unique_ptr<genesis_runtime>
    build_genesis_runtime(std::vector<std::uint8_t> rom, const genesis_config& config = {});

} // namespace mnemos::manifests::genesis
