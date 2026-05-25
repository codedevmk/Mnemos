#include "full_drive.hpp"

#include "chip_registry.hpp"
#include "d64_image.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::storage::c1541::d64_image;
    using mnemos::chips::storage::c1541::full_drive;

    // A 16 KiB ROM that turns the drive motor on, then loops forever.
    std::vector<std::uint8_t> make_rom() {
        std::vector<std::uint8_t> rom(full_drive::rom_size, 0U);
        const std::uint8_t program[] = {
            0xA9, 0xFF,       // LDA #$FF
            0x8D, 0x02, 0x1C, // STA $1C02  ; VIA2 DDRB = all output
            0xA9, 0x04,       // LDA #$04   ; motor on, stepper phase 0
            0x8D, 0x00, 0x1C, // STA $1C00  ; VIA2 PB
            0x4C, 0x0A, 0xC0, // JMP $C00A  ; spin
        };
        for (std::size_t i = 0; i < sizeof(program); ++i) {
            rom[i] = program[i];
        }
        rom[0x3FFCU] = 0x00U; // reset vector -> $C000
        rom[0x3FFDU] = 0xC0U;
        return rom;
    }

    std::vector<std::uint8_t> make_disk() {
        std::vector<std::uint8_t> img(d64_image::size_35_tracks, 0U);
        for (std::size_t i = 0; i < 256U; ++i) {
            img[i] = static_cast<std::uint8_t>(i); // track 1 sector 0 pattern
        }
        return img;
    }

} // namespace

TEST_CASE("full_drive registers under commodore.c1541.full") {
    REQUIRE(mnemos::chips::find_factory("commodore.c1541.full") != nullptr);
    REQUIRE(mnemos::chips::create_chip("commodore.c1541.full") != nullptr);
}

TEST_CASE("full_drive validates ROM and disk sizes") {
    full_drive drv;
    CHECK_FALSE(drv.load_rom(std::vector<std::uint8_t>(1024U, 0U)));
    CHECK(drv.load_rom(make_rom()));
    CHECK(drv.rom_loaded());
    CHECK_FALSE(drv.mount(std::vector<std::uint8_t>(100U, 0U)));
    CHECK(drv.mount(make_disk()));
    CHECK(drv.disk_loaded());
}

TEST_CASE("full_drive boots its ROM and runs the mechanism") {
    full_drive drv;
    REQUIRE(drv.load_rom(make_rom()));
    REQUIRE(drv.mount(make_disk()));
    drv.reset(mnemos::chips::reset_kind::power_on);

    CHECK(drv.cpu().cpu_registers().pc == 0xC000U); // reset vector from the ROM
    CHECK(drv.current_track() == 18U);              // head parked at track 18
    CHECK_FALSE(drv.motor_on());

    drv.tick(3000U);       // run the boot program + spin the platter
    CHECK(drv.motor_on()); // the ROM enabled the motor via VIA2

    // With the motor on and a disk mounted, the head latches GCR bytes; ticking
    // further keeps the drive 6502 + VIAs + head advancing without faulting.
    drv.tick(3000U);
    SUCCEED("drive ran 6000 cycles without fault");
}

TEST_CASE("full_drive save/load round-trips") {
    full_drive a;
    REQUIRE(a.load_rom(make_rom()));
    REQUIRE(a.mount(make_disk()));
    a.reset(mnemos::chips::reset_kind::power_on);
    a.tick(2500U);

    std::vector<std::uint8_t> buf1;
    mnemos::chips::state_writer w(buf1);
    a.save_state(w);

    full_drive b; // ROM + disk are external config, reloaded before state restore
    REQUIRE(b.load_rom(make_rom()));
    REQUIRE(b.mount(make_disk()));
    mnemos::chips::state_reader r(buf1);
    b.load_state(r);
    CHECK(r.ok());

    std::vector<std::uint8_t> buf2;
    mnemos::chips::state_writer w2(buf2);
    b.save_state(w2);
    CHECK(buf1 == buf2);
}
