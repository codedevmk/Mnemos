#include "tms9918a.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::video::tms9918a;

    void set_addr(tms9918a& vdp, std::uint16_t addr, std::uint8_t code) {
        vdp.ctrl_write(static_cast<std::uint8_t>(addr & 0xFFU));
        vdp.ctrl_write(static_cast<std::uint8_t>((code << 6U) | ((addr >> 8U) & 0x3FU)));
    }

    void write_reg(tms9918a& vdp, int reg, std::uint8_t value) {
        vdp.ctrl_write(value);
        vdp.ctrl_write(static_cast<std::uint8_t>(0x80U | (reg & 0x07)));
    }

    std::uint32_t pixel(const tms9918a& vdp, int x, int y) {
        const auto fb = vdp.framebuffer();
        return fb.pixels[static_cast<std::size_t>(y) * fb.effective_stride() +
                         static_cast<std::size_t>(x)];
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::ivideo, tms9918a>);

TEST_CASE("tms9918a reports identity and registers under ti.tms9918a") {
    const tms9918a vdp;
    const auto md = vdp.metadata();
    CHECK(md.manufacturer == "Texas Instruments");
    CHECK(md.part_number == "TMS9918A");
    CHECK(md.klass == mnemos::chips::chip_class::video);

    const auto chip = mnemos::chips::create_chip("ti.tms9918a");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == std::string("TMS9918A"));
}

TEST_CASE("tms9918a register writes and buffered VRAM reads use the control port") {
    tms9918a vdp;
    write_reg(vdp, 1, 0x60U);
    CHECK(vdp.reg(1) == 0x60U);

    set_addr(vdp, 0x0100U, 1U);
    vdp.data_write(0xABU);
    vdp.data_write(0xCDU);

    set_addr(vdp, 0x0100U, 0U);
    CHECK(vdp.data_read() == 0xABU);
    CHECK(vdp.data_read() == 0xCDU);
}

TEST_CASE("tms9918a renders Graphics I tiles") {
    tms9918a vdp;

    // Pattern 1 row 0 has the leftmost bit set. Color-table group 0 gives
    // foreground white, background black.
    set_addr(vdp, 0x0008U, 1U);
    vdp.data_write(0x80U);
    set_addr(vdp, 0x0800U, 1U);
    vdp.data_write(0x01U);
    set_addr(vdp, 0x2000U, 1U);
    vdp.data_write(0xF1U);

    write_reg(vdp, 1, 0x40U); // display enable, Graphics I
    write_reg(vdp, 2, 0x02U); // name table $0800
    write_reg(vdp, 3, 0x80U); // color table $2000
    write_reg(vdp, 4, 0x00U); // pattern table $0000
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x00FFFFFFU);
    CHECK(pixel(vdp, 1, 0) == 0x00000000U);
}

TEST_CASE("tms9918a renders Text mode with 40 columns centered in 256 pixels") {
    tms9918a vdp;

    set_addr(vdp, 0x0000U, 1U);
    vdp.data_write(0xFCU); // six text pixels on
    set_addr(vdp, 0x0800U, 1U);
    vdp.data_write(0x00U);
    vdp.data_write(0x01U);

    write_reg(vdp, 1, 0x50U); // display + text mode
    write_reg(vdp, 2, 0x02U);
    write_reg(vdp, 4, 0x00U);
    write_reg(vdp, 7, 0xF1U); // white text on black
    vdp.render_frame();

    CHECK(pixel(vdp, 7, 0) == 0x00000000U);
    CHECK(pixel(vdp, 8, 0) == 0x00FFFFFFU);
    CHECK(pixel(vdp, 13, 0) == 0x00FFFFFFU);
    CHECK(pixel(vdp, 14, 0) == 0x00000000U);
}

TEST_CASE("tms9918a renders Graphics II row-colour patterns") {
    tms9918a vdp;

    set_addr(vdp, 0x0008U, 1U); // pattern 1, page 0, row 0
    vdp.data_write(0x80U);
    set_addr(vdp, 0x0800U, 1U); // name table entry
    vdp.data_write(0x01U);
    set_addr(vdp, 0x2008U, 1U); // colour row for pattern 1
    vdp.data_write(0x61U);      // dark red on black

    write_reg(vdp, 0, 0x02U); // M3 -> Graphics II
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 2, 0x02U);
    write_reg(vdp, 3, 0xFFU);
    write_reg(vdp, 4, 0x03U);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x00D4524DU);
    CHECK(pixel(vdp, 1, 0) == 0x00000000U);
}

TEST_CASE("tms9918a mirrors Graphics II pattern and color tables through register masks") {
    tms9918a vdp;

    set_addr(vdp, 0x0008U, 1U); // pattern 1, mirrored into the middle screen third
    vdp.data_write(0x80U);
    set_addr(vdp, 0x0900U, 1U); // name table row 8, column 0 -> line 64
    vdp.data_write(0x01U);
    set_addr(vdp, 0x2008U, 1U); // color table row mirrored by R#3 mask bits
    vdp.data_write(0xE1U);      // gray on black

    write_reg(vdp, 0, 0x02U); // M3 -> Graphics II
    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 2, 0x02U);
    write_reg(vdp, 3, 0x9FU); // CT at $2000, mirror 2 KiB color table
    write_reg(vdp, 4, 0x00U); // PG at $0000, mirror first 256 patterns
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 64) == 0x00CCCCCCU);
    CHECK(pixel(vdp, 1, 64) == 0x00000000U);
}

TEST_CASE("tms9918a draws sprites and reports fifth-sprite overflow") {
    tms9918a vdp;

    // Sprite pattern 0: top-left pixel set.
    set_addr(vdp, 0x0000U, 1U);
    vdp.data_write(0x80U);

    // Two overflow groups prove the first detected fifth sprite stays latched.
    set_addr(vdp, 0x1B00U, 1U);
    for (int i = 0; i < 5; ++i) {
        vdp.data_write(0xFFU); // y -> line 0
        vdp.data_write(static_cast<std::uint8_t>(8 * i));
        vdp.data_write(0x00U);
        vdp.data_write(0x0FU); // white
    }
    for (int i = 0; i < 5; ++i) {
        vdp.data_write(0x0FU); // y -> line 16
        vdp.data_write(static_cast<std::uint8_t>(8 * i));
        vdp.data_write(0x00U);
        vdp.data_write(0x0FU);
    }
    vdp.data_write(0xD0U); // terminator

    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 5, 0x36U); // SAT $1B00
    write_reg(vdp, 6, 0x00U);
    vdp.render_frame();

    CHECK(pixel(vdp, 0, 0) == 0x00FFFFFFU);
    CHECK((vdp.status() & 0x40U) != 0U);
    CHECK((vdp.status() & 0x1FU) == 4U);

    const std::uint8_t status = vdp.status_read();
    CHECK((status & 0x40U) != 0U);
    CHECK((status & 0x1FU) == 4U);
    CHECK((vdp.status() & 0xE0U) == 0U);
}

TEST_CASE("tms9918a sprite coincidence includes transparent sprites and latches until read") {
    tms9918a vdp;

    set_addr(vdp, 0x0000U, 1U);
    vdp.data_write(0x80U);

    set_addr(vdp, 0x1B00U, 1U);
    vdp.data_write(0xFFU); // transparent sprite on line 0
    vdp.data_write(0x00U);
    vdp.data_write(0x00U);
    vdp.data_write(0x00U);
    vdp.data_write(0xFFU); // visible sprite overlaps the transparent sprite
    vdp.data_write(0x00U);
    vdp.data_write(0x00U);
    vdp.data_write(0x0FU);
    vdp.data_write(0xD0U);

    write_reg(vdp, 1, 0x40U);
    write_reg(vdp, 5, 0x36U);
    write_reg(vdp, 6, 0x00U);

    vdp.render_frame();
    CHECK(pixel(vdp, 0, 0) == 0x00FFFFFFU);
    CHECK((vdp.status() & 0x20U) != 0U);

    vdp.tick(static_cast<std::uint64_t>(tms9918a::cycles_per_line) * tms9918a::scanlines_ntsc);
    CHECK((vdp.status() & 0x20U) != 0U);

    const std::uint8_t status = vdp.status_read();
    CHECK((status & 0x20U) != 0U);
    CHECK((vdp.status() & 0x20U) == 0U);

    vdp.render_frame();
    CHECK((vdp.status() & 0x20U) != 0U);
}

TEST_CASE("tms9918a raises and clears frame interrupt") {
    tms9918a vdp;
    bool irq = false;
    vdp.set_irq_callback([&](bool asserted) { irq = asserted; });
    write_reg(vdp, 1, 0x60U); // display + frame interrupt

    vdp.tick(static_cast<std::uint64_t>(tms9918a::cycles_per_line) *
             (tms9918a::display_height + 1));
    CHECK(irq);
    CHECK((vdp.status() & 0x80U) != 0U);

    const std::uint8_t status = vdp.status_read();
    CHECK((status & 0x80U) != 0U);
    CHECK_FALSE(irq);
}

TEST_CASE("tms9918a round-trips state") {
    tms9918a vdp;
    set_addr(vdp, 0x0123U, 1U);
    vdp.data_write(0x5AU);
    write_reg(vdp, 7, 0xF1U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    vdp.save_state(writer);

    tms9918a restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.reg(7) == 0xF1U);
    set_addr(restored, 0x0123U, 0U);
    CHECK(restored.data_read() == 0x5AU);
}

TEST_CASE("tms9918a exposes VRAM and VDP registers through introspection") {
    tms9918a vdp;
    set_addr(vdp, 0x0123U, 1U);
    vdp.data_write(0x5AU);
    write_reg(vdp, 7, 0xF1U);

    auto& intro = vdp.introspection();
    const auto memories = intro.memory_views();
    REQUIRE(memories.size() == 2U);
    REQUIRE(memories[0] != nullptr);
    REQUIRE(memories[1] != nullptr);
    CHECK(memories[0]->name() == "vram");
    CHECK(memories[0]->bytes()[0x0123U] == 0x5AU);
    CHECK(memories[1]->name() == "registers");
    CHECK(memories[1]->bytes()[7U] == 0xF1U);

    auto* registers = intro.registers();
    REQUIRE(registers != nullptr);
    const auto snapshot = registers->registers();
    REQUIRE(snapshot.size() >= 13U);
    CHECK(snapshot[7].name == "R7");
    CHECK(snapshot[7].value == 0xF1U);
    CHECK(snapshot[8].name == "STATUS");
}
