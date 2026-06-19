// ZX Spectrum 48K manifest tests. The synthetic-ROM cases always run; the
// real-ROM boot case is data-gated on MNEMOS_SPECTRUM_ROM (a 16 KiB 48K system
// ROM) and SKIPs cleanly when unset.

#include "spectrum_system.hpp"

#include "spectrum_snapshot.hpp"
#include "ula.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

namespace {

    using mnemos::manifests::spectrum::assemble_spectrum;
    using mnemos::manifests::spectrum::spectrum_system;
    using ula = mnemos::chips::video::ula;

    // Drive the machine one frame the way the scheduler does: ULA, CPU, beeper,
    // one T-state each, for a whole 69888-T-state frame.
    void run_frame(spectrum_system& s) {
        for (int t = 0; t < ula::tstates_per_frame; ++t) {
            s.ula.tick(1);
            s.cpu.tick(1);
            s.beeper.tick(1);
        }
    }

#if defined(_MSC_VER)
#pragma warning(disable : 4996) // std::getenv: opt-in test data path
#endif
    std::vector<std::uint8_t> read_rom_or_empty() {
        const char* env = std::getenv("MNEMOS_SPECTRUM_ROM");
        if (env == nullptr || env[0] == '\0') {
            return {};
        }
        std::ifstream in(env, std::ios::binary);
        if (!in) {
            return {};
        }
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>{});
    }

} // namespace

TEST_CASE("spectrum assembles and advances frames", "[manifests][spectrum]") {
    const std::vector<std::uint8_t> rom(0x4000U, 0x00U); // all-NOP synthetic ROM
    const auto sys = assemble_spectrum(rom);
    REQUIRE(sys != nullptr);
    REQUIRE(sys->ula.frame_index() == 0U);

    run_frame(*sys);
    run_frame(*sys);
    CHECK(sys->ula.frame_index() == 2U);

    const auto fb = sys->ula.framebuffer();
    CHECK(fb.width == static_cast<std::uint32_t>(ula::frame_width));
    CHECK(fb.height == static_cast<std::uint32_t>(ula::frame_height));
    CHECK(fb.pixels != nullptr);
}

TEST_CASE("spectrum routes OUT ($FE) bit 4 to the beeper", "[manifests][spectrum]") {
    // A ROM that toggles port $FE bit 4 (the speaker) in a loop, with a short
    // DJNZ delay each half so the square is a few kHz.
    std::vector<std::uint8_t> rom(0x4000U, 0x00U);
    const std::array<std::uint8_t, 19> prog = {
        0x3E, 0x10,       // LD A,$10   ; speaker high
        0xD3, 0xFE,       // OUT ($FE),A
        0x06, 0x20,       // LD B,$20
        0x10, 0xFE,       // DJNZ $     ; delay
        0x3E, 0x00,       // LD A,$00   ; speaker low
        0xD3, 0xFE,       // OUT ($FE),A
        0x06, 0x20,       // LD B,$20
        0x10, 0xFE,       // DJNZ $
        0xC3, 0x00, 0x00, // JP $0000
    };
    std::copy(prog.begin(), prog.end(), rom.begin());

    const auto sys = assemble_spectrum(rom);
    REQUIRE(sys != nullptr);
    sys->beeper.enable_audio_capture(true);
    for (int i = 0; i < 4; ++i) {
        run_frame(*sys);
    }

    std::vector<std::int16_t> buf(sys->beeper.pending_samples());
    REQUIRE(buf.size() > 100U);
    sys->beeper.drain_samples(buf.data(), buf.size());
    std::int16_t mn = 32767;
    std::int16_t mx = -32768;
    for (const std::int16_t s : buf) {
        mn = std::min(mn, s);
        mx = std::max(mx, s);
    }
    CHECK(mn < -1000); // toggling bit 4 produced an AC (audible) signal
    CHECK(mx > 1000);
}

TEST_CASE("spectrum 128K pages RAM banks + ROM halves via $7FFD", "[manifests][spectrum]") {
    // 32 KiB ROM -> 128K model. Distinct first byte per ROM half.
    std::vector<std::uint8_t> rom(0x8000U, 0x00U);
    rom[0x0000] = 0xAA; // ROM half 0
    rom[0x4000] = 0xBB; // ROM half 1
    const auto sys = assemble_spectrum(rom);
    REQUIRE(sys != nullptr);
    REQUIRE(sys->model == mnemos::manifests::spectrum::spectrum_model::k128);

    CHECK(sys->bus.read8(0x0000U) == 0xAAU); // half 0 at reset

    // Tag $C000 in two different RAM banks, switching the page via $7FFD.
    sys->set_paging(0);
    sys->bus.write8(0xC000U, 0x11U); // bank 0
    sys->set_paging(1);
    sys->bus.write8(0xC000U, 0x22U); // bank 1
    sys->set_paging(0);
    CHECK(sys->bus.read8(0xC000U) == 0x11U);
    sys->set_paging(1);
    CHECK(sys->bus.read8(0xC000U) == 0x22U);

    // ROM half select (bit 4).
    sys->set_paging(0x10U);
    CHECK(sys->bus.read8(0x0000U) == 0xBBU);
    sys->set_paging(0x00U);
    CHECK(sys->bus.read8(0x0000U) == 0xAAU);

    // Paging lock (bit 5) freezes further $7FFD writes until reset.
    sys->set_paging(0x20U | 1U);             // lock + bank 1 at $C000
    sys->set_paging(0x00U);                  // ignored while locked
    CHECK(sys->bus.read8(0xC000U) == 0x22U); // still bank 1
}

TEST_CASE("spectrum keyboard half-rows are active-low", "[manifests][spectrum]") {
    const std::vector<std::uint8_t> rom(0x4000U, 0x00U);
    const auto sys = assemble_spectrum(rom);
    REQUIRE(sys != nullptr);
    // All released after assembly.
    for (const std::uint8_t row : sys->keyboard_rows) {
        CHECK(row == 0xFFU);
    }
    // CAPS SHIFT is row 0 bit 0; pressing clears that bit only.
    sys->set_key(0, 0, true);
    CHECK(sys->keyboard_rows[0] == 0xFEU);
    sys->set_key(0, 0, false);
    CHECK(sys->keyboard_rows[0] == 0xFFU);
}

TEST_CASE("spectrum boots a real 48K ROM to a mostly-white screen", "[manifests][spectrum][data]") {
    const std::vector<std::uint8_t> rom = read_rom_or_empty();
    if (rom.size() != 0x4000U) {
        SUCCEED("MNEMOS_SPECTRUM_ROM is not a 16 KiB 48K ROM -- skipping the boot check");
        return;
    }

    const auto sys = assemble_spectrum(rom);
    REQUIRE(sys != nullptr);
    // The 48K ROM runs a ~1.4 s RAM test before painting the copyright screen, so
    // give it a couple of seconds of frames to settle.
    for (int i = 0; i < 200; ++i) {
        run_frame(*sys);
    }

    // The 48K boot screen paints the display area white (paper) with black text.
    // Count white pixels inside the centred 256x192 screen window.
    const auto fb = sys->ula.framebuffer();
    constexpr std::uint32_t white = 0xC0C0C0U;
    std::uint32_t white_count = 0;
    std::uint32_t black_count = 0;
    for (int y = 0; y < ula::display_height; ++y) {
        const std::uint32_t* row =
            fb.pixels + static_cast<std::size_t>(y + ula::screen_y_offset) * fb.effective_stride();
        for (int x = 0; x < ula::display_width; ++x) {
            const std::uint32_t px = row[static_cast<std::size_t>(x + ula::screen_x_offset)];
            if (px == white) {
                ++white_count;
            } else if (px == 0x000000U) {
                ++black_count;
            }
        }
    }
    // The screen is overwhelmingly white paper, with a little black copyright text.
    CHECK(white_count > 40000U); // of 256*192 = 49152
    CHECK(black_count > 0U);
}

TEST_CASE("spectrum .z80 v1 snapshot parses registers + RAM", "[manifests][spectrum][snapshot]") {
    std::vector<std::uint8_t> z(30U + 0xC000U, 0x00U);
    z[0] = 0x12;           // A
    z[1] = 0x34;           // F
    z[2] = 0x78;           // C
    z[3] = 0x56;           // B  -> BC = 0x5678
    z[6] = 0x00;           // PC lo
    z[7] = 0x80;           // PC hi -> 0x8000 (non-zero => v1)
    z[8] = 0x00;           // SP lo
    z[9] = 0xFF;           // SP hi -> 0xFF00
    z[12] = 0x06;          // flags: border = 3, uncompressed
    z[29] = 0x01;          // IM 1
    z[30 + 0x0010] = 0xAB; // a RAM byte at $4010

    const auto snap = mnemos::manifests::spectrum::load_z80_snapshot(z);
    REQUIRE(snap.has_value());
    CHECK(snap->regs.af == 0x1234U);
    CHECK(snap->regs.bc == 0x5678U);
    CHECK(snap->regs.pc == 0x8000U);
    CHECK(snap->regs.sp == 0xFF00U);
    CHECK(snap->regs.im == 1U);
    CHECK(snap->border == 3U);
    CHECK_FALSE(snap->is_128k);
    CHECK(snap->bank[5][0x0010] == 0xABU); // $4010 -> RAM bank 5
}

TEST_CASE("spectrum .sna snapshot parses + pops PC off the stack",
          "[manifests][spectrum][snapshot]") {
    std::vector<std::uint8_t> s(27U + 0xC000U, 0x00U);
    s[21] = 0x34; // F
    s[22] = 0x12; // A  -> AF = 0x1234
    s[23] = 0x00; // SP lo
    s[24] = 0x60; // SP hi -> SP = 0x6000
    s[25] = 0x02; // IM 2
    s[26] = 0x05; // border 5
    // Put PC = 0x9ABC on the stack at $6000.
    s[27 + (0x6000 - 0x4000)] = 0xBC;
    s[27 + (0x6000 - 0x4000) + 1] = 0x9A;

    const auto snap = mnemos::manifests::spectrum::load_sna_snapshot(s);
    REQUIRE(snap.has_value());
    CHECK(snap->regs.af == 0x1234U);
    CHECK(snap->regs.pc == 0x9ABCU); // popped from the stack
    CHECK(snap->regs.sp == 0x6002U); // SP += 2 after the pop
    CHECK(snap->regs.im == 2U);
    CHECK(snap->border == 5U);
}

// Data-gated: load the 48K ROM (MNEMOS_SPECTRUM_ROM) + a .z80 game
// (MNEMOS_SPECTRUM_Z80), apply the snapshot, run a few frames, and confirm the
// game paints a varied screen (not a blank single-colour field).
TEST_CASE("spectrum runs a real .z80 game", "[manifests][spectrum][data]") {
#if defined(_MSC_VER)
#pragma warning(disable : 4996)
#endif
    const char* bios_env = std::getenv("MNEMOS_SPECTRUM_ROM");
    const char* game_env = std::getenv("MNEMOS_SPECTRUM_Z80");
    if (bios_env == nullptr || game_env == nullptr) {
        SUCCEED("MNEMOS_SPECTRUM_ROM / MNEMOS_SPECTRUM_Z80 unset -- skipping real-game run");
        return;
    }
    const auto read = [](const char* p) {
        std::ifstream in(p, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>{});
    };
    const std::vector<std::uint8_t> bios = read(bios_env);
    const std::vector<std::uint8_t> game = read(game_env);
    if (bios.size() < 0x4000U || game.empty()) {
        SUCCEED("could not read ROM/game -- skipping");
        return;
    }

    const auto sys = assemble_spectrum(bios);
    REQUIRE(sys != nullptr);
    const auto snap = mnemos::manifests::spectrum::load_snapshot(game);
    REQUIRE(snap.has_value());
    sys->apply_snapshot(*snap);
    for (int i = 0; i < 8; ++i) {
        run_frame(*sys);
    }

    // A real game screen has many distinct colours; a blank machine would be one.
    const auto fb = sys->ula.framebuffer();
    std::array<bool, 16> seen{};
    int distinct = 0;
    for (int y = 0; y < ula::display_height; ++y) {
        const std::uint32_t* row =
            fb.pixels + static_cast<std::size_t>(y + ula::screen_y_offset) * fb.effective_stride();
        for (int x = 0; x < ula::display_width; ++x) {
            // Fold the RGB to a small bucket so we count broad colour variety.
            const std::uint32_t px = row[static_cast<std::size_t>(x + ula::screen_x_offset)];
            const std::size_t bucket =
                ((px >> 23U) & 1U) | (((px >> 15U) & 1U) << 1U) | (((px >> 7U) & 1U) << 2U);
            if (!seen[bucket]) {
                seen[bucket] = true;
                ++distinct;
            }
        }
    }
    CHECK(distinct >= 3); // a real game uses several inks/papers
}
