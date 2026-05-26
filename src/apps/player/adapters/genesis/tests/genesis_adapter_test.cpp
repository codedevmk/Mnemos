// Adapter smoke. The Genesis player_system adapter is a thin shell over
// assemble_genesis() + a scheduler; this test boots it on a minimal ROM
// that just spins in place, steps a few frames, and checks the
// player_system contract holds.

#include "genesis_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

    using mnemos::apps::player::adapters::genesis::genesis_adapter;
    using mnemos::manifests::genesis::genesis_config;

    // Minimal 68000 boot: SSP, PC at $0008, MOVE.W #$2000,SR then BRA.S *
    // (the system just powers on and spins; the VDP still raster-times frames).
    std::vector<std::uint8_t> tiny_rom() {
        std::vector<std::uint8_t> rom(0x100, 0x00);
        const auto w16 = [&](std::size_t off, std::uint16_t v) {
            rom[off] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 1U] = static_cast<std::uint8_t>(v);
        };
        const auto w32 = [&](std::size_t off, std::uint32_t v) {
            rom[off + 0U] = static_cast<std::uint8_t>(v >> 24U);
            rom[off + 1U] = static_cast<std::uint8_t>(v >> 16U);
            rom[off + 2U] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 3U] = static_cast<std::uint8_t>(v);
        };
        w32(0x00, 0x00FFF000U); // SSP
        w32(0x04, 0x00000008U); // PC
        w16(0x08, 0x46FC);
        w16(0x0A, 0x2000); // MOVE.W #$2000,SR
        w16(0x0C, 0x60FE); // BRA.S *
        return rom;
    }

} // namespace

TEST_CASE("genesis_adapter advances frames and reports region") {
    genesis_adapter adapter(tiny_rom(), {.video_region = genesis_config::region::ntsc});

    CHECK(adapter.region().frames_per_second_x1000 == 60000U);
    CHECK(adapter.frames_stepped() == 0U);

    const auto fb_before = adapter.current_frame();
    REQUIRE(fb_before.width > 0U);
    REQUIRE(fb_before.height > 0U);

    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 2U);

    // current_frame() must remain accessible after stepping.
    const auto fb_after = adapter.current_frame();
    CHECK(fb_after.width == fb_before.width);
    CHECK(fb_after.height == fb_before.height);
}

TEST_CASE("genesis_adapter records input but commit 3 keeps audio silent") {
    genesis_adapter adapter(tiny_rom());

    mnemos::frontend_sdk::controller_state pad{};
    pad.start = true;
    pad.a = true;
    adapter.apply_input(0, pad);
    // No observable bus effect yet (commit 5 wires the MMIO). Just verify
    // out-of-range ports are ignored cleanly.
    adapter.apply_input(7, pad);  // ignored
    adapter.apply_input(-1, pad); // ignored

    const auto audio = adapter.drain_audio();
    CHECK(audio.samples == nullptr);
    CHECK(audio.frame_count == 0U);
    CHECK(audio.sample_rate == 0U);
}

TEST_CASE("genesis_adapter selects PAL pacing when configured") {
    genesis_adapter adapter(tiny_rom(), {.video_region = genesis_config::region::pal});
    CHECK(adapter.region().frames_per_second_x1000 == 50000U);
}

TEST_CASE("detect_region honours the cartridge header region field") {
    using mnemos::apps::player::adapters::genesis::detect_region;

    auto rom_with_region = [](const char* code) {
        auto rom = tiny_rom();
        rom.resize(0x200, 0x20U); // pad with spaces so $1F0..$1F2 are addressable
        for (std::size_t i = 0; i < 3; ++i) {
            rom[0x1F0 + i] = code[i] != '\0' ? static_cast<std::uint8_t>(code[i]) : 0x20U;
        }
        return rom;
    };

    // Pure regions.
    CHECK(detect_region(rom_with_region("J  ")) == genesis_config::region::ntsc);
    CHECK(detect_region(rom_with_region("U  ")) == genesis_config::region::ntsc);
    CHECK(detect_region(rom_with_region("E  ")) == genesis_config::region::pal);

    // Multi-region: Europe present -> PAL (parity with reference behaviour).
    CHECK(detect_region(rom_with_region("UE ")) == genesis_config::region::pal);
    CHECK(detect_region(rom_with_region("EJU")) == genesis_config::region::pal);
    CHECK(detect_region(rom_with_region("JE ")) == genesis_config::region::pal);

    // Multi-region without Europe -> NTSC.
    CHECK(detect_region(rom_with_region("JU ")) == genesis_config::region::ntsc);

    // No region info at all -> safe default NTSC.
    CHECK(detect_region(rom_with_region("   ")) == genesis_config::region::ntsc);

    // Hex-bitfield region byte: bit 2 = Europe.
    CHECK(detect_region(rom_with_region("4  ")) == genesis_config::region::pal); // bit 2 set
    CHECK(detect_region(rom_with_region("F  ")) == genesis_config::region::pal); // all bits
    CHECK(detect_region(rom_with_region("3  ")) == genesis_config::region::ntsc); // J+U only

    // Truncated ROM -> safe default NTSC.
    std::vector<std::uint8_t> short_rom{0x00U, 0x01U, 0x02U};
    CHECK(detect_region(short_rom) == genesis_config::region::ntsc);
}
