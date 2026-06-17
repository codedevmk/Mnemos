#include "vdp1.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::video::vdp1;

    // Native 0BGR1555 -> the 0x00RRGGBB the framebuffer view exposes.
    [[nodiscard]] constexpr std::uint32_t native_to_rgb(std::uint16_t native) {
        const auto expand = [](std::uint32_t five) { return (five << 3U) | (five >> 2U); };
        const std::uint32_t r = expand((native >> 0U) & 0x1FU);
        const std::uint32_t g = expand((native >> 5U) & 0x1FU);
        const std::uint32_t b = expand((native >> 10U) & 0x1FU);
        return (r << 16U) | (g << 8U) | b;
    }

    constexpr std::uint16_t opaque = 0x8000U;
    constexpr std::uint16_t red5 = static_cast<std::uint16_t>(opaque | 0x001FU);   // R=31
    constexpr std::uint16_t green5 = static_cast<std::uint16_t>(opaque | 0x03E0U); // G=31
    constexpr std::uint16_t blue5 = static_cast<std::uint16_t>(opaque | 0x7C00U);  // B=31

} // namespace

TEST_CASE("vdp1 registers through the chip registry as a video chip", "[vdp1]") {
    auto chip = mnemos::chips::create_chip("sega.vdp1");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::video);
    CHECK(chip->metadata().part_number == "vdp1");
}

TEST_CASE("vdp1 reset clears the contract surface", "[vdp1]") {
    vdp1 v;
    v.write_register(vdp1::reg_ptmr, vdp1::ptmr_start); // arm a plot
    v.write_vram_word(0U, 0x8000U);                     // end-of-list at the top
    v.run_to_end(8);

    v.reset(mnemos::chips::reset_kind::hard);
    CHECK(v.frame_index() == 0U);
    CHECK(v.state() == vdp1::draw_state::idle);
    CHECK(v.read_register(vdp1::reg_edsr) == 0U);
    CHECK(v.draw_buffer_index() == 0U);
    const auto frame = v.framebuffer();
    CHECK(frame.width == vdp1::fb_width);
    CHECK(frame.height == vdp1::fb_height);
    CHECK(frame.pixels[0] == 0U);
}

TEST_CASE("vdp1 plots a polygon and fills the framebuffer", "[vdp1]") {
    vdp1 v;

    // One command table entry at VRAM 0: a solid polygon (type 4) spanning
    // the rectangle (4,4)..(11,11) inclusive, colour = green, then an
    // end-of-list terminator in the following entry.
    v.write_vram_word(0x00U, 0x0004U); // CTRL: jump-next, type 4 (polygon)
    v.write_vram_word(0x04U, 0x0000U); // PMOD: replace, no clip
    v.write_vram_word(0x06U, green5);  // COLR: native fill colour
    v.write_vram_word(0x0CU, 4U);      // XA,YA
    v.write_vram_word(0x0EU, 4U);
    v.write_vram_word(0x10U, 11U); // XB,YB
    v.write_vram_word(0x12U, 4U);
    v.write_vram_word(0x14U, 11U); // XC,YC
    v.write_vram_word(0x16U, 11U);
    v.write_vram_word(0x18U, 4U); // XD,YD
    v.write_vram_word(0x1AU, 11U);
    v.write_vram_word(0x20U, vdp1::ctrl_end); // end-of-list terminator

    v.write_register(vdp1::reg_ptmr, vdp1::ptmr_start); // arms + begins plotting
    CHECK(v.state() == vdp1::draw_state::plotting);
    v.tick(1U); // functional model: runs the armed list to completion
    CHECK(v.state() == vdp1::draw_state::done);

    // EDSR current-frame end flag latched (bit 1).
    CHECK((v.read_register(vdp1::reg_edsr) & 0x0002U) != 0U);

    // Drawing went to the draw buffer (index 0); fb_read reads it directly.
    CHECK(v.fb_read(5, 5) == green5); // inside the fill
    CHECK(v.fb_read(0, 0) == 0U);     // outside the fill
    CHECK(v.fb_read(12, 12) == 0U);   // one past the bottom-right corner

    // The framebuffer() view exposes the DISPLAY buffer; swap the just-drawn
    // buffer into display so the rasterised polygon is visible there.
    v.swap_framebuffers();
    const auto frame = v.framebuffer();
    CHECK(frame.pixels[5U * frame.effective_stride() + 5U] == native_to_rgb(green5));
    CHECK(frame.pixels[0] == 0U);
}

TEST_CASE("vdp1 draws a 16bpp normal sprite with transparency", "[vdp1]") {
    vdp1 v;

    // Texture: a 8x2 (CH=1 -> 8px, CV=2) sprite stored at VRAM word offset
    // 0x100 (SRCA = 0x100/8 = 0x20). Row 0 = solid red, row 1 left half red /
    // right half transparent (native 0). 16bpp direct (colour-mode 5).
    constexpr std::uint32_t tex_words = 0x100U;
    for (std::uint32_t i = 0; i < 8U; ++i) {
        v.write_vram_word(tex_words + i * 2U, red5); // row 0
    }
    for (std::uint32_t i = 0; i < 8U; ++i) {
        const std::uint16_t texel = (i < 4U) ? blue5 : 0x0000U; // row 1
        v.write_vram_word(tex_words + 16U + i * 2U, texel);
    }

    v.write_vram_word(0x00U, 0x0000U);         // CTRL: jump-next, type 0 (normal sprite)
    v.write_vram_word(0x04U, 0x0028U);         // PMOD: colour-mode 5 (bits 5:3=101), replace
    v.write_vram_word(0x06U, 0x0000U);         // COLR (unused at 16bpp)
    v.write_vram_word(0x08U, tex_words / 8U);  // SRCA
    v.write_vram_word(0x0AU, (1U << 8U) | 2U); // SIZE: width 1*8=8, height 2
    v.write_vram_word(0x0CU, 20U);             // XA
    v.write_vram_word(0x0EU, 10U);             // YA
    v.write_vram_word(0x20U, vdp1::ctrl_end);  // end-of-list

    v.write_register(vdp1::reg_ptmr, vdp1::ptmr_start);
    v.tick(1U);

    CHECK(v.fb_read(20, 10) == red5);  // row 0, col 0
    CHECK(v.fb_read(27, 10) == red5);  // row 0, col 7
    CHECK(v.fb_read(20, 11) == blue5); // row 1, opaque left half
    CHECK(v.fb_read(24, 11) == 0U);    // row 1, transparent right half (not drawn)
    CHECK(v.fb_read(20, 9) == 0U);     // above the sprite
}

TEST_CASE("vdp1 walks the command list across a jump-assign link", "[vdp1]") {
    vdp1 v;

    // Entry 0 sets local coordinates (+100,+50) then JUMP-ASSIGN to entry at
    // VRAM 0x200 (link = 0x200/8 = 0x40). Entry @0x200 fills a polygon that,
    // with the local offset, lands at screen (100..103, 50..53), then ends.
    v.write_vram_word(0x00U, 0x100AU); // CTRL: jump-assign (0x1000) + type A (local coord)
    v.write_vram_word(0x02U, 0x0040U); // LINK -> 0x200
    v.write_vram_word(0x0CU, 100U);    // local X
    v.write_vram_word(0x0EU, 50U);     // local Y

    v.write_vram_word(0x200U + 0x00U, 0x0004U); // polygon, jump-next
    v.write_vram_word(0x200U + 0x06U, blue5);
    v.write_vram_word(0x200U + 0x0CU, 0U); // (0,0)
    v.write_vram_word(0x200U + 0x0EU, 0U);
    v.write_vram_word(0x200U + 0x10U, 3U); // (3,0)
    v.write_vram_word(0x200U + 0x12U, 0U);
    v.write_vram_word(0x200U + 0x14U, 3U); // (3,3)
    v.write_vram_word(0x200U + 0x16U, 3U);
    v.write_vram_word(0x200U + 0x18U, 0U); // (0,3)
    v.write_vram_word(0x200U + 0x1AU, 3U);
    v.write_vram_word(0x200U + 0x20U, vdp1::ctrl_end);

    v.write_register(vdp1::reg_ptmr, vdp1::ptmr_start);
    v.tick(1U);

    CHECK(v.fb_read(101, 51) == blue5); // local-offset polygon landed
    CHECK(v.fb_read(1, 1) == 0U);       // nothing at the un-offset origin
}

TEST_CASE("vdp1 erase clears a draw-buffer rectangle", "[vdp1]") {
    vdp1 v;
    // Paint a pixel, then erase a covering rectangle to fill colour 0.
    v.fb_write(10, 10, red5);
    CHECK(v.fb_read(10, 10) == red5);

    v.write_register(vdp1::reg_ewdr, 0x0000U); // fill word 0
    v.write_register(vdp1::reg_ewlr, 0x0000U); // upper-left (0,0)
    // lower-right: x2 units = full width / 8, y2 = full height.
    v.write_register(vdp1::reg_ewrr, static_cast<std::uint16_t>(((vdp1::fb_width / 8U) << 9U) |
                                                                (vdp1::fb_height - 1U)));
    v.erase_draw_buffer();
    CHECK(v.fb_read(10, 10) == 0U);
}

TEST_CASE("vdp1 round-trips its state", "[vdp1]") {
    vdp1 v;
    v.write_vram_word(0x00U, 0x0004U);
    v.write_vram_word(0x06U, red5);
    v.write_vram_word(0x0CU, 2U);
    v.write_vram_word(0x0EU, 2U);
    v.write_vram_word(0x10U, 6U);
    v.write_vram_word(0x12U, 2U);
    v.write_vram_word(0x14U, 6U);
    v.write_vram_word(0x16U, 6U);
    v.write_vram_word(0x18U, 2U);
    v.write_vram_word(0x1AU, 6U);
    v.write_vram_word(0x20U, vdp1::ctrl_end);
    v.write_register(vdp1::reg_ptmr, vdp1::ptmr_start);
    v.tick(1U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    v.save_state(writer);

    vdp1 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    CHECK(reader.ok());
    CHECK(restored.fb_read(4, 4) == red5);
    CHECK(restored.read_register(vdp1::reg_edsr) == v.read_register(vdp1::reg_edsr));
    CHECK(restored.state() == v.state());
}
