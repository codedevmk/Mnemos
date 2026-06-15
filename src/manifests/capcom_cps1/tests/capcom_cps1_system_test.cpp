// CPS1 board test. Increment 1 brings up the 68000 + cps_a_b video on a
// synthetic cart (boot, render one frame to the black backdrop, route the
// CPS-A/CPS-B register windows). Increment 2 adds the per-frame CPS-A -> video
// decode, the reg5 palette DMA (GFX RAM -> palette buffer), and the vblank
// level-2 IRQ that drives the game's main loop. Increment 3 adds the Z80 sound
// subsystem: the 68K -> Z80 sound latch, a running sound Z80 driving the
// YM2151 + OKIM6295, the bank window, and the two CPUs + sound chips scheduled
// together across the frame. Increment 4 adds the controls / DIP read windows,
// the coin-control write stub, the per-frame sprite-latch DMA, and the CPS-B
// protection read-back (board ID + 16x16 multiplier) through the active profile.

#include "capcom_cps1_system.hpp"
#include "cps_b_profiles.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace {

    namespace cps1 = mnemos::manifests::capcom_cps1;
    using cps1::assemble_cps1;
    using cps1::cps1_rom_skeleton;
    using mnemos::manifests::common::rom_set_image;

    // Store a big-endian 32-bit word into a byte buffer (vector or array span).
    void poke32(std::span<std::uint8_t> bytes, std::size_t at, std::uint32_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 24U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value >> 16U);
        bytes[at + 2U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 3U] = static_cast<std::uint8_t>(value);
    }

    // Store a big-endian 16-bit word into a byte buffer (vector or array span).
    void poke16(std::span<std::uint8_t> bytes, std::size_t at, std::uint16_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value);
    }

    // A maincpu region whose 68000 reset vectors ($0 = SSP, $4 = PC) point the CPU
    // at a trivial program (BRA * -- branch to self, opcode 0x60FE) at $000400.
    [[nodiscard]] rom_set_image make_image() {
        rom_set_image image;
        auto& main = image.regions["maincpu"];
        main.assign(cps1::main_rom_size, 0xFFU);
        poke32(main, 0x0U, 0x00FF0000U); // initial SSP -> top of work RAM
        poke32(main, 0x4U, 0x00000400U); // initial PC -> the program below
        main[0x400U] = 0x60U;            // BRA.s
        main[0x401U] = 0xFEU;            // displacement -2 (branch to self)
        // A small graphics region so attach_gfx has real backing.
        image.regions["gfx"].assign(0x400U, 0x00U);
        return image;
    }

} // namespace

TEST_CASE("cps1 boots a synthetic cart and renders one frame", "[cps1]") {
    auto system = assemble_cps1(make_image());

    // Reset loaded the vectors from the cart.
    const auto regs = system->main_cpu.cpu_registers();
    CHECK(regs.a[7] == 0x00FF0000U); // SSP
    CHECK(regs.pc == 0x00000400U);   // PC

    REQUIRE(system->video.frame_index() == 0U);
    system->run_frame();
    REQUIRE(system->video.frame_index() == 1U);

    // The frame is the CPS1 display geometry, cleared to the decoded backdrop.
    const auto fb = system->video.framebuffer();
    REQUIRE(fb.width == 384U);
    REQUIRE(fb.height == 224U);
    bool all_black = true;
    for (std::uint32_t i = 0; i < fb.width * fb.height; ++i) {
        if (fb.pixels[i] != 0x000000U) {
            all_black = false;
            break;
        }
    }
    CHECK(all_black); // zero palette -> black backdrop (no palette DMA yet)

    // The CPU executed (the branch-to-self ran for the whole frame budget).
    CHECK(system->main_cpu.cpu_registers().pc == 0x00000400U);
}

TEST_CASE("cps1 ROM skeleton declares the program region only", "[cps1]") {
    const auto decl = cps1_rom_skeleton("synthetic");
    CHECK(decl.name == "synthetic");
    CHECK(decl.board == "capcom_cps1");
    REQUIRE(decl.regions.size() == 1U);
    CHECK(decl.regions[0].name == "maincpu");
    CHECK(decl.regions[0].size == cps1::main_rom_size);
}

TEST_CASE("cps1 routes the CPS-A and CPS-B register windows", "[cps1]") {
    auto system = assemble_cps1(make_image());

    // A 68K word write to the CPS-A window lands in the raw register file.
    system->main_bus.write16_be(0x800100U, 0x1234U);
    CHECK(system->cps_a_regs[0] == 0x1234U);

    // A CPS-B window write is reflected by the video chip's register file and
    // reads back through the same window.
    system->main_bus.write16_be(0x800140U + 4U, 0xABCDU); // CPS-B register index 2
    CHECK(system->video.cps_b_reg(2U) == 0xABCDU);
    CHECK(system->main_bus.read16_be(0x800140U + 4U) == 0xABCDU);

    // An unmapped read returns benign open-bus rather than crashing.
    CHECK(system->main_bus.read16_be(0x700000U) == 0xFFFFU);
}

TEST_CASE("cps1 reg5 palette DMA fills the palette from GFX RAM", "[cps1]") {
    auto system = assemble_cps1(make_image());

    // Make every GFX tile pixel pen 15 (all four bit-planes set) so the scroll
    // layers are fully transparent and the backdrop pen shows through unobscured.
    auto& gfx = system->roms.regions["gfx"];
    gfx.assign(0x400U, 0xFFU);
    system->video.attach_gfx(std::span<const std::uint8_t>(gfx));

    // The backdrop pen is palette page 0xBF, pen 0x0F -> palette word index
    // 0xBFF -> byte offset 0x17FE (page 5 of the 6 copied 0x400-byte pages).
    // reg5 decodes to a page-aligned GFX-RAM source; stage the colour there at
    // the same in-buffer offset the backdrop reads.
    constexpr std::uint16_t reg5 = 0x0004U; // source = reg5 << 8 = 0x400 (page-aligned)
    constexpr std::uint32_t source = static_cast<std::uint32_t>(reg5) << 8U;
    constexpr std::uint16_t backdrop_word = 0x0F00U; // brightness 0, red 0xF -> non-black
    constexpr std::uint32_t backdrop_byte = 0xBFFU * 2U;
    poke16(system->gfx_ram, source + backdrop_byte, backdrop_word);

    // Before the DMA the palette is zero -> black backdrop.
    system->run_frame();
    CHECK(system->video.framebuffer().pixels[0] == 0x000000U);

    // A word write to the CPS-A reg5 window ($80010A/B) triggers the DMA on the
    // low byte. The decoded backdrop then becomes the staged red.
    system->main_bus.write16_be(0x800100U + cps1::cps_a_palette_base * 2U, reg5);

    system->run_frame();
    const std::uint32_t backdrop_rgb = system->video.framebuffer().pixels[0];
    CHECK(backdrop_rgb != 0x000000U);                  // DMA filled the palette
    CHECK(backdrop_rgb == (0xFF0000U & backdrop_rgb)); // pure-red channel only
    CHECK((backdrop_rgb & 0x00FFFFU) == 0U);           // no green/blue
}

TEST_CASE("cps1 raises and acknowledges the level-2 vblank IRQ each frame", "[cps1]") {
    rom_set_image image;
    auto& main = image.regions["maincpu"];
    main.assign(cps1::main_rom_size, 0xFFU);
    poke32(main, 0x0U, 0x01000000U); // SSP -> top of work RAM (predecrement lands inside)
    poke32(main, 0x4U, 0x00000400U); // PC -> the program below

    // Autovector level 2 = vector 26 -> address $68. Point it at the handler.
    poke32(main, 0x68U, 0x00000500U);

    // Program @ $400: drop the interrupt mask (MOVE #$2000,SR) then BRA *.
    poke16(main, 0x400U, 0x46FCU); // MOVE #imm,SR
    poke16(main, 0x402U, 0x2000U); // S=1, IPM=0 -> level-2 IRQ now accepted
    poke16(main, 0x404U, 0x60FEU); // BRA * (branch to self)

    // Handler @ $500: ADDQ.L #1,D0 then RTE. D0 counts serviced vblanks.
    poke16(main, 0x500U, 0x5280U); // ADDQ.L #1,D0
    poke16(main, 0x502U, 0x4E73U); // RTE

    image.regions["gfx"].assign(0x400U, 0x00U);

    auto system = assemble_cps1(std::move(image));
    REQUIRE(system->vblank_irq_raised == 0U);
    REQUIRE(system->vblank_irq_acked == 0U);

    system->run_frame();

    // The vblank callback raised the IRQ; the CPU accepted it (IACK fired) and
    // vectored through autovector 2 ($68) into the handler.
    CHECK(system->vblank_irq_raised == 1U);
    CHECK(system->vblank_irq_acked == 1U);
    CHECK(system->main_cpu.cpu_registers().d[0] == 1U); // handler ran once

    system->run_frame();
    CHECK(system->vblank_irq_raised == 2U);
    CHECK(system->vblank_irq_acked == 2U);
    CHECK(system->main_cpu.cpu_registers().d[0] == 2U);
}

TEST_CASE("cps1 drives the CPS-A scroll1 base (0x4000-aligned) to the video", "[cps1]") {
    auto system = assemble_cps1(make_image());

    // GFX ROM: make scroll1 tile code 0 opaque pen 1 across its whole 8x8 cell.
    // decode_packed reads 4bpp planar bytes; setting plane-0 bytes (group*4 + 0)
    // and clearing the other three planes yields pen 1 at every pixel.
    auto& gfx = system->roms.regions["gfx"];
    gfx.assign(0x100U, 0x00U);
    for (std::size_t off = 0U; off < 64U; off += 4U) {
        gfx[off + 0U] = 0xFFU; // plane 0 = all set -> pen bit0
    }
    system->video.attach_gfx(std::span<const std::uint8_t>(gfx));

    // The hardware aligns the scroll name-table base to 0x4000 (the low bits of
    // the CPS-A base register are masked off). Stage two scroll1 tile-index-0
    // entries: one at GFX offset 0 (what a zero base reads) with attr -> palette
    // page 32, and one at the aligned base 0x4000 with attr -> palette page 33.
    constexpr std::uint16_t scroll1_base_reg = 0x0041U; // raw base = reg << 8 = 0x4100
    constexpr std::uint32_t aligned_base = 0x4000U;     // masked to the 0x4000 boundary
    auto stage_entry = [&](std::uint32_t base, std::uint16_t attr) {
        poke16(system->gfx_ram, base + 0U, 0x0000U); // tile code 0
        poke16(system->gfx_ram, base + 2U, attr);    // attribute
    };
    stage_entry(0x0000U, 0x0000U);      // page 32 entry at offset 0 (the unaligned target)
    stage_entry(aligned_base, 0x0001U); // page 33 entry at the 0x4000-aligned base

    // Colour palette page 32 red and page 33 green directly (focus this case on
    // the scroll wiring, not the DMA path). pal_num = 32 + (attr & 0x1F); the
    // word index is pal_num*16 + pen, pen 1 here.
    poke16(system->palette, (32U * 16U + 1U) * 2U, 0x0F00U); // page 32 pen1 = red
    poke16(system->palette, (33U * 16U + 1U) * 2U, 0x00F0U); // page 33 pen1 = green

    // The visible top-left pixel maps to world (64,16); offset the scroll so it
    // lands on tile-index 0 (world (0,0)) -- this also requires the scroll X/Y
    // registers to reach the chip. Route base + scroll X/Y via the CPS-A window.
    system->main_bus.write16_be(0x800100U + cps1::cps_a_scroll1_base * 2U, scroll1_base_reg);
    system->main_bus.write16_be(0x800100U + cps1::cps_a_scroll1_x * 2U, 0xFFC0U); // -64
    system->main_bus.write16_be(0x800100U + cps1::cps_a_scroll1_y * 2U, 0xFFF0U); // -16

    system->run_frame();

    // The visible image is the scroll1 layer. The base register's low bits
    // (0x0100) are masked off to the 0x4000 boundary, so the lookup used the
    // page-33 entry at 0x4000 -> green; an unaligned (raw 0x4100) base would read
    // the page-32 entry at offset 0 -> red. Green proves the base reached the
    // chip AND that the low bits were masked to the 0x4000 alignment.
    const std::uint32_t pixel = system->video.framebuffer().pixels[0];
    CHECK((pixel & 0x00FF00U) != 0U); // green present -> aligned base addressing
    CHECK((pixel & 0xFF0000U) == 0U); // not red -> not the unaligned offset
}

namespace {

    // Add an "audiocpu" sound-program region to an image, with `program` placed
    // at $0000 (the Z80 reset entry) and the rest padded so the bank window has
    // backing. `total` sizes the region (>= 0x18000 makes the bank window decode
    // from $10000, the real-set layout; smaller stays in the linear layout).
    void add_sound_rom(rom_set_image& image, const std::vector<std::uint8_t>& program,
                       std::size_t total = 0x8000U) {
        auto& rom = image.regions["audiocpu"];
        rom.assign(total, 0x00U);
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[i] = program[i];
        }
    }

    // A minimal OKIM6295 sample ROM: one phrase whose 8-byte table entry points
    // at an ADPCM byte stream long enough to still be sounding at frame end (a
    // frame is ~126 native OKI steps, so a 0x200-byte phrase outlasts it).
    [[nodiscard]] std::vector<std::uint8_t> make_oki_rom() {
        std::vector<std::uint8_t> rom(0x400U, 0x00U);
        // Phrase 1 table entry (offset 8): start 0x000100, end 0x0002FF.
        rom[8] = 0x00;
        rom[9] = 0x01;
        rom[10] = 0x00;
        rom[11] = 0x00;
        rom[12] = 0x02;
        rom[13] = 0xFF;
        for (std::size_t i = 0; i < 0x200U; ++i) {
            rom[0x100U + i] = static_cast<std::uint8_t>(0x11U * (i + 1U)); // non-silent ADPCM
        }
        return rom;
    }

} // namespace

TEST_CASE("cps1 routes a 68K sound command to the Z80 via the sound latch", "[cps1][sound]") {
    // Sound program @ $0000: LD A,($F008); LD ($D000),A; JR $ (loop).
    auto image = make_image();
    add_sound_rom(image, {
                             0x3A,
                             0x08,
                             0xF0, // LD A,($F008)  -- read the primary latch
                             0x32,
                             0x00,
                             0xD0, // LD ($D000),A  -- store into Z80 work RAM
                             0x18,
                             0xFE, // JR $          -- spin
                         });
    auto system = assemble_cps1(std::move(image));

    // The 68K pokes a sound command into the primary latch ($800180).
    system->main_bus.write8(cps1::sound_latch_addr, 0x5AU);
    CHECK(system->sound_latch == 0x5AU);
    CHECK(system->sound_latch_pending);

    // After a frame the Z80 has run, read the latch (clearing pending), and
    // copied it into its work RAM.
    system->run_frame();
    CHECK(system->z80_ram[0] == 0x5AU);
    CHECK_FALSE(system->sound_latch_pending); // the Z80 read consumed the latch

    // The secondary latch routes through $800188 (low byte) to $F00A.
    system->main_bus.write16_be(cps1::sound_latch2_addr, 0x00C3U);
    CHECK(system->sound_latch2 == 0xC3U);
    CHECK(system->sound_bus.read8(cps1::z80_io_latch2) == 0xC3U);
}

TEST_CASE("cps1 runs the sound Z80 and the bank register selects the window", "[cps1][sound]") {
    // Sound program @ $0000: select bank 1, then read the banked window byte at
    // $8000 into B, and spin. With the linear (small) layout, bank 1's window
    // base is $8000 + 1*$4000 = $C000.
    auto image = make_image();
    add_sound_rom(image,
                  {
                      0x3E,
                      0x01, // LD A,1
                      0x32,
                      0x04,
                      0xF0, // LD ($F004),A -- bank = 1
                      0x3A,
                      0x00,
                      0x80, // LD A,($8000) -- read the banked window
                      0x32,
                      0x01,
                      0xD0, // LD ($D001),A -- observe the fetched byte
                      0x18,
                      0xFE, // JR $
                  },
                  0x10000U);
    // Stamp a sentinel at the bank-1 window source ($C000 in the linear layout).
    image.regions["audiocpu"][0xC000U] = 0xA7U;

    auto system = assemble_cps1(std::move(image));
    system->run_frame();

    // The Z80 executed: PC advanced past reset and the bank register latched.
    CHECK(system->sound_bank == 0x01U);
    CHECK(system->sound_cpu.cpu_registers().pc != 0x0000U);
    // The banked window fetched the sentinel from the selected bank.
    CHECK(system->z80_ram[1] == 0xA7U);
}

TEST_CASE("cps1 reaches the YM2151 + OKIM6295 from the Z80 and drains audio", "[cps1][sound]") {
    // Sound program @ $0000: program a YM2151 register, fire an OKIM6295 phrase,
    // then spin. The YM write exercises the address/data port pair; the OKI
    // command (2 bytes) plays phrase 1 on channel 0.
    auto image = make_image();
    add_sound_rom(image, {
                             0x3E, 0x20,       // LD A,$20      -- a YM2151 register addr
                             0x32, 0x00, 0xF0, // LD ($F000),A  -- YM address port
                             0x3E, 0xC0,       // LD A,$C0      -- some data
                             0x32, 0x01, 0xF0, // LD ($F001),A  -- YM data port
                             0x3E, 0x81,       // LD A,$81      -- OKI: play phrase 1
                             0x32, 0x02, 0xF0, // LD ($F002),A  -- OKI command port
                             0x3E, 0x10,       // LD A,$10      -- channel 0, full volume
                             0x32, 0x02, 0xF0, // LD ($F002),A  -- OKI command port
                             0x18, 0xFE,       // JR $
                         });
    image.regions["oki"] = make_oki_rom();

    auto system = assemble_cps1(std::move(image));
    // Capture OKI output so a frame's worth of native steps queues samples.
    system->oki.enable_audio_capture(true);

    system->run_frame();

    // The YM2151 register the Z80 wrote is reflected in the chip's register file.
    CHECK(system->fm.register_value(0x20U) == 0xC0U);
    // The OKIM6295 accepted the play command (channel 0 busy in its status).
    CHECK((system->oki.read_status() & 0x01U) != 0U);
    // The OKI advanced and queued audio over the frame; drain a non-empty buffer.
    const std::size_t pending = system->oki.pending_samples();
    REQUIRE(pending > 0U);
    std::vector<std::int16_t> buf(pending * 2U);
    const std::size_t pairs = system->oki.drain_samples(buf.data(), pending);
    CHECK(pairs == pending);
    bool any_nonzero = false;
    for (std::int16_t s : buf) {
        if (s != 0) {
            any_nonzero = true;
            break;
        }
    }
    CHECK(any_nonzero); // the decoded ADPCM stream is audible, not silence
}

TEST_CASE("cps1 exposes player inputs + system/DIP through the read windows", "[cps1][input]") {
    auto system = assemble_cps1(make_image());

    // Inputs default to all-released (active-low 0xFFFF / 0xFF per byte).
    CHECK(system->main_bus.read16_be(cps1::player_input_base) == 0xFFFFU);
    CHECK(system->main_bus.read16_be(cps1::system_dsw_base) == 0xFFFFU);

    // Drive a board input value: P2 = 0xAA (high byte), P1 = 0x55 (low byte);
    // system word low byte = 0x3C; the three DIP bytes 0x12 / 0x34 / 0x56.
    system->set_inputs(0xAA55U, 0xFF3CU, 0x12U, 0x34U, 0x56U);

    // The player word reads back, and mirrors across the $800000-$800007 window.
    CHECK(system->main_bus.read16_be(cps1::player_input_base) == 0xAA55U);
    CHECK(system->main_bus.read16_be(cps1::player_input_base + 2U) == 0xAA55U);
    CHECK(system->main_bus.read16_be(cps1::player_input_base + 6U) == 0xAA55U);

    // The system word is `(sys_low << 8) | 0xFF`; the DIP words `(dip << 8) | 0xFF`.
    CHECK(system->main_bus.read16_be(cps1::system_dsw_base + 0U) == 0x3CFFU); // system
    CHECK(system->main_bus.read16_be(cps1::system_dsw_base + 2U) == 0x12FFU); // DIP A
    CHECK(system->main_bus.read16_be(cps1::system_dsw_base + 4U) == 0x34FFU); // DIP B
    CHECK(system->main_bus.read16_be(cps1::system_dsw_base + 6U) == 0x56FFU); // DIP C

    // Coin control is a write-only stub: the high-byte write lands in the latch.
    system->main_bus.write8(cps1::coin_control_base, 0x0CU);
    CHECK(system->coin_control == 0x0CU);
}

TEST_CASE("cps1 CPS-B protection returns the board ID and 16x16 product", "[cps1][protection]") {
    // Profile 24 (board / PAL "BT1") carries both an ID port and a multiplier:
    // id_offset 0x32 -> id_value 0x0800; mult_offset {factor1=0x0E, factor2=0x0C,
    // result-lo=0x0A, result-hi=0x08}. Assemble the board on that profile.
    constexpr std::uint16_t profile_id = 24U;
    const auto profile = cps1::profile_for_id(profile_id);
    REQUIRE(profile.has_value());

    auto system =
        assemble_cps1(make_image(), cps1::cps1_board_params{.cps_b_profile_id = profile_id});
    REQUIRE(system->profile.id == profile_id);

    // Byte offset -> CPS-B window address; a word read decodes through the profile.
    const auto cps_b_addr = [](std::uint8_t byte_off) { return cps1::cps_b_reg_base + byte_off; };

    // The ID port returns the fixed identity value (the game's protection check).
    CHECK(system->main_bus.read16_be(cps_b_addr(profile->id_offset)) == profile->id_value);

    // Write the two 16-bit factors through the CPS-B window, then read the result
    // ports: lo = (f1 * f2) & 0xFFFF, hi = (f1 * f2) >> 16.
    constexpr std::uint16_t factor1 = 0x1234U;
    constexpr std::uint16_t factor2 = 0x0010U;
    constexpr std::uint32_t product = static_cast<std::uint32_t>(factor1) * factor2; // 0x00012340
    system->main_bus.write16_be(cps_b_addr(profile->mult_offset[0]), factor1);
    system->main_bus.write16_be(cps_b_addr(profile->mult_offset[1]), factor2);

    CHECK(system->main_bus.read16_be(cps_b_addr(profile->mult_offset[2])) ==
          static_cast<std::uint16_t>(product & 0xFFFFU)); // result-lo = 0x2340
    CHECK(system->main_bus.read16_be(cps_b_addr(profile->mult_offset[3])) ==
          static_cast<std::uint16_t>(product >> 16U)); // result-hi = 0x0001

    // A CPS-B offset with no special port returns the raw register the 68K wrote
    // (here the layer-control latch -- render registers are write latches).
    system->main_bus.write16_be(cps_b_addr(profile->layer_control_offset), 0x1357U);
    CHECK(system->main_bus.read16_be(cps_b_addr(profile->layer_control_offset)) == 0x1357U);
}

TEST_CASE("cps1 sprite-latch DMA snapshots the object table at end of frame", "[cps1][sprite]") {
    auto system = assemble_cps1(make_image());

    // GFX ROM: make sprite tile code 0 opaque pen 1 across its whole cell. A
    // sprite cell decodes 4bpp planar bytes via decode_packed(code*128, 8, ...);
    // setting plane-0 bytes (stride 4) and clearing the rest yields pen 1.
    auto& gfx = system->roms.regions["gfx"];
    gfx.assign(0x100U, 0x00U);
    for (std::size_t off = 0U; off < 128U; off += 4U) {
        gfx[off + 0U] = 0xFFU; // plane 0 set -> pen bit0
    }
    system->video.attach_gfx(std::span<const std::uint8_t>(gfx));

    // Colour sprite palette page 0 pen 1 red so a drawn sprite pixel is visible.
    poke16(system->palette, (0U * 16U + 1U) * 2U, 0x0F00U);

    // Drive an object-table base into GFX RAM via CPS-A reg0 (aligned to 0x800).
    constexpr std::uint16_t obj_base_reg = 0x0008U; // base = reg << 8 = 0x800
    constexpr std::uint32_t obj_base = static_cast<std::uint32_t>(obj_base_reg) << 8U;
    system->main_bus.write16_be(0x800100U + cps1::cps_a_obj_base * 2U, obj_base_reg);

    // Helper: stage one object entry (x, y, code, attr) at the table base; a
    // 0xFFxx attribute on the next entry terminates the list.
    const auto stage_sprite = [&](std::uint16_t x, std::uint16_t y, std::uint16_t code,
                                  std::uint16_t attr) {
        poke16(system->gfx_ram, obj_base + 0U, x);
        poke16(system->gfx_ram, obj_base + 2U, y);
        poke16(system->gfx_ram, obj_base + 4U, code);
        poke16(system->gfx_ram, obj_base + 6U, attr);
        poke16(system->gfx_ram, obj_base + 8U, 0xFF00U); // terminator entry
    };
    const auto clear_table = [&]() {
        poke16(system->gfx_ram, obj_base + 6U, 0xFF00U); // first entry terminates
    };

    // A sprite at world (visible_x_start, visible_y_start) = (64, 16) lands its
    // top-left at visible pixel (0, 0). Pen-1, palette page 0.
    constexpr std::uint16_t sx = 64U;
    constexpr std::uint16_t sy = 16U;

    // Frame 1: empty table -> the renderer latches an empty table and draws no
    // sprite; the board's end-of-frame latch also snapshots the empty table.
    clear_table();
    system->run_frame();
    CHECK(system->video.framebuffer().pixels[0] == 0x000000U); // backdrop, no sprite

    // Stage the sprite now. Frame 2 renders from the buffer the board latched at
    // the end of frame 1 (still empty) -> no sprite yet; the board then latches
    // the staged sprite at the end of frame 2.
    stage_sprite(sx, sy, 0x0000U, 0x0000U);
    system->run_frame();
    CHECK(system->video.framebuffer().pixels[0] == 0x000000U); // latch lags one frame

    // Frame 3 renders from the end-of-frame-2 snapshot -> the sprite appears. This
    // is observable only because the board re-latched the object table each frame.
    system->run_frame();
    const std::uint32_t pixel = system->video.framebuffer().pixels[0];
    CHECK((pixel & 0xFF0000U) != 0U); // red sprite pixel present
    CHECK((pixel & 0x00FFFFU) == 0U); // pure red

    // Clearing the table and running another frame drains it back out (the latch
    // tracks the live table, not a one-shot copy).
    clear_table();
    system->run_frame(); // end-of-frame snapshot = empty
    system->run_frame(); // renders the empty snapshot
    CHECK(system->video.framebuffer().pixels[0] == 0x000000U);
}
