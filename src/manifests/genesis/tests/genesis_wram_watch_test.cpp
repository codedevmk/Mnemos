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

    // Install a bus access observer on the 68K main bus. It fires after every
    // completed access; we log writes to the watched address with the CPU PC.
    auto* cpu = rt->cpu();
    rt->state.main_bus->set_access_observer(
        [cpu, watch_addr](const mnemos::topology::access_event& ev) {
            // Watch a 2-byte window [addr, addr+1] so word writes are caught.
            const std::uint32_t a = ev.address & 0xFFFFFFU;
            const std::uint32_t w = watch_addr & 0xFFFFFFU;
            if (ev.write && (a == w || a == w + 1U)) {
                const auto pc = cpu->cpu_registers().pc;
                std::fprintf(stderr, "[wram] pc=$%06X [$%06X]=%02X\n",
                             static_cast<unsigned>(pc & 0xFFFFFFU), static_cast<unsigned>(a),
                             static_cast<unsigned>(ev.value));
            }
        });

    std::vector<mnemos::runtime::scheduled_chip> chips;
    for (const auto& e : rt->schedule()) {
        chips.push_back({e.chip, e.weight});
    }
    mnemos::runtime::scheduler sched(std::move(chips), rt->vdp());
    for (int i = 0; i < frames; ++i) {
        std::fprintf(stderr, "=== frame %d ===\n", i);
        sched.run_frame();
    }
    SUCCEED();
}
