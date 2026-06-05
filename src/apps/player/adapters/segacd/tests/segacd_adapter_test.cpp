// Phase D2: the segacd_adapter drives a full Sega CD frame -- the Genesis
// scheduler runs the main 68000 (which boots the BIOS), and the per-frame
// accumulator runs the sub-CPU + CD frames. A synthetic BIOS marks work RAM and
// releases the sub-CPU so we can see both halves advance.

#include "segacd_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace {

    using mnemos::apps::player::adapters::segacd::segacd_adapter;

    // 128 KB BIOS. Reset SSP=$00FF0000, PC=$000200. The main program writes a
    // $BEEF marker to work RAM, releases the sub-CPU (gate $A12001 bit0), and
    // spins.
    std::vector<std::uint8_t> make_bios() {
        std::vector<std::uint8_t> bios(0x20000, 0);
        bios[0] = 0x00;
        bios[1] = 0xFF;
        bios[2] = 0x00;
        bios[3] = 0x00; // SSP = $00FF0000
        bios[4] = 0x00;
        bios[5] = 0x00;
        bios[6] = 0x02;
        bios[7] = 0x00; // PC  = $000200

        std::size_t p = 0x200;
        const auto put = [&](std::initializer_list<std::uint8_t> bytes) {
            for (const std::uint8_t b : bytes) {
                bios[p++] = b;
            }
        };
        put({0x33, 0xFC, 0xBE, 0xEF, 0x00, 0xFF, 0x00, 0x00}); // MOVE.W #$BEEF,($00FF0000).L
        put({0x13, 0xFC, 0x00, 0x01, 0x00, 0xA1, 0x20, 0x01}); // MOVE.B #$01,($00A12001).L
        put({0x60, 0xFE});                                     // BRA *
        return bios;
    }

} // namespace

TEST_CASE("segacd_adapter boots the BIOS and runs both CPUs", "[segacd][adapter]") {
    segacd_adapter adapter(make_bios());

    for (int i = 0; i < 3; ++i) {
        adapter.step_one_frame();
    }
    REQUIRE(adapter.frames_stepped() == 3U);

    // The main 68000 executed the BIOS program (marker landed in work RAM).
    REQUIRE(adapter.machine().genesis->work_ram[0] == 0xBE);
    REQUIRE(adapter.machine().genesis->work_ram[1] == 0xEF);

    // The BIOS released the sub-CPU, which the adapter then ran.
    REQUIRE(adapter.machine().sub->sub_cpu.elapsed_cycles() > 0U);

    // The adapter presents a framebuffer + a sane refresh rate.
    REQUIRE(adapter.region().frames_per_second_x1000 > 0U);
}

TEST_CASE("segacd_adapter holds the sub-CPU until the BIOS releases it", "[segacd][adapter]") {
    // A BIOS that never touches gate $01: the sub-CPU must stay halted.
    std::vector<std::uint8_t> bios(0x20000, 0);
    bios[1] = 0xFF;     // SSP = $00FF0000
    bios[6] = 0x02;     // PC  = $000200
    bios[0x200] = 0x60; // BRA *
    bios[0x201] = 0xFE;

    segacd_adapter adapter(std::move(bios));
    adapter.step_one_frame();
    adapter.step_one_frame();
    REQUIRE(adapter.machine().sub->sub_cpu.elapsed_cycles() == 0U); // never released
}
