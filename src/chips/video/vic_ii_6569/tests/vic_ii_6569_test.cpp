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

    vic.write(0x21U, 0x05U);         // background colour 0 (4-bit register)
    CHECK(vic.read(0x21U) == 0xF5U); // high nibble of a colour register reads 1
    CHECK(vic.read(0x61U) == 0xF5U); // $D061 mirrors $D021 (address & 0x3F)

    // $D02F..$D03F decode to all-ones.
    CHECK(vic.read(0x2FU) == 0xFFU);
    CHECK(vic.read(0x3FU) == 0xFFU);
    vic.write(0x2FU, 0x12U); // dropped
    CHECK(vic.read(0x2FU) == 0xFFU);
}

TEST_CASE("vic_ii_6569 reads unused register bits as 1") {
    vic_ii_6569 vic;

    // Control register 2 ($D016): bits 6,7 are unused and read 1.
    vic.write(0x16U, 0x08U);
    CHECK(vic.read(0x16U) == 0xC8U);

    // Memory pointers ($D018): bit 0 is unused and reads 1.
    vic.write(0x18U, 0x14U);
    CHECK(vic.read(0x18U) == 0x15U);

    // Every colour register $D020-$D02E is 4-bit; the high nibble reads 1.
    for (std::uint8_t reg = 0x20U; reg <= 0x2EU; ++reg) {
        vic.write(reg, 0x0AU);
        CHECK(vic.read(reg) == 0xFAU);
    }

    // Fully-used registers are unaffected (e.g. sprite enable $D015).
    vic.write(0x15U, 0x5AU);
    CHECK(vic.read(0x15U) == 0x5AU);
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
    CHECK(vic.read(0x21U) == 0xF0U); // cleared register, high nibble still reads 1
    CHECK_FALSE(vic.is_pal());       // revision survives reset
    CHECK(vic.cycles_per_line() == 65U);
}

TEST_CASE("vic_ii_6569 exposes its registers through i_mmio") {
    vic_ii_6569 vic;
    mnemos::chips::i_mmio& mmio = vic;
    mmio.mmio_write(0x21U, 0x0EU);         // background colour 0 (4-bit register)
    CHECK(mmio.mmio_read(0x21U) == 0xFEU); // high nibble reads 1
    CHECK(vic.read(0x21U) == 0xFEU);
    CHECK(mmio.mmio_read(0x61U) == 0xFEU); // mirror within the 1KB window

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

TEST_CASE("vic_ii_6569 latches the last fetched byte for the floating bus") {
    vic_ii_6569 vic;
    CHECK(vic.last_fetched_byte() == 0xFFU); // floats high until the first fetch

    // Fill every byte the VIC pulls off the main bus (video matrix + character
    // data) with one value, so the latch is deterministic whatever the last
    // access was. Colour RAM rides a separate nibble bus and is not latched.
    std::vector<std::uint8_t> ram(0x10000U, 0x5AU);
    std::vector<std::uint8_t> char_rom(0x1000U, 0x5AU);
    std::vector<std::uint8_t> color_ram(0x0400U, 0x00U);
    vic.attach_memory({.ram = std::span<const std::uint8_t>(ram),
                       .char_rom = std::span<const std::uint8_t>(char_rom),
                       .color_ram = std::span<const std::uint8_t>(color_ram)});
    vic.set_bank(0U);
    vic.write(0x11U, 0x1BU); // DEN=1 -> the display fetches run
    vic.write(0x18U, 0x14U); // screen $0400, char gen $1000 (char-ROM shadow)

    vic.tick(63U * 312U); // a full PAL frame drives the fetches
    CHECK(vic.last_fetched_byte() == 0x5AU);
}

TEST_CASE("vic_ii_6569 fires the IRQ callback on raster assert and acknowledge") {
    vic_ii_6569 vic;
    bool line = false;
    int edges = 0;
    vic.set_irq_callback([&](bool asserted) {
        line = asserted;
        ++edges;
    });

    vic.write(0x1AU, 0x01U); // enable the raster IRQ source in the mask
    vic.write(0x12U, 0x40U); // compare line 64
    CHECK_FALSE(line);

    vic.set_raster(0x40U); // beam reaches the compare line -> /IRQ asserts
    CHECK(line);
    CHECK(vic.irq_asserted());
    CHECK(edges == 1);

    vic.write(0x19U, 0x01U); // write-1 acknowledge -> /IRQ releases
    CHECK_FALSE(line);
    CHECK(edges == 2);
}

namespace {
    // A VIC plus its fetch memory, kept alive together for the sprite tests. The
    // spans the VIC borrows point into these vectors.
    struct sprite_fixture final {
        vic_ii_6569 vic;
        std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(0x10000U, 0U);
        std::vector<std::uint8_t> char_rom = std::vector<std::uint8_t>(0x1000U, 0U);
        std::vector<std::uint8_t> color_ram = std::vector<std::uint8_t>(0x0400U, 0U);

        sprite_fixture() {
            vic.attach_memory({.ram = std::span<const std::uint8_t>(ram),
                               .char_rom = std::span<const std::uint8_t>(char_rom),
                               .color_ram = std::span<const std::uint8_t>(color_ram)});
            vic.set_bank(0U);
        }
        // Point sprite `i` at data block `pointer` (data at pointer*64), via the
        // sprite pointer at the default video-matrix base ($0000 + $3F8 + i).
        void set_pointer(std::uint8_t i, std::uint8_t pointer) { ram[0x03F8U + i] = pointer; }
        std::uint32_t pixel(std::uint16_t x, std::uint16_t y) {
            return vic.framebuffer().pixels[static_cast<std::size_t>(y) * 504U + x];
        }
    };

    constexpr std::uint32_t frame = 63U * 312U;
} // namespace

TEST_CASE("vic_ii_6569 renders a hi-res sprite at its position") {
    sprite_fixture fx;
    fx.set_pointer(0U, 0x80U); // sprite 0 data at $2000
    fx.ram[0x2000U] = 0x80U;   // row 0: only the leftmost pixel set

    fx.vic.write(0x15U, 0x01U); // enable sprite 0
    fx.vic.write(0x27U, 0x07U); // sprite 0 colour = yellow
    fx.vic.write(0x00U, 100U);  // sprite 0 X = 100
    fx.vic.write(0x01U, 60U);   // sprite 0 Y = 60 (raster line 60)
    fx.vic.tick(frame);

    CHECK(fx.pixel(100U, 60U) == vic_ii_6569::color_rgb888(0x07U)); // the lit pixel
    CHECK(fx.pixel(101U, 60U) != vic_ii_6569::color_rgb888(0x07U)); // next is transparent
    CHECK(fx.pixel(100U, 81U) != vic_ii_6569::color_rgb888(0x07U)); // line below the 21-row sprite
}

TEST_CASE("vic_ii_6569 disabled sprites do not draw") {
    sprite_fixture fx;
    fx.set_pointer(0U, 0x80U);
    fx.ram[0x2000U] = 0xFFU;    // a full row, but...
    fx.vic.write(0x15U, 0x00U); // ...sprite 0 disabled
    fx.vic.write(0x27U, 0x07U);
    fx.vic.write(0x00U, 100U);
    fx.vic.write(0x01U, 60U);
    fx.vic.tick(frame);
    CHECK(fx.pixel(100U, 60U) != vic_ii_6569::color_rgb888(0x07U));
}

TEST_CASE("vic_ii_6569 renders a multicolour sprite") {
    sprite_fixture fx;
    fx.set_pointer(0U, 0x80U);
    fx.ram[0x2000U] = 0x6CU; // pairs (MSB first): 01,10,11,00

    fx.vic.write(0x15U, 0x01U); // enable sprite 0
    fx.vic.write(0x1CU, 0x01U); // sprite 0 multicolour
    fx.vic.write(0x25U, 0x01U); // shared MC0 = white
    fx.vic.write(0x26U, 0x02U); // shared MC1 = red
    fx.vic.write(0x27U, 0x07U); // sprite 0 colour = yellow
    fx.vic.write(0x00U, 100U);
    fx.vic.write(0x01U, 60U);
    fx.vic.tick(frame);

    CHECK(fx.pixel(100U, 60U) == vic_ii_6569::color_rgb888(0x01U)); // pair 01 -> MC0
    CHECK(fx.pixel(101U, 60U) == vic_ii_6569::color_rgb888(0x01U)); // dots are 2 px wide
    CHECK(fx.pixel(102U, 60U) == vic_ii_6569::color_rgb888(0x07U)); // pair 10 -> sprite colour
    CHECK(fx.pixel(104U, 60U) == vic_ii_6569::color_rgb888(0x02U)); // pair 11 -> MC1
    CHECK(fx.pixel(106U, 60U) != vic_ii_6569::color_rgb888(0x01U)); // pair 00 -> transparent
}

TEST_CASE("vic_ii_6569 expands sprites in X and Y") {
    SECTION("X expansion doubles pixel width") {
        sprite_fixture fx;
        fx.set_pointer(0U, 0x80U);
        fx.ram[0x2000U] = 0x80U;    // leftmost pixel
        fx.vic.write(0x15U, 0x01U); // enable
        fx.vic.write(0x1DU, 0x01U); // sprite 0 X-expand
        fx.vic.write(0x27U, 0x07U);
        fx.vic.write(0x00U, 100U);
        fx.vic.write(0x01U, 60U);
        fx.vic.tick(frame);
        CHECK(fx.pixel(100U, 60U) == vic_ii_6569::color_rgb888(0x07U));
        CHECK(fx.pixel(101U, 60U) == vic_ii_6569::color_rgb888(0x07U)); // doubled
        CHECK(fx.pixel(102U, 60U) != vic_ii_6569::color_rgb888(0x07U));
    }
    SECTION("Y expansion doubles row height") {
        sprite_fixture fx;
        fx.set_pointer(0U, 0x80U);
        fx.ram[0x2000U] = 0x80U;    // sprite row 0
        fx.vic.write(0x15U, 0x01U); // enable
        fx.vic.write(0x17U, 0x01U); // sprite 0 Y-expand
        fx.vic.write(0x27U, 0x07U);
        fx.vic.write(0x00U, 100U);
        fx.vic.write(0x01U, 60U);
        fx.vic.tick(frame);
        CHECK(fx.pixel(100U, 60U) == vic_ii_6569::color_rgb888(0x07U)); // row 0, line 0
        CHECK(fx.pixel(100U, 61U) == vic_ii_6569::color_rgb888(0x07U)); // row 0 doubled
    }
}

TEST_CASE("vic_ii_6569 honours sprite-background priority") {
    sprite_fixture fx;
    // Hi-res text: cell 0 = char code 1; glyph row 0 lights the leftmost pixel,
    // which lands at the display origin (framebuffer x 24, y 51).
    fx.ram[0x0400U] = 0x01U;       // screen base $0400 (from $D018=$18), cell 0
    fx.ram[0x2008U] = 0x80U;       // char base $2000, code 1 row 0
    fx.color_ram[0x0000U] = 0x0AU; // cell 0 foreground colour
    // Sprite 0 over that same pixel.
    fx.ram[0x07F8U] = 0x90U; // sprite pointer (vm base $0400 + $3F8)
    fx.ram[0x2400U] = 0x80U; // sprite data row 0: leftmost pixel

    fx.vic.write(0x11U, 0x1BU); // DEN=1, RSEL=1
    fx.vic.write(0x16U, 0x08U); // CSEL=1
    fx.vic.write(0x18U, 0x18U); // screen $0400, char gen $2000 (RAM)
    fx.vic.write(0x15U, 0x01U); // enable sprite 0
    fx.vic.write(0x27U, 0x02U); // sprite colour = red
    fx.vic.write(0x00U, 24U);   // sprite X = 24 (display left)
    fx.vic.write(0x01U, 51U);   // sprite Y = 51 (display top)

    SECTION("sprite behind a foreground pixel is hidden") {
        fx.vic.write(0x1BU, 0x01U); // sprite 0 behind the background
        fx.vic.tick(frame);
        CHECK(fx.pixel(24U, 51U) == vic_ii_6569::color_rgb888(0x0AU)); // char wins
    }
    SECTION("sprite in front of the background shows") {
        fx.vic.write(0x1BU, 0x00U); // sprite 0 in front
        fx.vic.tick(frame);
        CHECK(fx.pixel(24U, 51U) == vic_ii_6569::color_rgb888(0x02U)); // sprite wins
    }
}

TEST_CASE("vic_ii_6569 detects sprite-sprite collisions") {
    sprite_fixture fx;
    fx.set_pointer(0U, 0x80U);
    fx.ram[0x2000U] = 0x80U; // sprite 0 data ($2000): leftmost pixel
    fx.set_pointer(1U, 0x81U);
    fx.ram[0x2040U] = 0x80U; // sprite 1 data ($2040): leftmost pixel

    fx.vic.write(0x15U, 0x03U); // enable sprites 0 and 1
    fx.vic.write(0x27U, 0x07U); // colours
    fx.vic.write(0x28U, 0x02U);
    fx.vic.write(0x00U, 100U); // both sprites at the same spot
    fx.vic.write(0x01U, 60U);
    fx.vic.write(0x02U, 100U);
    fx.vic.write(0x03U, 60U);
    fx.vic.write(0x1AU, 0x04U); // enable the sprite-sprite (IMMC) IRQ
    fx.vic.tick(frame);

    CHECK(fx.vic.irq_asserted());
    CHECK(fx.vic.read(0x1EU) == 0x03U); // both sprites flagged
    CHECK(fx.vic.read(0x1EU) == 0x00U); // reading the latch clears it
    // No foreground background under the sprites (DEN off) -> no data collision.
    CHECK(fx.vic.read(0x1FU) == 0x00U);
}

TEST_CASE("vic_ii_6569 detects sprite-data collisions") {
    sprite_fixture fx;
    // A foreground char pixel at the display origin (framebuffer 24,51).
    fx.ram[0x0400U] = 0x01U;
    fx.ram[0x2008U] = 0x80U;
    fx.color_ram[0x0000U] = 0x0AU;
    fx.ram[0x07F8U] = 0x90U; // sprite 0 pointer (vm base $0400 + $3F8)
    fx.ram[0x2400U] = 0x80U; // sprite 0 data row 0

    fx.vic.write(0x11U, 0x1BU); // DEN=1, RSEL=1
    fx.vic.write(0x16U, 0x08U); // CSEL=1
    fx.vic.write(0x18U, 0x18U); // screen $0400, char gen $2000 (RAM)
    fx.vic.write(0x15U, 0x01U); // enable sprite 0
    fx.vic.write(0x27U, 0x02U); // sprite colour
    fx.vic.write(0x00U, 24U);   // sprite over the foreground pixel
    fx.vic.write(0x01U, 51U);
    fx.vic.write(0x1AU, 0x02U); // enable the sprite-data (IMBC) IRQ
    fx.vic.tick(frame);

    CHECK(fx.vic.irq_asserted());
    CHECK(fx.vic.read(0x1FU) == 0x01U); // sprite 0 hit foreground graphics
    CHECK(fx.vic.read(0x1FU) == 0x00U); // read-clear
}

TEST_CASE("vic_ii_6569 renders standard bitmap mode") {
    sprite_fixture fx;
    fx.ram[0x0400U] = 0x1AU; // cell colours: hi nibble 1 (set), lo nibble A (clear)
    fx.ram[0x2000U] = 0x80U; // bitmap cell 0 row 0: leftmost pixel set

    fx.vic.write(0x11U, 0x3BU); // DEN + BMM + RSEL
    fx.vic.write(0x16U, 0x08U); // CSEL
    fx.vic.write(0x18U, 0x18U); // screen $0400, bitmap base $2000
    fx.vic.tick(frame);

    CHECK(fx.pixel(24U, 51U) == vic_ii_6569::color_rgb888(0x01U)); // set bit -> hi nibble
    CHECK(fx.pixel(25U, 51U) == vic_ii_6569::color_rgb888(0x0AU)); // clear bit -> lo nibble
}

TEST_CASE("vic_ii_6569 renders multicolour bitmap mode") {
    sprite_fixture fx;
    fx.ram[0x0400U] = 0x1AU;       // scr hi nibble 1, lo nibble A
    fx.color_ram[0x0000U] = 0x05U; // colour RAM (pair 11)
    fx.ram[0x2000U] = 0x1BU;       // pairs (MSB first): 00, 01, 10, 11

    fx.vic.write(0x11U, 0x3BU); // DEN + BMM + RSEL
    fx.vic.write(0x16U, 0x18U); // MCM + CSEL
    fx.vic.write(0x18U, 0x18U); // screen $0400, bitmap base $2000
    fx.vic.write(0x21U, 0x06U); // background colour 0
    fx.vic.tick(frame);

    CHECK(fx.pixel(24U, 51U) == vic_ii_6569::color_rgb888(0x06U)); // 00 -> bg0
    CHECK(fx.pixel(26U, 51U) == vic_ii_6569::color_rgb888(0x01U)); // 01 -> scr hi nibble
    CHECK(fx.pixel(28U, 51U) == vic_ii_6569::color_rgb888(0x0AU)); // 10 -> scr lo nibble
    CHECK(fx.pixel(30U, 51U) == vic_ii_6569::color_rgb888(0x05U)); // 11 -> colour RAM
}

TEST_CASE("vic_ii_6569 renders extended-colour text mode") {
    sprite_fixture fx;
    fx.ram[0x0400U] = 0x41U;       // code 1 with background-select bits = 01
    fx.ram[0x2008U] = 0x80U;       // char base $2000, glyph 1 row 0
    fx.color_ram[0x0000U] = 0x03U; // foreground colour

    fx.vic.write(0x11U, 0x5BU); // DEN + ECM + RSEL
    fx.vic.write(0x16U, 0x08U); // CSEL
    fx.vic.write(0x18U, 0x18U); // screen $0400, char gen $2000
    fx.vic.write(0x21U, 0x02U); // bg0 (distinct from bg1)
    fx.vic.write(0x22U, 0x09U); // bg1 (selected by code bits 6-7 = 01)
    fx.vic.tick(frame);

    CHECK(fx.pixel(24U, 51U) == vic_ii_6569::color_rgb888(0x03U)); // set bit -> colour RAM
    CHECK(fx.pixel(25U, 51U) == vic_ii_6569::color_rgb888(0x09U)); // clear bit -> bg1
}
