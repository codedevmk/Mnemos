// Parity test for the Genesis manifest path: drive the SAME cartridge through
// both the hand-written assemble_genesis path (the oracle) and the manifest-built
// build_genesis_runtime path for N frames, then compare the VDP framebuffers
// byte-for-byte.
//
//   - Synthetic cart (always runs): a tiny ROM whose 68000 boots into a self
//     loop. Exercises reset-vector load, ROM fetch, VDP raster, and the
//     V-blank->Z80 edge through both paths.
//   - Real cart (data-gated on MNEMOS_GENESIS_ROM): full boot A/B over more
//     frames, exercising every MMIO block, the Z80 arbitration, and DMA.

#include "genesis_runtime.hpp"
#include "genesis_system.hpp"

#include "scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

    using mnemos::manifests::genesis::assemble_genesis;
    using mnemos::manifests::genesis::build_genesis_runtime;
    using mnemos::manifests::genesis::genesis_config;

    // Minimal 4 KiB cart: SSP=$00FFFE00, PC=$00000008, then BRA.S * (self loop).
    std::vector<std::uint8_t> loop_rom() {
        std::vector<std::uint8_t> rom(0x1000U, 0x00U);
        rom[0] = 0x00; // initial SSP (big-endian) = $00FFFE00
        rom[1] = 0xFF;
        rom[2] = 0xFE;
        rom[3] = 0x00;
        rom[4] = 0x00; // initial PC (big-endian) = $00000008
        rom[5] = 0x00;
        rom[6] = 0x00;
        rom[7] = 0x08;
        rom[8] = 0x60; // BRA.S *  (0x60 0xFE -> branch to self)
        rom[9] = 0xFE;
        return rom;
    }

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in test data path
#endif
    std::optional<std::vector<std::uint8_t>> read_cart_or_skip() {
        const char* env = std::getenv("MNEMOS_GENESIS_ROM");
        if (env == nullptr || env[0] == '\0') {
            return std::nullopt;
        }
        std::ifstream in(env, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>{});
    }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    struct frame_pixels final {
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint32_t> pixels;
    };

    [[nodiscard]] frame_pixels snapshot(const mnemos::chips::frame_buffer_view& fb) {
        frame_pixels out;
        out.width = fb.width;
        out.height = fb.height;
        const std::uint32_t stride = fb.effective_stride();
        out.pixels.reserve(static_cast<std::size_t>(fb.width) * fb.height);
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                out.pixels.push_back(row[x]);
            }
        }
        return out;
    }

    [[nodiscard]] frame_pixels run_assemble(const std::vector<std::uint8_t>& rom,
                                            const genesis_config& config, int frames) {
        auto sys = assemble_genesis(rom, config);
        // Same schedule order/weights as genesis_adapter (gated 68K + Z80).
        std::vector<mnemos::runtime::scheduled_chip> chips = {{&sys->vdp, 1U},
                                                              {&sys->cpu_gate, 7U},
                                                              {&sys->z80_gate, 15U},
                                                              {&sys->fm, 7U},
                                                              {&sys->psg, 15U}};
        mnemos::runtime::scheduler sched(std::move(chips), &sys->vdp);
        for (int i = 0; i < frames; ++i) {
            sched.run_frame();
        }
        return snapshot(sys->vdp.framebuffer());
    }

    [[nodiscard]] frame_pixels run_runtime(const std::vector<std::uint8_t>& rom,
                                           const genesis_config& config, int frames) {
        auto rt = build_genesis_runtime(rom, config);
        std::vector<mnemos::runtime::scheduled_chip> chips;
        for (const auto& e : rt->schedule()) {
            chips.push_back({e.chip, e.weight});
        }
        mnemos::runtime::scheduler sched(std::move(chips), rt->vdp());
        for (int i = 0; i < frames; ++i) {
            sched.run_frame();
        }
        return snapshot(rt->vdp()->framebuffer());
    }

    void require_parity(const frame_pixels& a, const frame_pixels& b) {
        REQUIRE(a.width == b.width);
        REQUIRE(a.height == b.height);
        REQUIRE(a.pixels.size() == b.pixels.size());
        std::size_t diff = 0;
        std::uint32_t first = 0;
        bool found = false;
        for (std::size_t i = 0; i < a.pixels.size(); ++i) {
            if (a.pixels[i] != b.pixels[i]) {
                if (!found) {
                    first = static_cast<std::uint32_t>(i);
                    found = true;
                }
                ++diff;
            }
        }
        INFO("pixels differing: " << diff << " / " << a.pixels.size() << "  first diff at index "
                                  << first);
        CHECK(diff == 0);
    }

} // namespace

TEST_CASE("build_genesis_runtime matches assemble_genesis (synthetic cart)",
          "[genesis][manifest][parity]") {
    const auto rom = loop_rom();
    require_parity(run_assemble(rom, {}, 3), run_runtime(rom, {}, 3));
}

TEST_CASE("build_genesis_runtime matches assemble_genesis on a real cart",
          "[genesis][manifest][rom][parity]") {
    auto rom = read_cart_or_skip();
    if (!rom) {
        SKIP("MNEMOS_GENESIS_ROM env var unset; data-gated parity check skipped");
    }
    constexpr int frames = 30;
    require_parity(run_assemble(*rom, {}, frames), run_runtime(*rom, {}, frames));
}
