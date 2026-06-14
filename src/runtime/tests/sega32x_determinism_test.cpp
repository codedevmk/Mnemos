// Threaded-vs-synchronous determinism for the Sega 32X (ARCH-004 / INV-TIM-002).
//
// The 32X paces its two SH-2s either synchronously (catch_up_sh2 after each main
// slice) or on a depth-1 worker thread. The worker is a wall-clock optimisation
// that must NOT change emulated state. This test pins the synchronous path as the
// canonical determinism reference and asserts the threaded path reproduces it
// byte-for-byte. No ROM is required: a synthetic cart boots the machine BIOS-less
// and the SH-2s run from reset, which is enough to exercise the fence discipline.
//
// The fingerprint covers the core emulated state -- the whole Genesis, both SH-2s,
// the 32X VDP, SDRAM, and the framebuffer. It deliberately excludes the PWM
// DC-blocker (float; a separate audio-determinism concern, review F8).

#include "scheduler.hpp"       // runtime::scheduler
#include "sega32x_machine.hpp" // assemble_sega32x_machine, sega32x_machine, sega32x_system

#include "chip.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace {

    using namespace mnemos;
    namespace s32x = manifests::sega32x;

    // Minimal Genesis cart: valid 68000 reset vectors + a branch-to-self idle loop,
    // so the 68000 advances deterministically when ticked (mirrors the machine test).
    std::vector<std::uint8_t> make_cart() {
        std::vector<std::uint8_t> cart(0x10000U, 0U);
        cart[1] = 0xFFU;     // SSP = $00FF0000
        cart[6] = 0x02U;     // PC  = $00000200
        cart[0x200] = 0x60U; // BRA.B *  (idle loop)
        cart[0x201] = 0xFEU;
        return cart;
    }

    // The Genesis scheduler view the 32X adapter uses (VDP, gated 68000 + Z80, FM,
    // PSG). The SH-2s are NOT scheduler chips; the machine paces them per slice.
    std::vector<runtime::scheduled_chip> build_schedule(manifests::genesis::genesis_system& g) {
        return {
            {.chip = &g.vdp, .divider = 1U},       {.chip = &g.cpu_gate, .divider = 7U},
            {.chip = &g.z80_gate, .divider = 15U}, {.chip = &g.fm, .divider = 7U},
            {.chip = &g.psg, .divider = 15U},
        };
    }

    constexpr std::uint64_t k_slice_master_cycles = 3420U; // the adapter's per-scanline budget

    std::unique_ptr<s32x::sega32x_machine> booted_machine() {
        std::unique_ptr<s32x::sega32x_machine> m = s32x::assemble_sega32x_machine(make_cart());
        m->genesis->bus.write8(0xA15101U, 0x03U); // ADEN + release the SH-2 /RES line
        return m;
    }

    std::vector<std::uint8_t> fingerprint(s32x::sega32x_machine& m) {
        std::vector<std::uint8_t> buffer;
        chips::state_writer writer(buffer);
        m.genesis->save_state(writer);
        m.sega32x->master_cpu.save_state(writer);
        m.sega32x->slave_cpu.save_state(writer);
        m.sega32x->vdp.save_state(writer);
        writer.bytes(std::span<const std::uint8_t>(m.sega32x->sdram));
        writer.bytes(std::span<const std::uint8_t>(m.sega32x->framebuffer));
        return buffer;
    }

    void run_frames_synchronous(s32x::sega32x_machine& m, runtime::scheduler& sched, int frames) {
        for (int f = 0; f < frames; ++f) {
            const std::uint64_t start = sched.frame_index();
            while (sched.frame_index() == start) {
                sched.run_master_cycles(k_slice_master_cycles);
                m.catch_up_sh2();
            }
        }
    }

    void run_frames_threaded(s32x::sega32x_machine& m, runtime::scheduler& sched, int frames) {
        m.start_sh2_worker();
        for (int f = 0; f < frames; ++f) {
            const std::uint64_t start = sched.frame_index();
            while (sched.frame_index() == start) {
                sched.run_master_cycles(k_slice_master_cycles);
                m.join_sh2();
                m.schedule_sh2_catch_up();
            }
        }
        m.join_sh2();
    }

} // namespace

TEST_CASE("synchronous 32X advances bit-deterministically", "[sega32x][determinism]") {
    std::unique_ptr<s32x::sega32x_machine> a = booted_machine();
    std::unique_ptr<s32x::sega32x_machine> b = booted_machine();
    runtime::scheduler sa(build_schedule(*a->genesis), &a->genesis->vdp);
    runtime::scheduler sb(build_schedule(*b->genesis), &b->genesis->vdp);

    run_frames_synchronous(*a, sa, 3);
    run_frames_synchronous(*b, sb, 3);

    const std::vector<std::uint8_t> fa = fingerprint(*a);
    REQUIRE(!fa.empty());
    CHECK(fa == fingerprint(*b));
}

TEST_CASE("threaded 32X reproduces the synchronous reference", "[sega32x][determinism]") {
    std::unique_ptr<s32x::sega32x_machine> reference = booted_machine();
    std::unique_ptr<s32x::sega32x_machine> threaded = booted_machine();
    runtime::scheduler s_ref(build_schedule(*reference->genesis), &reference->genesis->vdp);
    runtime::scheduler s_thr(build_schedule(*threaded->genesis), &threaded->genesis->vdp);

    run_frames_synchronous(*reference, s_ref, 3);
    run_frames_threaded(*threaded, s_thr, 3);

    CHECK(fingerprint(*reference) == fingerprint(*threaded));
}
