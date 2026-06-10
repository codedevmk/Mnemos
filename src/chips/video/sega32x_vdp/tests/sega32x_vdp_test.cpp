#include "sega32x_vdp.hpp"

#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::video::sega32x_vdp;

    // 15bpp BGR helper: bit 15 = priority, then 5:5:5 B:G:R.
    constexpr std::uint16_t bgr15(unsigned r5, unsigned g5, unsigned b5, bool priority = false) {
        return static_cast<std::uint16_t>((priority ? 0x8000U : 0U) | (b5 << 10U) | (g5 << 5U) |
                                          r5);
    }
} // namespace

TEST_CASE("sega32x_vdp registers round-trip with hardware write masks", "[sega32x_vdp]") {
    sega32x_vdp v;

    // Bitmap-mode bit 15 is the read-only video-standard pin (1 = NTSC,
    // 0 = PAL): stripped on write, mirrored on read.
    v.write16(sega32x_vdp::reg_bitmap_mode, 0xFFFFU);
    CHECK(v.read16(sega32x_vdp::reg_bitmap_mode) == 0xFFFFU); // NTSC default
    v.set_pal(true);
    CHECK(v.read16(sega32x_vdp::reg_bitmap_mode) == 0x7FFFU);
    v.set_pal(false);
    CHECK(v.mode() == sega32x_vdp::mode_rle);

    // Autofill length keeps the low byte only.
    v.write16(sega32x_vdp::reg_autofill_length, 0xABCDU);
    CHECK(v.read16(sega32x_vdp::reg_autofill_length) == 0x00CDU);

    v.write16(sega32x_vdp::reg_screen_shift, 0x1234U);
    CHECK(v.read16(sega32x_vdp::reg_screen_shift) == 0x1234U);
    v.write16(sega32x_vdp::reg_autofill_addr, 0xBEEFU);
    CHECK(v.read16(sega32x_vdp::reg_autofill_addr) == 0xBEEFU);

    // The register cells mirror across the window (offset & 0x0E).
    CHECK(v.read16(sega32x_vdp::reg_autofill_addr + 0x10U) == 0xBEEFU);
}

TEST_CASE("sega32x_vdp palette stores 256 16-bit entries", "[sega32x_vdp]") {
    sega32x_vdp v;
    v.palette_write16(0x000U, 0x1234U);
    v.palette_write16(0x1FEU, 0x8001U);
    CHECK(v.palette_read16(0x000U) == 0x1234U);
    CHECK(v.palette_read16(0x1FEU) == 0x8001U);
    CHECK(v.palette(0) == 0x1234U);
    CHECK(v.palette(255) == 0x8001U);
    // The 512-byte window wraps (offset & 0x1FE).
    CHECK(v.palette_read16(0x200U) == 0x1234U);
}

TEST_CASE("sega32x_vdp autofill sweeps one 256-word row and latches the end address",
          "[sega32x_vdp]") {
    sega32x_vdp v;
    std::vector<std::uint8_t> fb(0x40000, 0);

    // Start near the end of a row: the low address byte wraps within the row
    // (the top byte holds), so the fill continues at the row start.
    v.write16(sega32x_vdp::reg_autofill_addr, 0x01FEU);
    v.write16(sega32x_vdp::reg_autofill_length, 3U); // count = 4 words
    v.write16(sega32x_vdp::reg_autofill_data, 0xA55AU);
    CHECK(v.autofill_execute(fb) == 4U);

    // Words at $1FE, $1FF, then wrap to $100, $101 (byte offsets x2),
    // stored big-endian.
    CHECK(fb[0x1FEU * 2U] == 0xA5U);
    CHECK(fb[0x1FEU * 2U + 1U] == 0x5AU);
    CHECK(fb[0x1FFU * 2U] == 0xA5U);
    CHECK(fb[0x100U * 2U] == 0xA5U);
    CHECK(fb[0x101U * 2U + 1U] == 0x5AU);
    // The word after the wrapped run is untouched.
    CHECK(fb[0x102U * 2U] == 0x00U);
    // Final address latched back: $102 within the same row.
    CHECK(v.read16(sega32x_vdp::reg_autofill_addr) == 0x0102U);
}

TEST_CASE("sega32x_vdp frame-select commits on the V-blank rising edge", "[sega32x_vdp]") {
    sega32x_vdp v;

    // Out of reset FS = 0: the CPUs access bank 0, bank 1 is displayed. The FS
    // write latches but nothing flips until V-blank rises.
    CHECK(v.access_bank() == 0);
    CHECK(v.display_bank() == 1);
    v.write16(sega32x_vdp::reg_fb_control, 0x0001U);
    CHECK(v.access_bank() == 0);
    CHECK((v.read16(sega32x_vdp::reg_fb_control) & 0x0001U) == 0U);

    v.set_blanking(false, false); // still in active display
    CHECK(v.access_bank() == 0);

    v.set_blanking(false, true); // V-blank rising edge: commit (both flip)
    CHECK(v.access_bank() == 1);
    CHECK(v.display_bank() == 0);
    CHECK((v.read16(sega32x_vdp::reg_fb_control) & 0x8000U) != 0U); // VBLK
    CHECK((v.read16(sega32x_vdp::reg_fb_control) & 0x4000U) == 0U); // HBLK

    // Falling edge: VBLK clears, the committed bank stays.
    v.set_blanking(true, false);
    CHECK(v.access_bank() == 1);
    CHECK((v.read16(sega32x_vdp::reg_fb_control) & 0x4000U) != 0U); // HBLK
    CHECK((v.read16(sega32x_vdp::reg_fb_control) & 0x8000U) == 0U);
}

TEST_CASE("sega32x_vdp composes packed-mode pixels with priority and transparency",
          "[sega32x_vdp]") {
    sega32x_vdp v;
    std::vector<std::uint8_t> fb(0x40000, 0);
    v.write16(sega32x_vdp::reg_bitmap_mode, sega32x_vdp::mode_packed);
    v.palette_write16(1U * 2U, bgr15(31, 0, 0, true));  // index 1: red, over
    v.palette_write16(2U * 2U, bgr15(0, 31, 0, false)); // index 2: green, behind

    // With FS = 0 the DISPLAYED bank is bank 1 (byte $20000). Line table:
    // row 0's data starts at word $0100 (byte $200) within that bank.
    fb[0x20000] = 0x01U;
    fb[0x20001] = 0x00U;
    // Row 0: pixel 0 = transparent, 1 = red/over, 2..3 = green/behind.
    fb[0x20200] = 0U;
    fb[0x20201] = 1U;
    fb[0x20202] = 2U;
    fb[0x20203] = 2U;

    // The Genesis row has content at x=1 and x=2 but backdrop (black) at x=3.
    std::vector<std::uint32_t> row(320, 0U);
    row[1] = 0x00112233U;
    row[2] = 0x00445566U;

    v.compose_scanline(fb, row, 0);

    CHECK(row[0] == 0U);          // index 0 transparent, backdrop stays
    CHECK(row[1] == 0x00FF0000U); // priority pixel wins over Genesis content
    CHECK(row[2] == 0x00445566U); // behind pixel loses where Genesis has content
    CHECK(row[3] == 0x0000FF00U); // behind pixel shows on the Genesis backdrop
}

TEST_CASE("sega32x_vdp behind pixels honour the Genesis backdrop mask", "[sega32x_vdp]") {
    sega32x_vdp v;
    std::vector<std::uint8_t> fb(0x40000, 0);
    v.write16(sega32x_vdp::reg_bitmap_mode, sega32x_vdp::mode_packed);
    v.palette_write16(2U * 2U, bgr15(0, 31, 0, false)); // index 2: green, behind

    // Displayed bank (FS = 0) is bank 1; row 0 data at byte $200.
    fb[0x20000] = 0x01U;
    fb[0x20001] = 0x00U;
    fb[0x20200] = 2U; // x=0: behind pixel over an opaque-black Genesis pixel
    fb[0x20201] = 2U; // x=1: behind pixel over the Genesis backdrop

    // Both Genesis pixels are black -- indistinguishable by colour. The mask
    // says x=0 is an opaque tile pixel and x=1 is backdrop.
    std::vector<std::uint32_t> row(320, 0U);
    const std::array<std::uint8_t, 320> backdrop = [] {
        std::array<std::uint8_t, 320> m{};
        m.fill(1U);
        m[0] = 0U;
        return m;
    }();

    v.compose_scanline(fb, row, 0, backdrop.data());

    CHECK(row[0] == 0U);          // hidden behind the opaque black tile
    CHECK(row[1] == 0x0000FF00U); // shows on the true backdrop
}

TEST_CASE("sega32x_vdp composes direct-colour pixels through the line table", "[sega32x_vdp]") {
    sega32x_vdp v;
    std::vector<std::uint8_t> fb(0x40000, 0);
    v.write16(sega32x_vdp::reg_bitmap_mode, sega32x_vdp::mode_direct);

    // Displayed bank (FS = 0) is bank 1. Line table: row 3's entry points at
    // word $0200 (byte $400) within the bank.
    fb[0x20000U + 3U * 2U] = 0x02U;
    fb[0x20000U + 3U * 2U + 1U] = 0x00U;
    // Pixel 5 of that row: priority white, big-endian word.
    const std::size_t bo = 0x20400U + 5U * 2U;
    const std::uint16_t white = bgr15(31, 31, 31, true);
    fb[bo] = static_cast<std::uint8_t>(white >> 8U);
    fb[bo + 1U] = static_cast<std::uint8_t>(white);

    std::vector<std::uint32_t> row(320, 0x00101010U);
    v.compose_scanline(fb, row, 3);
    CHECK(row[5] == 0x00FFFFFFU);
    CHECK(row[4] == 0x00101010U); // word 0 = transparent elsewhere
}

TEST_CASE("sega32x_vdp mode off leaves the Genesis row untouched", "[sega32x_vdp]") {
    sega32x_vdp v;
    std::vector<std::uint8_t> fb(0x40000, 0xFFU);
    std::vector<std::uint32_t> row(320, 0x00ABCDEFU);
    v.compose_scanline(fb, row, 0);
    CHECK(row[0] == 0x00ABCDEFU);
    CHECK(row[319] == 0x00ABCDEFU);
}

TEST_CASE("sega32x_vdp save/load round-trips the full state", "[sega32x_vdp]") {
    sega32x_vdp v;
    v.write16(sega32x_vdp::reg_bitmap_mode, 0x0001U);
    v.write16(sega32x_vdp::reg_autofill_addr, 0x1234U);
    v.write16(sega32x_vdp::reg_fb_control, 0x0001U); // pending FS
    v.palette_write16(4U, 0x7FFFU);
    v.set_blanking(false, true); // commit FS, set VBLK

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer w(blob);
    v.save_state(w);

    sega32x_vdp restored;
    mnemos::chips::state_reader r(blob);
    restored.load_state(r);
    REQUIRE(r.ok());
    REQUIRE(r.remaining() == 0U);
    CHECK(restored.read16(sega32x_vdp::reg_bitmap_mode) == 0x8001U); // NTSC pin set
    CHECK(restored.read16(sega32x_vdp::reg_autofill_addr) == 0x1234U);
    CHECK(restored.palette_read16(4U) == 0x7FFFU);
    CHECK(restored.access_bank() == 1);
    CHECK(restored.fb_control() == v.fb_control());
}

TEST_CASE("sega32x_vdp frame-select written during V-blank commits immediately", "[sega32x_vdp]") {
    sega32x_vdp v;
    v.set_blanking(false, true); // enter V-blank (VBLK set)
    CHECK(v.access_bank() == 0);
    v.write16(sega32x_vdp::reg_fb_control, 0x0001U);
    CHECK(v.access_bank() == 1); // immediate during V-blank
    v.set_blanking(false, false);
    v.write16(sega32x_vdp::reg_fb_control, 0x0000U);
    CHECK(v.access_bank() == 1); // deferred during active display
    v.set_blanking(false, true);
    CHECK(v.access_bank() == 0); // committed at the rising edge
}
