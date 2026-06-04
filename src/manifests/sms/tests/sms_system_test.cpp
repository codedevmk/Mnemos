#include "sms_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::audio::sn76489;
    using mnemos::manifests::sms::assemble_sms;
    using mnemos::manifests::sms::detect_93c46;
    using mnemos::manifests::sms::gg_io;
    using mnemos::manifests::sms::korean_mapper_for_crc;
    using mnemos::manifests::sms::resolve_mapper;
    using mnemos::manifests::sms::sms_config;

    // A one-page (16 KiB) zero-filled cartridge image to poke a test program into.
    std::vector<std::uint8_t> blank_rom(std::size_t size = 0x4000U) {
        return std::vector<std::uint8_t>(size, 0U);
    }

    // A 32 KiB cart carrying a valid Codemasters checksum header (word at $7FE6 plus
    // the complement word at $7FE8 == $10000), so auto-detection picks Codemasters.
    // Page 0 ($0000-$3FFF) reads 0, page 1 ($4000-$7FFF) reads 1.
    std::vector<std::uint8_t> codies_rom() {
        std::vector<std::uint8_t> rom(0x8000U, 0U);
        for (std::size_t i = 0x4000U; i < 0x8000U; ++i) {
            rom[i] = 1U;
        }
        rom[0x7FE6U] = 0x34U; // checksum $1234
        rom[0x7FE7U] = 0x12U;
        rom[0x7FE8U] = 0xCCU; // complement $EDCC -> $1234 + $EDCC == $10000
        rom[0x7FE9U] = 0xEDU;
        return rom;
    }
} // namespace

TEST_CASE("assemble_sms boots the Z80 from cartridge ROM into work RAM", "[sms][boot]") {
    auto rom = blank_rom();
    // LD A,$42 ; LD ($C000),A ; HALT
    rom[0] = 0x3EU;
    rom[1] = 0x42U;
    rom[2] = 0x32U;
    rom[3] = 0x00U;
    rom[4] = 0xC0U;
    rom[5] = 0x76U;

    auto sys = assemble_sms(std::move(rom));
    sys->cpu.step_instruction(); // LD A,$42
    sys->cpu.step_instruction(); // LD ($C000),A

    CHECK(sys->bus.read8(0xC000U) == 0x42U);
    CHECK(sys->bus.read8(0xE000U) == 0x42U); // RAM mirror sees the same byte
}

TEST_CASE("assemble_sms honours the region config", "[sms][region]") {
    auto ntsc = assemble_sms(blank_rom(), {.video_region = mnemos::video_region::ntsc});
    CHECK_FALSE(ntsc->vdp.is_pal());
    auto pal = assemble_sms(blank_rom(), {.video_region = mnemos::video_region::pal});
    CHECK(pal->vdp.is_pal());
}

TEST_CASE("assemble_sms banks ROM slots through the mapper registers", "[sms][mapper]") {
    auto rom = blank_rom(0x8000U); // two 16 KiB pages
    rom[0x4000U + 0x410U] = 0x77U; // page 1, in-page offset $410

    auto sys = assemble_sms(std::move(rom));
    CHECK(sys->bus.read8(0x4410U) == 0x77U); // slot 1 defaults to page 1

    sys->bus.write8(0xFFFDU, 0x01U); // slot 0 -> page 1 (via the bus overlay)
    CHECK(sys->mapper.page(0) == 0x01U);
    CHECK(sys->bus.read8(0x0410U) == 0x77U); // slot 0 now reads page 1
    CHECK(sys->bus.read8(0xFFFDU) == 0x01U); // the register write also lands in work RAM
}

TEST_CASE("assemble_sms auto-detects the Codemasters mapper from the header", "[sms][mapper]") {
    auto sys = assemble_sms(codies_rom());
    REQUIRE(sys->codemasters_active);

    // Codemasters pages through ROM-space writes (no $FFFC registers).
    CHECK(sys->bus.read8(0x0000U) == 0U); // slot 0 default page 0
    CHECK(sys->bus.read8(0x8000U) == 0U); // slot 2 default page 0

    sys->bus.write8(0x8000U, 1U); // page slot 2 -> page 1
    CHECK(sys->bus.read8(0x8000U) == 1U);
    sys->bus.write8(0x0000U, 1U); // page slot 0 -> page 1 (no fixed first 1 KiB)
    CHECK(sys->bus.read8(0x0000U) == 1U);
}

TEST_CASE("assemble_sms defaults to the Sega mapper without a Codemasters header",
          "[sms][mapper]") {
    auto sys = assemble_sms(blank_rom(0x8000U));
    CHECK_FALSE(sys->codemasters_active);
}

TEST_CASE("assemble_sms honours a forced mapper choice", "[sms][mapper]") {
    auto forced_sega = assemble_sms(codies_rom(), {.cartridge_mapper = sms_config::mapper::sega});
    CHECK_FALSE(forced_sega->codemasters_active); // header ignored when forced

    auto forced_codies =
        assemble_sms(blank_rom(0x8000U), {.cartridge_mapper = sms_config::mapper::codemasters});
    CHECK(forced_codies->codemasters_active);
}

TEST_CASE("assemble_sms honours a forced Korean mapper", "[sms][mapper]") {
    auto rom = blank_rom(0x10000U); // four 16 KiB pages
    rom[2U * 0x4000U] = 0x5AU;      // page 2 base marker
    rom[3U * 0x4000U] = 0x6BU;      // page 3 base marker

    auto sys = assemble_sms(std::move(rom), {.cartridge_mapper = sms_config::mapper::korean});
    REQUIRE(sys->korean_active);
    CHECK_FALSE(sys->codemasters_active);

    // Power-on: slots 0/1 fixed to pages 0/1, slot 2 = page 0 (reset default).
    CHECK(sys->korean.page() == 0U);
    CHECK(sys->bus.read8(0x8000U) == 0x00U); // slot 2 -> page 0 (not the page-2 marker)

    // A write to $A000 (inside the cartridge window) pages slot 2.
    sys->bus.write8(0xA000U, 3U);
    CHECK(sys->korean.page() == 3U);
    CHECK(sys->bus.read8(0x8000U) == 0x6BU); // slot 2 -> page 3
    CHECK(sys->bus.read8(0x0000U) == 0x00U); // slots 0/1 unaffected
    CHECK(sys->bus.read8(0x4000U) == 0x00U);
}

TEST_CASE("assemble_sms honours a forced Korean MSX mapper", "[sms][mapper]") {
    auto rom = blank_rom(0x10000U); // eight 8 KiB pages
    for (std::uint8_t p = 0; p < 8U; ++p) {
        rom[static_cast<std::size_t>(p) * 0x2000U] = static_cast<std::uint8_t>(0xC0U + p);
    }

    auto sys = assemble_sms(rom, {.cartridge_mapper = sms_config::mapper::korean_msx});
    REQUIRE(sys->korean_msx_active);
    CHECK_FALSE(sys->korean_active);

    // Fixed bank 0: $0000 = page 0, $2000 = page 1. Windows power on at page 0.
    CHECK(sys->bus.read8(0x0000U) == 0xC0U);
    CHECK(sys->bus.read8(0x2000U) == 0xC1U);
    CHECK(sys->bus.read8(0x8000U) == 0xC0U);

    // Page the windows: reg 0 -> $8000, reg 2 -> $4000.
    sys->bus.write8(0x0000U, 3U);
    sys->bus.write8(0x0002U, 5U);
    CHECK(sys->bus.read8(0x8000U) == 0xC3U);
    CHECK(sys->bus.read8(0x4000U) == 0xC5U);
    CHECK(sys->bus.read8(0x0000U) == 0xC0U); // fixed window unaffected

    // Nemesis variant: $0000-$1FFF maps the last 8 KiB page.
    auto nem = assemble_sms(rom, {.cartridge_mapper = sms_config::mapper::korean_msx_nemesis});
    REQUIRE(nem->korean_msx_active);
    CHECK(nem->korean_msx.chip_variant() ==
          mnemos::chips::mapper::korean_msx_mapper::variant::nemesis);
    CHECK(nem->bus.read8(0x0000U) == 0xC7U); // last 8 KiB page
    CHECK(nem->bus.read8(0x2000U) == 0xC1U); // $2000-$3FFF stays bank 0
}

TEST_CASE("assemble_sms honours a forced HiCom mapper", "[sms][mapper]") {
    auto rom = blank_rom(0x10000U); // two 32 KiB pages
    rom[0x0000U] = 0xA0U;           // page 0, offset $0000
    rom[0x4000U] = 0xA4U;           // page 0, offset $4000
    rom[0x8000U] = 0xB0U;           // page 1, offset $0000
    rom[0xC000U] = 0xB4U;           // page 1, offset $4000

    auto sys = assemble_sms(std::move(rom), {.cartridge_mapper = sms_config::mapper::korean_hicom});
    REQUIRE(sys->korean_hicom_active);
    CHECK_FALSE(sys->korean_active);

    // Power-on: page 0 mapped across $0000-$BFFF. $8000-$BFFF mirrors the page's
    // lower 16 KiB, so $8000 reads the same byte as $0000.
    CHECK(sys->hicom.page() == 0U);
    CHECK(sys->bus.read8(0x0000U) == 0xA0U);
    CHECK(sys->bus.read8(0x4000U) == 0xA4U);
    CHECK(sys->bus.read8(0x8000U) == 0xA0U); // mirror of $0000

    // A write to $FFFF selects the 32 KiB page; the byte also lands in work RAM.
    sys->bus.write8(0xFFFFU, 0x01U);
    CHECK(sys->hicom.page() == 0x01U);
    CHECK(sys->bus.read8(0x0000U) == 0xB0U);
    CHECK(sys->bus.read8(0x4000U) == 0xB4U);
    CHECK(sys->bus.read8(0x8000U) == 0xB0U); // mirror of page 1 $0000
    CHECK(sys->bus.read8(0xFFFFU) == 0x01U); // the register write also lands in work RAM
}

TEST_CASE("assemble_sms honours a forced Janggun mapper", "[sms][mapper]") {
    auto rom = blank_rom(0x10000U); // eight 8 KiB pages
    rom[3U * 0x2000U] = 0x3CU;      // page 3 marker
    rom[5U * 0x2000U] = 0xC0U;      // page 5 marker (reverse(0xC0) == 0x03)
    rom[7U * 0x2000U] = 0x7EU;      // page 7 marker

    auto sys =
        assemble_sms(std::move(rom), {.cartridge_mapper = sms_config::mapper::korean_janggun});
    REQUIRE(sys->korean_janggun_active);
    CHECK_FALSE(sys->korean_hicom_active);

    // In-window select: a write to $8000 (inside the cart window) pages FCR0.
    sys->bus.write8(0x8000U, 3U);
    CHECK(sys->janggun.fcr(0) == 3U);
    CHECK(sys->bus.read8(0x8000U) == 0x3CU); // $8000-$9FFF -> bank 3

    // Sega-style 16 KiB pair at $FFFF (work-RAM mirror): FCR0=6, FCR1=7; the byte
    // also lands in work RAM.
    sys->bus.write8(0xFFFFU, 3U);
    CHECK(sys->janggun.fcr(0) == 6U);
    CHECK(sys->janggun.fcr(1) == 7U);
    CHECK(sys->bus.read8(0xA000U) == 0x7EU); // $A000-$BFFF -> bank 7
    CHECK(sys->bus.read8(0xFFFFU) == 0x03U); // the pair write also lands in work RAM

    // Bit-reversal: select bank 5 in window 2 with the reverse flag (bit 7) set.
    sys->bus.write8(0x4000U, 0x85U);
    CHECK(sys->bus.read8(0x4000U) == 0x03U); // reverse(0xC0)
}

TEST_CASE("assemble_sms honours a forced Multi 4x8K mapper", "[sms][mapper]") {
    auto rom = blank_rom(0x40000U); // 32 pages of 8 KiB
    rom[0x2000U] = 0xB1U;           // fixed $2000-$3FFF marker (bank 1)
    rom[0x1FU * 0x2000U] = 0x5AU;   // page 31 marker (window 2 after a $2000=0 write)

    auto sys =
        assemble_sms(std::move(rom), {.cartridge_mapper = sms_config::mapper::korean_multi_4x8k});
    REQUIRE(sys->korean_multi_4x8k_active);

    // Power-on: fixed first 16 KiB; the windows bank 0.
    CHECK(sys->bus.read8(0x2000U) == 0xB1U); // fixed bank 1
    CHECK(sys->bus.read8(0x4000U) == 0x00U); // window 2 -> bank 0

    // A write to $2000 (inside the cart window) XOR-banks the four windows.
    sys->bus.write8(0x2000U, 0x00U);
    CHECK(sys->multi4x8k.page(2) == 0x1FU);  // $4000 window -> 0 ^ 0x1F
    CHECK(sys->bus.read8(0x4000U) == 0x5AU); // page 31
}

TEST_CASE("assemble_sms honours a forced Multi 16K mapper", "[sms][mapper]") {
    auto rom = blank_rom(0x20000U); // 8 pages of 16 KiB
    for (std::uint8_t p = 0; p < 4U; ++p) {
        rom[static_cast<std::size_t>(p) * 0x4000U] = static_cast<std::uint8_t>(0xA0U + p);
    }

    auto sys =
        assemble_sms(std::move(rom), {.cartridge_mapper = sms_config::mapper::korean_multi_16k});
    REQUIRE(sys->korean_multi_16k_active);

    // Power-on banks {0, 1, 0}.
    CHECK(sys->bus.read8(0x0000U) == 0xA0U); // slot 0 -> bank 0
    CHECK(sys->bus.read8(0x4000U) == 0xA1U); // slot 1 -> bank 1
    CHECK(sys->bus.read8(0x8000U) == 0xA0U); // slot 2 -> bank 0

    // In-window slot selects ride the cartridge MMIO ($3FFE/$7FFF/$BFFF).
    sys->bus.write8(0x3FFEU, 2U); // slot 0 -> bank 2
    sys->bus.write8(0x7FFFU, 3U); // slot 1 -> bank 3
    sys->bus.write8(0xBFFFU, 1U); // slot 2 -> (2 & 0x30) + 1 = bank 1
    CHECK(sys->bus.read8(0x0000U) == 0xA2U);
    CHECK(sys->bus.read8(0x4000U) == 0xA3U);
    CHECK(sys->bus.read8(0x8000U) == 0xA1U);
}

TEST_CASE("korean_mapper_for_crc maps known cart CRCs to their mapper", "[sms][mapper][crc]") {
    CHECK(korean_mapper_for_crc(0x97D03541U) == sms_config::mapper::korean);     // Sangokushi 3
    CHECK(korean_mapper_for_crc(0x77EFE84AU) == sms_config::mapper::korean_msx); // Cyborg Z
    CHECK(korean_mapper_for_crc(0xE316C06DU) == sms_config::mapper::korean_msx_nemesis); // Nemesis
    CHECK(korean_mapper_for_crc(0x98AF0236U) == sms_config::mapper::korean_hicom); // Hi-Com 3-in-1
    CHECK(korean_mapper_for_crc(0x192949D5U) == sms_config::mapper::korean_janggun);    // Janggun
    CHECK(korean_mapper_for_crc(0xBA5EC0E3U) == sms_config::mapper::korean_multi_4x8k); // 128 Hap
    CHECK(korean_mapper_for_crc(0xA67F2A5CU) ==
          sms_config::mapper::korean_multi_16k);                 // 4-Pak All Action
    CHECK_FALSE(korean_mapper_for_crc(0x00000000U).has_value()); // unknown CRC
}

TEST_CASE("resolve_mapper auto-detects Korean carts by CRC, else falls back",
          "[sms][mapper][crc]") {
    // An arbitrary image (unknown CRC, no Codemasters header) -> Sega.
    const std::vector<std::uint8_t> blank(0x8000U, 0U);
    CHECK(resolve_mapper({}, blank) == sms_config::mapper::sega);
    // The Codemasters checksum header still wins when the CRC is unknown.
    CHECK(resolve_mapper({}, codies_rom()) == sms_config::mapper::codemasters);
}

TEST_CASE("assemble_sms routes Z80 OUT to the VDP control port", "[sms][vdp]") {
    auto rom = blank_rom();
    // LD A,$AA ; OUT ($BF),A ; LD A,$81 ; OUT ($BF),A  -> VDP register 1 = $AA
    rom[0] = 0x3EU;
    rom[1] = 0xAAU;
    rom[2] = 0xD3U;
    rom[3] = 0xBFU;
    rom[4] = 0x3EU;
    rom[5] = 0x81U;
    rom[6] = 0xD3U;
    rom[7] = 0xBFU;

    auto sys = assemble_sms(std::move(rom));
    for (int i = 0; i < 4; ++i) {
        sys->cpu.step_instruction();
    }
    CHECK(sys->vdp.reg(1) == 0xAAU);
}

TEST_CASE("assemble_sms routes Z80 OUT to the PSG", "[sms][psg]") {
    auto rom = blank_rom();
    // LD A,$90 ; OUT ($7F),A  -> latch channel 0 volume, attenuation 0 (loud)
    rom[0] = 0x3EU;
    rom[1] = 0x90U;
    rom[2] = 0xD3U;
    rom[3] = 0x7FU;

    auto sys = assemble_sms(std::move(rom));
    REQUIRE(sys->psg.volume(0) == 0x0FU); // reset default: silent
    sys->cpu.step_instruction();          // LD A,$90
    sys->cpu.step_instruction();          // OUT ($7F),A
    CHECK(sys->psg.volume(0) == 0x00U);
}

TEST_CASE("assemble_sms reads the joypads on ports $DC/$DD", "[sms][input]") {
    auto rom = blank_rom();
    // IN A,($DC) ; LD ($C000),A
    rom[0] = 0xDBU;
    rom[1] = 0xDCU;
    rom[2] = 0x32U;
    rom[3] = 0x00U;
    rom[4] = 0xC0U;

    auto sys = assemble_sms(std::move(rom));
    mnemos::peripheral::controller_state held{};
    held.up = true;
    if (auto* dev = sys->port_device(0)) {
        dev->apply_state(held); // player 1 up held
    }
    sys->cpu.step_instruction(); // IN A,($DC)
    sys->cpu.step_instruction(); // LD ($C000),A

    const std::uint8_t v = sys->bus.read8(0xC000U);
    CHECK((v & 0x01U) == 0U); // up pressed -> bit 0 active-low
    CHECK((v & 0x02U) != 0U); // down released -> bit 1 high
}

TEST_CASE("assemble_sms vectors the VDP frame interrupt into the Z80", "[sms][irq]") {
    auto rom = blank_rom();
    // Main: enable display + frame IRQ, set IM 1, EI, then spin.
    rom[0] = 0x3EU;
    rom[1] = 0xE0U; // LD A,$E0  (display on + frame IRQ enable)
    rom[2] = 0xD3U;
    rom[3] = 0xBFU; // OUT ($BF),A  (control byte)
    rom[4] = 0x3EU;
    rom[5] = 0x81U; // LD A,$81  (code 2, register 1)
    rom[6] = 0xD3U;
    rom[7] = 0xBFU; // OUT ($BF),A  -> reg1 = $E0
    rom[8] = 0xEDU;
    rom[9] = 0x56U;  // IM 1
    rom[10] = 0xFBU; // EI
    rom[11] = 0x18U;
    rom[12] = 0xFEU; // JR $-2 (self loop)
    // IM 1 service routine at $0038: write a marker, EI, RETI.
    rom[0x38] = 0x3EU;
    rom[0x39] = 0x99U; // LD A,$99
    rom[0x3A] = 0x32U;
    rom[0x3B] = 0x00U;
    rom[0x3C] = 0xC0U; // LD ($C000),A
    rom[0x3D] = 0xFBU; // EI
    rom[0x3E] = 0xEDU;
    rom[0x3F] = 0x4DU; // RETI

    auto sys = assemble_sms(std::move(rom));
    // Run up to ~1.2 NTSC frames of cycles; the frame IRQ fires at vblank.
    for (int i = 0; i < 70000 && sys->bus.read8(0xC000U) != 0x99U; ++i) {
        sys->vdp.tick(1U);
        sys->cpu.tick(1U);
    }
    CHECK(sys->bus.read8(0xC000U) == 0x99U); // the ISR ran
}

TEST_CASE("gg_io decodes the Game Gear handset ports", "[sms][gg][io]") {
    sn76489 psg;
    gg_io gg;
    gg.enable(true);

    // $00 mode register: bit7 = START (active-low), bit6 = region, low bits 0.
    gg.set_export(true);
    CHECK(gg.read(0x00U) == 0xC0U); // START idle high, export region set
    gg.set_start(true);
    CHECK(gg.read(0x00U) == 0x40U); // START pressed clears bit 7
    gg.set_start(false);
    gg.set_export(false);
    CHECK(gg.read(0x00U) == 0x80U); // domestic region: bit 6 clear

    // $06 PSG stereo: a write routes to the PSG stereo register; reads open-bus.
    gg.write(0x06U, 0x3CU, psg);
    CHECK(psg.stereo_register() == 0x3CU);
    CHECK(gg.read(0x06U) == 0xFFU);

    // EXT serial link (unconnected): reset loopback + read-only bits.
    CHECK(gg.read(0x01U) == 0x7FU); // parallel data, all pins as direction inputs
    CHECK(gg.read(0x02U) == 0xFFU); // direction register reset value
    gg.write(0x03U, 0x5AU, psg);
    CHECK(gg.read(0x03U) == 0x5AU); // transmit buffer latches
    gg.write(0x05U, 0xFFU, psg);
    CHECK(gg.read(0x05U) == 0xF8U); // serial control: bits 0-2 are read-only
}

TEST_CASE("assemble_sms enables Game Gear mode", "[sms][gg]") {
    auto sys = assemble_sms(blank_rom(), {.game_gear = true});
    CHECK(sys->vdp.is_gg());
    CHECK_FALSE(sys->vdp.is_pal()); // the Game Gear is 60 Hz NTSC only

    // The GG LCD crops the Mode-4 256x192 frame to its central 160x144 window.
    const auto fb = sys->vdp.framebuffer();
    CHECK(fb.width == 160U);
    CHECK(fb.height == 144U);

    // A base Master System leaves GG mode off.
    CHECK_FALSE(assemble_sms(blank_rom())->vdp.is_gg());
}

TEST_CASE("assemble_sms routes the Game Gear $06 and $00 ports", "[sms][gg]") {
    auto rom = blank_rom();
    // LD A,$3C ; OUT ($06),A ; IN A,($00) ; LD ($C000),A
    rom[0] = 0x3EU;
    rom[1] = 0x3CU;
    rom[2] = 0xD3U;
    rom[3] = 0x06U;
    rom[4] = 0xDBU;
    rom[5] = 0x00U;
    rom[6] = 0x32U;
    rom[7] = 0x00U;
    rom[8] = 0xC0U;

    auto sys = assemble_sms(std::move(rom), {.game_gear = true});
    sys->cpu.step_instruction(); // LD A,$3C
    sys->cpu.step_instruction(); // OUT ($06),A -> PSG stereo register
    CHECK(sys->psg.stereo_register() == 0x3CU);

    sys->cpu.step_instruction();             // IN A,($00) -> GG mode register
    sys->cpu.step_instruction();             // LD ($C000),A
    CHECK(sys->bus.read8(0xC000U) == 0xC0U); // START idle, export region
}

TEST_CASE("detect_93c46 rejects an unknown cartridge", "[sms][eeprom]") {
    CHECK_FALSE(detect_93c46(blank_rom(0x8000U)));
}

TEST_CASE("assemble_sms wires the 93C46 EEPROM onto the cart path", "[sms][eeprom]") {
    auto sys = assemble_sms(blank_rom(0x8000U), {.eeprom_93c46 = true});
    REQUIRE(sys->eeprom_93c46_active);
    REQUIRE(sys->battery_ram().size() == 128U);

    // Until $FFFC enables it, $8000 is plain slot-2 ROM (blank cart reads 0).
    CHECK(sys->bus.read8(0x8000U) == 0x00U);

    // Enable the serial port; in standby the read-back is CLK-high(2) + DO-high(1).
    sys->bus.write8(0xFFFCU, 0x08U);
    CHECK(sys->bus.read8(0x8000U) == 0x03U);

    // Bit-bang a Microwire command stream over $8000 (DI=bit0, CLK=bit1, CS=bit2).
    const auto clk_bit = [&](bool di) {
        const auto base = static_cast<std::uint8_t>(0x04U | (di ? 1U : 0U)); // CS high, CLK low
        sys->bus.write8(0x8000U, base);
        sys->bus.write8(0x8000U, static_cast<std::uint8_t>(base | 0x02U)); // CLK rising edge
    };
    const auto send = [&](unsigned value, int n) {
        for (int i = n - 1; i >= 0; --i) {
            clk_bit(((value >> i) & 1U) != 0U);
        }
    };
    const auto command = [&](unsigned opcode8) {
        sys->bus.write8(0x8000U, 0x04U); // CS high, CLK low
        clk_bit(true);                   // START bit
        send(opcode8, 8);
    };

    command(0x30U);                  // EWEN (write-enable)
    sys->bus.write8(0x8000U, 0x00U); // CS low
    command(0x40U);                  // WRITE word 0
    send(0xABCDU, 16);
    sys->bus.write8(0x8000U, 0x00U); // CS low latches the write

    // The byte reached the EEPROM store (little-endian word 0).
    CHECK(sys->battery_ram()[0] == 0xCDU);
    CHECK(sys->battery_ram()[1] == 0xABU);

    // Disabling the port returns $8000 to ROM.
    sys->bus.write8(0xFFFCU, 0x00U);
    CHECK(sys->bus.read8(0x8000U) == 0x00U);
}
