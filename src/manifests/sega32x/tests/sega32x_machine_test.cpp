// Phase B8: the Genesis 68000 reaches the 32X board through its own bus -- the
// adapter-control register at $A15100 drives the SH-2 /RES line, and the
// machine's scheduler runs the two SH-2s at 3x the 68000 clock. Built additively
// on assemble_genesis (a plain Genesis never maps any of this).

#include "sega32x_machine.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using mnemos::manifests::sega32x::assemble_sega32x_machine;

    // A minimal Genesis cartridge with valid 68000 reset vectors and a self-branch
    // loop at the entry point, so the 68000 advances deterministically when ticked.
    std::vector<std::uint8_t> make_cart() {
        std::vector<std::uint8_t> cart(0x10000, 0);
        cart[0] = 0x00;
        cart[1] = 0xFF;
        cart[2] = 0x00;
        cart[3] = 0x00; // SSP = $00FF0000
        cart[4] = 0x00;
        cart[5] = 0x00;
        cart[6] = 0x02;
        cart[7] = 0x00;     // PC  = $00000200
        cart[0x200] = 0x60; // BRA.B *  (0x60FE: branch to self -- a tight idle loop)
        cart[0x201] = 0xFE;
        return cart;
    }

} // namespace

TEST_CASE("sega32x_machine boots the cartridge and holds the SH-2s in reset",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    REQUIRE(m->genesis != nullptr);
    REQUIRE(m->thirtytwox != nullptr);
    auto& bus = m->genesis->bus;

    // The cartridge is visible at $000000 on the Genesis bus (reset vectors).
    REQUIRE(bus.read8(0x000001U) == 0xFF);
    REQUIRE(bus.read8(0x000006U) == 0x02);

    // The SH-2s power on held in reset and have executed nothing.
    REQUIRE(m->thirtytwox->sh2_reset_asserted);
    REQUIRE(m->thirtytwox->master_cpu.elapsed_cycles() == 0U);
    REQUIRE(m->thirtytwox->slave_cpu.elapsed_cycles() == 0U);

    // While held, the scheduler does not advance them even as the 68000 runs.
    m->begin_slice();
    m->genesis->cpu.tick(2000U);
    m->catch_up_sh2();
    CHECK(m->thirtytwox->master_cpu.elapsed_cycles() == 0U);
}

TEST_CASE("sega32x_machine $A15100 ADEN+RES release starts and parks the SH-2s",
          "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;

    // Low byte of the adapter-control word: ADEN (bit 1) + RES-release (bit 0).
    bus.write8(0xA15101U, 0x03U);
    CHECK_FALSE(m->thirtytwox->sh2_reset_asserted); // released
    // Read-back: RES (bit 0) is driven not stored, so it reads 0; ADEN stays set.
    CHECK(bus.read8(0xA15101U) == 0x02U);

    // Clearing RES (bit 0 = 0) parks the SH-2s again regardless of ADEN.
    bus.write8(0xA15101U, 0x00U);
    CHECK(m->thirtytwox->sh2_reset_asserted);
}

TEST_CASE("sega32x_machine runs the SH-2s at 3x the 68000 after release", "[sega32x][machine]") {
    auto m = assemble_sega32x_machine(make_cart());
    auto& bus = m->genesis->bus;
    bus.write8(0xA15101U, 0x03U); // ADEN + release RES

    m->begin_slice();
    m->genesis->cpu.tick(3000U);
    const std::uint64_t main_delta = m->genesis->cpu.elapsed_cycles();
    m->catch_up_sh2();

    const std::uint64_t sh2 = m->thirtytwox->master_cpu.elapsed_cycles();
    // The SH-2s tick at 3x the 68000; instruction-atomic stepping overshoots the
    // target by at most one instruction, never undershoots.
    CHECK(sh2 >= main_delta * 3U);
    CHECK(sh2 < main_delta * 3U + 64U);
}
