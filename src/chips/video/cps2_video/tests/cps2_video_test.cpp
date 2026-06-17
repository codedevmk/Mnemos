#include "cps2_video.hpp"

#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::video::cps2_video;
    using reset_kind = mnemos::chips::reset_kind;

    void put16(std::vector<std::uint8_t>& mem, std::size_t at, std::uint16_t v) {
        mem[at] = static_cast<std::uint8_t>(v >> 8U); // big-endian
        mem[at + 1U] = static_cast<std::uint8_t>(v);
    }
} // namespace

TEST_CASE("cps2 video decodes the 16-bit brightness:R:G:B colour", "[cps2_video]") {
    // brightness 0xF, R 0xF -> 0xF*0x11*(0x0F+0x1E)/0x2D = 0xFF (full red).
    CHECK(cps2_video::decode_color(0xFF00U) == 0x00FF0000U);
    CHECK(cps2_video::decode_color(0xF0F0U) == 0x0000FF00U); // full green
    CHECK(cps2_video::decode_color(0xF00FU) == 0x000000FFU); // full blue
    CHECK(cps2_video::decode_color(0x0000U) == 0x00000000U); // black
    // brightness 0, all channels max -> dimmed grey (0xF*0x11*0x0F/0x2D = 0x55).
    CHECK(cps2_video::decode_color(0x0FFFU) == 0x00555555U);
}

TEST_CASE("cps2 video DMAs the palette from video RAM and reports it back", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x10000U, 0U);
    put16(vram, 0x0000U, 0xFF00U); // colour 0 = full red
    put16(vram, 0x0002U, 0xF00FU); // colour 1 = full blue
    video.attach_video_ram(vram);

    video.copy_palette(0x0000U, 0x003FU); // all six pages
    CHECK(video.palette_color(0U) == 0xFF00U);
    CHECK(video.palette_color(1U) == 0xF00FU);
}

TEST_CASE("cps2 video renders the decoded backdrop into the framebuffer", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x10000U, 0U);
    put16(vram, 0x0000U, 0xFF00U); // backdrop = full red
    video.attach_video_ram(vram);

    const std::uint64_t before = video.frame_index();
    video.render(0x0000U, 0x003FU);
    const auto fb = video.framebuffer();
    REQUIRE(fb.width == 384U);
    REQUIRE(fb.height == 224U);
    REQUIRE(fb.pixels != nullptr);
    CHECK(fb.pixels[0] == 0x00FF0000U);
    CHECK(fb.pixels[fb.width * fb.height - 1U] == 0x00FF0000U);
    CHECK(video.frame_index() == before + 1U);
}

TEST_CASE("cps2 video palette-control gates which pages copy", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x10000U, 0U);
    put16(vram, 0x0000U, 0xFF00U); // red at the source base
    video.attach_video_ram(vram);

    // Only page 1 enabled. Page 0 of the palette stays black; page 1 is copied --
    // and because no earlier page copied, the source has NOT advanced, so page 1
    // reads from the source base (the reference's source-advance quirk).
    video.copy_palette(0x0000U, 0x0002U);
    CHECK(video.palette_color(0U) == 0x0000U);     // page 0 not copied (black)
    CHECK(video.palette_color(0x200U) == 0xFF00U); // page 1 = palette byte 0x400 = source base
}

TEST_CASE("cps2 video save/load round-trips the palette + frame index", "[cps2_video]") {
    cps2_video video;
    std::vector<std::uint8_t> vram(0x10000U, 0U);
    put16(vram, 0x0000U, 0x1234U);
    video.attach_video_ram(vram);
    video.render(0x0000U, 0x003FU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    video.save_state(writer);

    cps2_video restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.palette_color(0U) == 0x1234U);
    CHECK(restored.frame_index() == video.frame_index());
}
