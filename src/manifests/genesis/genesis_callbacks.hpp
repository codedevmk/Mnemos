#pragma once

// Host-side glue for the Genesis manifests (genesis.ntsc.toml / genesis.pal.toml).
//
// Builds the named callback / predicate / mmio-factory tables those manifests
// reference, capturing closures over a caller-owned `genesis_callbacks_state`
// that must outlive the constructed system. Same chicken-and-egg resolution as
// the SMS bundle: closures capture `&state` (a stable address); the caller
// populates the chip + bus pointers from the constructed system_graph AFTER
// build_system returns, before the system ticks.
//
// Genesis needs all three host tables (the SMS bundle needed only callbacks +
// mmio_factories):
//   - callback_table     -- 68K irq_ack / tas-drop, VDP dma_read / irq /
//                           delayed_irq / vblank
//   - predicate_table    -- z80_running + cpu_runnable chip gates ([[gate]])
//   - mmio_factory_table -- the 11 stateful MMIO windows across both buses
//
// All wiring is lifted intact from assemble_genesis so behavioural parity is by
// construction (verified in genesis_manifest_parity_test).

#include "callbacks.hpp"
#include "mmio_factory.hpp"
#include "predicates.hpp"

#include "bus.hpp" // topology::bus (dma_read + banked window touch the buses)
#include "genesis_vdp.hpp"
#include "m68000.hpp"
#include "peripheral.hpp"
#include "sn76489.hpp"
#include "ym2612.hpp"
#include "z80.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace mnemos::manifests::genesis {

    // Host state the Genesis manifest's callbacks / predicates / mmio_factories
    // close over. Mirrors the fields assemble_genesis kept on `genesis_system`
    // that aren't owned by a chip. The caller fills the chip + bus pointers
    // (and `version_register`) from the constructed system_graph after
    // build_system returns.
    struct genesis_callbacks_state final {
        // Filled by the caller from system_graph after build_system.
        chips::cpu::m68000* cpu{};
        chips::cpu::z80* z80{};
        chips::video::genesis_vdp* vdp{};
        chips::audio::ym2612* fm{};
        chips::audio::sn76489* psg{};
        topology::bus* main_bus{}; // dma_read source + banked-window target
        topology::bus* z80_bus{};  // (kept for symmetry / future use)

        // Non-chip architectural state (lifted from genesis_system).
        std::array<std::uint8_t, 0x2000> z80_ram{}; // 8 KiB, shared by both buses
        std::array<std::uint8_t, 0x20> io_regs{};   // $A10000-$A1001F register file
        std::uint8_t version_register{};            // $A10001 region/version (set by caller)

        // 16-bit coalescing latches for the VDP ports (68K word access splits
        // into a high-byte-even + low-byte-odd pair).
        std::uint8_t vdp_write_high{};
        std::uint8_t vdp_read_low{};

        // Z80 sound-CPU arbitration. The 68K holds the Z80 via RESET ($A11200)
        // and BUSREQ ($A11100); z80_bank is the 9-bit window base for $8000-$FFFF.
        bool z80_bus_requested{};
        bool z80_reset_released{};
        bool z80_running{};
        std::uint16_t z80_bank{};

        // V-blank-entry counter (per-frame device hooks; not architectural).
        std::uint64_t frame_index{};

        // Controller ports 1/2 (default-plugged with MK-1653 6-button pads by
        // the caller). The $A10003/$A10005 data ports route through these.
        std::array<std::unique_ptr<peripheral::device>, 2> ports{};

        [[nodiscard]] peripheral::device* port_device(int port) noexcept {
            return (port >= 0 && port < 2) ? ports[static_cast<std::size_t>(port)].get() : nullptr;
        }

        void set_reset_button(bool /*pressed*/) noexcept {
            // Genesis reset is a 68K line, not a controller-port bit; no-op for
            // parity with the assemble_genesis surface (kept for adapter symmetry).
        }
    };

    struct genesis_host_tables final {
        chips::callback_table callbacks;
        chips::predicate_table predicates;
        chips::mmio_factory_table mmio_factories;
    };

    // Build the host tables the Genesis manifests reference. `state` is captured
    // by pointer in every closure and MUST outlive the returned tables and any
    // system built with them.
    [[nodiscard]] genesis_host_tables make_genesis_host_tables(genesis_callbacks_state& state);

} // namespace mnemos::manifests::genesis
