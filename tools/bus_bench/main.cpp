// Microbench for topology::bus hot paths. Measures the cost of read8/write8
// under representative scenarios so we can ground future bus optimizations in
// data, not guesses. Not part of the test suite; build via the bus_bench tool
// target and run the executable directly.

#include "bus.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <vector>

namespace {

    using mnemos::topology::bus;
    using mnemos::topology::endianness;

    constexpr std::uint64_t kIters = 200'000'000ULL;

    template <class Fn>
    void time_loop(const char* name, Fn fn) {
        using clock = std::chrono::steady_clock;
        const auto t0 = clock::now();
        fn();
        const auto t1 = clock::now();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        const double per = static_cast<double>(ns) / static_cast<double>(kIters);
        std::printf("%-58s %8.2f ns/op\n", name, per);
    }

    volatile std::uint64_t g_sink = 0;

    void run() {
        std::vector<std::uint8_t> ram(65536U, 0xABU);

        // Scenario 1: direct array indexing. Absolute floor; no bus involved.
        time_loop("[direct]   raw array indexing", [&] {
            std::uint64_t s = 0;
            for (std::uint64_t i = 0; i < kIters; ++i) {
                s += ram[i & 0xFFFFU];
            }
            g_sink += s;
        });

        // Scenario 2: bus RAM read, single region, no predicate. The minimum
        // cost the bus abstraction itself imposes (virtual call, mask, resolve
        // linear scan over 1 region, switch on backing kind, RAM index).
        {
            bus b(16U, endianness::little);
            b.map_ram(0U, ram, 0);
            time_loop("[bus]      1 RAM region, no predicate", [&] {
                std::uint64_t s = 0;
                for (std::uint64_t i = 0; i < kIters; ++i) {
                    s += b.read8(static_cast<std::uint32_t>(i));
                }
                g_sink += s;
            });
        }

        // Scenario 3: same as 2 but with a trivial always-true predicate. Isolates
        // the std::function call overhead in the predicate slot. Per-access cost
        // of the banking predicate is what matters because the bus checks it on
        // EVERY read/write for regions that have one.
        {
            bus b(16U, endianness::little);
            b.map_ram(0U, ram, 0, [](std::uint32_t, bool) { return true; });
            time_loop("[bus]      1 RAM region, std::function predicate", [&] {
                std::uint64_t s = 0;
                for (std::uint64_t i = 0; i < kIters; ++i) {
                    s += b.read8(static_cast<std::uint32_t>(i));
                }
                g_sink += s;
            });
        }

        // Scenario 4: MMIO read through std::function on_read. Models a VDP
        // register read on Genesis -- the typical MMIO hot path.
        {
            bus b(16U, endianness::little);
            std::uint8_t mmio_value = 0xABU;
            b.map_mmio(
                0U, 65536U, [&](std::uint32_t) { return mmio_value; },
                [&](std::uint32_t, std::uint8_t) {}, 0);
            time_loop("[bus]      MMIO read, std::function on_read", [&] {
                std::uint64_t s = 0;
                for (std::uint64_t i = 0; i < kIters; ++i) {
                    s += b.read8(static_cast<std::uint32_t>(i));
                }
                g_sink += s;
            });
        }

        // Scenario 5: 8 overlapping RAM regions, all with predicates, only
        // priority 0 active. Models heavy banking (C64 PLA, Genesis Z80 window).
        // Isolates the cost of the resolve() linear scan when it has to walk
        // past inactive regions on every access.
        {
            bus b(16U, endianness::little);
            for (int p = 0; p < 8; ++p) {
                const bool only_priority_0 = (p == 0);
                b.map_ram(0U, ram, p, [only_priority_0](std::uint32_t, bool) {
                    return only_priority_0;
                });
            }
            time_loop("[bus]      8 RAM regions, 7 inactive predicates", [&] {
                std::uint64_t s = 0;
                for (std::uint64_t i = 0; i < kIters; ++i) {
                    s += b.read8(static_cast<std::uint32_t>(i));
                }
                g_sink += s;
            });
        }

        // Scenario 6: write hot path. Same as 4 but write8. Genesis VDP register
        // pokes flow through this.
        {
            bus b(16U, endianness::little);
            std::uint8_t mmio_value = 0;
            b.map_mmio(
                0U, 65536U, [&](std::uint32_t) { return mmio_value; },
                [&mmio_value](std::uint32_t, std::uint8_t v) { mmio_value = v; }, 0);
            time_loop("[bus]      MMIO write, std::function on_write", [&] {
                for (std::uint64_t i = 0; i < kIters; ++i) {
                    b.write8(static_cast<std::uint32_t>(i),
                             static_cast<std::uint8_t>(i));
                }
                g_sink += mmio_value;
            });
        }
    }

} // namespace

int main() {
    std::printf("topology::bus hot-path microbench  (iters per scenario = %llu)\n",
                static_cast<unsigned long long>(kIters));
    std::printf("---------------------------------------------------------------------\n");
    run();
    std::printf("\nsink = %llu (anti-DCE)\n", static_cast<unsigned long long>(g_sink));
    return 0;
}
