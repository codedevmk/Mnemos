#include "genesis_vdp.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::video::genesis_vdp;
    using reset_kind = mnemos::chips::reset_kind;

    // ---- command/register-port helpers ----
    void set_reg(genesis_vdp& v, int r, std::uint8_t value) {
        v.write16(0x04,
                  static_cast<std::uint16_t>(0x8000U | (static_cast<unsigned>(r) << 8U) | value));
    }

    // Program the command port for an access to `addr` with control `code` (the 6-bit
    // CD field; bit 5 = DMA). Encoded as the two-word control sequence.
    void set_command(genesis_vdp& v, std::uint32_t addr, std::uint8_t code) {
        const auto first = static_cast<std::uint16_t>(((code & 0x03U) << 14U) | (addr & 0x3FFFU));
        const auto second =
            static_cast<std::uint16_t>(((code & 0x3CU) << 2U) | ((addr >> 14U) & 0x03U));
        v.write16(0x04, first);
        v.write16(0x04, second);
    }

    // Write a run of words to VRAM at `addr` (assumes M5 + auto-increment 2 are set).
    void write_vram(genesis_vdp& v, std::uint32_t addr,
                    std::initializer_list<std::uint16_t> words) {
        set_command(v, addr, 0x01);
        for (const auto w : words) {
            v.write16(0x00, w);
        }
    }

    void write_cram(genesis_vdp& v, int idx, std::uint16_t color) {
        set_command(v, static_cast<std::uint32_t>(idx) << 1U, 0x03);
        v.write16(0x00, color);
    }

    // A solid 8x8 tile of colour index 1 (every nibble = 1).
    constexpr std::initializer_list<std::uint16_t> solid_tile_1 = {
        0x1111, 0x1111, 0x1111, 0x1111, 0x1111, 0x1111, 0x1111, 0x1111,
        0x1111, 0x1111, 0x1111, 0x1111, 0x1111, 0x1111, 0x1111, 0x1111};

    // Max-intensity CRAM channel (3-bit value 7) renders to 0xEF after the
    // 5:6:5 round-trip in cram_to_rgb (R/B 5-bit, G 6-bit).
    constexpr std::uint32_t rgb_red = 0x00EF0000U;
    constexpr std::uint32_t rgb_green = 0x0000EF00U;
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::ivideo, genesis_vdp>);
static_assert(std::is_base_of_v<mnemos::chips::immio, genesis_vdp>);

TEST_CASE("genesis_vdp reports identity and registers under sega.315_5313") {
    const genesis_vdp vdp;
    const auto md = vdp.metadata();
    CHECK(md.manufacturer == "Sega");
    CHECK(md.part_number == "315-5313");
    CHECK(md.klass == mnemos::chips::chip_class::video);

    auto chip = mnemos::chips::create_chip("sega.315_5313");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == "315-5313");
}

TEST_CASE("genesis_vdp writes VRAM through the data port") {
    genesis_vdp vdp;
    set_command(vdp, 0x1000, 0x01); // VRAM write
    vdp.write16(0x00, 0x1234);
    CHECK(vdp.vram16(0x1000) == 0x1234);
}

TEST_CASE("genesis_vdp auto-increments the data address") {
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x04);  // M5: unlock registers above R10
    set_reg(vdp, 15, 0x02); // auto-increment 2
    set_command(vdp, 0x2000, 0x01);
    vdp.write16(0x00, 0xAAAA);
    vdp.write16(0x00, 0xBBBB);
    CHECK(vdp.vram16(0x2000) == 0xAAAA);
    CHECK(vdp.vram16(0x2002) == 0xBBBB);
}

TEST_CASE("genesis_vdp writes CRAM and VSRAM with the hardware masks") {
    genesis_vdp vdp;
    set_reg(vdp, 15, 0x02);
    set_command(vdp, 0x0000, 0x03); // CRAM write
    vdp.write16(0x00, 0xFFFF);
    CHECK(vdp.cram(0) == 0x0EEE); // CRAM keeps only the 3x3-bit colour bits

    set_command(vdp, 0x0000, 0x05); // VSRAM write
    vdp.write16(0x00, 0xFFFF);
    CHECK(vdp.vsram(0) == 0x07FF); // VSRAM is 11-bit
}

TEST_CASE("genesis_vdp prefetches data-port reads") {
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x04); // M5: unlock registers above R10
    set_reg(vdp, 15, 0x02);
    set_command(vdp, 0x0100, 0x01); // write two words
    vdp.write16(0x00, 0x1111);
    vdp.write16(0x00, 0x2222);

    set_command(vdp, 0x0100, 0x00); // VRAM read (prefetches the first word)
    CHECK(vdp.read16(0x00) == 0x1111);
    CHECK(vdp.read16(0x00) == 0x2222);
}

TEST_CASE("genesis_vdp performs a 68K->VRAM DMA transfer") {
    genesis_vdp vdp;
    const std::array<std::uint16_t, 4> source = {0x1111, 0x2222, 0x3333, 0x4444};
    vdp.set_dma_read([&source](std::uint32_t addr) -> std::uint16_t {
        const std::uint32_t word = addr >> 1U;
        return word < source.size() ? source[word] : std::uint16_t{0};
    });

    set_reg(vdp, 1, 0x14);  // M5 + DMA enable
    set_reg(vdp, 15, 0x02); // auto-increment 2
    set_reg(vdp, 19, 0x04); // DMA length = 4 words
    set_reg(vdp, 20, 0x00);
    set_reg(vdp, 21, 0x00); // source word address 0
    set_reg(vdp, 22, 0x00);
    set_reg(vdp, 23, 0x00);         // type 0 -> 68K transfer
    set_command(vdp, 0x3000, 0x21); // VRAM write + DMA, runs on the 2nd control word

    CHECK(vdp.vram16(0x3000) == 0x1111);
    CHECK(vdp.vram16(0x3002) == 0x2222);
    CHECK(vdp.vram16(0x3004) == 0x3333);
    CHECK(vdp.vram16(0x3006) == 0x4444);
    CHECK_FALSE(vdp.dma_busy());
}

TEST_CASE("genesis_vdp performs a VRAM fill DMA") {
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x14);  // M5 + DMA enable
    set_reg(vdp, 15, 0x01); // auto-increment 1
    set_reg(vdp, 19, 0x04); // fill length
    set_reg(vdp, 20, 0x00);
    set_reg(vdp, 23, 0x80);         // type 2 -> VRAM fill
    set_command(vdp, 0x4000, 0x21); // VRAM write + DMA: arms the fill
    vdp.write16(0x00, 0xAB00);      // data-port write supplies the fill byte (high)

    CHECK(vdp.vram16(0x4002) == 0xABAB); // bytes filled with 0xAB
    // VRAM fill runs on the VDP's internal data path: the DMA-busy status bit
    // is held for the fill duration (the 68K is NOT bus-stalled), then clears
    // once the busy timer drains in tick(). A game that defensively polls
    // dma_busy after the fill must see it set, then see it clear.
    CHECK(vdp.dma_busy());
    vdp.tick(genesis_vdp::master_clocks_per_line); // one full line >> the 4-byte fill
    CHECK_FALSE(vdp.dma_busy());
}

TEST_CASE("genesis_vdp performs a VRAM copy DMA") {
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x14);
    set_reg(vdp, 15, 0x01);
    set_command(vdp, 0x0100, 0x01); // seed source data
    vdp.write16(0x00, 0x1234);

    set_reg(vdp, 19, 0x02); // copy length 2 bytes
    set_reg(vdp, 20, 0x00);
    set_reg(vdp, 21, 0x00); // source 0x0100
    set_reg(vdp, 22, 0x01);
    set_reg(vdp, 23, 0xC0);         // type 3 -> VRAM copy
    set_command(vdp, 0x0200, 0x21); // VRAM write + DMA at dest 0x0200

    CHECK(vdp.vram16(0x0200) == 0x1234);
    // VRAM-to-VRAM copy also runs on the VDP's internal path: busy is held for
    // the copy duration, then clears once the timer drains.
    CHECK(vdp.dma_busy());
    vdp.tick(genesis_vdp::master_clocks_per_line);
    CHECK_FALSE(vdp.dma_busy());
}

TEST_CASE("genesis_vdp raises the V-blank interrupt") {
    genesis_vdp vdp;
    int irq_seen = -1;
    vdp.set_irq_callback([&irq_seen](int level) { irq_seen = level; });
    set_reg(vdp, 1, 0x60); // display enable + V-int enable

    // Reset leaves the VDP at the VBL entry line; the first tick drains
    // the prepared VINT delay (770 master) and raises the IRQ. A single
    // scanline-worth tick is safely past the drain window.
    vdp.tick(genesis_vdp::master_clocks_per_line);
    CHECK(vdp.pending_irq_level() == 6);
    CHECK(irq_seen == 6);

    // Status read must NOT clear the CPU-facing IRQ pending: only
    // cmd_pending, SOVR and SCOL clear on status read. Only IACK
    // (acknowledge_irq) clears vblank_pending_.
    (void)vdp.read16(0x04);
    CHECK(vdp.pending_irq_level() == 6); // still pending
    CHECK(irq_seen == 6);

    // The IACK callback (driven by the CPU when accepting the IRQ) is what
    // clears the pending bit.
    vdp.acknowledge_irq(6);
    CHECK(vdp.pending_irq_level() == 0);
    CHECK(irq_seen == 0);
}

TEST_CASE("genesis_vdp raises the H-blank interrupt on the H-int counter") {
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x40);  // display enable, no V-int
    set_reg(vdp, 0, 0x10);  // H-int enable
    set_reg(vdp, 10, 0x00); // H-int every line

    vdp.tick(genesis_vdp::master_clocks_per_line); // one active scanline
    CHECK(vdp.pending_irq_level() == 4);
}

TEST_CASE("genesis_vdp HINT counter is held at R10 during V-blank") {
    // The HINT counter decrements through active display + once on the first
    // V-blank line, then is held at R10 for the remaining V-blank lines (no
    // firing). A naive decrement-everywhere implementation fires spurious HINTs
    // during V-blank whenever R10 < V-blank-line-count, breaking games that
    // drive raster effects off HINT.
    genesis_vdp vdp;
    int hint_count = 0;
    vdp.set_irq_callback([&](int level) {
        if (level == 4) {
            ++hint_count;
        }
        vdp.acknowledge_irq(level); // drain so we count each assertion
    });
    set_reg(vdp, 1, 0x40); // display enable, no V-int
    set_reg(vdp, 0, 0x10); // H-int enable
    set_reg(vdp, 10, 20);  // HINT every 21 active lines

    // Drive line-by-line so refresh_irq runs between each scanline (the test
    // counts level-rising edges, so we must drain HINT each line via the
    // acknowledge in the callback). With R10=20: across the 224 active lines
    // the counter underflows ~10 times. On the V-blank entry line it underflows
    // once more if the counter reached 0 there, so 10-11 total. The remaining
    // 38 V-blank lines must NOT fire (held at R10).
    for (int line = 0; line < 262; ++line) {
        vdp.tick(genesis_vdp::master_clocks_per_line);
    }
    CHECK(hint_count >= 10);
    CHECK(hint_count <= 11); // no extra spurious HINTs from V-blank decrement
}

TEST_CASE("genesis_vdp advances the V counter readback") {
    genesis_vdp vdp;
    // Reset starts at the VBL entry line (= field_height(), 224 for
    // default H40 mode 5). After 10 scanline ticks, V counter is 234.
    CHECK((vdp.read16(0x08) >> 8U) == 224U); // power-on at VBL entry line
    vdp.tick(10U * genesis_vdp::master_clocks_per_line);
    CHECK((vdp.read16(0x08) >> 8U) == 234U);
    CHECK(vdp.mmio_read(0x08) == 234U); // mmio byte path agrees
}

TEST_CASE("genesis_vdp round-trips its state") {
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x04); // M5: unlock registers above R10
    set_reg(vdp, 15, 0x02);
    set_command(vdp, 0x1000, 0x01);
    vdp.write16(0x00, 0xCAFE);
    vdp.tick(50U * genesis_vdp::master_clocks_per_line);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    vdp.save_state(writer);

    genesis_vdp restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.vram16(0x1000) == 0xCAFE);
    CHECK(restored.scanline() == vdp.scanline());
    CHECK(restored.reg(15) == 0x02);
}

// Reset leaves the VDP at the VBL entry line, so the first 38 ticks
// (NTSC mode 5 = 262 - 224 lines) advance through V-blank without
// rendering. After this helper the next master_clocks_per_line tick
// renders scanline 0.
static void tick_to_active_line(genesis_vdp& vdp) {
    constexpr std::uint64_t vbl_lines = 38;
    vdp.tick(vbl_lines * genesis_vdp::master_clocks_per_line);
}

TEST_CASE("genesis_vdp write FIFO back-pressures the 68K during active display") {
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x40);  // display enable, no V-int
    set_reg(vdp, 15, 0x02); // auto-increment 2

    tick_to_active_line(vdp);
    vdp.tick(genesis_vdp::master_clocks_per_line); // render scanline 0 -> in_vblank_ clears
    CHECK_FALSE(vdp.dma_stall_active());

    // A burst of data-port words faster than the FIFO drains fills the 4-entry
    // FIFO and stalls the 68K (no tick() advances the within-line clock between
    // these writes, so each consumes the next access slot until the FIFO is full).
    set_command(vdp, 0x0000, 0x01); // VRAM write
    for (int i = 0; i < 8; ++i) {
        vdp.write16(0x00, 0x1234);
    }
    CHECK(vdp.dma_stall_active());

    // The stall is finite: enough master cycles drain it (a full line is well
    // beyond the FIFO's per-line slot schedule).
    vdp.tick(genesis_vdp::master_clocks_per_line);
    CHECK_FALSE(vdp.dma_stall_active());
}

TEST_CASE("genesis_vdp write FIFO does not stall during V-blank") {
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x40);  // display enable
    set_reg(vdp, 15, 0x02); // auto-increment 2
    // Reset leaves the VDP on the V-blank entry line; do not advance to a
    // visible line. FIFO back-pressure must not engage outside active display.
    set_command(vdp, 0x0000, 0x01);
    for (int i = 0; i < 8; ++i) {
        vdp.write16(0x00, 0x1234);
    }
    CHECK_FALSE(vdp.dma_stall_active());
}

TEST_CASE("genesis_vdp round-trips the write-FIFO stall state") {
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x40);
    set_reg(vdp, 15, 0x02);
    tick_to_active_line(vdp);
    vdp.tick(genesis_vdp::master_clocks_per_line); // into active display
    set_command(vdp, 0x0000, 0x01);
    for (int i = 0; i < 8; ++i) {
        vdp.write16(0x00, 0x1234);
    }
    REQUIRE(vdp.dma_stall_active()); // mid-burst stall debt to preserve

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    vdp.save_state(writer);

    genesis_vdp restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    // The pending back-pressure survives the round-trip (deterministic resume).
    CHECK(restored.dma_stall_active());
}

TEST_CASE("genesis_vdp renders a plane-A tile") {
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x44);                 // M5 + display enable
    set_reg(vdp, 15, 0x02);                // auto-increment 2
    set_reg(vdp, 2, 0x30);                 // plane A name table = 0xC000
    set_reg(vdp, 4, 0x07);                 // plane B name table = 0xE000 (kept clear)
    write_vram(vdp, 0x0020, solid_tile_1); // tile 1 = solid colour index 1
    write_vram(vdp, 0xC000, {0x0001});     // plane A cell (0,0) -> tile 1, palette 0
    write_cram(vdp, 1, 0x000E);            // palette colour 1 = max red

    tick_to_active_line(vdp);
    vdp.tick(genesis_vdp::master_clocks_per_line); // render scanline 0
    const auto fb = vdp.framebuffer();
    REQUIRE(fb.pixels != nullptr);
    CHECK(fb.pixels[0] == rgb_red);
}

TEST_CASE("genesis_vdp fills the backdrop when display is disabled") {
    genesis_vdp vdp;
    write_cram(vdp, 0, 0x00E0); // backdrop (index 0) = max green
    // Display stays disabled (reg1 bit6 clear at power-on).
    tick_to_active_line(vdp);
    vdp.tick(genesis_vdp::master_clocks_per_line);
    const auto fb = vdp.framebuffer();
    REQUIRE(fb.pixels != nullptr);
    CHECK(fb.pixels[0] == rgb_green);
}

TEST_CASE("genesis_vdp window plane covers full width when V-condition is set") {
    // reg[18] V-condition covering all active lines must render the window
    // plane full-width regardless of reg[17] H (otherwise plane A with its
    // V-scroll bleeds through where the window should sit).
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x44);                 // M5 + display enable
    set_reg(vdp, 12, 0x81);                // H40
    set_reg(vdp, 15, 0x02);                // auto-increment 2
    set_reg(vdp, 2, 0x30);                 // plane A nametable = $C000
    set_reg(vdp, 3, 0x28);                 // window nametable  = $A000
    set_reg(vdp, 4, 0x07);                 // plane B nametable = $E000 (kept clear)
    set_reg(vdp, 17, 0x00);                // no window H
    set_reg(vdp, 18, 0x1C);                // window V covers cell rows 0..27 (lines 0..223)
    write_vram(vdp, 0x0020, solid_tile_1); // tile 1 = solid colour index 1
    write_vram(vdp, 0xC000, {0x0001});     // plane A cell (0,0) -> tile 1 / palette 0
    write_vram(vdp, 0xA000, {0x2001});     // window cell (0,0) -> tile 1 / palette 1
    write_cram(vdp, 1, 0x000E);            // palette 0 colour 1 = max red
    write_cram(vdp, 17, 0x00E0);           // palette 1 colour 1 = max green

    tick_to_active_line(vdp);
    vdp.tick(genesis_vdp::master_clocks_per_line);
    const auto fb = vdp.framebuffer();
    REQUIRE(fb.pixels != nullptr);
    CHECK(fb.pixels[0] == rgb_green); // window wins, not plane A
}

TEST_CASE("genesis_vdp renders a sprite over the planes") {
    genesis_vdp vdp;
    set_reg(vdp, 1, 0x44);                 // M5 + display enable
    set_reg(vdp, 15, 0x02);                // auto-increment 2
    set_reg(vdp, 2, 0x30);                 // plane A 0xC000 (transparent)
    set_reg(vdp, 4, 0x07);                 // plane B 0xE000 (transparent)
    set_reg(vdp, 5, 0x68);                 // sprite attribute table = 0xD000
    write_vram(vdp, 0x0020, solid_tile_1); // tile 1 = solid colour index 1
    // Sprite 0: Y=0 (w0=128), 1x1 cell + link 0 (w1=0), tile 1 high-priority
    // (w2=0x8001), X=0 (w3=128).
    write_vram(vdp, 0xD000, {0x0080, 0x0000, 0x8001, 0x0080});
    write_cram(vdp, 1, 0x000E); // colour 1 = max red

    tick_to_active_line(vdp);
    vdp.tick(genesis_vdp::master_clocks_per_line);
    const auto fb = vdp.framebuffer();
    REQUIRE(fb.pixels != nullptr);
    CHECK(fb.pixels[0] == rgb_red);
}
