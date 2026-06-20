// NES player-adapter smoke test: construct from a synthetic NROM, step frames,
// and confirm the adapter advertises its chips and a 256x240 framebuffer. The
// real-game render is a manual / data-gated check (no .nes in the local corpus).

#include "nes_adapter.hpp"

#include "ppu2c02.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {
    using mnemos::apps::player::adapters::nes::nes_adapter;
    using ppu = mnemos::chips::video::ppu2c02;

    // Minimal NROM (header + 16 KiB PRG + 8 KiB CHR); PRG spins, reset -> $8000.
    std::vector<std::uint8_t> tiny_nrom() {
        std::vector<std::uint8_t> rom(16U + 0x4000U + 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 1U;
        rom[5] = 1U;
        rom[16 + 0] = 0x4CU; // JMP $8000
        rom[16 + 1] = 0x00U;
        rom[16 + 2] = 0x80U;
        rom[16 + 0x3FFCU] = 0x00U; // reset vector -> $8000
        rom[16 + 0x3FFDU] = 0x80U;
        return rom;
    }
} // namespace

namespace {
    // An NROM whose 6502 boot programs the PPU to draw the top 16 character rows
    // (128 px) as solid white over a black backdrop: wait two vblanks, write the
    // palette ($3F00 = black, $3F01 = white), fill 512 nametable cells with the
    // solid tile 1, reset scroll, enable background. Exercises CPU -> $2000-$3FFF
    // MMIO -> PPU render end-to-end without a real ROM.
    std::vector<std::uint8_t> render_test_nrom() {
        std::vector<std::uint8_t> code = {
            0x78, 0xD8, 0xA2, 0xFF, 0x9A,                   // SEI / CLD / LDX #$FF / TXS
            0xA9, 0x00, 0x8D, 0x00, 0x20, 0x8D, 0x01, 0x20, // disable NMI + rendering
            0x2C, 0x02, 0x20, 0x10, 0xFB,                   // vbl1: BIT $2002 / BPL
            0x2C, 0x02, 0x20, 0x10, 0xFB,                   // vbl2: BIT $2002 / BPL
            0xA9, 0x3F, 0x8D, 0x06, 0x20, 0xA9, 0x00, 0x8D, 0x06, 0x20, // PPUADDR=$3F00
            0xA9, 0x0F, 0x8D, 0x07, 0x20,                               // palette[0] = $0F (black)
            0xA9, 0x30, 0x8D, 0x07, 0x20,                               // palette[1] = $30 (white)
            0xA9, 0x20, 0x8D, 0x06, 0x20, 0xA9, 0x00, 0x8D, 0x06, 0x20, // PPUADDR=$2000
            0xA9, 0x01,                                                 // LDA #$01 (solid tile)
            0xA2, 0x00, 0x8D, 0x07, 0x20, 0xCA, 0xD0, 0xFA,             // l1: 256x STA $2007
            0xA2, 0x00, 0x8D, 0x07, 0x20, 0xCA, 0xD0, 0xFA,             // l2: 256x STA $2007
            0xA9, 0x00, 0x8D, 0x05, 0x20, 0x8D, 0x05, 0x20, 0x8D, 0x00, 0x20, // scroll 0; ctrl 0
            0xA9, 0x0A, 0x8D, 0x01, 0x20, // PPUMASK = BG enable + left column
        };
        const std::uint16_t loop = static_cast<std::uint16_t>(0x8000U + code.size());
        code.push_back(0x4CU); // JMP loop (self)
        code.push_back(static_cast<std::uint8_t>(loop & 0xFFU));
        code.push_back(static_cast<std::uint8_t>(loop >> 8U));

        std::vector<std::uint8_t> prg(0x4000U, 0x00U);
        for (std::size_t i = 0; i < code.size(); ++i) {
            prg[i] = code[i];
        }
        prg[0x3FFCU] = 0x00U; // reset vector -> $8000
        prg[0x3FFDU] = 0x80U;

        std::vector<std::uint8_t> chr(0x2000U, 0x00U);
        for (int i = 0; i < 8; ++i) {
            chr[16 + i] = 0xFFU; // tile 1 plane 0 solid -> pixel value 1 everywhere
        }

        std::vector<std::uint8_t> rom = {'N', 'E', 'S', 0x1AU, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        rom.insert(rom.end(), prg.begin(), prg.end());
        rom.insert(rom.end(), chr.begin(), chr.end());
        return rom;
    }
} // namespace

TEST_CASE("nes adapter boots a program and renders the background", "[apps][player][nes]") {
    nes_adapter adapter(render_test_nrom());
    for (int i = 0; i < 16; ++i) {
        adapter.step_one_frame();
    }
    const auto fb = adapter.current_frame();
    constexpr std::uint32_t white = 0x00FFFFFFU;
    constexpr std::uint32_t black = 0x00000000U;
    const std::uint32_t w = fb.width;
    // Top rows are the solid-white band; the lower screen is the black backdrop.
    CHECK(fb.pixels[10U * w + 10U] == white);
    CHECK(fb.pixels[200U * w + 10U] == black);
}

TEST_CASE("nes adapter constructs and steps frames", "[apps][player][nes]") {
    nes_adapter adapter(tiny_nrom());

    const auto fb0 = adapter.current_frame();
    CHECK(fb0.width == ppu::visible_width);
    CHECK(fb0.height == ppu::visible_height);

    const std::uint64_t before = adapter.system().ppu.frame_index();
    adapter.step_one_frame();
    adapter.step_one_frame();

    // PPU (frame source), CPU, APU.
    CHECK(adapter.chips().size() == 3U);
    CHECK(adapter.current_frame().pixels != nullptr);
    CHECK(adapter.system().ppu.frame_index() > before); // the PPU advanced frames
}

TEST_CASE("nes adapter drains APU audio after a frame", "[apps][player][nes]") {
    nes_adapter adapter(tiny_nrom());
    auto& sys = adapter.system();

    // Program pulse 1 through the bus, then run a frame so the scheduler ticks the
    // APU (capture is enabled in the adapter ctor) and drain the mixed output.
    sys.bus.write8(0x4015U, 0x01U);
    sys.bus.write8(0x4000U, 0xBFU);
    sys.bus.write8(0x4002U, 0xFFU);
    sys.bus.write8(0x4003U, 0x08U);

    adapter.step_one_frame();
    const auto chunk = adapter.drain_audio();
    REQUIRE(chunk.frame_count > 0U);
    CHECK(chunk.sample_rate > 0U);

    std::int16_t peak = 0;
    for (std::uint32_t i = 0; i < chunk.frame_count * 2U; ++i) {
        peak = std::max(peak, static_cast<std::int16_t>(std::abs(chunk.samples[i])));
    }
    CHECK(peak > 1000); // resampled APU output is audible
}
