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

TEST_CASE("genesis_adapter ignores out-of-range input ports") {
    genesis_adapter adapter(tiny_rom());
    mnemos::frontend_sdk::controller_state pad{};
    pad.start = true;
    pad.a = true;
    adapter.apply_input(0, pad);
    adapter.apply_input(7, pad);  // ignored
    adapter.apply_input(-1, pad); // ignored
    // No throw, no UB; in-range input was recorded (port 0).
    SUCCEED();
}

TEST_CASE("genesis_adapter drain_audio resamples to 48 kHz output") {
    genesis_adapter adapter(tiny_rom());
    // Before stepping, no samples queued but the adapter still reports the
    // fixed 48 kHz output rate so the player can open SDL_AudioStream upfront.
    auto audio = adapter.drain_audio();
    CHECK(audio.frame_count == 0U);
    CHECK(audio.sample_rate == 48000U);

    // Stepping one NTSC frame produces a frame's-worth of 48 kHz samples
    // (48000 / 60 = 800; allow +-2 for fractional accumulator rounding).
    adapter.step_one_frame();
    audio = adapter.drain_audio();
    CHECK(audio.sample_rate == 48000U);
    CHECK(audio.frame_count >= 798U);
    CHECK(audio.frame_count <= 802U);
    REQUIRE(audio.samples != nullptr);

    // Second drain of the same frame returns nothing (chip queues consumed).
    audio = adapter.drain_audio();
    CHECK(audio.frame_count == 0U);
}

TEST_CASE("genesis_adapter selects PAL pacing when configured") {
    genesis_adapter adapter(tiny_rom(), {.video_region = genesis_config::region::pal});
    CHECK(adapter.region().frames_per_second_x1000 == 50000U);
}

TEST_CASE("genesis_adapter routes controller_state through the system pad protocol") {
    using namespace mnemos::manifests::genesis;
    genesis_adapter adapter(tiny_rom());
    auto& sys = adapter.system();

    // Idle (no buttons pressed). On a 3-button Genesis pad with TH=high
    // (default), all buttons read as active-low '1' bits, so the byte is
    // 0x40 | 0x3F = 0x7F (bit 6 = TH high reflection, bits 0-5 = no buttons).
    mnemos::frontend_sdk::controller_state idle{};
    adapter.apply_input(0, idle);
    sys.pad_th[0] = true;
    CHECK(sys.read_pad_port(0) == 0x7FU);

    // Press Start. With TH high, Start isn't visible (bank shows B/C/dpad).
    // The byte must still report all other bits idle.
    mnemos::frontend_sdk::controller_state with_start{};
    with_start.start = true;
    adapter.apply_input(0, with_start);
    sys.pad_th[0] = true;
    CHECK(sys.read_pad_port(0) == 0x7FU); // TH high -> Start invisible

    // Toggle TH low. Start now visible at bit 5; bits 2,3 always 0 so a
    // 3-button pad is identifiable (left+right always "pressed" in this bank).
    sys.pad_th[0] = false;
    // Expected byte: TH=0 (no 0x40), Start press clears bit 5, bits 2/3 = 0.
    // Idle for U/D/A: bits 0,1,4 = 1. So byte = 0b00010011 = 0x13.
    CHECK(sys.read_pad_port(0) == 0x13U);

    // Press D-pad Right + button A with TH low.
    mnemos::frontend_sdk::controller_state combo{};
    combo.right = true;
    combo.a = true;
    adapter.apply_input(0, combo);
    sys.pad_th[0] = false;
    // TH low: bits 2,3 = 0 (L/R show as pressed regardless); A clears bit 4.
    // U/D idle (bits 0,1 = 1); Start idle (bit 5 = 1). Byte = 0b00100011 = 0x23.
    CHECK(sys.read_pad_port(0) == 0x23U);

    // Same combo with TH high: now Right is visible at bit 3 (cleared).
    sys.pad_th[0] = true;
    // Bits: 0=U(1), 1=D(1), 2=L(1), 3=R(0 pressed), 4=B(1), 5=C(1), 6=TH(1) = 0x77.
    CHECK(sys.read_pad_port(0) == 0x77U);
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

    // Multi-region: USA wins over Europe in the country-byte priority.
    CHECK(detect_region(rom_with_region("UE ")) == genesis_config::region::ntsc);
    CHECK(detect_region(rom_with_region("EJU")) == genesis_config::region::ntsc);
    CHECK(detect_region(rom_with_region("JE ")) == genesis_config::region::ntsc); // J wins over E

    // Multi-region without Europe -> NTSC.
    CHECK(detect_region(rom_with_region("JU ")) == genesis_config::region::ntsc);

    // No region info at all -> safe default NTSC.
    CHECK(detect_region(rom_with_region("   ")) == genesis_config::region::ntsc);

    // Hex-bitfield region byte: real Genesis bits are J=1, U=4, E=8.
    CHECK(detect_region(rom_with_region("8  ")) == genesis_config::region::pal);  // E only
    CHECK(detect_region(rom_with_region("4  ")) == genesis_config::region::ntsc); // U only
    CHECK(detect_region(rom_with_region("F  ")) == genesis_config::region::ntsc); // J+U+E (U wins)
    CHECK(detect_region(rom_with_region("3  ")) == genesis_config::region::ntsc); // J+U

    // Truncated ROM -> safe default NTSC.
    std::vector<std::uint8_t> short_rom{0x00U, 0x01U, 0x02U};
    CHECK(detect_region(short_rom) == genesis_config::region::ntsc);
}
