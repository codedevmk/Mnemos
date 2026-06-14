// Sega CD save-state: sub-board determinism + save/load transparency, with emphasis
// on the host-side pacing anchors (segacd_system::sub_elapsed_base and the machine's
// sub_cycle_carry_) that live OUTSIDE the chip set (review F3). The sub-board is
// driven in isolation through segacd_system::run_cycles; no disc and no real BIOS are
// required (a synthetic BIOS boots the machine and the sub-CPU runs from reset).

#include "segacd_machine.hpp" // assemble_segacd_machine, segacd_machine, segacd_system

#include "chip.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace {

    using mnemos::manifests::segacd::assemble_segacd_machine;
    using mnemos::manifests::segacd::segacd_machine;
    using mnemos::manifests::segacd::segacd_system;

    std::vector<std::uint8_t> make_bios() {
        std::vector<std::uint8_t> bios(0x20000U, 0U); // 128 KiB
        bios[1] = 0xFFU;                              // SSP = $00FF0000
        bios[6] = 0x02U;                              // PC  = $00000200
        return bios;
    }

    std::unique_ptr<segacd_machine> booted() {
        std::unique_ptr<segacd_machine> m = assemble_segacd_machine(make_bios());
        m->sub->release_sub_reset(); // run the sub-CPU from its PRG-RAM vectors
        return m;
    }

    std::vector<std::uint8_t> snapshot(const segacd_system& sub) {
        std::vector<std::uint8_t> buffer;
        mnemos::chips::state_writer writer(buffer);
        sub.save_state(writer);
        return buffer;
    }

} // namespace

TEST_CASE("Sega CD sub-board save-state is deterministic across independent runs",
          "[segacd][save_state]") {
    const std::unique_ptr<segacd_machine> a = booted();
    const std::unique_ptr<segacd_machine> b = booted();
    a->sub->run_cycles(60000U);
    b->sub->run_cycles(60000U);

    const std::vector<std::uint8_t> sa = snapshot(*a->sub);
    REQUIRE(!sa.empty());
    CHECK(sa == snapshot(*b->sub));
}

TEST_CASE("Sega CD sub-board save/load round-trips the host-side pacing base",
          "[segacd][save_state]") {
    const std::unique_ptr<segacd_machine> m = booted();
    segacd_system& sub = *m->sub;

    sub.run_cycles(40000U);
    // A reset edge folds the elapsed counter into sub_elapsed_base, so the save
    // exercises a NON-ZERO monotone base.
    sub.assert_sub_reset();
    sub.release_sub_reset();
    sub.run_cycles(12000U);

    const std::vector<std::uint8_t> save = snapshot(sub);

    sub.run_cycles(40000U);
    const std::vector<std::uint8_t> reference = snapshot(sub);
    REQUIRE(reference != save); // the board genuinely advanced (not vacuous)

    {
        mnemos::chips::state_reader reader(save);
        sub.load_state(reader);
        REQUIRE(reader.ok());
    }
    CHECK(snapshot(sub) == save); // load is exact (idempotent)

    sub.run_cycles(40000U);
    CHECK(snapshot(sub) == reference); // transparency -> sub_elapsed_base round-tripped
}

TEST_CASE("Sega CD machine pacing anchors round-trip", "[segacd][save_state]") {
    const std::unique_ptr<segacd_machine> m = booted();

    // A comm slice + sub catch-up advances the fractional 87.5/53.69 MHz carry and
    // the slice baselines -- the machine-level anchors that are NOT in the sub-board.
    m->begin_comm_slice();
    m->genesis->cpu.tick(5000U);
    m->catch_up_sub();

    std::vector<std::uint8_t> first;
    {
        mnemos::chips::state_writer writer(first);
        m->save_state(writer);
    }

    // Perturb the live anchors, then restore the saved ones.
    m->begin_comm_slice();
    m->genesis->cpu.tick(3000U);
    m->catch_up_sub();
    {
        mnemos::chips::state_reader reader(first);
        m->load_state(reader);
        REQUIRE(reader.ok());
    }

    std::vector<std::uint8_t> second;
    {
        mnemos::chips::state_writer writer(second);
        m->save_state(writer);
    }
    CHECK(first == second); // anchors restored exactly
}
