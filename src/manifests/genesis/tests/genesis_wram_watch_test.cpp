// Diagnostic harness: trace 68K writes to a watched work-RAM address with the
// writing PC + frame, on the real (build_genesis_runtime) path. Used to find
// where a parity divergence is seeded.
//
// Data-gated. Set:
//   MNEMOS_GENESIS_ROM        cartridge image
//   MNEMOS_WRAM_WATCH_ADDR    24-bit 68K address to watch (hex, e.g. FFFFEE)
//   MNEMOS_WRAM_WATCH_FRAMES  frames to run (decimal, default 10)
// Each qualifying write logs "[wram] f=N pc=$XXXXXX [$YYYYYY]=ZZ" to stderr.
// SKIPs cleanly when MNEMOS_GENESIS_ROM is unset.

#include "genesis_callbacks.hpp"
#include "genesis_runtime.hpp"

#include "bus.hpp"
#include "scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {
    using mnemos::manifests::genesis::build_genesis_runtime;
    using mnemos::manifests::genesis::genesis_config;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    std::optional<std::string> env(const char* n) {
        const char* v = std::getenv(n);
        if (v == nullptr || v[0] == '\0') {
            return std::nullopt;
        }
        return std::string(v);
    }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace

TEST_CASE("genesis WRAM write watch (diagnostic)", "[genesis][diag]") {
    const auto rom_path = env("MNEMOS_GENESIS_ROM");
    if (!rom_path) {
        SKIP("set MNEMOS_GENESIS_ROM (+ MNEMOS_WRAM_WATCH_ADDR) to trace WRAM writes");
    }
    std::ifstream in(*rom_path, std::ios::binary);
    REQUIRE(in);
    std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>{});
    REQUIRE_FALSE(rom.empty());

    const std::uint32_t watch_addr = env("MNEMOS_WRAM_WATCH_ADDR")
                                         ? static_cast<std::uint32_t>(std::strtoul(
                                               env("MNEMOS_WRAM_WATCH_ADDR")->c_str(), nullptr, 16))
                                         : 0xFFFFEEU;
    const int frames =
        env("MNEMOS_WRAM_WATCH_FRAMES") ? std::atoi(env("MNEMOS_WRAM_WATCH_FRAMES")->c_str()) : 10;

    auto rt = build_genesis_runtime(rom, genesis_config{});
    auto* cpu = rt->cpu();

    // Install a bus access observer on the 68K main bus. It fires after every
    // completed access; we log writes to the watched address with the CPU PC.
    rt->state.main_bus->set_access_observer(
        [cpu, watch_addr](const mnemos::topology::access_event& ev) {
            // Watch a 2-byte window [addr, addr+1] so word writes are caught.
            const std::uint32_t a = ev.address & 0xFFFFFFU;
            const std::uint32_t w = watch_addr & 0xFFFFFFU;
            if (ev.write && (a == w || a == w + 1U)) {
                // The instruction-start PC (the actual writer), matching the
                // reference runner's last-executed-PC semantic -- NOT the
                // already-advanced cpu_registers().pc.
                const auto pc = cpu->current_instruction_addr();
                std::fprintf(stderr, "[wram] pc=$%06X [$%06X]=%02X\n",
                             static_cast<unsigned>(pc & 0xFFFFFFU), static_cast<unsigned>(a),
                             static_cast<unsigned>(ev.value));
            }
        });

    // Optional PC-window SR/IRQ trace: MNEMOS_SR_TRACE_LO/HI (hex PCs) logs each
    // instruction whose PC is in [LO,HI] as "[sr] pc=$X sr=$Y ipm=N irq=L" so the
    // interrupt-mask + pending-level can be compared at a divergence point.
    const bool sr_trace = env("MNEMOS_SR_TRACE_LO").has_value();
    const std::uint32_t sr_lo =
        sr_trace ? static_cast<std::uint32_t>(std::strtoul(env("MNEMOS_SR_TRACE_LO")->c_str(),
                                                           nullptr, 16))
                 : 0;
    const std::uint32_t sr_hi =
        env("MNEMOS_SR_TRACE_HI")
            ? static_cast<std::uint32_t>(std::strtoul(env("MNEMOS_SR_TRACE_HI")->c_str(), nullptr,
                                                      16))
            : sr_lo + 0x40U;
    static long long sr_cb_count = 0;
    if (sr_trace) {
        cpu->diagnostics().set_trace_callback([cpu, sr_lo, sr_hi](std::uint32_t pc) {
            ++sr_cb_count;
            const std::uint32_t p = pc & 0xFFFFFFU;
            if (p >= sr_lo && p <= sr_hi) {
                const auto regs = cpu->cpu_registers();
                std::fprintf(stderr, "[sr] pc=$%06X sr=$%04X ipm=%u irq=%d resample=%d\n",
                             static_cast<unsigned>(p), static_cast<unsigned>(regs.sr),
                             static_cast<unsigned>((regs.sr >> 8U) & 7U), cpu->irq_level(),
                             cpu->irq_resample() ? 1 : 0);
            }
        });
    }

    // Optional refresh-fire trace: MNEMOS_REFRESH_TRACE=1 logs every instruction
    // as "[ref] pc=$XXXXXX r=N cyc=M" where r is whether a DRAM bus-refresh fired
    // on it. cycle_sources is populated at the END of an instruction, so the
    // trace callback (fired at the START of the NEXT instruction) reports the
    // PREVIOUS instruction's flag -- we buffer one PC to align them.
    const bool refresh_trace = env("MNEMOS_REFRESH_TRACE").has_value();
    struct ref_state {
        mnemos::chips::cpu::m68000* cpu;
        std::uint32_t prev_pc;
        bool have_prev;
    } rs{cpu, 0, false};
    if (refresh_trace) {
        cpu->diagnostics().set_trace_callback([&rs](std::uint32_t pc) {
            if (rs.have_prev) {
                const auto& cs = rs.cpu->diagnostics().last_cycle_sources();
                std::fprintf(stderr, "[ref] pc=$%06X r=%u cyc=%llu\n",
                             static_cast<unsigned>(rs.prev_pc & 0xFFFFFFU),
                             static_cast<unsigned>(cs.refresh_fired),
                             static_cast<unsigned long long>(rs.cpu->elapsed_cycles()));
            }
            rs.prev_pc = pc;
            rs.have_prev = true;
        });
    }

    std::vector<mnemos::runtime::scheduled_chip> chips;
    for (const auto& e : rt->schedule()) {
        chips.push_back({e.chip, e.weight});
    }
    mnemos::runtime::scheduler sched(std::move(chips), rt->vdp());
    // 1-based frame labels to match the reference runner's g_trace_frame_num
    // (= retro_run call count, i+1), so frame numbers compare directly.
    for (int i = 0; i < frames; ++i) {
        std::fprintf(stderr, "=== frame %d ===\n", i + 1);
        sched.run_frame();
    }
    if (sr_trace) {
        std::fprintf(stderr, "[sr] trace callback fired %lld times total\n", sr_cb_count);
    }
    SUCCEED();
}
