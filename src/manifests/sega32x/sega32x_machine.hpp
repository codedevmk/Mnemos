#pragma once

// A Sega 32X machine: a Genesis (booting the 32X cartridge) plus the
// sega32x_system (two SH-2s + the 32X memory and interrupt sources), joined by
// the 68000-side $A15xxx bridge. Built ADDITIVELY on the unchanged
// assemble_genesis -- a plain Genesis cart never traverses any 32X path, so the
// Genesis byte-parity floor is structurally untouched.

#include "genesis_system.hpp" // genesis_system + genesis_config + assemble_genesis
#include "sega32x_system.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace mnemos::manifests::sega32x {

    struct sega32x_machine final {
        std::unique_ptr<genesis::genesis_system> genesis; // main side (the 68000)
        std::unique_ptr<sega32x_system> sega32x;          // the 32X board (two SH-2s)

        // SH-2 : 68000 clock ratio. Both derive from the same NTSC master clock --
        // the 68000 runs at master/7, each SH-2 at master*3/7 -- so the SH-2s tick
        // at exactly 3x the 68000. The integer ratio means no fractional carry is
        // needed (unlike the Sega CD sub-CPU's 87.5/53.693175).
        static constexpr std::uint64_t sh2_clock_multiplier = 3U;

        // Whether a G BIOS image was supplied: the ADEN 0->1 edge then overlays
        // its vectors at $000000 (no image: the cart vectors stay, as the
        // BIOS-less unit tests expect).
        bool g_bios_present = false;

        // catch_up_sh2() advances both SH-2s up to the 68000's position * 3,
        // against the same cumulative anchors the worker schedule uses (so the
        // synchronous and threaded schedules fence at identical SH-2 cycle
        // positions). Consumes the delta without running while the SH-2s are
        // held in reset.
        void catch_up_sh2();
        // 68000-side /RES write path: parks any threaded SH-2 batch, applies
        // the hardware reset edge, then rebases the shared schedule anchors.
        // Distinct from sega32x_system::set_sh2_reset (which this wraps): callers
        // holding a machine must use this so the worker is parked first.
        void apply_sh2_reset(bool asserted);

        ~sega32x_machine(); // parks and joins the SH-2 worker

        // ---- SH-2 worker: a depth-1 scanline pipeline ----
        // The worker runs the SH-2 batch for line N while the main thread
        // emulates Genesis line N+1. Emulated-state equivalence with the
        // synchronous schedule holds because every 68000 access the SH-2s can
        // observe (the $A15xxx windows, the FB windows, the V-blank/H-int
        // wrappers) joins the in-flight batch first, and both schedules use the
        // same cumulative cycle targets -- between fences the SH-2s see exactly
        // the state the synchronous schedule would have shown them. Caveat: the
        // adapter composes line N's 32X overlay only after Genesis line N+1 has
        // run (the join point), so 68000-side writes to the 32X VDP registers /
        // palette during line N+1 are visible in line N's composed pixels --
        // a one-line presentation skew, not an emulated-state divergence.
        void start_sh2_worker();
        [[nodiscard]] bool sh2_worker_running() const noexcept { return worker_.joinable(); }
        // Wait for the published batch to complete (no-op when idle / no worker).
        void join_sh2() noexcept;
        // Publish the next batch: everything the 68000 advanced since the last
        // schedule, times the clock ratio. Joins the previous batch first.
        void schedule_sh2_catch_up();
        // The exact-interlock fence (COMM writes, DREQ-full): join, then bring
        // the SH-2s synchronously to the 68000's current position.
        void fence_sh2();

      private:
        void worker_main();

        std::thread worker_{};
        std::atomic<std::uint64_t> sh2_target_{0}; // published batch end (master SH-2 cycles)
        std::atomic<std::uint64_t> sh2_done_{0};   // worker progress against the target
        std::atomic<bool> worker_quit_{false};
        std::uint64_t sched_target_ = 0;    // cumulative SH-2 cycle target (main thread)
        std::uint64_t sched_main_base_ = 0; // 68000 elapsed at the last schedule
    };

    // The three 32X boot ROM images. m_bios/s_bios seed the SH-2s' reset
    // vectors; g_bios overlays the 68000's $000000-$0000FF vectors so the boot
    // sequence runs the adapter's security/handshake code before the cart.
    // Any image may be empty: the matching window then stays on the cart (the
    // unit tests run BIOS-less).
    struct sega32x_bios final {
        std::vector<std::uint8_t> m_bios;
        std::vector<std::uint8_t> s_bios;
        std::vector<std::uint8_t> g_bios;
    };

    // Assemble a 32X machine from its cartridge image. The Genesis boots it as the
    // cartridge; the two SH-2s stay held in reset until the 68000 sets ADEN and
    // releases RES through the adapter-control register at $A15100.
    [[nodiscard]] std::unique_ptr<sega32x_machine>
    assemble_sega32x_machine(std::vector<std::uint8_t> cart, const sega32x_bios& bios = {},
                             const genesis::genesis_config& config = {});

} // namespace mnemos::manifests::sega32x
