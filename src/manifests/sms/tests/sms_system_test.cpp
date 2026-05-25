#include "sms_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::manifests::sms::assemble_sms;
    using mnemos::manifests::sms::sms_config;
    namespace pad_button = mnemos::manifests::sms::pad_button;

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
    auto ntsc = assemble_sms(blank_rom(), {.video_region = sms_config::region::ntsc});
    CHECK_FALSE(ntsc->vdp.is_pal());
    auto pal = assemble_sms(blank_rom(), {.video_region = sms_config::region::pal});
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
    sys->set_pad(0, pad_button::up); // player 1 up held
    sys->cpu.step_instruction();     // IN A,($DC)
    sys->cpu.step_instruction();     // LD ($C000),A

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
