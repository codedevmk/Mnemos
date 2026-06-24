#include "amiga500_system.hpp"

#include "scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::manifests::amiga500::amiga500_system;
    using mnemos::manifests::amiga500::assemble_amiga500;
    using agnus = mnemos::chips::video::agnus;

    [[nodiscard]] std::vector<std::uint8_t> tiny_kickstart() {
        std::vector<std::uint8_t> rom(amiga500_system::kickstart_window_size, 0x00U);
        const auto w16 = [&](std::size_t off, std::uint16_t v) {
            rom[off] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 1U] = static_cast<std::uint8_t>(v);
        };
        const auto w32 = [&](std::size_t off, std::uint32_t v) {
            rom[off + 0U] = static_cast<std::uint8_t>(v >> 24U);
            rom[off + 1U] = static_cast<std::uint8_t>(v >> 16U);
            rom[off + 2U] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 3U] = static_cast<std::uint8_t>(v);
        };
        w32(0x0000U, 0x0007F000U); // SSP inside chip RAM
        w32(0x0004U, amiga500_system::kickstart_base + 0x0008U);
        w16(0x0008U, 0x46FCU); // MOVE.W #$2700,SR
        w16(0x000AU, 0x2700U);
        w16(0x000CU, 0x60FEU); // BRA.S *
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> tiny_adf() {
        std::vector<std::uint8_t> adf(amiga500_system::floppy_dd_size, 0x00U);
        for (std::size_t i = 0; i < amiga500_system::floppy_sector_size; ++i) {
            adf[i] = static_cast<std::uint8_t>(i);
        }
        return adf;
    }

    void select_df0(amiga500_system& sys) {
        sys.cia_b.write(0x03U, 0xFFU); // DDRB: disk control lines are outputs.
        sys.cia_b.write(0x01U, 0x75U); // /MTR=0, /SEL0=0, /SIDE=1, /STEP=1.
    }

    void select_df1(amiga500_system& sys) {
        sys.cia_b.write(0x03U, 0xFFU);
        sys.cia_b.write(0x01U, 0x6DU); // /MTR=0, /SEL1=0, /SIDE=1, /STEP=1.
    }

    [[nodiscard]] bool joy_up(std::uint16_t joy) {
        return (((joy >> 9U) ^ (joy >> 8U)) & 0x01U) != 0U;
    }

    [[nodiscard]] bool joy_down(std::uint16_t joy) { return (((joy >> 1U) ^ joy) & 0x01U) != 0U; }

    [[nodiscard]] bool joy_left(std::uint16_t joy) { return (joy & 0x0200U) != 0U; }

    [[nodiscard]] bool joy_right(std::uint16_t joy) { return (joy & 0x0002U) != 0U; }

    [[nodiscard]] std::uint8_t keyboard_sdr(std::uint8_t raw_code) noexcept {
        const auto inverted = static_cast<std::uint8_t>(~raw_code);
        return static_cast<std::uint8_t>((inverted << 1U) | (inverted >> 7U));
    }

    void write_chip_word(amiga500_system& sys, std::uint32_t address, std::uint16_t value) {
        sys.bus.write8(address, static_cast<std::uint8_t>(value >> 8U));
        sys.bus.write8(address + 1U, static_cast<std::uint8_t>(value));
    }

    void write_kickstart_word(amiga500_system& sys, std::uint32_t offset, std::uint16_t value) {
        sys.kickstart_rom[offset] = static_cast<std::uint8_t>(value >> 8U);
        sys.kickstart_rom[offset + 1U] = static_cast<std::uint8_t>(value);
    }

    [[nodiscard]] std::uint16_t read_chip_word(const amiga500_system& sys,
                                               std::uint32_t address) noexcept {
        return static_cast<std::uint16_t>((sys.chip_ram[address] << 8U) |
                                          sys.chip_ram[address + 1U]);
    }

    void program_one_plane_display(amiga500_system& sys) {
        sys.write_custom_word(0x180U, 0x000FU); // COLOR00 = blue backdrop
        sys.write_custom_word(0x182U, 0x0F00U); // COLOR01 = red foreground
        sys.write_custom_word(0x100U, 0x1000U); // BPU = 1
        sys.write_custom_word(0x08EU, 0x2C00U);
        sys.write_custom_word(0x090U, 0xF400U);
        sys.write_custom_word(0x092U, 0x0038U);
        sys.write_custom_word(0x094U, 0x00D0U);
        sys.write_custom_word(0x0E0U, 0x0000U); // BPL1PTH
        sys.write_custom_word(0x0E2U, 0x0000U); // BPL1PTL
        sys.write_custom_word(0x096U, static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                                 agnus::dmacon_dmaen |
                                                                 agnus::dmacon_bplen));
    }

    void run_frame(mnemos::runtime::scheduler& scheduler) { scheduler.run_frame(); }

    void run_scanlines(amiga500_system& sys, std::uint32_t lines) {
        sys.agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * lines);
    }

    void run_blitter_to_idle(amiga500_system& sys) {
        for (std::uint32_t cycle = 0U; cycle < 1'000'000U; ++cycle) {
            if ((sys.read_custom_word(0x002U) & agnus::dmacon_bbusy) == 0U) {
                return;
            }
            sys.agnus.tick(1U);
        }
        FAIL("Amiga500 blitter did not retire within the bounded test window");
    }

    void acknowledge_keyboard(amiga500_system& sys) {
        sys.cia_a.write(0x04U, 0x01U); // Timer A latch = 1 for a short SP pulse.
        sys.cia_a.write(0x05U, 0x00U);
        sys.cia_a.write(0x0EU, 0x41U); // CRA: START | SPMODE output.
        sys.cia_a.write(0x0CU, 0x00U); // Drive KDAT/SP low.
        sys.cia_a.tick(12U);
        sys.cia_a.write(0x0EU, 0x00U); // Release KDAT/SP high.
        sys.service_keyboard_queue();
    }
} // namespace

TEST_CASE("amiga500 boots through the Kickstart reset overlay", "[manifests][amiga500]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    const auto regs = sys->cpu.cpu_registers();
    CHECK(regs.a[7] == 0x0007F000U);
    CHECK(regs.pc == amiga500_system::kickstart_base + 0x0008U);
    CHECK(sys->kickstart_overlay_active());

    // Writes under the overlay still land in chip RAM, but reads see Kickstart
    // until CIA-A PA0 is driven high.
    sys->bus.write8(0x000000U, 0x42U);
    CHECK(sys->bus.read8(0x000000U) == 0x00U); // ROM vector high byte

    sys->bus.write8(0x00BFE201U, 0x01U); // CIA-A DDRA bit 0 output
    sys->bus.write8(0x00BFE001U, 0x01U); // CIA-A PRA bit 0 = overlay off
    CHECK_FALSE(sys->kickstart_overlay_active());
    CHECK(sys->bus.read8(0x000000U) == 0x42U);
}

TEST_CASE("amiga500 joystick registers and CIA fire lines expose gamepad input",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->set_joystick(1U, static_cast<std::uint8_t>(
                              amiga500_system::joy_up | amiga500_system::joy_left |
                              amiga500_system::joy_fire | amiga500_system::joy_secondary_fire));
    const std::uint16_t joy1 = sys->read_custom_word(0x00CU);
    CHECK(joy_up(joy1));
    CHECK_FALSE(joy_down(joy1));
    CHECK(joy_left(joy1));
    CHECK_FALSE(joy_right(joy1));
    CHECK((sys->cia_a.read(0x00U) & 0x80U) == 0U);
    CHECK((sys->cia_a.read(0x00U) & 0x40U) != 0U);
    CHECK((sys->read_custom_word(0x016U) & 0x4000U) == 0U);

    sys->set_joystick(0U, static_cast<std::uint8_t>(amiga500_system::joy_down |
                                                    amiga500_system::joy_right |
                                                    amiga500_system::joy_fire));
    const std::uint16_t joy0 = sys->read_custom_word(0x00AU);
    CHECK_FALSE(joy_up(joy0));
    CHECK(joy_down(joy0));
    CHECK_FALSE(joy_left(joy0));
    CHECK(joy_right(joy0));
    CHECK((sys->cia_a.read(0x00U) & 0x40U) == 0U);
}

TEST_CASE("amiga500 mouse counters and buttons expose controller port input",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->set_mouse(0U, 3, -2, true, true, true);
    CHECK(sys->read_custom_word(0x00AU) == 0xFE03U);
    CHECK((sys->cia_a.read(0x00U) & 0x40U) == 0U);
    CHECK((sys->read_custom_word(0x016U) & 0x0400U) == 0U);
    CHECK((sys->read_custom_word(0x016U) & 0x0100U) == 0U);

    sys->set_mouse(0U, -4, 5, false, false, false);
    CHECK(sys->read_custom_word(0x00AU) == 0x03FFU);
    CHECK((sys->cia_a.read(0x00U) & 0x40U) != 0U);
    CHECK((sys->read_custom_word(0x016U) & 0x0400U) != 0U);
    CHECK((sys->read_custom_word(0x016U) & 0x0100U) != 0U);
}

TEST_CASE("amiga500 POTGO starts RC-calibrated pot counters by raster line",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->set_pot_position(0U, 3U, 5U);
    sys->set_pot_position(1U, 10U, 2U);
    sys->write_custom_word(0x034U, 0x0001U); // POTGO START.

    CHECK(sys->read_custom_word(0x012U) == 0x0000U);
    CHECK(sys->read_custom_word(0x014U) == 0x0000U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 7U);

    CHECK(sys->read_custom_word(0x012U) == 0x0000U);
    CHECK(sys->read_custom_word(0x014U) == 0x0000U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 2U);

    CHECK(sys->read_custom_word(0x012U) == 0x0202U);
    CHECK(sys->read_custom_word(0x014U) == 0x0202U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 2U);

    CHECK(sys->read_custom_word(0x012U) == 0x0403U);
    CHECK(sys->read_custom_word(0x014U) == 0x0204U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 8U);

    CHECK(sys->read_custom_word(0x012U) == 0x0503U);
    CHECK(sys->read_custom_word(0x014U) == 0x020AU);

    sys->set_pot_position(0U, 0xFFU, 0xFFU);
    sys->write_custom_word(0x034U, 0x0001U); // POTGO START.
    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 7U);
    CHECK(sys->read_custom_word(0x012U) == 0x0000U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    (agnus::scanlines_ntsc - 7U));

    CHECK(sys->read_custom_word(0x012U) == 0xFFFFU);
}

TEST_CASE("amiga500 keyboard events arrive through CIA-A serial input",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->cia_a.write(0x0DU, 0x88U);                  // Enable CIA serial interrupt mask.
    REQUIRE(sys->enqueue_keyboard_key(0x44U, true)); // Return down.

    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x44U));
    sys->cia_a.tick(1U);
    CHECK(sys->cia_a.irq_asserted());
    CHECK((sys->cia_a.read(0x0DU) & 0x08U) != 0U);
}

TEST_CASE("amiga500 keyboard queue waits for CIA serial acknowledgement",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    REQUIRE(sys->enqueue_keyboard_key(0x4CU, true));  // Cursor up down.
    REQUIRE(sys->enqueue_keyboard_key(0x4CU, false)); // Cursor up release.
    CHECK(sys->keyboard_pending_count() == 1U);
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x4CU));

    CHECK((sys->cia_a.read(0x0DU) & 0x08U) != 0U);
    sys->service_keyboard_queue();
    CHECK(sys->keyboard_pending_count() == 1U);
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x4CU));

    acknowledge_keyboard(*sys);
    CHECK(sys->keyboard_pending_count() == 0U);
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0xCCU));
}

TEST_CASE("amiga500 keyboard control codes and caps-lock LED use raw serial codes",
          "[manifests][amiga500][input]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    REQUIRE(sys->enqueue_keyboard_control_code(amiga500_system::keyboard_reset_warning_code));
    CHECK(sys->cia_a.read(0x0CU) ==
          keyboard_sdr(amiga500_system::keyboard_reset_warning_code));

    acknowledge_keyboard(*sys);
    REQUIRE(sys->press_caps_lock());
    CHECK(sys->keyboard_caps_lock_led_on());
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0x62U));

    acknowledge_keyboard(*sys);
    REQUIRE(sys->press_caps_lock());
    CHECK_FALSE(sys->keyboard_caps_lock_led_on());
    CHECK(sys->cia_a.read(0x0CU) == keyboard_sdr(0xE2U));
}

TEST_CASE("amiga500 custom registers drive OCS bitplane rendering", "[manifests][amiga500]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->bus.write8(0x000000U, 0x80U); // BPL1 first word = 0x8000
    sys->bus.write8(0x000001U, 0x00U);
    program_one_plane_display(*sys);

    mnemos::runtime::scheduler scheduler({{&sys->agnus, 2U},
                                          {&sys->cpu, 1U},
                                          {&sys->paula, 2U},
                                          {&sys->cia_a, 10U},
                                          {&sys->cia_b, 10U}},
                                         &sys->agnus);
    run_frame(scheduler);

    const auto frame = sys->agnus.framebuffer();
    REQUIRE(frame.pixels != nullptr);
    CHECK(frame.width == agnus::visible_width);
    CHECK(frame.height == agnus::visible_height_pal);
    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[1] == 0x000000FFU);
    CHECK(sys->denise.read_color(1U) == 0x0F00U);
}

TEST_CASE("amiga500 BPLCON1 custom register delays the Agnus bitplane serializer",
          "[manifests][amiga500][video]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->bus.write8(0x000000U, 0x80U); // BPL1 first word = 0x8000
    sys->bus.write8(0x000001U, 0x00U);
    program_one_plane_display(*sys);
    sys->write_custom_word(0x102U, 0x0011U); // Delay both playfield nibbles by one pixel.

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    const auto frame = sys->agnus.framebuffer();

    CHECK(frame.pixels[0] == 0x000000FFU);
    CHECK(frame.pixels[1] == 0x00FF0000U);
}

TEST_CASE("amiga500 BPLCON0 HIRES custom register exposes 640-pixel OCS rows",
          "[manifests][amiga500][video]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0000U, 0x8000U);
    write_chip_word(*sys, 39U * 2U, 0x0001U);
    sys->write_custom_word(0x180U, 0x000FU); // COLOR00 = blue backdrop
    sys->write_custom_word(0x182U, 0x0F00U); // COLOR01 = red foreground
    sys->write_custom_word(0x100U, 0x9000U); // HIRES | BPU = 1
    sys->write_custom_word(0x08EU, 0x2C00U);
    sys->write_custom_word(0x090U, 0xF400U);
    sys->write_custom_word(0x092U, 0x003CU);
    sys->write_custom_word(0x094U, 0x00D4U);
    sys->write_custom_word(0x0E0U, 0x0000U); // BPL1PTH
    sys->write_custom_word(0x0E2U, 0x0000U); // BPL1PTL
    sys->write_custom_word(0x096U, static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                             agnus::dmacon_dmaen |
                                                             agnus::dmacon_bplen));

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    const auto frame = sys->agnus.framebuffer();

    CHECK(frame.width == agnus::visible_width_hires);
    CHECK(frame.effective_stride() == agnus::framebuffer_stride);
    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[agnus::visible_width_hires - 1U] == 0x00FF0000U);
}

TEST_CASE("amiga500 DMACON routes audio DMA to Paula", "[manifests][amiga500]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->bus.write8(0x000100U, 0x10U);
    sys->bus.write8(0x000101U, 0xF0U);
    sys->write_custom_word(0x0A0U, 0x0000U); // AUD0LCH
    sys->write_custom_word(0x0A2U, 0x0100U); // AUD0LCL
    sys->write_custom_word(0x0A4U, 0x0001U); // AUD0LEN
    sys->write_custom_word(0x0A6U, 0x0001U); // AUD0PER
    sys->write_custom_word(0x0A8U, 0x0040U); // AUD0VOL
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_aud0en));

    REQUIRE(sys->paula.channel_active(0));
    std::array<std::int16_t, 4> samples{};
    sys->paula.generate(samples);
    CHECK(samples[0] != 0);
}

TEST_CASE("amiga500 disk DMA streams a mounted ADF track into chip RAM",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    sys->write_custom_word(0x020U, 0x0000U); // DSKPTH
    sys->write_custom_word(0x022U, 0x0200U); // DSKPTL
    sys->write_custom_word(0x07EU, 0x4489U); // DSKSYNC
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words_to_read = 8U;
    const std::uint16_t dsklen = static_cast<std::uint16_t>(0x8000U | words_to_read);
    sys->write_custom_word(0x024U, dsklen);
    sys->write_custom_word(0x024U, dsklen);

    CHECK(sys->chip_ram[0x0200U] == 0x00U);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_dskblk) == 0U);

    run_scanlines(*sys, 2U);
    CHECK(sys->chip_ram[0x0200U] == 0x44U);
    CHECK(sys->chip_ram[0x0201U] == 0x89U);
    CHECK(sys->chip_ram[0x0202U] == 0x44U);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_dskblk) == 0U);
    CHECK((sys->read_custom_word(0x01AU) & 0x8000U) != 0U);

    run_scanlines(*sys, sys->floppy_index_lines_per_revolution());
    CHECK(sys->chip_ram[0x0200U] == 0x44U);
    CHECK(sys->chip_ram[0x0201U] == 0x89U);
    CHECK(sys->chip_ram[0x0202U] == 0x44U);
    CHECK(sys->chip_ram[0x0203U] == 0x89U);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 disk byte ready is paced across color clocks",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 2U);
    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(lines_per_revolution, 0x00U);
    drive.track_stream[0] = 0xA5U;
    drive.track_stream[1] = 0x5AU;
    drive.stream_offset = 0U;
    drive.byte_clock_accumulator = 0U;

    CHECK((sys->read_custom_word(0x01AU) & 0x8000U) == 0U);
    sys->agnus.tick(agnus::color_clocks_per_line - 1U);
    CHECK((sys->read_custom_word(0x01AU) & 0x8000U) == 0U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);
    sys->floppy_drives[0].stream_offset = 1U;
    sys->floppy_drives[0].byte_clock_accumulator = 0U;

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());

    sys->agnus.tick(1U);
    const std::uint16_t first = sys->read_custom_word(0x01AU);
    CHECK((first & 0x8000U) != 0U);
    CHECK((first & 0x00FFU) == 0x00A5U);

    sys->agnus.tick(agnus::color_clocks_per_line - 1U);
    CHECK((sys->read_custom_word(0x01AU) & 0x8000U) == 0U);
    sys->agnus.tick(1U);
    const std::uint16_t second = sys->read_custom_word(0x01AU);
    CHECK((second & 0x8000U) != 0U);
    CHECK((second & 0x00FFU) == 0x005AU);
}

TEST_CASE("amiga500 ADKCON WORDSYNC gates disk read DMA until DSKSYNC",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 8U);
    auto& drive = sys->floppy_drives[0];
    drive.track_stream.assign(lines_per_revolution, 0x00U);
    drive.track_stream[0] = 0x12U;
    drive.track_stream[1] = 0x34U;
    drive.track_stream[2] = 0x44U;
    drive.track_stream[3] = 0x89U;
    drive.track_stream[4] = 0xAAU;
    drive.track_stream[5] = 0xBBU;
    drive.track_stream[6] = 0xCCU;
    drive.track_stream[7] = 0xDDU;
    drive.stream_offset = 0U;
    drive.byte_clock_accumulator = 0U;

    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0600U);
    sys->write_custom_word(0x07EU, 0x4489U);
    sys->write_custom_word(0x09EU,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit | 0x0400U));
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words_to_read = 2U;
    const std::uint16_t dsklen = static_cast<std::uint16_t>(0x8000U | words_to_read);
    sys->write_custom_word(0x024U, dsklen);
    sys->write_custom_word(0x024U, dsklen);
    REQUIRE(sys->disk_wordsync_waiting);

    run_scanlines(*sys, 2U);
    CHECK(sys->disk_wordsync_waiting);
    CHECK(sys->chip_ram[0x0600U] == 0x00U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);
    sys->floppy_drives[0].stream_offset = 0U;
    sys->disk_wordsync_waiting = false;

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(sys->disk_wordsync_waiting);

    run_scanlines(*sys, 2U);
    CHECK_FALSE(sys->disk_wordsync_waiting);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_dsksyn) != 0U);
    CHECK(sys->chip_ram[0x0600U] == 0x00U);

    run_scanlines(*sys, 4U);
    CHECK(sys->chip_ram[0x0600U] == 0xAAU);
    CHECK(sys->chip_ram[0x0601U] == 0xBBU);
    CHECK(sys->chip_ram[0x0602U] == 0xCCU);
    CHECK(sys->chip_ram[0x0603U] == 0xDDU);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 disk write DMA patches and saves the raw track stream",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);
    sys->set_floppy_write_protected(0U, false);

    sys->chip_ram[0x0400U] = 0xDEU;
    sys->chip_ram[0x0401U] = 0xADU;
    sys->chip_ram[0x0402U] = 0xBEU;
    sys->chip_ram[0x0403U] = 0xEFU;
    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0400U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words = 2U;
    const std::uint16_t write_dsklen = static_cast<std::uint16_t>(0xC000U | words);
    sys->write_custom_word(0x024U, write_dsklen);
    sys->write_custom_word(0x024U, write_dsklen);

    const std::uint16_t dskbytr = sys->read_custom_word(0x01AU);
    CHECK((dskbytr & 0x6000U) == 0x6000U);

    run_scanlines(*sys, 2U);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_dskblk) != 0U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);
    REQUIRE(!sys->floppy_drives[0].track_stream.empty());
    sys->floppy_drives[0].track_stream[0] = 0x00U;

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());

    sys->write_custom_word(0x09CU, amiga500_system::int_dskblk);
    sys->floppy_drives[0].stream_offset = 0U;
    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0500U);
    const std::uint16_t read_dsklen = static_cast<std::uint16_t>(0x8000U | words);
    sys->write_custom_word(0x024U, read_dsklen);
    sys->write_custom_word(0x024U, read_dsklen);

    run_scanlines(*sys, 1U);
    CHECK(sys->chip_ram[0x0500U] == 0xDEU);
    CHECK(sys->chip_ram[0x0501U] == 0xADU);
    CHECK(sys->chip_ram[0x0502U] == 0xBEU);
    CHECK(sys->chip_ram[0x0503U] == 0xEFU);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 disk write DMA decodes AmigaDOS sectors back into the ADF image",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);
    sys->set_floppy_write_protected(0U, false);

    std::vector<std::uint8_t> patched = tiny_adf();
    patched[0x0000U] = 0xA5U;
    patched[0x01FFU] = 0x5AU;
    auto reference = assemble_amiga500(tiny_kickstart());
    REQUIRE(reference != nullptr);
    REQUIRE(reference->mount_floppy(patched));

    constexpr std::size_t raw_sector_bytes =
        4U + 10U * 4U + 2U * 8U + amiga500_system::floppy_sector_size * 2U;
    REQUIRE(reference->floppy_drives[0].track_stream.size() >= raw_sector_bytes);
    std::copy_n(reference->floppy_drives[0].track_stream.begin(), raw_sector_bytes,
                sys->chip_ram.begin() + 0x0800U);

    sys->floppy_drives[0].stream_offset = 0U;
    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0800U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));

    constexpr std::uint16_t words = static_cast<std::uint16_t>(raw_sector_bytes / 2U);
    const std::uint16_t write_dsklen = static_cast<std::uint16_t>(0xC000U | words);
    sys->write_custom_word(0x024U, write_dsklen);
    sys->write_custom_word(0x024U, write_dsklen);

    run_scanlines(*sys, sys->floppy_index_lines_per_revolution());

    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_dskblk) != 0U);
    REQUIRE(sys->floppy_drives[0].image.size() == amiga500_system::floppy_dd_size);
    CHECK(sys->floppy_drives[0].image[0x0000U] == 0xA5U);
    CHECK(sys->floppy_drives[0].image[0x01FFU] == 0x5AU);
}

TEST_CASE("amiga500 paced disk DMA survives system save state",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    sys->write_custom_word(0x020U, 0x0000U);
    sys->write_custom_word(0x022U, 0x0300U);
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_dsken));
    constexpr std::uint16_t words_to_read = 8U;
    const std::uint16_t dsklen = static_cast<std::uint16_t>(0x8000U | words_to_read);
    sys->write_custom_word(0x024U, dsklen);
    sys->write_custom_word(0x024U, dsklen);
    run_scanlines(*sys, 1U);
    CHECK(sys->chip_ram[0x0300U] == 0x44U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    std::fill(sys->chip_ram.begin() + 0x0300U, sys->chip_ram.begin() + 0x0310U,
              std::uint8_t{0x00U});
    run_scanlines(*sys, sys->floppy_index_lines_per_revolution());

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    run_scanlines(*sys, sys->floppy_index_lines_per_revolution());

    CHECK(sys->chip_ram[0x0300U] == 0x44U);
    CHECK(sys->chip_ram[0x0301U] == 0x89U);
    CHECK(sys->chip_ram[0x0302U] == 0x44U);
    CHECK(sys->chip_ram[0x0303U] == 0x89U);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_dskblk) != 0U);
}

TEST_CASE("amiga500 CIAB disk control steps the mounted ADF drive", "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));

    select_df0(*sys);
    CHECK(sys->floppy_cylinder() == 0U);
    CHECK(sys->floppy_side() == 0U);

    sys->cia_b.write(0x01U, 0x74U); // falling /STEP with DIR=0 moves inward.
    CHECK(sys->floppy_cylinder() == 1U);

    sys->cia_b.write(0x01U, 0x77U); // /STEP high, DIR=1.
    sys->cia_b.write(0x01U, 0x76U); // falling /STEP with DIR=1 moves outward.
    CHECK(sys->floppy_cylinder() == 0U);

    sys->cia_b.write(0x01U, 0x71U); // /SIDE=0 selects side 1.
    CHECK(sys->floppy_side() == 1U);
}

TEST_CASE("amiga500 CIAB disk select lines address independent DF0-DF3 state",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(0U, tiny_adf()));
    REQUIRE(sys->mount_floppy(1U, tiny_adf()));

    select_df1(*sys);
    CHECK(sys->selected_floppy_drive() == 1U);
    CHECK(sys->floppy_loaded(1U));
    CHECK(sys->floppy_cylinder(1U) == 0U);

    sys->cia_b.write(0x01U, 0x6CU); // Falling /STEP on DF1.
    CHECK(sys->floppy_cylinder(0U) == 0U);
    CHECK(sys->floppy_cylinder(1U) == 1U);
    CHECK((sys->cia_a.read(0x00U) & 0x10U) != 0U);

    select_df0(*sys);
    CHECK(sys->selected_floppy_drive() == 0U);
    CHECK(sys->floppy_cylinder(0U) == 0U);
    CHECK((sys->cia_a.read(0x00U) & 0x10U) == 0U);

    sys->unmount_floppy(1U);
    select_df1(*sys);
    CHECK_FALSE(sys->floppy_loaded(1U));
    CHECK((sys->cia_a.read(0x00U) & 0x20U) != 0U);
    CHECK((sys->cia_a.read(0x00U) & 0x04U) == 0U);
}

TEST_CASE("amiga500 multidrive selection survives system save state",
          "[manifests][amiga500][disk][save]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(0U, tiny_adf()));
    REQUIRE(sys->mount_floppy(1U, tiny_adf()));

    select_df1(*sys);
    sys->cia_b.write(0x01U, 0x6CU);
    CHECK(sys->floppy_cylinder(1U) == 1U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    select_df0(*sys);
    sys->cia_b.write(0x01U, 0x74U);
    CHECK(sys->floppy_cylinder(0U) == 1U);

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(sys->selected_floppy_drive() == 1U);
    CHECK(sys->floppy_cylinder(0U) == 0U);
    CHECK(sys->floppy_cylinder(1U) == 1U);
    CHECK((sys->cia_a.read(0x00U) & 0x10U) != 0U);
}

TEST_CASE("amiga500 CIAA disk change latch clears after a selected step",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    select_df0(*sys);
    CHECK((sys->cia_a.read(0x00U) & 0x04U) == 0U);

    REQUIRE(sys->mount_floppy(tiny_adf()));
    CHECK((sys->cia_a.read(0x00U) & 0x04U) == 0U);
    CHECK((sys->cia_a.read(0x00U) & 0x10U) == 0U);

    sys->cia_b.write(0x01U, 0x74U); // falling /STEP with media present clears /CHNG.
    CHECK((sys->cia_a.read(0x00U) & 0x04U) != 0U);
    CHECK(sys->floppy_cylinder() == 1U);

    sys->unmount_floppy();
    select_df0(*sys);
    CHECK((sys->cia_a.read(0x00U) & 0x04U) == 0U);
    CHECK((sys->cia_a.read(0x00U) & 0x10U) != 0U);
}

TEST_CASE("amiga500 selected disk rotation pulses CIAB FLAG at index",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    sys->cia_b.write(0x0DU, 0x90U); // Enable FLAG interrupt mask.
    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 1U);

    run_scanlines(*sys, lines_per_revolution - 1U);
    sys->cia_b.tick(1U);
    CHECK_FALSE(sys->cia_b.irq_asserted());
    CHECK((sys->cia_b.read(0x0DU) & 0x10U) == 0U);

    run_scanlines(*sys, 1U);
    sys->cia_b.tick(1U);
    CHECK(sys->cia_b.irq_asserted());
    CHECK((sys->cia_b.read(0x0DU) & 0x90U) == 0x90U);

    run_scanlines(*sys, lines_per_revolution - 1U);
    sys->cia_b.tick(1U);
    CHECK_FALSE(sys->cia_b.irq_asserted());
}

TEST_CASE("amiga500 floppy index phase survives system save state",
          "[manifests][amiga500][disk]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);

    const std::uint32_t lines_per_revolution = sys->floppy_index_lines_per_revolution();
    REQUIRE(lines_per_revolution > 1U);
    run_scanlines(*sys, lines_per_revolution - 1U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    run_scanlines(*sys, 1U);
    CHECK((sys->cia_b.read(0x0DU) & 0x10U) != 0U);

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());

    run_scanlines(*sys, 1U);
    CHECK((sys->cia_b.read(0x0DU) & 0x10U) != 0U);
}

TEST_CASE("amiga500 vblank requests a level-3 interrupt source", "[manifests][amiga500]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x09AU, static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                              amiga500_system::int_master |
                                                              amiga500_system::int_vertb));
    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);

    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_vertb) != 0U);
    CHECK(sys->frame_index == 1U);
}

TEST_CASE("amiga500 Copper MOVEs update board-owned custom registers",
          "[manifests][amiga500][copper]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    constexpr std::uint32_t list = 0x0100U;
    write_chip_word(*sys, list + 0U, 0x0180U); // COLOR00 = green backdrop.
    write_chip_word(*sys, list + 2U, 0x00F0U);
    write_chip_word(*sys, list + 4U, 0x009CU); // INTREQ = COPER.
    write_chip_word(*sys, list + 6U,
                    static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                               amiga500_system::int_coper));
    write_chip_word(*sys, list + 8U, 0xFFFFU);
    write_chip_word(*sys, list + 10U, 0xFFFEU);

    sys->write_custom_word(0x080U, 0x0000U); // COP1LCH
    sys->write_custom_word(0x082U, static_cast<std::uint16_t>(list));
    sys->write_custom_word(0x09AU, static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                              amiga500_system::int_master |
                                                              amiga500_system::int_coper));
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_copen));

    sys->agnus.tick(8U);
    CHECK(sys->read_custom_word(0x180U) == 0x00F0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_coper) != 0U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    CHECK(sys->agnus.framebuffer().pixels[0] == 0x0000FF00U);
}

TEST_CASE("amiga500 blitter copies A channel words to D through BLTSIZE",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0x1234U);
    sys->write_custom_word(0x050U, 0x0000U); // BLTAPTH
    sys->write_custom_word(0x052U, 0x0100U); // BLTAPTL
    sys->write_custom_word(0x054U, 0x0000U); // BLTDPTH
    sys->write_custom_word(0x056U, 0x0200U); // BLTDPTL
    sys->write_custom_word(0x040U, 0x09F0U); // USEA|USED, minterm D=A.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));
    sys->write_custom_word(0x09AU, static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                              amiga500_system::int_master |
                                                              amiga500_system::int_blit));

    sys->write_custom_word(0x058U, 0x0041U); // one row, one word.

    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) != 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_blit) == 0U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0x1234U);
    CHECK(sys->read_custom_word(0x076U) == 0x1234U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) == 0U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bzero) == 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_blit) != 0U);
}

TEST_CASE("amiga500 blitter keeps BBUSY and BLIT IRQ timing across save state",
          "[manifests][amiga500][blitter][state]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0x5678U);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0x0100U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x040U, 0x09F0U); // USEA|USED, minterm D=A.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));
    sys->write_custom_word(0x09AU, static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                              amiga500_system::int_master |
                                                              amiga500_system::int_blit));

    sys->write_custom_word(0x058U, 0x0041U);
    REQUIRE((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) != 0U);
    sys->agnus.tick(1U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    sys->blitter_cycles_remaining = 0U;
    sys->agnus.set_blitter_busy(false);
    sys->write_custom_word(0x09CU, static_cast<std::uint16_t>(amiga500_system::int_blit));

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) != 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_blit) == 0U);

    sys->agnus.tick(1U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) == 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_blit) != 0U);
    CHECK(read_chip_word(*sys, 0x0200U) == 0x5678U);
}

TEST_CASE("amiga500 BLTPRI stalls CPU chip-RAM bus cycles while blitter DMA is busy",
          "[manifests][amiga500][blitter][timing]") {
    auto run_move_with_blitter_priority = [](bool blitter_priority) {
        auto sys = assemble_amiga500(tiny_kickstart());
        REQUIRE(sys != nullptr);

        constexpr std::uint32_t code_offset = 0x0100U;
        constexpr std::uint32_t data_address = 0x0200U;
        write_kickstart_word(*sys, code_offset, 0x3010U); // MOVE.W (A0),D0.
        write_chip_word(*sys, data_address, 0x1234U);
        sys->overlay_active = false;

        auto regs = sys->cpu.cpu_registers();
        regs.pc = amiga500_system::kickstart_base + code_offset;
        regs.a[0] = data_address;
        sys->cpu.set_registers(regs);

        sys->write_custom_word(0x096U,
                               static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                          agnus::dmacon_dmaen |
                                                          agnus::dmacon_blten |
                                                          (blitter_priority
                                                               ? agnus::dmacon_bltpri
                                                               : 0U)));
        sys->write_custom_word(0x040U, 0x0100U); // D channel only.
        sys->write_custom_word(0x058U, static_cast<std::uint16_t>((3U << 6U) | 4U));
        const auto expected_wait = static_cast<std::uint32_t>(sys->blitter_cycles_remaining);

        const int cycles = sys->cpu.step_instruction();
        const auto sources = sys->cpu.diagnostics().last_cycle_sources();
        CHECK(sys->cpu.cpu_registers().d[0] == 0x1234U);
        return std::array<std::uint32_t, 3>{
            static_cast<std::uint32_t>(cycles), sources.external_wait_cycles, expected_wait};
    };

    const auto normal = run_move_with_blitter_priority(false);
    const auto priority = run_move_with_blitter_priority(true);

    CHECK(normal[1] == 0U);
    CHECK(priority[1] == priority[2]);
    CHECK(priority[0] == normal[0] + priority[2]);
}

TEST_CASE("amiga500 high-resolution display DMA stalls CPU chip-RAM bus cycles during fetch",
          "[manifests][amiga500][video][timing]") {
    auto run_move_during_display_fetch = [](std::uint16_t bplcon0) {
        auto sys = assemble_amiga500(tiny_kickstart());
        REQUIRE(sys != nullptr);

        constexpr std::uint32_t code_offset = 0x0120U;
        constexpr std::uint32_t data_address = 0x0240U;
        constexpr std::uint32_t fetch_clock = 0x50U;
        write_kickstart_word(*sys, code_offset, 0x3010U); // MOVE.W (A0),D0.
        write_chip_word(*sys, data_address, 0xCAFEU);
        sys->overlay_active = false;

        sys->write_custom_word(0x100U, bplcon0);
        sys->write_custom_word(0x08EU, 0x2C00U);
        sys->write_custom_word(0x090U, 0xF400U);
        sys->write_custom_word(0x092U, 0x003CU);
        sys->write_custom_word(0x094U, 0x00D4U);
        sys->write_custom_word(0x096U,
                               static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                          agnus::dmacon_dmaen |
                                                          agnus::dmacon_bplen));
        sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                            agnus::display_line_origin +
                        fetch_clock);

        auto regs = sys->cpu.cpu_registers();
        regs.pc = amiga500_system::kickstart_base + code_offset;
        regs.a[0] = data_address;
        sys->cpu.set_registers(regs);

        const int cycles = sys->cpu.step_instruction();
        const auto sources = sys->cpu.diagnostics().last_cycle_sources();
        CHECK(sys->cpu.cpu_registers().d[0] == 0xCAFEU);
        return std::array<std::uint32_t, 2>{static_cast<std::uint32_t>(cycles),
                                            sources.external_wait_cycles};
    };

    constexpr std::uint32_t ddf_start = 0x3CU;
    constexpr std::uint32_t words_per_hires_line = 40U;
    constexpr std::uint32_t clocks_per_hires_word = 4U;
    constexpr std::uint32_t fetch_clock = 0x50U;
    constexpr std::uint32_t expected_wait =
        ((ddf_start + words_per_hires_line * clocks_per_hires_word) - fetch_clock) * 2U;

    const auto one_plane = run_move_during_display_fetch(0x9000U);  // HIRES | BPU = 1.
    const auto four_planes = run_move_during_display_fetch(0xC000U); // HIRES | BPU = 4.

    CHECK(one_plane[1] == 0U);
    CHECK(four_planes[1] == expected_wait);
    CHECK(four_planes[0] == one_plane[0] + expected_wait);
}

TEST_CASE("amiga500 blitter applies first-word masks and minterms",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0xFFFFU); // A mask source, restricted by BLTAFWM.
    write_chip_word(*sys, 0x0110U, 0xAAAAU); // B foreground.
    write_chip_word(*sys, 0x0120U, 0x5555U); // C/D background.
    sys->write_custom_word(0x044U, 0x00FFU); // BLTAFWM
    sys->write_custom_word(0x046U, 0xFFFFU); // BLTALWM
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0x0100U);
    sys->write_custom_word(0x04CU, 0x0000U);
    sys->write_custom_word(0x04EU, 0x0110U);
    sys->write_custom_word(0x048U, 0x0000U);
    sys->write_custom_word(0x04AU, 0x0120U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0120U);
    sys->write_custom_word(0x040U, 0x0FCAU); // USEA|USEB|USEC|USED, D=(A&B)|(~A&C).
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0041U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0120U) == 0x55AAU);
    CHECK(sys->paula.chipram()[0x0120U] == 0x55U);
    CHECK(sys->paula.chipram()[0x0121U] == 0xAAU);
}

TEST_CASE("amiga500 blitter performs inclusive area fill from the right edge",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0x2418U);
    sys->write_custom_word(0x044U, 0xFFFFU); // Fill mode keeps A first/last masks open.
    sys->write_custom_word(0x046U, 0xFFFFU);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0x0100U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x040U, 0x09F0U); // USEA|USED, minterm D=A.
    sys->write_custom_word(0x042U, 0x000AU); // IFE|DESC: inclusive fill scans right-to-left.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0041U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0x3C18U);
    CHECK(sys->read_custom_word(0x076U) == 0x3C18U);
}

TEST_CASE("amiga500 blitter performs exclusive area fill from the right edge",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0x2418U);
    sys->write_custom_word(0x044U, 0xFFFFU);
    sys->write_custom_word(0x046U, 0xFFFFU);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0x0100U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x040U, 0x09F0U); // USEA|USED, minterm D=A.
    sys->write_custom_word(0x042U, 0x0012U); // EFE|DESC: exclusive fill scans right-to-left.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0041U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0x1C08U);
    CHECK(sys->read_custom_word(0x076U) == 0x1C08U);
}

TEST_CASE("amiga500 blitter area fill honors fill carry input",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0100U, 0x0000U);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0x0100U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x040U, 0x09F0U); // USEA|USED, minterm D=A.
    sys->write_custom_word(0x042U, 0x0016U); // EFE|FCI|DESC.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0041U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0xFFFFU);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bzero) == 0U);
}

TEST_CASE("amiga500 blitter line mode draws a shallow octant line",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x048U, 0x0000U); // BLTCPTH
    sys->write_custom_word(0x04AU, 0x0200U); // BLTCPTL
    sys->write_custom_word(0x054U, 0x0000U); // BLTDPTH
    sys->write_custom_word(0x056U, 0x0200U); // BLTDPTL
    sys->write_custom_word(0x050U, 0x0000U); // BLTAPTH
    sys->write_custom_word(0x052U, 0xFFFEU); // 4 * dy - 2 * dx = -2.
    sys->write_custom_word(0x064U, 0xFFF4U); // BLTAMOD = 4 * (dy - dx).
    sys->write_custom_word(0x062U, 0x0008U); // BLTBMOD = 4 * dy.
    sys->write_custom_word(0x060U, 0x0020U); // BLTCMOD = row stride.
    sys->write_custom_word(0x066U, 0x0020U); // BLTDMOD = row stride.
    sys->write_custom_word(0x072U, 0xFFFFU); // Solid texture.
    sys->write_custom_word(0x040U, 0x0BCAU); // USEA|USEC|USED, D=(A&B)|(~A&C).
    sys->write_custom_word(0x042U, 0x0051U); // SIGN|SUD|LINE: shallow right/down.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0182U); // width=2, length=6 dots.
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0xC000U);
    CHECK(read_chip_word(*sys, 0x0220U) == 0x3000U);
    CHECK(read_chip_word(*sys, 0x0240U) == 0x0C00U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bbusy) == 0U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bzero) == 0U);
    CHECK((sys->read_custom_word(0x01EU) & amiga500_system::int_blit) != 0U);
}

TEST_CASE("amiga500 blitter line mode draws a steep octant line",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x048U, 0x0000U);
    sys->write_custom_word(0x04AU, 0x0200U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0xFFFEU);
    sys->write_custom_word(0x064U, 0xFFF4U);
    sys->write_custom_word(0x062U, 0x0008U);
    sys->write_custom_word(0x060U, 0x0020U);
    sys->write_custom_word(0x066U, 0x0020U);
    sys->write_custom_word(0x072U, 0xFFFFU);
    sys->write_custom_word(0x040U, 0x0BCAU); // USEA|USEC|USED, D=(A&B)|(~A&C).
    sys->write_custom_word(0x042U, 0x0041U); // SIGN|LINE: steep right/down.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0182U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0x8000U);
    CHECK(read_chip_word(*sys, 0x0220U) == 0x8000U);
    CHECK(read_chip_word(*sys, 0x0240U) == 0x4000U);
    CHECK(read_chip_word(*sys, 0x0260U) == 0x4000U);
    CHECK(read_chip_word(*sys, 0x0280U) == 0x2000U);
    CHECK(read_chip_word(*sys, 0x02A0U) == 0x2000U);
}

TEST_CASE("amiga500 blitter line mode applies texture start and SING",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x048U, 0x0000U);
    sys->write_custom_word(0x04AU, 0x0200U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0xFFFAU); // 4 * 0 - 2 * 3 = -6.
    sys->write_custom_word(0x064U, 0xFFF4U);
    sys->write_custom_word(0x062U, 0x0000U);
    sys->write_custom_word(0x060U, 0x0020U);
    sys->write_custom_word(0x066U, 0x0020U);
    sys->write_custom_word(0x072U, 0x4001U); // Texture bits 0 and 14 are set.
    sys->write_custom_word(0x040U, 0x0BCAU); // USEA|USEC|USED, D=(A&B)|(~A&C).
    sys->write_custom_word(0x042U, 0x0051U); // SIGN|SUD|LINE.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0102U); // Horizontal line, four dots.
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0xA000U);

    write_chip_word(*sys, 0x0200U, 0x0000U);
    sys->write_custom_word(0x048U, 0x0000U);
    sys->write_custom_word(0x04AU, 0x0200U);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0xFFFAU);
    sys->write_custom_word(0x072U, 0xFFFFU);
    sys->write_custom_word(0x042U, 0x0053U); // SIGN|SUD|SING|LINE.

    sys->write_custom_word(0x058U, 0x0102U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0x8000U);
}

TEST_CASE("amiga500 blitter zero result updates DMACONR BZERO",
          "[manifests][amiga500][blitter]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    write_chip_word(*sys, 0x0200U, 0xFFFFU);
    sys->write_custom_word(0x054U, 0x0000U);
    sys->write_custom_word(0x056U, 0x0200U);
    sys->write_custom_word(0x040U, 0x0100U); // USED, minterm 0 => clear destination.
    sys->write_custom_word(0x096U,
                           static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                      agnus::dmacon_dmaen | agnus::dmacon_blten));

    sys->write_custom_word(0x058U, 0x0041U);
    run_blitter_to_idle(*sys);

    CHECK(read_chip_word(*sys, 0x0200U) == 0x0000U);
    CHECK((sys->read_custom_word(0x002U) & agnus::dmacon_bzero) != 0U);
}

TEST_CASE("amiga500 custom sprite registers render through Agnus", "[manifests][amiga500][video]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->write_custom_word(0x1A2U, 0x0F00U); // COLOR17 = red
    sys->write_custom_word(0x1A4U, 0x00F0U); // COLOR18 = green
    sys->write_custom_word(0x140U, 0x2C20U); // SPR0POS: visible line 0, x 0.
    sys->write_custom_word(0x142U, 0x2D00U); // SPR0CTL: one visible line.
    sys->write_custom_word(0x146U, 0x4000U); // SPR0DATB: pixel 1 high bit.
    sys->write_custom_word(0x144U, 0x8000U); // SPR0DATA: pixel 0 low bit, arm.

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    const auto frame = sys->agnus.framebuffer();

    CHECK(frame.pixels[0] == 0x00FF0000U);
    CHECK(frame.pixels[1] == 0x0000FF00U);
}

TEST_CASE("amiga500 BPLCON2 custom priority places sprites ahead of a playfield",
          "[manifests][amiga500][video]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->bus.write8(0x000000U, 0x80U); // BPL1 first word = 0x8000.
    sys->bus.write8(0x000001U, 0x00U);
    program_one_plane_display(*sys);
    sys->write_custom_word(0x1A2U, 0x00F0U); // COLOR17 = green sprite.
    sys->write_custom_word(0x104U, 0x0020U); // PF2 priority slot 4: behind sprites.
    sys->write_custom_word(0x140U, 0x2C20U);
    sys->write_custom_word(0x142U, 0x2D00U);
    sys->write_custom_word(0x144U, 0x8000U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    const auto frame = sys->agnus.framebuffer();

    CHECK(frame.pixels[0] == 0x0000FF00U);
}

TEST_CASE("amiga500 CLXCON and CLXDAT expose OCS collision latches",
          "[manifests][amiga500][video]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);

    sys->bus.write8(0x000000U, 0x80U); // BPL1 first word = 0x8000.
    sys->bus.write8(0x000001U, 0x00U);
    program_one_plane_display(*sys);
    sys->write_custom_word(0x098U, 0x0041U); // Include BPL1 and require BPL1=1.
    sys->write_custom_word(0x140U, 0x2C20U); // SPR0 over the first playfield pixel.
    sys->write_custom_word(0x142U, 0x2D00U);
    sys->write_custom_word(0x144U, 0x8000U);
    sys->write_custom_word(0x150U, 0x2C20U); // SPR2 overlaps SPR0.
    sys->write_custom_word(0x152U, 0x2D00U);
    sys->write_custom_word(0x154U, 0x8000U);

    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                    agnus::scanlines_pal);
    const std::uint16_t first = sys->read_custom_word(0x00EU);

    CHECK((first & 0x0002U) != 0U);
    CHECK((first & 0x0004U) != 0U);
    CHECK((first & 0x0200U) != 0U);
    CHECK(sys->read_custom_word(0x00EU) == 0U);
}

TEST_CASE("amiga500 save_state restores overlay and chip RAM", "[manifests][amiga500]") {
    auto sys = assemble_amiga500(tiny_kickstart());
    REQUIRE(sys != nullptr);
    sys->bus.write8(0x000123U, 0x5AU);
    sys->bus.write8(0x00BFE201U, 0x01U);
    sys->bus.write8(0x00BFE001U, 0x01U);
    sys->write_custom_word(0x040U, 0x09F0U);
    sys->write_custom_word(0x044U, 0x00FFU);
    sys->write_custom_word(0x050U, 0x0001U);
    sys->write_custom_word(0x052U, 0x2340U);
    sys->write_custom_word(0x064U, 0xFFFEU);
    sys->set_pot_position(0U, 6U, 8U);
    sys->write_custom_word(0x034U, 0x0001U);
    sys->agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) * 10U);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    select_df0(*sys);
    sys->cia_b.write(0x01U, 0x74U);
    REQUIRE(sys->press_caps_lock());
    CHECK(sys->keyboard_caps_lock_led_on());
    CHECK((sys->cia_a.read(0x00U) & 0x04U) != 0U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    sys->bus.write8(0x000123U, 0x00U);
    sys->overlay_active = true;
    sys->write_custom_word(0x040U, 0x0000U);
    sys->write_custom_word(0x044U, 0xFFFFU);
    sys->write_custom_word(0x050U, 0x0000U);
    sys->write_custom_word(0x052U, 0x0000U);
    sys->write_custom_word(0x064U, 0x0000U);
    sys->set_pot_position(0U, 1U, 1U);
    sys->write_custom_word(0x034U, 0x0001U);
    REQUIRE(sys->mount_floppy(tiny_adf()));
    sys->keyboard_caps_lock_led = false;
    CHECK((sys->cia_a.read(0x00U) & 0x04U) == 0U);

    mnemos::chips::state_reader reader(blob);
    sys->load_state(reader);
    REQUIRE(reader.ok());
    CHECK_FALSE(sys->kickstart_overlay_active());
    CHECK(sys->bus.read8(0x000123U) == 0x5AU);
    CHECK(sys->read_custom_word(0x040U) == 0x09F0U);
    CHECK(sys->read_custom_word(0x044U) == 0x00FFU);
    CHECK(sys->read_custom_word(0x050U) == 0x0001U);
    CHECK(sys->read_custom_word(0x052U) == 0x2340U);
    CHECK(sys->read_custom_word(0x064U) == 0xFFFEU);
    CHECK(sys->read_custom_word(0x012U) == 0x0303U);
    CHECK(sys->floppy_cylinder() == 1U);
    CHECK(sys->keyboard_caps_lock_led_on());
    CHECK((sys->cia_a.read(0x00U) & 0x04U) != 0U);
}
