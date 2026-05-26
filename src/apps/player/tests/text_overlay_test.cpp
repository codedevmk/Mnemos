// Behavioural tests for the bitmap-font overlay helpers. The glyph
// shapes themselves are visually verified once when the font is
// designed; these tests just pin down the API contracts (clipping,
// width, fg-only writes, fill_rect).

#include "text_overlay.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using mnemos::apps::player::draw_text;
using mnemos::apps::player::fill_rect;
using mnemos::apps::player::kGlyphHeight;
using mnemos::apps::player::kGlyphWidth;
using mnemos::apps::player::text_pixel_width;

namespace {
    constexpr int kDstW = 32;
    constexpr int kDstH = 16;
    using buffer = std::array<std::uint32_t, kDstW * kDstH>;

    [[nodiscard]] int count_pixels(const buffer& b, std::uint32_t color) {
        int n = 0;
        for (auto p : b) {
            if (p == color) {
                ++n;
            }
        }
        return n;
    }
} // namespace

TEST_CASE("text_pixel_width is monospaced") {
    CHECK(text_pixel_width("") == 0);
    CHECK(text_pixel_width("A") == kGlyphWidth);
    CHECK(text_pixel_width("FPS:60") == 6 * kGlyphWidth);
}

TEST_CASE("draw_text only writes set-bit pixels to fg_color") {
    buffer buf{};
    buf.fill(0x00000000U);

    // Space is all-blank; nothing should change.
    draw_text(" ", 0xFFFFFFFFU, buf.data(), kDstW, kDstH, 0, 0);
    CHECK(count_pixels(buf, 0xFFFFFFFFU) == 0);

    // A digit must light at least one fg pixel.
    draw_text("8", 0xFFFFFFFFU, buf.data(), kDstW, kDstH, 0, 0);
    CHECK(count_pixels(buf, 0xFFFFFFFFU) > 0);
}

TEST_CASE("draw_text clips off-screen positions without writing OOB") {
    buffer buf{};
    buf.fill(0xDEADBEEFU);

    // Far-negative / past-right: must not corrupt anything.
    draw_text("HELLO", 0x00112233U, buf.data(), kDstW, kDstH, -1000, -1000);
    draw_text("HELLO", 0x00112233U, buf.data(), kDstW, kDstH, 1000, 1000);
    CHECK(count_pixels(buf, 0x00112233U) == 0);
    CHECK(count_pixels(buf, 0xDEADBEEFU) == kDstW * kDstH);

    // Partial clip: drawing at (-3,-3) still lands some visible pixels.
    draw_text("8", 0x00112233U, buf.data(), kDstW, kDstH, -3, -3);
    CHECK(count_pixels(buf, 0x00112233U) > 0);
}

TEST_CASE("fill_rect clips to the destination bounds") {
    buffer buf{};
    buf.fill(0x00000000U);

    fill_rect(0x11U, buf.data(), kDstW, kDstH, -4, -4, 8, 8);
    CHECK(count_pixels(buf, 0x11U) == 4 * 4);

    buf.fill(0x00000000U);
    fill_rect(0x22U, buf.data(), kDstW, kDstH, kDstW - 4, kDstH - 4, 16, 16);
    CHECK(count_pixels(buf, 0x22U) == 4 * 4);

    buf.fill(0x00000000U);
    fill_rect(0x33U, buf.data(), kDstW, kDstH, 0, 0, kDstW, kDstH);
    CHECK(count_pixels(buf, 0x33U) == kDstW * kDstH);
}

TEST_CASE("unsupported codepoints render as the tofu fallback") {
    buffer buf{};
    buf.fill(0x00000000U);
    // 0x01 is a control character -- the font doesn't cover it.
    const char tofu_char = static_cast<char>(0x01);
    const std::string_view tofu{&tofu_char, 1};
    draw_text(tofu, 0xFFFFFFFFU, buf.data(), kDstW, kDstH, 0, 0);
    // The hollow-square fallback is 6 pixels wide x 7 tall:
    //   top row + bottom row = 6 each,
    //   5 middle rows each contribute 2 (the left + right edge pixels).
    //   Total: 6 + 6 + 5 * 2 = 22 lit pixels.
    CHECK(count_pixels(buf, 0xFFFFFFFFU) == 22);
}
