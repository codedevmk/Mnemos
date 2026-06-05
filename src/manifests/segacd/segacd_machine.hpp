#pragma once

// A complete Sega CD machine: a Genesis (booting the Sega CD BIOS as its
// cartridge) plus the segacd_system sub side, joined by the main-side gate-array
// bridge and the PRG/word-RAM windows. Built ADDITIVELY on top of the unchanged
// assemble_genesis -- a plain Genesis cart never goes through here, so the
// Genesis byte-parity floor is structurally untouched.

#include "genesis_system.hpp" // genesis_system + genesis_config + assemble_genesis
#include "segacd_system.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace mnemos::manifests::segacd {

    struct segacd_machine final {
        std::unique_ptr<genesis::genesis_system> genesis; // main side (boots the BIOS)
        std::unique_ptr<segacd_system> sub;               // sub side (CD hardware)
    };

    // Assemble a Sega CD machine from its BIOS ROM. The Genesis boots the BIOS
    // as its cartridge; the same image overlays the sub-CPU $0 vectors. The
    // caller attaches a disc via machine->sub->attach_disc().
    [[nodiscard]] std::unique_ptr<segacd_machine>
    assemble_segacd_machine(std::vector<std::uint8_t> bios,
                            const genesis::genesis_config& config = {});

} // namespace mnemos::manifests::segacd
