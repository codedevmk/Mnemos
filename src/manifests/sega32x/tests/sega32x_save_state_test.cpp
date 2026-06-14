// 32X board save-state: determinism + save/load transparency, with emphasis on the
// host-side pacing anchors (sh2_elapsed_base + the per-resource bus-contention
// scoreboard) that live OUTSIDE the chip set (review B3/F3). The board is driven in
// isolation through sega32x_system::run_cycles so the test depends on nothing above
// tier 4; no ROM is required (a synthetic cart boots the machine BIOS-less and the
// SH-2s run from reset).

#include "sega32x_machine.hpp" // assemble_sega32x_machine, sega32x_system

#include "chip.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace {

using mnemos::manifests::sega32x::assemble_sega32x_machine;
using mnemos::manifests::sega32x::sega32x_machine;
using mnemos::manifests::sega32x::sega32x_system;

std::vector<std::uint8_t> make_cart() {
    std::vector<std::uint8_t> cart(0x10000U, 0U);
    cart[1] = 0xFFU;     // SSP = $00FF0000
    cart[6] = 0x02U;     // PC  = $00000200
    cart[0x200] = 0x60U; // BRA.B * (idle loop on the 68000 side)
    cart[0x201] = 0xFEU;
    return cart;
}

// The 32X board with both SH-2s released and bus-contention metering on, so the
// host-side pacing anchors are live during run_cycles.
std::unique_ptr<sega32x_machine> booted_board() {
    std::unique_ptr<sega32x_machine> m = assemble_sega32x_machine(make_cart());
    m->sega32x->set_sh2_reset(false);
    m->sega32x->set_bus_contention_metering(true);
    return m;
}

std::vector<std::uint8_t> snapshot(const sega32x_system& sys) {
    std::vector<std::uint8_t> buffer;
    mnemos::chips::state_writer writer(buffer);
    sys.save_state(writer);
    return buffer;
}

} // namespace

TEST_CASE("32X board save-state is deterministic across independent runs",
          "[sega32x][save_state]") {
    const std::unique_ptr<sega32x_machine> a = booted_board();
    const std::unique_ptr<sega32x_machine> b = booted_board();
    a->sega32x->run_cycles(60000U);
    b->sega32x->run_cycles(60000U);

    const std::vector<std::uint8_t> sa = snapshot(*a->sega32x);
    REQUIRE(!sa.empty());
    CHECK(sa == snapshot(*b->sega32x));
}

TEST_CASE("32X board save/load round-trips the host-side pacing anchors",
          "[sega32x][save_state]") {
    const std::unique_ptr<sega32x_machine> m = booted_board();
    sega32x_system& sys = *m->sega32x;

    sys.run_cycles(40000U);
    // A reset edge folds the elapsed counter into sh2_elapsed_base, so the save
    // exercises a NON-ZERO monotone base (not just the per-resource scoreboard).
    sys.set_sh2_reset(true);
    sys.set_sh2_reset(false);
    sys.run_cycles(12000U);

    const std::vector<std::uint8_t> save = snapshot(sys);

    // Reference: continue forward from the live board.
    sys.run_cycles(40000U);
    const std::vector<std::uint8_t> reference = snapshot(sys);
    REQUIRE(reference != save); // the board genuinely advanced (the test is not vacuous)

    // Restore the save and replay the same span. Reproducing `reference` exactly
    // requires sh2_elapsed_base AND resource_busy_until_ to have round-tripped --
    // drop either and the contention timing / inter-CPU phase would diverge.
    {
        mnemos::chips::state_reader reader(save);
        sys.load_state(reader);
        REQUIRE(reader.ok());
    }
    CHECK(snapshot(sys) == save); // load is exact (idempotent)

    sys.run_cycles(40000U);
    CHECK(snapshot(sys) == reference);
}
