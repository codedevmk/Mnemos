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
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::ivideo, genesis_vdp>);
static_assert(std::is_base_of_v<mnemos::chips::immio, genesis_vdp>);
static_assert(genesis_vdp::static_class == mnemos::chips::chip_class::video);

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
}

TEST_CASE("genesis_vdp raises the V-blank interrupt") {
    genesis_vdp vdp;
    int irq_seen = -1;
    vdp.set_irq_callback([&irq_seen](int level) { irq_seen = level; });
    set_reg(vdp, 1, 0x60); // display enable + V-int enable

    vdp.tick(225U * genesis_vdp::master_clocks_per_line); // through scanline 224 (vblank start)
    CHECK(vdp.pending_irq_level() == 6);
    CHECK(irq_seen == 6);

    (void)vdp.read16(0x04); // status read acknowledges the V-int
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

TEST_CASE("genesis_vdp advances the V counter readback") {
    genesis_vdp vdp;
    CHECK((vdp.read16(0x08) >> 8U) == 0U); // V counter 0 at power-on line
    vdp.tick(10U * genesis_vdp::master_clocks_per_line);
    CHECK((vdp.read16(0x08) >> 8U) == 10U);
    CHECK(vdp.mmio_read(0x08) == 10U); // immio byte path agrees
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
