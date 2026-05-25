#include <mnemos/chips/video/vic_ii_6569.hpp>

#include <mnemos/chips/common/chip_registry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::video::vic_ii_6569;
    using status = mnemos::chips::reset_kind;
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::i_video, vic_ii_6569>);
static_assert(vic_ii_6569::static_class == mnemos::chips::chip_class::video);

TEST_CASE("vic_ii_6569 reports its identity and registers under mos.6569") {
    const vic_ii_6569 vic;
    const auto md = vic.metadata();
    CHECK(md.manufacturer == "MOS Technology");
    CHECK(md.part_number == "6569");
    CHECK(md.family == "VIC-II");
    CHECK(md.klass == mnemos::chips::chip_class::video);

    const auto* descriptor = mnemos::chips::find_factory("mos.6569");
    REQUIRE(descriptor != nullptr);
    CHECK(descriptor->klass == mnemos::chips::chip_class::video);
    auto chip = mnemos::chips::create_chip("mos.6569");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == "6569");
}

TEST_CASE("vic_ii_6569 register window mirrors and unused range reads 0xFF") {
    vic_ii_6569 vic;

    vic.write(0x21U, 0x05U); // background colour 0
    CHECK(vic.read(0x21U) == 0x05U);
    CHECK(vic.read(0x61U) == 0x05U); // $D061 mirrors $D021 (address & 0x3F)

    // $D02F..$D03F decode to all-ones.
    CHECK(vic.read(0x2FU) == 0xFFU);
    CHECK(vic.read(0x3FU) == 0xFFU);
    vic.write(0x2FU, 0x12U); // dropped
    CHECK(vic.read(0x2FU) == 0xFFU);
}

TEST_CASE("vic_ii_6569 decodes SCROLY and SCROLX mode bits") {
    vic_ii_6569 vic;
    vic.write(0x11U, 0x6BU); // ECM | BMM | DEN... 0x6B = 0110_1011: ECM,BMM,RSEL,yscroll=3
    vic.write(0x16U, 0x18U); // 0001_1000: MCM, CSEL, xscroll=0

    const auto& m = vic.modes();
    CHECK(m.ecm);
    CHECK(m.bmm);
    CHECK_FALSE(m.den);
    CHECK(m.rsel);
    CHECK(m.yscroll == 3U);
    CHECK(m.mcm);
    CHECK(m.csel);
    CHECK(m.xscroll == 0U);
}

TEST_CASE("vic_ii_6569 builds 9-bit sprite X from low byte and MSIGX") {
    vic_ii_6569 vic;
    vic.write(0x00U, 0x40U); // SP0X low
    vic.write(0x10U, 0x01U); // MSIGX: sprite 0 high bit
    CHECK(vic.sprite_x(0) == 0x140U);
    vic.write(0x02U, 0x80U); // SP1X low, no MSB
    CHECK(vic.sprite_x(1) == 0x080U);
    vic.write(0x03U, 0x32U); // SP1Y
    CHECK(vic.sprite_y(1) == 0x32U);
}

TEST_CASE("vic_ii_6569 raster compare combines RASTER and SCROLY bit 7") {
    vic_ii_6569 vic;
    vic.write(0x12U, 0x2AU); // RASTER low = $2A
    CHECK(vic.raster_compare() == 0x2AU);
    vic.write(0x11U, 0x80U); // SCROLY bit7 -> compare MSB
    CHECK(vic.raster_compare() == 0x12AU);
}

TEST_CASE("vic_ii_6569 raster readback exposes the live beam line") {
    vic_ii_6569 vic;
    vic.set_raster(0x12AU);                 // line 298
    CHECK(vic.read(0x12U) == 0x2AU);        // RASTER low byte
    CHECK((vic.read(0x11U) & 0x80U) != 0U); // SCROLY bit7 = raster bit 8
}

TEST_CASE("vic_ii_6569 PAL geometry advances the beam and wraps the frame") {
    vic_ii_6569 vic;
    CHECK(vic.cycles_per_line() == 63U);
    CHECK(vic.total_lines() == 312U);

    vic.tick(63U);
    CHECK(vic.raster_y() == 1U);
    CHECK(vic.raster_x() == 0U);

    vic.tick(63U * 311U); // complete the frame
    CHECK(vic.raster_y() == 0U);
}

TEST_CASE("vic_ii_6569 NTSC revision changes geometry") {
    vic_ii_6569 vic;
    vic.set_revision(vic_ii_6569::revision::ntsc_6567r8);
    CHECK_FALSE(vic.is_pal());
    CHECK(vic.cycles_per_line() == 65U);
    CHECK(vic.total_lines() == 263U);
}

TEST_CASE("vic_ii_6569 raises a masked raster IRQ at the compare line") {
    vic_ii_6569 vic;
    vic.write(0x12U, 0x64U); // compare line 100
    vic.write(0x1AU, 0x01U); // enable raster IRQ in the mask
    CHECK_FALSE(vic.irq_asserted());

    vic.set_raster(0x64U); // beam reaches the compare line
    CHECK(vic.irq_asserted());
    CHECK((vic.read(0x19U) & 0x01U) != 0U); // raster source latched

    vic.write(0x19U, 0x01U); // write-1 acknowledge
    CHECK_FALSE(vic.irq_asserted());
}

TEST_CASE("vic_ii_6569 raster IRQ stays masked when not enabled") {
    vic_ii_6569 vic;
    vic.write(0x12U, 0x32U); // compare line 50, mask left clear
    vic.set_raster(0x32U);
    CHECK_FALSE(vic.irq_asserted());        // master not asserted
    CHECK((vic.read(0x19U) & 0x01U) != 0U); // but the source still latches
}

TEST_CASE("vic_ii_6569 VICIRQ/IRQMSK reads set the unused high bits") {
    vic_ii_6569 vic;
    CHECK((vic.read(0x19U) & 0x70U) == 0x70U);
    CHECK((vic.read(0x1AU) & 0xF0U) == 0xF0U);
}

TEST_CASE("vic_ii_6569 detects bad lines and the BA-low window") {
    vic_ii_6569 vic;
    vic.write(0x11U, 0x10U); // DEN set, yscroll = 0
    vic.set_raster(0x30U);   // DEN latches at line $30
    CHECK(vic.bad_line_condition());
    CHECK_FALSE(vic.ba_low()); // raster_x = 0, outside [12,54]

    vic.tick(12U); // raster_x = 12: BA goes low, but the CPU isn't stalled yet
    CHECK(vic.raster_y() == 0x30U);
    CHECK(vic.ba_low());
    CHECK_FALSE(vic.cpu_read_stalled()); // stall begins at cycle 15

    vic.tick(3U); // raster_x = 15: CPU reads now stall
    CHECK(vic.ba_low());
    CHECK(vic.cpu_read_stalled());
}

TEST_CASE("vic_ii_6569 light pen latches coordinates and the LP IRQ source") {
    vic_ii_6569 vic;
    vic.write(0x1AU, 0x08U); // enable light-pen IRQ
    vic.trigger_light_pen(0x80U, 0x64U);
    CHECK(vic.read(0x13U) == 0x40U); // LPX = x >> 1
    CHECK(vic.read(0x14U) == 0x64U); // LPY = y low byte
    CHECK(vic.irq_asserted());
    CHECK((vic.read(0x19U) & 0x08U) != 0U);
}

TEST_CASE("vic_ii_6569 exposes the Pepto palette") {
    CHECK(vic_ii_6569::color_rgb888(0U) == 0x00000000U);
    CHECK(vic_ii_6569::color_rgb888(1U) == 0x00FFFFFFU);
    CHECK(vic_ii_6569::color_rgb888(6U) == 0x00352879U);
    CHECK(vic_ii_6569::color_rgb888(0x16U) == 0x00352879U); // index masked to 4 bits
}

TEST_CASE("vic_ii_6569 reset clears runtime state but keeps the revision") {
    vic_ii_6569 vic;
    vic.set_revision(vic_ii_6569::revision::ntsc_6567r8);
    vic.write(0x21U, 0x07U);
    vic.set_raster(120U);

    vic.reset(status::hard);
    CHECK(vic.raster_y() == 0U);
    CHECK(vic.read(0x21U) == 0x00U);
    CHECK_FALSE(vic.is_pal()); // revision survives reset
    CHECK(vic.cycles_per_line() == 65U);
}

TEST_CASE("vic_ii_6569 exposes its registers through i_mmio") {
    vic_ii_6569 vic;
    mnemos::chips::i_mmio& mmio = vic;
    mmio.mmio_write(0x21U, 0x0EU); // background colour 0
    CHECK(mmio.mmio_read(0x21U) == 0x0EU);
    CHECK(vic.read(0x21U) == 0x0EU);
    CHECK(mmio.mmio_read(0x61U) == 0x0EU); // mirror within the 1KB window

    auto chip = mnemos::chips::create_chip("mos.6569");
    REQUIRE(chip != nullptr);
    CHECK(dynamic_cast<mnemos::chips::i_mmio*>(chip.get()) != nullptr);
}

TEST_CASE("vic_ii_6569 register snapshot reports raster + IRQ state") {
    vic_ii_6569 vic;
    vic.set_raster(99U);
    const auto regs = vic.register_snapshot();
    REQUIRE(regs.size() == 5U);
    CHECK(regs[0].name == "RASTER_Y");
    CHECK(regs[0].value == 99U);
    CHECK(regs[3].name == "VICIRQ");
}

TEST_CASE("vic_ii_6569 renders the border colour with no display") {
    vic_ii_6569 vic;
    vic.write(0x20U, 0x0EU); // border = light blue
    vic.tick(63U * 312U);    // one full PAL frame

    const auto fb = vic.framebuffer();
    REQUIRE(fb.width == 504U);
    REQUIRE(fb.height == 312U);
    REQUIRE(fb.pixels != nullptr);
    CHECK(vic.frame_index() == 1U);
    // DEN never armed and no memory attached: the whole raster is border.
    CHECK(fb.pixels[0] == vic_ii_6569::color_rgb888(0x0EU));
    CHECK(fb.pixels[51U * 504U + 24U] == vic_ii_6569::color_rgb888(0x0EU));
}

TEST_CASE("vic_ii_6569 renders hi-res text from attached memory") {
    vic_ii_6569 vic;

    std::vector<std::uint8_t> ram(0x10000U, 0U);
    std::vector<std::uint8_t> char_rom(0x1000U, 0U);
    std::vector<std::uint8_t> color_ram(0x0400U, 0U);

    // Screen cell (0,0) -> char code 0; glyph row 0 of code 0 lights the leftmost
    // pixel only; colour RAM picks white for the foreground.
    ram[0x0400U] = 0x00U;       // video matrix base $0400 (from $D018=$14), cell 0
    char_rom[0x0000U] = 0x80U;  // code 0, row 0: bit 7 set
    color_ram[0x0000U] = 0x01U; // white

    vic.attach_memory({.ram = std::span<const std::uint8_t>(ram),
                       .char_rom = std::span<const std::uint8_t>(char_rom),
                       .color_ram = std::span<const std::uint8_t>(color_ram)});
    vic.set_bank(0U);
    vic.write(0x11U, 0x1BU); // DEN=1, RSEL=1, YSCROLL=3 (default)
    vic.write(0x16U, 0x08U); // CSEL=1
    vic.write(0x18U, 0x14U); // screen $0400, char gen $1000 (char ROM shadow)
    vic.write(0x20U, 0x0EU); // border light blue
    vic.write(0x21U, 0x06U); // background blue

    vic.tick(63U * 312U); // render a full frame

    const auto fb = vic.framebuffer();
    const std::size_t origin = 51U * 504U + 24U;                       // display top-left
    CHECK(fb.pixels[0] == vic_ii_6569::color_rgb888(0x0EU));           // border
    CHECK(fb.pixels[origin] == vic_ii_6569::color_rgb888(0x01U));      // glyph bit set -> white
    CHECK(fb.pixels[origin + 1U] == vic_ii_6569::color_rgb888(0x06U)); // glyph bit clear -> bg
}
