// ZX Spectrum 48K manifest tests. The synthetic-ROM cases always run; the
// real-ROM boot case is data-gated on MNEMOS_SPECTRUM_ROM (a 16 KiB 48K system
// ROM) and SKIPs cleanly when unset.

#include "spectrum_system.hpp"

#include "ula.hpp"

#include <catch2/catch_test_macros.hpp>

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

    // Drive the machine one frame the way the scheduler does: ULA then CPU, one
    // T-state each, for a whole 69888-T-state frame.
    void run_frame(spectrum_system& s) {
        for (int t = 0; t < ula::tstates_per_frame; ++t) {
            s.ula.tick(1);
            s.cpu.tick(1);
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
    if (rom.size() < 0x4000U) {
        SUCCEED("MNEMOS_SPECTRUM_ROM unset / too small -- skipping real-ROM boot");
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
