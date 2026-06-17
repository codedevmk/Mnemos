#include "denise.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::video::denise;

    constexpr std::uint64_t frame_ticks =
        static_cast<std::uint64_t>(denise::line_pixels) * denise::frame_lines;
    constexpr std::uint32_t stride = (denise::visible_width + 7U) / 8U; // 40 bytes/row

    // One packed 1bpp bitplane (visible_width x visible_height bits). The
    // predicate decides whether the bit at (x, y) is set (MSB = leftmost).
    template <typename Pred> [[nodiscard]] std::vector<std::uint8_t> make_plane(Pred set) {
        std::vector<std::uint8_t> bits(static_cast<std::size_t>(stride) * denise::visible_height,
                                       0U);
        for (std::uint32_t y = 0; y < denise::visible_height; ++y) {
            for (std::uint32_t x = 0; x < denise::visible_width; ++x) {
                if (set(x, y)) {
                    bits[static_cast<std::size_t>(y) * stride + (x >> 3U)] |=
                        static_cast<std::uint8_t>(0x80U >> (x & 7U));
                }
            }
        }
        return bits;
    }

} // namespace

TEST_CASE("denise registers through the chip registry", "[denise]") {
    auto chip = mnemos::chips::create_chip("commodore.denise");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::video);
}

TEST_CASE("denise expands 4-bit guns to 8 bits like the hardware DAC", "[denise]") {
    denise video;
    video.write_color(1U, 0x0F00U); // pure red
    video.write_color(2U, 0x00F0U); // pure green
    video.write_color(3U, 0x000FU); // pure blue
    video.write_color(4U, 0x0555U); // mid grey: 0x5 -> 0x55 per channel
    video.write_color(5U, 0x1FFFU); // high bits above 11:0 must drop

    CHECK(video.palette_rgb888(1U) == 0x00FF0000U);
    CHECK(video.palette_rgb888(2U) == 0x0000FF00U);
    CHECK(video.palette_rgb888(3U) == 0x000000FFU);
    CHECK(video.palette_rgb888(4U) == 0x00555555U);
    CHECK(video.read_color(5U) == 0x0FFFU); // top nibble dropped at write
    CHECK(video.palette_rgb888(5U) == 0x00FFFFFFU);
}

TEST_CASE("denise decodes BPLCON0 into the mode view", "[denise]") {
    denise video;
    // HIRES | BPU=4 | HAM | DPF | COLOR | ECSENA.
    video.write_bplcon0(static_cast<std::uint16_t>(denise::bplcon0_hires | (4U << 12U) |
                                                   denise::bplcon0_ham | denise::bplcon0_dpf |
                                                   denise::bplcon0_color | denise::bplcon0_ecsena));
    const auto& d = video.decoded();
    CHECK(d.hires);
    CHECK(d.bitplane_count == 4U);
    CHECK(d.ham);
    CHECK(d.dual_playfield);
    CHECK(d.color_enable);
    CHECK(d.ecs_enabled);
    CHECK_FALSE(d.interlace);
}

TEST_CASE("denise fires the scanline and vblank callbacks", "[denise]") {
    denise video;
    std::vector<std::uint32_t> vblank_lines;
    std::vector<std::uint32_t> scanlines;
    video.set_vblank_callback([&](std::uint32_t line) { vblank_lines.push_back(line); });
    video.set_scanline_callback([&](std::uint32_t line) { scanlines.push_back(line); });

    video.tick(frame_ticks);
    CHECK(video.frame_index() == 1U);
    REQUIRE(vblank_lines.size() == 1U);
    CHECK(vblank_lines[0] == denise::visible_height);
    REQUIRE(scanlines.size() == denise::frame_lines);
    CHECK(scanlines.front() == 0U);
    CHECK(scanlines.back() == denise::frame_lines - 1U);

    video.tick(frame_ticks);
    CHECK(video.frame_index() == 2U);
    CHECK(video.beam_line() == 0U);
    CHECK(video.beam_dot() == 0U);
}

TEST_CASE("denise resets the register and beam state", "[denise]") {
    denise video;
    video.write_color(7U, 0x0ABCU);
    video.write_bplcon0(denise::bplcon0_hires);
    video.tick(frame_ticks);
    REQUIRE(video.frame_index() == 1U);

    video.reset(mnemos::chips::reset_kind::hard);
    CHECK(video.frame_index() == 0U);
    CHECK(video.beam_line() == 0U);
    CHECK(video.beam_dot() == 0U);
    CHECK(video.read_color(7U) == 0U);
    CHECK(video.read_bplcon0() == 0U);
    CHECK_FALSE(video.decoded().hires);
    CHECK(video.framebuffer().pixels[0] == 0U);
}

TEST_CASE("denise serializes bitplanes through the colour palette", "[denise]") {
    denise video;
    // Two bitplanes. Plane 0: set only at (0,0). Plane 1: set on the whole
    // top row. So colour index at (0,0) = 0b11 = 3; at (1,0) = 0b10 = 2;
    // at (0,1) = 0b00 = 0.
    const auto plane0 =
        make_plane([](std::uint32_t x, std::uint32_t y) { return x == 0 && y == 0; });
    const auto plane1 = make_plane([](std::uint32_t /*x*/, std::uint32_t y) { return y == 0; });

    video.attach_bitplane(0U, plane0);
    video.attach_bitplane(1U, plane1);
    video.write_bplcon0(static_cast<std::uint16_t>(2U << 12U)); // BPU = 2
    video.write_color(0U, 0x0000U);                             // index 0: black
    video.write_color(2U, 0x00F0U);                             // index 2: green
    video.write_color(3U, 0x0F00U);                             // index 3: red

    video.tick(frame_ticks);
    const auto frame = video.framebuffer();
    REQUIRE(frame.pixels != nullptr);
    CHECK(frame.width == denise::visible_width);
    CHECK(frame.height == denise::visible_height);
    const auto at = [&](std::uint32_t x, std::uint32_t y) {
        return frame.pixels[y * frame.effective_stride() + x];
    };
    CHECK(at(0U, 0U) == 0x00FF0000U); // index 3 -> red
    CHECK(at(1U, 0U) == 0x0000FF00U); // index 2 -> green
    CHECK(at(0U, 1U) == 0x00000000U); // index 0 -> black
}

TEST_CASE("denise extra-half-brite halves the high-index colours", "[denise]") {
    denise video;
    // Six planes. Plane 5 set on the whole frame, plane 0 set on the whole
    // frame -> index bit5 | bit0 = 0x21. EHB: bit5 selects half-bright of
    // colour (0x21 & 0x1F) = 1.
    const auto plane_all = make_plane([](std::uint32_t, std::uint32_t) { return true; });
    const auto plane_none = make_plane([](std::uint32_t, std::uint32_t) { return false; });
    video.attach_bitplane(0U, plane_all);
    video.attach_bitplane(1U, plane_none);
    video.attach_bitplane(2U, plane_none);
    video.attach_bitplane(3U, plane_none);
    video.attach_bitplane(4U, plane_none);
    video.attach_bitplane(5U, plane_all);
    video.write_bplcon0(static_cast<std::uint16_t>(6U << 12U)); // BPU = 6, no HAM/DPF -> EHB
    video.write_color(1U, 0x0FFFU);                             // white

    video.tick(frame_ticks);
    // White 0x00FFFFFF halved per channel -> 0x007F7F7F.
    CHECK(video.framebuffer().pixels[0] == 0x007F7F7FU);
}

TEST_CASE("denise dual-playfield shows the back playfield through index-0 holes", "[denise]") {
    denise video;
    // 4 planes, dual-playfield. Front (playfield 1, odd planes 0/2) index 0
    // everywhere -> transparent. Back (playfield 2, even planes 1/3): plane 1
    // set on the whole frame -> pf2 index 1 -> register 8+1 = 9.
    const auto plane_none = make_plane([](std::uint32_t, std::uint32_t) { return false; });
    const auto plane_all = make_plane([](std::uint32_t, std::uint32_t) { return true; });
    video.attach_bitplane(0U, plane_none); // pf1 bit0
    video.attach_bitplane(1U, plane_all);  // pf2 bit0
    video.attach_bitplane(2U, plane_none); // pf1 bit1
    video.attach_bitplane(3U, plane_none); // pf2 bit1
    video.write_bplcon0(
        static_cast<std::uint16_t>((4U << 12U) | denise::bplcon0_dpf)); // BPU=4, DPF
    video.write_color(9U, 0x000FU);                                     // back colour 1 -> blue

    video.tick(frame_ticks);
    CHECK(video.framebuffer().pixels[0] == 0x000000FFU);
}

TEST_CASE("denise display disable blanks the frame", "[denise]") {
    denise video;
    const auto plane_all = make_plane([](std::uint32_t, std::uint32_t) { return true; });
    video.attach_bitplane(0U, plane_all);
    video.write_bplcon0(static_cast<std::uint16_t>(1U << 12U)); // BPU = 1
    video.write_color(1U, 0x0F00U);                             // red

    video.set_display_enable(false);
    video.tick(frame_ticks);
    CHECK(video.framebuffer().pixels[0] == 0U);

    video.set_display_enable(true);
    video.tick(frame_ticks);
    CHECK(video.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("denise save_state / load_state round-trips", "[denise]") {
    denise source;
    source.write_color(3U, 0x0F0FU);
    source.write_color(31U, 0x0ABCU);
    source.write_bplcon0(static_cast<std::uint16_t>(denise::bplcon0_hires | (5U << 12U)));
    source.write_bplcon1(0x1234U);
    source.write_bplcon2(0x5678U);
    source.write_bplcon3(0x9ABCU);
    source.set_plane_stride(64U);
    source.tick(frame_ticks); // advance frame_index to 1

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    source.save_state(writer);

    denise restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored.frame_index() == source.frame_index());
    CHECK(restored.read_color(3U) == 0x0F0FU);
    CHECK(restored.read_color(31U) == 0x0ABCU);
    CHECK(restored.read_bplcon0() == source.read_bplcon0());
    CHECK(restored.decoded().hires);
    CHECK(restored.decoded().bitplane_count == 5U);
}
