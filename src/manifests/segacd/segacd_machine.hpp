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

        // Comm poll-sync. begin_comm_slice() baselines both CPUs at the start of a
        // main interleave slice; catch_up_sub() runs the sub-CPU up to the main's
        // current cycle. The bridge calls catch_up_sub() before every main write to
        // a comm register ($0E/$0F/$10-$1F); the adapter calls it at each slice end.
        // So the sub observes the main's intermediate flag pulses instead of only the
        // settled value (the reference runs the other CPU up to the writer's cycle
        // before committing a comm-register write; without it a transient main flag
        // pulse is invisible to the sub and the boot comm handshake can deadlock).
        void begin_comm_slice() noexcept;
        void catch_up_sub();

        // Machine-level pacing anchors (review F3): the comm-slice baselines and
        // the fractional sub-cycle carry the 87.5/53.69 MHz ratio leaves between
        // slices. These live outside the sub-board, so a save must capture them or
        // the sub-CPU resumes at a drifted phase.
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);

      private:
        std::uint64_t slice_base_main_ = 0; // main 68k cycles at the slice baseline
        std::uint64_t slice_base_sub_ = 0;  // sub 68k cycles at the slice baseline
        std::uint64_t sub_cycle_carry_ = 0; // carried fractional sub-cycle remainder
    };

    // Assemble a Sega CD machine from its BIOS ROM. The Genesis boots the BIOS as
    // its cartridge; the sub-CPU boots from PRG-RAM vectors the main BIOS loads
    // there. The caller attaches a disc via machine->sub->attach_disc().
    [[nodiscard]] std::unique_ptr<segacd_machine>
    assemble_segacd_machine(std::vector<std::uint8_t> bios,
                            const genesis::genesis_config& config = {});

} // namespace mnemos::manifests::segacd
