#pragma once

// Manifest-path counterpart of c64_system / assemble_c64.
//
// assemble_c64 hand-wires a C64 into a value-member struct and stays the
// byte-for-byte parity oracle. build_c64_runtime produces the SAME machine
// through build_system: the six chips + RAM + ROM overlays + colour RAM + the
// VIC/SID/CIA MMIO windows come from the manifest (with the c64.bank.* PLA
// predicates doing the dynamic banking), and this wires everything that isn't
// declarative -- the VIC memory attach (via region_span), IRQ/NMI, the CIA
// keyboard/joystick/IEC/RS-232 callbacks, the datasette, the cartridge/REU/open-
// I/O windows, and the post-reset state -- lifted intact from assemble_c64.

#include "builder.hpp"       // system_graph
#include "c64_callbacks.hpp" // c64_callbacks_state
#include "c64_system.hpp"    // c64_config + the peripheral chip headers

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace mnemos::manifests::c64 {

    // One scheduler slot (ichip* keeps this within the manifests tier, below
    // runtime; the test/CLI maps it to runtime::scheduled_chip).
    struct c64_scheduled_entry final {
        chips::ichip* chip{};
        std::uint32_t weight{};
    };

    // A fully wired C64 built through the manifest path. Member ORDER is
    // load-bearing: `graph` (the chips) is declared LAST so it destructs first,
    // before the peripherals + state its callbacks close over.
    struct c64_runtime final {
        c64_callbacks_state state; // cpu/pla/cart pointers (PLA predicates close over this)

        // Non-chip peripherals, mirroring c64_system's members. The CIA/VIC/IRQ
        // callbacks capture these, so they must outlive the chips.
        chips::mapper::c64_cartridge cart; // empty expansion-port cart by default
        c64_input input;                   // keyboard matrix + joysticks
        chips::iec_bus iec;
        chips::storage::c1541::synthetic_drive drive8{8U};
        chips::storage::c1541::full_drive drive8_full{8U};
        chips::peripheral::reu reu_unit{chips::peripheral::reu::model::ram_128k};
        chips::peripheral::rs232 rs232_unit;
        chips::peripheral::modem modem_unit;
        chips::storage::datasette tape;
        chips::audio::sid_6581 sid2; // second SID at $D420, mapped only when dual_sid

        // Chip pointers from the constructed graph (the primary scheduled chips).
        chips::video::vic_ii_6569* vic{};
        chips::audio::sid_6581* sid{};
        chips::bus_controller::cia_6526* cia1{};
        chips::bus_controller::cia_6526* cia2{};

        system_graph graph; // owns the six chips + RAM/ROM buffers; destructs first

        [[nodiscard]] chips::cpu::m6510* cpu() const noexcept { return state.cpu; }
        [[nodiscard]] chips::video::vic_ii_6569* video() const noexcept { return vic; }

        // Scheduler view: VIC first, then CPU, CIAs, SID, drive, tape (all phi2),
        // matching the c64 golden-boot order.
        [[nodiscard]] std::array<c64_scheduled_entry, 7> schedule() noexcept {
            return {{{vic, 1U},
                     {state.cpu, 1U},
                     {cia1, 1U},
                     {cia2, 1U},
                     {sid, 1U},
                     {&drive8, 1U},
                     {&tape, 1U}}};
        }
    };

    // Build a runnable C64 from the three ROM images (moved in) via the manifest
    // path. Equivalent to assemble_c64 by construction -- verified byte-for-byte
    // in c64_manifest_parity_test.
    [[nodiscard]] std::unique_ptr<c64_runtime>
    build_c64_runtime(std::vector<std::uint8_t> basic_rom, std::vector<std::uint8_t> kernal_rom,
                      std::vector<std::uint8_t> chargen_rom, const c64_config& config = {});

} // namespace mnemos::manifests::c64
