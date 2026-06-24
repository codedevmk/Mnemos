#include "msx2_system.hpp"

#include "v9938.hpp"
#include "wd1793.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {
    using mnemos::chips::video::v9938;
    using mnemos::manifests::msx2::assemble_msx2;
    using mnemos::manifests::msx2::msx2_cartridge_mapper;
    using mnemos::manifests::msx2::msx2_config;
    using mnemos::manifests::msx2::msx2_system;

    void run_frame(msx2_system& sys) {
        const std::uint64_t cycles = static_cast<std::uint64_t>(v9938::cycles_per_line) *
                                     static_cast<std::uint64_t>(sys.vdp.total_scanlines());
        for (std::uint64_t i = 0; i < cycles; ++i) {
            sys.vdp.tick(1);
            sys.cpu.tick(1);
            sys.psg.tick(1);
            sys.rtc.tick(1);
            sys.fdc.tick(1);
            sys.scc.tick(1);
            if (sys.msx_music_enabled()) {
                sys.music.tick(1);
            }
        }
    }

    std::vector<std::uint8_t> make_8k_paged_cart(std::size_t pages) {
        std::vector<std::uint8_t> cart(pages * msx2_system::ascii8_page_size, 0x00U);
        for (std::size_t page = 0; page < pages; ++page) {
            cart[page * msx2_system::ascii8_page_size] = static_cast<std::uint8_t>(0x80U + page);
        }
        return cart;
    }

    std::vector<std::uint8_t> make_dsk() {
        std::vector<std::uint8_t> disk(static_cast<std::size_t>(80U) * 2U * 9U *
                                           mnemos::chips::storage::wd1793::sector_size,
                                       0xE5U);
        disk[0] = 0x41U;
        disk[1] = 0x42U;
        disk[mnemos::chips::storage::wd1793::sector_size] = 0x51U;
        disk[9U * mnemos::chips::storage::wd1793::sector_size] = 0x61U;
        return disk;
    }

    void append_repeat(std::vector<std::uint8_t>& out, std::uint8_t value, std::size_t count) {
        out.insert(out.end(), count, value);
    }

    std::vector<std::uint8_t> make_mfm_format_track(std::uint8_t track, std::uint8_t side,
                                                    std::uint8_t sectors) {
        std::vector<std::uint8_t> out;
        out.reserve(8192U);
        append_repeat(out, 0x4EU, 80U);
        append_repeat(out, 0x00U, 12U);
        append_repeat(out, 0xF6U, 3U);
        out.push_back(0xFCU);
        append_repeat(out, 0x4EU, 50U);

        for (std::uint8_t sector = 1U; sector <= sectors; ++sector) {
            append_repeat(out, 0x00U, 12U);
            append_repeat(out, 0xF5U, 3U);
            out.push_back(0xFEU);
            out.push_back(track);
            out.push_back(side);
            out.push_back(sector);
            out.push_back(0x02U);
            out.push_back(0xF7U);
            append_repeat(out, 0x4EU, 22U);
            append_repeat(out, 0x00U, 12U);
            append_repeat(out, 0xF5U, 3U);
            out.push_back(0xFBU);
            for (std::uint16_t i = 0U; i < mnemos::chips::storage::wd1793::sector_size; ++i) {
                out.push_back(static_cast<std::uint8_t>((0x90U | sector) + (i & 0x0FU)));
            }
            out.push_back(0xF7U);
            append_repeat(out, 0x4EU, 54U);
        }
        append_repeat(out, 0x4EU, 160U);
        return out;
    }

    [[nodiscard]] std::size_t find_mfm_id_mark(std::span<const std::uint8_t> stream,
                                               std::uint8_t track, std::uint8_t side,
                                               std::uint8_t sector) noexcept {
        const std::array<std::uint8_t, 5> pattern{0xFEU, track, side, sector, 0x02U};
        const auto found =
            std::search(stream.begin(), stream.end(), pattern.begin(), pattern.end());
        return found == stream.end() ? stream.size()
                                     : static_cast<std::size_t>(found - stream.begin());
    }

    [[nodiscard]] std::size_t find_data_mark_after(std::span<const std::uint8_t> stream,
                                                   std::size_t offset) noexcept {
        for (std::size_t i = offset; i < stream.size(); ++i) {
            if (stream[i] == 0xFBU || stream[i] == 0xF8U) {
                return i;
            }
        }
        return stream.size();
    }
} // namespace

TEST_CASE("msx2 assembles and advances frames", "[manifests][msx2]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    run_frame(*sys);
    CHECK(sys->vdp.frame_index() == 1U);
    CHECK(sys->vdp.framebuffer().pixels != nullptr);
    CHECK(sys->vdp.framebuffer().height == 192U);
}

TEST_CASE("msx2 BIOS can select a cartridge slot and jump into it", "[manifests][msx2]") {
    std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::array<std::uint8_t, 7> bios_prog = {
        0x3E, 0xF4,       // LD A,$F4  ; p0=slot0, p1=slot1, p2/p3=slot3 RAM
        0xD3, 0xA8,       // OUT ($A8),A
        0xC3, 0x00, 0x40, // JP $4000
    };
    std::copy(bios_prog.begin(), bios_prog.end(), bios.begin());

    std::vector<std::uint8_t> cart(0x8000U, 0x00U);
    const std::array<std::uint8_t, 7> cart_prog = {
        0x3E, 0x42,       // LD A,$42
        0x32, 0x00, 0xC0, // LD ($C000),A
        0x18, 0xFE,       // JR $
    };
    std::copy(cart_prog.begin(), cart_prog.end(), cart.begin());

    const auto sys = assemble_msx2(bios, cart);
    REQUIRE(sys != nullptr);
    for (int i = 0; i < 6; ++i) {
        (void)sys->cpu.step_instruction();
    }

    CHECK(sys->primary_slot == 0xF4U);
    CHECK(sys->bus.read8(0xC000U) == 0x42U);
}

TEST_CASE("msx2 RAM mapper ports select independent 16 KiB segments", "[manifests][msx2]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);
    sys->io_write(0xA8U, 0xFFU); // all four pages read/write RAM

    sys->io_write(0xFFU, 3U);
    sys->bus.write8(0xC000U, 0x11U);
    sys->io_write(0xFFU, 4U);
    sys->bus.write8(0xC000U, 0x22U);

    sys->io_write(0xFFU, 3U);
    CHECK(sys->bus.read8(0xC000U) == 0x11U);
    sys->io_write(0xFFU, 4U);
    CHECK(sys->bus.read8(0xC000U) == 0x22U);
}

TEST_CASE("msx2 expanded slot register selects internal sub-ROM and reads inverted",
          "[manifests][msx2][slots]") {
    std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    bios[0x0000U] = 0x11U;
    std::vector<std::uint8_t> sub_bios(0x4000U, 0x00U);
    sub_bios[0x0000U] = 0x5AU;

    const auto sys = assemble_msx2(bios, {}, msx2_config{.sub_bios = sub_bios});
    REQUIRE(sys != nullptr);

    CHECK(sys->bus.read8(0x0000U) == 0x11U);
    sys->bus.write8(0xFFFFU, 0x01U); // page 0 -> subslot 1, page 3 remains slot 0-x

    CHECK(sys->secondary_slot[0] == 0x01U);
    CHECK(sys->bus.read8(0xFFFFU) == 0xFEU);
    CHECK(sys->bus.read8(0x0000U) == 0x5AU);
}

TEST_CASE("msx2 secondary slot register is only visible through page 3 selected primary slot",
          "[manifests][msx2][slots]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> sub_bios(0x4000U, 0x00U);
    const auto sys = assemble_msx2(bios, {}, msx2_config{.sub_bios = sub_bios});
    REQUIRE(sys != nullptr);

    sys->io_write(0xA8U, 0xC0U); // page 3 -> RAM primary slot 3, not expanded slot 0
    sys->bus.write8(0xFFFFU, 0xAAU);

    CHECK(sys->secondary_slot[0] == 0x00U);
    CHECK(sys->ram[3U * msx2_system::page_size + 0x3FFFU] == 0xAAU);
}

TEST_CASE("msx2 ASCII8 mapper banks the cartridge window", "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    const auto sys =
        assemble_msx2(bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::ascii8});
    REQUIRE(sys != nullptr);
    sys->io_write(0xA8U, 0x04U); // page 1 -> cartridge slot

    CHECK(sys->bus.read8(0x4000U) == 0x80U);
    sys->bus.write8(0x6000U, 4U);
    CHECK(sys->bus.read8(0x4000U) == 0x84U);
}

TEST_CASE("msx2 Konami mapper banks the 8 KiB game windows", "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    const auto sys =
        assemble_msx2(bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::konami});
    REQUIRE(sys != nullptr);
    sys->io_write(0xA8U, 0x14U); // pages 1 and 2 -> cartridge slot

    CHECK(sys->bus.read8(0x4000U) == 0x80U);
    CHECK(sys->bus.read8(0x6000U) == 0x81U);
    CHECK(sys->bus.read8(0x8000U) == 0x82U);
    CHECK(sys->bus.read8(0xA000U) == 0x83U);

    sys->bus.write8(0x4000U, 7U);
    CHECK(sys->bus.read8(0x4000U) == 0x80U);
    sys->bus.write8(0x6000U, 4U);
    sys->bus.write8(0x8000U, 5U);
    sys->bus.write8(0xA000U, 6U);
    CHECK(sys->bus.read8(0x6000U) == 0x84U);
    CHECK(sys->bus.read8(0x8000U) == 0x85U);
    CHECK(sys->bus.read8(0xA000U) == 0x86U);
}

TEST_CASE("msx2 Konami SCC mapper exposes banks and SCC register aperture",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(64U);
    const auto sys = assemble_msx2(
        bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::konami_scc});
    REQUIRE(sys != nullptr);
    sys->io_write(0xA8U, 0x14U); // pages 1 and 2 -> cartridge slot

    sys->bus.write8(0x5000U, 4U);
    sys->bus.write8(0x7000U, 5U);
    sys->bus.write8(0x9000U, 6U);
    sys->bus.write8(0xB000U, 7U);
    CHECK(sys->bus.read8(0x4000U) == 0x84U);
    CHECK(sys->bus.read8(0x6000U) == 0x85U);
    CHECK(sys->bus.read8(0x8000U) == 0x86U);
    CHECK(sys->bus.read8(0xA000U) == 0x87U);

    sys->bus.write8(0x9000U, 0x3FU);
    sys->bus.write8(0x9800U, 0x5AU);
    CHECK(sys->bus.read8(0x9800U) == 0x5AU);
    CHECK(sys->scc.wave_sample(0, 0) == 0x5AU);
    CHECK(sys->konami_bank[2] == 0x3FU);
    CHECK(sys->bus.read8(0x8000U) == 0xBFU);
}

TEST_CASE("msx2 mapper state round-trips Konami and SCC registers", "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(64U);
    auto sys = assemble_msx2(bios, cart,
                             msx2_config{.cartridge_mapper = msx2_cartridge_mapper::konami_scc});
    REQUIRE(sys != nullptr);
    sys->io_write(0xA8U, 0x14U);
    sys->bus.write8(0x5000U, 4U);
    sys->bus.write8(0x9000U, 0x3FU);
    sys->bus.write8(0x9804U, 0x33U);
    sys->bus.write8(0x9880U, 0x02U);
    sys->bus.write8(0x988AU, 0x0FU);
    sys->bus.write8(0x988FU, 0x01U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    auto restored = assemble_msx2(
        bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::konami_scc});
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->primary_slot == 0x14U);
    CHECK(restored->bus.read8(0x4000U) == 0x84U);
    CHECK(restored->bus.read8(0x9804U) == 0x33U);
    CHECK(restored->scc.frequency(0) == 0x0002U);
    CHECK(restored->scc.volume(0) == 0x0FU);
    CHECK(restored->scc.channel_enabled(0));
}

TEST_CASE("msx2 routes VDP, PSG and PPI ports", "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    sys->io_write(0x99U, 0x20U);
    sys->io_write(0x99U, 0x81U); // VDP R#1 = frame IRQ enable
    CHECK(sys->vdp.reg(1) == 0x20U);

    sys->io_write(0xA0U, 8U);    // PSG channel A level register
    sys->io_write(0xA1U, 0x0FU); // full volume
    CHECK(sys->psg.volume(0) == 0x0FU);

    sys->set_key(2, 1, true);
    sys->io_write(0xAAU, 0x02U); // PPI C low nibble selects keyboard row 2
    CHECK(sys->io_read(0xA9U) == 0xFDU);
}

TEST_CASE("msx2 routes joystick input through PSG GPIO ports", "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    sys->set_joystick(0, true, false, true, false, true, false);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x00U); // select joystick port 1
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x2AU);
    CHECK((sys->io_read(0xA2U) & 0xC0U) == 0xC0U);

    sys->set_joystick(1, false, true, false, true, false, true);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x40U); // select joystick port 2 for register 14 reads
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x15U);
}

TEST_CASE("msx2 routes cassette input through PSG port A", "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    sys->set_cassette_input(false);
    CHECK((sys->io_read(0xA2U) & 0x80U) == 0x00U);
    CHECK((sys->io_read(0xA2U) & 0x40U) == 0x40U);

    sys->set_cassette_input(true);
    CHECK((sys->io_read(0xA2U) & 0x80U) == 0x80U);
}

TEST_CASE("msx2 PPI port C controls cassette motor and output lines", "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    sys->io_write(0xAAU, 0x30U);
    CHECK_FALSE(sys->cassette_motor_on());
    CHECK(sys->cassette_output_high());

    sys->io_write(0xABU, 0x08U); // reset PPI C bit 4: motor on
    sys->io_write(0xABU, 0x0AU); // reset PPI C bit 5: output low
    CHECK(sys->cassette_motor_on());
    CHECK_FALSE(sys->cassette_output_high());
}

TEST_CASE("msx2 routes RP-5C01 RTC ports", "[manifests][msx2][io][rtc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    sys->io_write(0xB4U, 0x0DU);
    sys->io_write(0xB5U, 0x0AU); // timer enabled, block 2 selected
    sys->io_write(0xB4U, 0x00U);
    CHECK(sys->io_read(0xB5U) == 0x0AU); // MSX RTC-valid CMOS marker

    sys->io_write(0xB4U, 0x0DU);
    sys->io_write(0xB5U, 0x0BU); // timer enabled, block 3 selected
    sys->io_write(0xB4U, 0x05U);
    sys->io_write(0xB5U, 0xBCU);
    CHECK(sys->io_read(0xB5U) == 0x0CU);
}

TEST_CASE("msx2 routes WD1793 FDC through D0-D4 I/O ports", "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.disk_loaded());

    sys->io_write(0xD4U, 0x01U); // drive A selected, motor latched on
    sys->io_write(0xD1U, 0U);    // track 0
    sys->io_write(0xD2U, 1U);    // sector 1
    sys->io_write(0xD0U, 0x80U); // read sector

    CHECK((sys->io_read(0xD4U) & 0x40U) != 0U);
    CHECK(sys->io_read(0xD3U) == 0x41U);
    CHECK(sys->io_read(0xD3U) == 0x42U);
}

TEST_CASE("msx2 routes WD1793 multiple sector reads through D0-D4 I/O ports",
          "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.disk_loaded());

    sys->io_write(0xD4U, 0x01U); // drive A selected
    sys->io_write(0xD1U, 0U);    // track 0
    sys->io_write(0xD2U, 1U);    // sector 1
    sys->io_write(0xD0U, 0x90U); // read multiple sectors

    CHECK(sys->io_read(0xD3U) == 0x41U);
    CHECK(sys->io_read(0xD3U) == 0x42U);
    for (std::uint16_t i = 2U; i < mnemos::chips::storage::wd1793::sector_size; ++i) {
        (void)sys->io_read(0xD3U);
    }

    CHECK(sys->io_read(0xD2U) == 2U);
    CHECK(sys->io_read(0xD3U) == 0x51U);
    sys->io_write(0xD0U, 0xD0U); // BIOS may stop a multi-sector transfer early
    CHECK((sys->fdc.status() & 0x11U) == 0x00U);
    CHECK_FALSE(sys->fdc.drq());
}

TEST_CASE("msx2 preserves WD1793 write-protect status through D0-D4 I/O ports",
          "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys =
        assemble_msx2(bios, {}, msx2_config{.disk_image = disk, .disk_write_protected = true});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.disk_loaded());
    REQUIRE(sys->fdc.write_protected());

    sys->io_write(0xD4U, 0x01U); // drive A selected
    sys->io_write(0xD1U, 0U);    // track 0
    sys->io_write(0xD2U, 2U);    // sector 2
    sys->io_write(0xD0U, 0xA0U); // write sector

    CHECK_FALSE(sys->fdc.drq());
    CHECK((sys->io_read(0xD0U) & 0x40U) != 0U);
    CHECK(sys->fdc.disk_image()[mnemos::chips::storage::wd1793::sector_size] == 0x51U);
}

TEST_CASE("msx2 preserves WD1793 side compare through D0-D4 I/O ports",
          "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.disk_loaded());

    sys->io_write(0xD4U, 0x01U); // drive A, side 0
    sys->io_write(0xD1U, 0U);
    sys->io_write(0xD2U, 1U);
    sys->io_write(0xD0U, 0x8AU); // read sector, C=1, S=1

    CHECK_FALSE(sys->fdc.drq());
    CHECK((sys->io_read(0xD0U) & 0x10U) != 0U);

    sys->io_write(0xD4U, 0x05U); // drive A, side 1
    sys->io_write(0xD2U, 1U);
    sys->io_write(0xD0U, 0x8AU);

    REQUIRE(sys->fdc.drq());
    CHECK(sys->io_read(0xD3U) == 0x61U);
}

TEST_CASE("msx2 formats WD1793 DSK sectors through D0-D4 I/O ports", "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.disk_loaded());

    const std::vector<std::uint8_t> stream = make_mfm_format_track(4U, 1U, 9U);
    sys->io_write(0xD4U, 0x05U); // drive A, side 1
    sys->io_write(0xD1U, 4U);
    sys->io_write(0xD0U, 0xF0U);

    REQUIRE((sys->io_read(0xD4U) & 0x40U) != 0U);
    for (const std::uint8_t value : stream) {
        sys->io_write(0xD3U, value);
        if (!sys->fdc.drq()) {
            break;
        }
    }

    CHECK((sys->io_read(0xD4U) & 0x80U) != 0U);
    CHECK_FALSE(sys->fdc.drq());
    const std::size_t track_side_offset = ((static_cast<std::size_t>(4U) * 2U + 1U) * 9U) *
                                          mnemos::chips::storage::wd1793::sector_size;
    CHECK(sys->fdc.disk_image()[track_side_offset] == 0x91U);
    CHECK(sys->fdc.disk_image()[track_side_offset +
                                8U * mnemos::chips::storage::wd1793::sector_size] == 0x99U);
}

TEST_CASE("msx2 reads WD1793 track streams through D0-D4 I/O ports", "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.disk_loaded());

    sys->io_write(0xD4U, 0x05U); // drive A, side 1
    sys->io_write(0xD1U, 0U);
    sys->io_write(0xD0U, 0xE0U);

    REQUIRE((sys->io_read(0xD4U) & 0x40U) != 0U);
    std::vector<std::uint8_t> stream;
    while (sys->fdc.drq()) {
        stream.push_back(sys->io_read(0xD3U));
    }

    CHECK((sys->io_read(0xD4U) & 0x80U) != 0U);
    const std::size_t id_mark = find_mfm_id_mark(stream, 0U, 1U, 1U);
    REQUIRE(id_mark < stream.size());
    const std::size_t data_mark = find_data_mark_after(stream, id_mark + 5U);
    REQUIRE(data_mark + mnemos::chips::storage::wd1793::sector_size < stream.size());
    CHECK(stream[data_mark] == 0xFBU);
    CHECK(stream[data_mark + 1U] == 0x61U);
}

TEST_CASE("msx2 maps optional disk ROM and memory-mapped FDC window",
          "[manifests][msx2][slots][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> disk_rom(0x4000U, 0xFFU);
    disk_rom[0] = 0xD5U;
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_rom = disk_rom, .disk_image = disk});
    REQUIRE(sys != nullptr);

    sys->io_write(0xA8U, 0x28U); // pages 1 and 2 -> disk interface slot 2
    CHECK(sys->bus.read8(0x4000U) == 0xD5U);

    sys->bus.write8(0xBFFDU, 0x00U); // drive A
    sys->bus.write8(0xBFFCU, 0x00U); // side 0
    sys->bus.write8(0xBFF9U, 0U);    // track
    sys->bus.write8(0xBFFAU, 1U);    // sector
    sys->bus.write8(0xBFF8U, 0x80U);

    CHECK((sys->bus.read8(0xBFFFU) & 0x80U) != 0U);
    CHECK(sys->bus.read8(0xBFFBU) == 0x41U);
}

TEST_CASE("msx2 RTC state round-trips through the system save state", "[manifests][msx2][rtc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    const std::uint32_t first_half = mnemos::chips::peripheral::rp5c01::input_clock_hz / 2U;
    const std::uint32_t second_half =
        mnemos::chips::peripheral::rp5c01::input_clock_hz - first_half;
    sys->rtc.tick(first_half);
    sys->io_write(0xB4U, 0x0DU);
    sys->io_write(0xB5U, 0x0BU);
    sys->io_write(0xB4U, 0x04U);
    sys->io_write(0xB5U, 0x0EU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    auto restored = assemble_msx2(bios);
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored->rtc.selected_block() == 3U);
    restored->io_write(0xB4U, 0x04U);
    CHECK(restored->io_read(0xB5U) == 0x0EU);

    restored->io_write(0xB4U, 0x0DU);
    restored->io_write(0xB5U, 0x08U);
    restored->rtc.tick(second_half);
    restored->io_write(0xB4U, 0x00U);
    CHECK(restored->io_read(0xB5U) == 1U);
}

TEST_CASE("msx2 FDC state round-trips through the system save state", "[manifests][msx2][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);

    sys->io_write(0xD2U, 1U);
    sys->io_write(0xD0U, 0x80U);
    CHECK(sys->io_read(0xD3U) == 0x41U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    auto restored = assemble_msx2(bios);
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored->fdc.disk_loaded());
    CHECK(restored->fdc.drq());
    CHECK(restored->io_read(0xD3U) == 0x42U);
}

TEST_CASE("msx2 joystick state round-trips through the system save state",
          "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);
    sys->set_joystick(0, true, false, false, true, true, false);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    auto restored = assemble_msx2(bios);
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    restored->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    restored->io_write(0xA1U, 0x00U);
    restored->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((restored->io_read(0xA2U) & 0x3FU) == 0x26U);
}

TEST_CASE("msx2 cassette input state round-trips through the system save state",
          "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);
    sys->set_cassette_input(false);
    sys->io_write(0xAAU, 0x30U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    auto restored = assemble_msx2(bios);
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    restored->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((restored->io_read(0xA2U) & 0x80U) == 0x00U);
    CHECK_FALSE(restored->cassette_motor_on());
    CHECK(restored->cassette_output_high());
}

TEST_CASE("msx2 optional MSX-MUSIC routes YM2413 ports", "[manifests][msx2][audio]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios, {}, msx2_config{.msx_music = true});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->msx_music_enabled());

    sys->music.enable_audio_capture(true);
    sys->music.set_clock_divider(1);
    sys->io_write(0x7CU, 0x30U); // ch0 instrument/volume
    sys->io_write(0x7DU, 0x70U); // preset 7, loudest volume
    sys->io_write(0x7CU, 0x10U); // ch0 F-number low
    sys->io_write(0x7DU, 0xA0U);
    sys->io_write(0x7CU, 0x20U); // key-on, block 4, F-number high bit clear
    sys->io_write(0x7DU, 0x18U);
    sys->music.tick(10000);

    CHECK(sys->music.address_latch() == 0x20U);
    CHECK(sys->music.pending_samples() > 0U);
}

TEST_CASE("msx2 optional MSX-MUSIC save state round-trips", "[manifests][msx2][audio]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    auto sys = assemble_msx2(bios, {}, msx2_config{.msx_music = true});
    REQUIRE(sys != nullptr);

    sys->io_write(0x7CU, 0x30U);
    sys->io_write(0x7DU, 0x70U);
    sys->io_write(0x7CU, 0x10U);
    sys->io_write(0x7DU, 0xA0U);
    sys->io_write(0x7CU, 0x20U);
    sys->io_write(0x7DU, 0x18U);
    std::array<std::int16_t, 128> warm{};
    sys->music.generate(warm);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    std::array<std::int16_t, 128> from_sys{};
    sys->music.generate(from_sys);

    auto restored = assemble_msx2(bios);
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored->msx_music_enabled());

    std::array<std::int16_t, 128> from_restored{};
    restored->music.generate(from_restored);
    CHECK(from_restored == from_sys);
}
