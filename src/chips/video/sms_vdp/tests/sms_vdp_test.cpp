#include "sms_vdp.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::video::sms_vdp;
    using reset_kind = mnemos::chips::reset_kind;

    // Helpers driving the two-byte control-port command protocol.
    void set_addr(sms_vdp& v, std::uint16_t addr, std::uint8_t code) {
        v.ctrl_write(static_cast<std::uint8_t>(addr & 0xFFU));
        v.ctrl_write(static_cast<std::uint8_t>((code << 6U) | ((addr >> 8U) & 0x3FU)));
    }
    void write_reg(sms_vdp& v, int reg, std::uint8_t value) {
        v.ctrl_write(value);
        v.ctrl_write(static_cast<std::uint8_t>(0x80U | reg)); // code 2
    }
    std::uint32_t pixel(const sms_vdp& v, int x, int y) {
        return v.framebuffer().pixels[static_cast<std::size_t>(y) * sms_vdp::fb_width + x];
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::ivideo, sms_vdp>);

TEST_CASE("sms_vdp reports identity and registers under sega.sms_vdp") {
    const sms_vdp vdp;
    const auto md = vdp.metadata();
    CHECK(md.manufacturer == "Sega");
    CHECK(md.family == "VDP");
    CHECK(md.klass == mnemos::chips::chip_class::video);

    auto chip = mnemos::chips::create_chip("sega.sms_vdp");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().family == std::string("VDP"));
}

TEST_CASE("sms_vdp resets to a 192-line NTSC mode") {
    sms_vdp vdp;
    CHECK(vdp.visible_height() == 192);
    CHECK_FALSE(vdp.is_pal());
}

TEST_CASE("sms_vdp register writes go through the control port") {
    sms_vdp vdp;
    write_reg(vdp, 1, 0x60U);
    CHECK(vdp.reg(1) == 0x60U);
}

TEST_CASE("sms_vdp reads and writes VRAM with a one-byte read delay") {
    sms_vdp vdp;
    set_addr(vdp, 0x0000U, 1U); // VRAM write
    vdp.data_write(0xABU);
    vdp.data_write(0xCDU);

    set_addr(vdp, 0x0000U, 0U);      // VRAM read (primes the buffer)
    CHECK(vdp.data_read() == 0xABU); // buffered first byte
    CHECK(vdp.data_read() == 0xCDU);
}

TEST_CASE("sms_vdp renders a Mode-4 tile to the framebuffer") {
    sms_vdp vdp;

    // Tile 1, row 0, plane 0 bit 7 set -> pixel (0,0) of the tile is colour 1.
    set_addr(vdp, 0x0020U, 1U); // tile 1 at 1*32 = $20
    vdp.data_write(0x80U);

    // Name table (default base $3800) entry (0,0) -> tile 1, palette 0.
    set_addr(vdp, 0x3800U, 1U);
    vdp.data_write(0x01U); // tile index low
    vdp.data_write(0x00U); // attributes high

    // CRAM index 1 = red (--BBGGRR: R = 3).
    set_addr(vdp, 0x0001U, 3U); // CRAM write at index 1
    vdp.data_write(0x03U);

    write_reg(vdp, 0, 0x04U); // mode 4, left-column blanking off
    write_reg(vdp, 1, 0xC0U); // display enable (bit 6)

    vdp.tick(static_cast<std::uint64_t>(sms_vdp::cycles_per_line)); // render scanline 0

    CHECK(pixel(vdp, 0, 0) == 0x00FF0000U); // tile pixel -> CRAM[1] red
    CHECK(pixel(vdp, 8, 0) == 0x00000000U); // next tile is blank -> CRAM[0]
}

TEST_CASE("sms_vdp Game Gear mode renders 12-bit BGR444 CRAM colours") {
    sms_vdp vdp;
    vdp.set_gg(true);
    CHECK(vdp.is_gg());

    // Border colour = CRAM entry 16 (reg7 low nibble 0). Write it as a Game Gear
    // 16-bit BGR444 green (g=$F -> entry $00F0): low byte latched, high byte commits.
    set_addr(vdp, 0x0020U, 3U); // GG CRAM byte 32 = entry 16
    vdp.data_write(0xF0U);      // low half (latched)
    vdp.data_write(0x00U);      // high half (commits the entry)

    write_reg(vdp, 7, 0x00U); // border colour = CRAM entry 16
    write_reg(vdp, 1, 0x00U); // display off -> whole line is the border colour

    // Render through to scanline 24 (the top of the 160x144 LCD window).
    vdp.tick(static_cast<std::uint64_t>(sms_vdp::cycles_per_line) * 25U);

    // The viewport's top-left pixel is the 12-bit green (g=$F -> 0x00FF00).
    CHECK(vdp.framebuffer().pixels[0] == 0x0000FF00U);
}

TEST_CASE("sms_vdp Game Gear mode crops the framebuffer to the central 160x144") {
    sms_vdp gg;
    gg.set_gg(true);
    const auto fb = gg.framebuffer();
    CHECK(fb.width == 160U);
    CHECK(fb.height == 144U);
    CHECK(fb.effective_stride() == 256U); // strided sub-view over the full SMS frame

    const sms_vdp sms; // SMS mode is unchanged: full 256x192.
    const auto sfb = sms.framebuffer();
    CHECK(sfb.width == 256U);
    CHECK(sfb.height == 192U);
}

TEST_CASE("sms_vdp raises the frame interrupt at vblank") {
    sms_vdp vdp;
    bool irq = false;
    vdp.set_irq_callback([&](bool asserted) { irq = asserted; });
    write_reg(vdp, 1, 0xE0U); // display + frame interrupt enable (bit 5)

    // Run to the start of vblank (line 192).
    vdp.tick(static_cast<std::uint64_t>(sms_vdp::cycles_per_line) * 193U);
    CHECK(irq);
    CHECK(vdp.irq_asserted());
    CHECK((vdp.status() & 0x80U) != 0U); // frame flag

    const std::uint8_t s = vdp.ctrl_read(); // reading status clears it
    CHECK((s & 0x80U) != 0U);
    CHECK_FALSE(vdp.irq_asserted());
    CHECK_FALSE(irq);
}

TEST_CASE("sms_vdp advances the frame index once per frame") {
    sms_vdp vdp;
    CHECK(vdp.frame_index() == 0U);
    vdp.tick(static_cast<std::uint64_t>(sms_vdp::cycles_per_line) * sms_vdp::scanlines_ntsc);
    CHECK(vdp.frame_index() == 1U);
}

TEST_CASE("sms_vdp round-trips its state") {
    sms_vdp vdp;
    set_addr(vdp, 0x0100U, 1U);
    vdp.data_write(0x55U); // vram[$0100] = 0x55
    write_reg(vdp, 2, 0xFFU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    vdp.save_state(writer);

    sms_vdp restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.reg(2) == 0xFFU);
    // Confirm VRAM survived: read back $0100.
    set_addr(restored, 0x0100U, 0U);
    CHECK(restored.data_read() == 0x55U);
}
