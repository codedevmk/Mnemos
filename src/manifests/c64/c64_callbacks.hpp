#pragma once

// Host-side glue for the C64 manifests (c64.ntsc.toml / c64.pal.toml).
//
// The C64's banking is non-declarative: the PLA reads the live 6510 $01 port
// (LORAM/HIRAM/CHAREN) plus the cartridge /GAME and /EXROM lines on every access
// to decide which region (RAM / BASIC / KERNAL / CHARGEN / I/O) is visible. This
// bundle builds the overlay-predicate table the manifest's region/chip
// active_predicate fields name ("c64.bank.basic/kernal/chargen/io"), each
// closing over a caller-owned state whose chip pointers are filled from the
// constructed system_graph after build_system returns.

#include "predicates.hpp" // chips::overlay_predicate_table

#include "c64_cartridge.hpp"
#include "c64_pla.hpp"
#include "m6510.hpp"

namespace mnemos::manifests::c64 {

    // The pointers the PLA-decode predicates close over. Filled by the caller
    // (c64_runtime) from system_graph + the host cartridge after build_system.
    struct c64_callbacks_state final {
        chips::cpu::m6510* cpu{};
        chips::mapper::c64_pla* pla{};
        chips::mapper::c64_cartridge* cart{};
    };

    // Build the "c64.bank.*" overlay predicates: each runs the PLA decode for the
    // accessed address against the live CPU port + cartridge lines and reports
    // whether its region is the selected one (ROM overlays shadow reads only).
    [[nodiscard]] chips::overlay_predicate_table
    make_c64_overlay_predicates(c64_callbacks_state& state);

} // namespace mnemos::manifests::c64
