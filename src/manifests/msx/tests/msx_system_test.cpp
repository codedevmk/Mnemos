#include "msx_system.hpp"

#include "msx_cassette.hpp"
#include "tms9918a.hpp"
#include "v9938.hpp"
#include "wd1793.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>

namespace {
    using mnemos::manifests::msx::assemble_msx;
    using mnemos::manifests::msx::msx_cartridge_mapper;
    using mnemos::manifests::msx::msx_config;
    using mnemos::manifests::msx::msx_system;
    using mnemos::manifests::msx::msx_video_model;
    using msx_cassette = mnemos::chips::storage::msx_cassette;
    using tms9918a = mnemos::chips::video::tms9918a;
    using v9938 = mnemos::chips::video::v9938;
    using wd1793 = mnemos::chips::storage::wd1793;

    constexpr std::uint8_t k_fdc_status_record_not_found = 0x10U;
    constexpr std::uint8_t k_fdc_status_record_type = 0x20U;
    constexpr std::uint8_t k_fdc_status_write_protect = 0x40U;
    constexpr std::uint8_t k_fdc_status_not_ready = 0x80U;

    [[nodiscard]] std::vector<std::uint8_t> make_dsk(std::uint16_t tracks = 40U,
                                                     std::uint8_t sides = 2U) {
        std::vector<std::uint8_t> disk(static_cast<std::size_t>(tracks) * sides *
                                           wd1793::standard_sectors_per_track * wd1793::sector_size,
                                       0xE5U);
        for (std::size_t i = 0; i < disk.size(); ++i) {
            disk[i] = static_cast<std::uint8_t>(i & 0xFFU);
        }
        return disk;
    }

    [[nodiscard]] std::vector<std::uint8_t> make_cas(std::initializer_list<std::uint8_t> bytes) {
        std::vector<std::uint8_t> cas(msx_cassette::cas_header_magic.begin(),
                                      msx_cassette::cas_header_magic.end());
        cas.insert(cas.end(), bytes.begin(), bytes.end());
        return cas;
    }

    void put_le16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
        REQUIRE(offset + 1U < bytes.size());
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    }

    [[nodiscard]] std::vector<std::uint8_t> make_msx_dos_dsk(std::uint8_t sides,
                                                             std::uint8_t sectors_per_track) {
        constexpr std::uint16_t tracks = 80U;
        std::vector<std::uint8_t> disk(static_cast<std::size_t>(tracks) * sides *
                                           sectors_per_track * wd1793::sector_size,
                                       0xE5U);
        disk[0] = 0xEBU;
        disk[1] = 0xFEU;
        disk[2] = 0x90U;
        put_le16(disk, 0x0BU, static_cast<std::uint16_t>(wd1793::sector_size));
        disk[0x0DU] = 2U;
        put_le16(disk, 0x0EU, 1U);
        disk[0x10U] = 2U;
        put_le16(disk, 0x11U, 112U);
        put_le16(disk, 0x13U, static_cast<std::uint16_t>(disk.size() / wd1793::sector_size));
        disk[0x15U] =
            static_cast<std::uint8_t>(0xF7U + ((sectors_per_track == 8U) ? 2U : 0U) + sides);
        put_le16(disk, 0x16U,
                 sectors_per_track == 8U ? static_cast<std::uint16_t>(sides)
                                         : static_cast<std::uint16_t>(sides + 1U));
        put_le16(disk, 0x18U, sectors_per_track);
        put_le16(disk, 0x1AU, sides);
        return disk;
    }

    [[nodiscard]] std::size_t
    disk_sector_offset(std::uint8_t track, std::uint8_t side, std::uint8_t sector,
                       std::uint8_t sides = 2U,
                       std::uint8_t sectors_per_track = wd1793::standard_sectors_per_track) {
        return (((static_cast<std::size_t>(track) * sides) + side) * sectors_per_track +
                (sector - 1U)) *
               wd1793::sector_size;
    }

    void append_bytes(std::vector<std::uint8_t>& out, std::size_t count, std::uint8_t value) {
        out.insert(out.end(), count, value);
    }

    void wait_for_fdc_drq(msx_system& sys) {
        for (std::uint64_t i = 0; i <= wd1793::nominal_mfm_byte_cycles; ++i) {
            if (sys.fdc.drq()) {
                return;
            }
            sys.fdc.tick(1U);
        }
        REQUIRE(sys.fdc.drq());
    }

    std::uint8_t read_fdc_io(msx_system& sys) {
        wait_for_fdc_drq(sys);
        return sys.read_io(0xD3U);
    }

    void write_fdc_io(msx_system& sys, std::uint8_t value) {
        wait_for_fdc_drq(sys);
        sys.write_io(0xD3U, value);
    }

    std::uint8_t read_fdc_memory(msx_system& sys, std::uint16_t address) {
        wait_for_fdc_drq(sys);
        return sys.bus.read8(address);
    }

    [[nodiscard]] std::vector<std::uint8_t>
    make_write_track_stream(std::uint8_t track, std::uint8_t side, std::uint8_t sector,
                            std::uint8_t first_byte, std::uint8_t second_byte) {
        std::vector<std::uint8_t> stream;
        stream.reserve(wd1793::standard_mfm_track_size);

        append_bytes(stream, 80U, 0x4EU);
        append_bytes(stream, 12U, 0x00U);
        append_bytes(stream, 3U, 0xF6U);
        stream.push_back(0xFCU);
        append_bytes(stream, 50U, 0x4EU);
        append_bytes(stream, 12U, 0x00U);
        append_bytes(stream, 3U, 0xF5U);
        stream.push_back(0xFEU);
        stream.push_back(track);
        stream.push_back(side);
        stream.push_back(sector);
        stream.push_back(0x02U);
        stream.push_back(0xF7U);
        append_bytes(stream, 22U, 0x4EU);
        append_bytes(stream, 12U, 0x00U);
        append_bytes(stream, 3U, 0xF5U);
        stream.push_back(0xFBU);
        stream.push_back(first_byte);
        stream.push_back(second_byte);
        append_bytes(stream, wd1793::sector_size - 2U, 0xE5U);
        stream.push_back(0xF7U);
        append_bytes(stream, wd1793::standard_mfm_track_size - stream.size(), 0x4EU);
        return stream;
    }

    [[nodiscard]] std::vector<std::uint8_t> make_kanji_rom() {
        std::vector<std::uint8_t> rom(0x40000U, 0xFFU);
        const std::size_t level1_char = 0x0123U;
        const std::size_t level2_char = 0x0045U;
        for (std::size_t i = 0; i < 32U; ++i) {
            rom[(level1_char * 32U) + i] = static_cast<std::uint8_t>(0x40U + i);
            rom[0x20000U + (level2_char * 32U) + i] = static_cast<std::uint8_t>(0xA0U + i);
        }
        return rom;
    }

    void write_mapper_signature(std::vector<std::uint8_t>& rom, std::size_t offset,
                                std::uint16_t address) {
        REQUIRE(offset + 2U < rom.size());
        rom[offset] = 0x32U; // LD (nn),A
        rom[offset + 1U] = static_cast<std::uint8_t>(address & 0xFFU);
        rom[offset + 2U] = static_cast<std::uint8_t>(address >> 8U);
    }

    void add_mapper_signatures(std::vector<std::uint8_t>& rom,
                               std::initializer_list<std::uint16_t> addresses) {
        std::size_t offset = 0x0100U;
        for (const std::uint16_t address : addresses) {
            write_mapper_signature(rom, offset, address);
            offset += 4U;
        }
    }

    [[nodiscard]] std::vector<std::uint8_t> make_8k_paged_cart(std::size_t pages) {
        std::vector<std::uint8_t> cart(pages * 0x2000U, 0x00U);
        for (std::size_t page = 0; page < pages; ++page) {
            cart[page * 0x2000U] = static_cast<std::uint8_t>(0x80U + page);
        }
        return cart;
    }

    [[nodiscard]] std::vector<std::uint8_t> make_16k_paged_cart(std::size_t pages) {
        std::vector<std::uint8_t> cart(pages * 0x4000U, 0x00U);
        for (std::size_t page = 0; page < pages; ++page) {
            cart[page * 0x4000U] = static_cast<std::uint8_t>(0x90U + page);
        }
        return cart;
    }

    void run_cycles(msx_system& sys, int cycles) {
        for (int i = 0; i < cycles; ++i) {
            sys.active_video().tick(1);
            sys.cpu.tick(1);
            sys.psg.tick(1);
            sys.cassette.tick(1);
            if (sys.fm_music_enabled) {
                sys.fm.tick(1);
            }
            if (sys.rtc_enabled) {
                sys.rtc.tick(1);
            }
        }
    }

    void run_frame(msx_system& sys) {
        run_cycles(sys, tms9918a::cycles_per_line * tms9918a::scanlines_ntsc);
    }

    void configure_ppi_outputs(msx_system& sys) { sys.write_io(0xABU, 0x82U); }

    void configure_psg_gpio(msx_system& sys) {
        sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_mixer);
        sys.write_io(0xA1U, 0xBFU); // port A input, port B output, tone/noise disabled
        sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
        sys.write_io(0xA1U, 0x0FU); // port 1 selected; trigger pins released for input
    }
} // namespace

TEST_CASE("msx assembles and advances frames", "[manifests][msx]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx(bios);
    REQUIRE(sys != nullptr);

    run_frame(*sys);
    run_frame(*sys);

    CHECK(sys->vdp.frame_index() == 2U);
    const auto fb = sys->vdp.framebuffer();
    CHECK(fb.width == static_cast<std::uint32_t>(tms9918a::display_width));
    CHECK(fb.height == static_cast<std::uint32_t>(tms9918a::display_height));
    CHECK(fb.pixels != nullptr);
}

TEST_CASE("msx PPI primary slot register maps RAM for writes", "[manifests][msx]") {
    std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::array<std::uint8_t, 14> program = {
        0x3E, 0x82,       // LD A,$82  (PPI: PA/PC output, PB input)
        0xD3, 0xAB,       // OUT ($AB),A
        0x3E, 0xC0,       // LD A,$C0  (page 3 -> slot 3 RAM)
        0xD3, 0xA8,       // OUT ($A8),A
        0x3E, 0x42,       // LD A,$42
        0x32, 0x00, 0xC0, // LD ($C000),A
        0x76,             // HALT
    };
    std::copy(program.begin(), program.end(), bios.begin());

    const auto sys = assemble_msx(bios);
    REQUIRE(sys != nullptr);
    run_cycles(*sys, 160);

    CHECK(sys->ram[0xC000U] == 0x42U);
}

TEST_CASE("msx primary slot register follows PPI port A direction", "[manifests][msx]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(sys != nullptr);
    REQUIRE(sys->ppi_control == 0x9BU);

    CHECK(sys->read_io(0xA8U) == 0x00U);

    sys->write_io(0xA8U, 0xC0U);
    CHECK(sys->ppi_a == 0xC0U);
    CHECK(sys->read_io(0xA8U) == 0x00U);

    sys->write_io(0xABU, 0x82U);
    CHECK(sys->read_io(0xA8U) == 0xC0U);
    sys->bus.write8(0xC000U, 0x5AU);
    CHECK(sys->ram[0xC000U] == 0x5AU);
}

TEST_CASE("msx routes VDP ports and IRQ to the Z80 line", "[manifests][msx]") {
    std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::array<std::uint8_t, 9> program = {
        0x3E, 0x60, // LD A,$60   (display + frame IRQ)
        0xD3, 0x99, // OUT ($99),A
        0x3E, 0x81, // LD A,$81   (write VDP register 1)
        0xD3, 0x99, // OUT ($99),A
        0x76,       // HALT
    };
    std::copy(program.begin(), program.end(), bios.begin());

    const auto sys = assemble_msx(bios);
    REQUIRE(sys != nullptr);
    run_cycles(*sys, 120);
    CHECK(sys->vdp.reg(1) == 0x60U);

    run_frame(*sys);
    CHECK(sys->vdp.irq_asserted());
}

TEST_CASE("msx VDP frame IRQ wakes the Z80 from HALT", "[manifests][msx][irq]") {
    std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::array<std::uint8_t, 4> program = {
        0xEDU, 0x56U, // IM 1
        0xFBU,       // EI
        0x76U,       // HALT
    };
    std::copy(program.begin(), program.end(), bios.begin());

    const auto sys = assemble_msx(bios);
    REQUIRE(sys != nullptr);

    sys->write_io(0x99U, 0x60U);
    sys->write_io(0x99U, 0x81U); // VDP R#1 = display + frame IRQ enable

    (void)sys->cpu.step_instruction(); // IM 1
    (void)sys->cpu.step_instruction(); // EI
    (void)sys->cpu.step_instruction(); // HALT with IFF1 enabled after EI delay
    auto regs = sys->cpu.cpu_registers();
    REQUIRE(regs.halted);
    REQUIRE(regs.iff1);

    sys->vdp.tick(static_cast<std::uint64_t>(tms9918a::cycles_per_line) *
                  static_cast<std::uint64_t>(tms9918a::display_height + 1));
    REQUIRE(sys->vdp.irq_asserted());

    (void)sys->cpu.step_instruction();
    regs = sys->cpu.cpu_registers();
    CHECK_FALSE(regs.halted);
    CHECK(regs.pc == 0x0038U);
    CHECK_FALSE(regs.iff1);
}

TEST_CASE("msx2 routes V9938 ports, palette, and upper VRAM", "[manifests][msx]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {},
                                  msx_config{.video_model = msx_video_model::v9938});
    REQUIRE(sys != nullptr);
    CHECK(sys->msx2_video());
    CHECK(&sys->active_video() == static_cast<mnemos::chips::ivideo*>(&sys->vdp2));

    sys->write_io(0x99U, 0x06U);
    sys->write_io(0x99U, 0x80U); // R#0 M3+M4: Graphic 4
    sys->write_io(0x99U, 0x40U);
    sys->write_io(0x99U, 0x81U); // R#1 display enable
    sys->write_io(0x99U, 0x80U);
    sys->write_io(0x99U, 0x89U); // R#9 LN=1: 212 visible lines
    CHECK(sys->vdp2.visible_height() == v9938::display_height_212);
    CHECK(sys->framebuffer().height == static_cast<std::uint32_t>(v9938::display_height_212));

    sys->write_io(0x99U, 0x01U);
    sys->write_io(0x99U, 0x90U); // R#16 = palette index 1
    sys->write_io(0x9AU, 0x70U);
    sys->write_io(0x9AU, 0x00U);
    CHECK(sys->vdp2.palette(1) == 0x01C0U);

    sys->write_io(0x99U, 0x0EU);
    sys->write_io(0x99U, 0x91U); // R#17 = 14, auto-increment enabled
    sys->write_io(0x9BU, 0x05U);
    sys->write_io(0x9BU, 0x03U);
    CHECK(sys->vdp2.reg(14) == 5U);
    CHECK(sys->vdp2.reg(15) == 3U);
    CHECK((sys->vdp2.reg(17) & 0x3FU) == 16U);

    sys->write_io(0x99U, 0x06U);
    sys->write_io(0x99U, 0x8EU); // R#14 = 6
    sys->write_io(0x99U, 0x34U);
    sys->write_io(0x99U, 0x52U); // write address $19234
    sys->write_io(0x98U, 0xA5U);
    CHECK(sys->vdp2.vram()[0x19234U] == 0xA5U);
}

TEST_CASE("msx PPI keyboard matrix is active-low", "[manifests][msx]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(sys != nullptr);
    sys->set_key(2, 3, true);
    sys->write_io(0xAAU, 0x02U);
    CHECK(sys->read_io(0xA9U) == 0xFFU);
    configure_ppi_outputs(*sys);
    CHECK(sys->read_io(0xA9U) == 0xF7U);
    sys->set_key(2, 3, false);
    CHECK(sys->read_io(0xA9U) == 0xFFU);
}

TEST_CASE("msx PPI mode control survives BSR writes and save state", "[manifests][msx]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(sys != nullptr);

    CHECK(sys->ppi_control == 0x9BU);
    sys->write_io(0xABU, 0x82U); // mode set: PA/PC output, PB input
    CHECK(sys->ppi_control == 0x82U);
    sys->write_io(0xABU, 0x08U); // BSR reset PC4: cassette motor on
    CHECK(sys->ppi_control == 0x82U);
    CHECK((sys->ppi_c & 0x10U) == 0x00U);
    CHECK(sys->cassette.motor_on());

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored->ppi_control == 0x82U);
    CHECK((restored->ppi_c & 0x10U) == 0x00U);
    CHECK(restored->cassette.motor_on());
}

TEST_CASE("msx PPI port B output latch is selected by mode control", "[manifests][msx]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(sys != nullptr);

    sys->set_key(2, 3, true);
    sys->write_io(0xAAU, 0x02U);
    configure_ppi_outputs(*sys);
    CHECK(sys->read_io(0xA9U) == 0xF7U);

    sys->write_io(0xA9U, 0x5AU);
    CHECK(sys->read_io(0xA9U) == 0xF7U);

    sys->write_io(0xABU, 0x80U); // mode set: all ports output
    CHECK(sys->read_io(0xA9U) == 0x5AU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored->read_io(0xA9U) == 0x5AU);
    restored->write_io(0xABU, 0x82U); // mode set: PA/PC output, PB input
    CHECK(restored->read_io(0xA9U) == 0xF7U);
}

TEST_CASE("msx PSG port A exposes the selected joystick", "[manifests][msx]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(sys != nullptr);
    configure_psg_gpio(*sys);

    mnemos::peripheral::controller_state pad{};
    pad.up = true;
    pad.a = true;
    sys->apply_joystick(0, pad);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x2EU); // up + trigger A active-low

    mnemos::peripheral::controller_state pad2{};
    pad2.down = true;
    pad2.x = true;
    sys->apply_joystick(1, pad2);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->write_io(0xA1U, 0x4FU); // PSG port B bit 6 selects joystick port 2
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x1DU); // down + trigger B active-low
}

TEST_CASE("msx PSG register 15 gates joystick trigger input pins", "[manifests][msx][io]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(sys != nullptr);
    configure_psg_gpio(*sys);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x3FU);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->write_io(0xA1U, 0x00U);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x0FU);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->write_io(0xA1U, 0x4CU);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x3FU);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->write_io(0xA1U, 0x40U);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x0FU);
}

TEST_CASE("msx optional logo ROM maps at slot 0 page 2", "[manifests][msx][slots]") {
    std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    bios.front() = 0x11U;
    bios.back() = 0x22U;
    std::vector<std::uint8_t> logo_rom(0x4000U, 0x44U);
    logo_rom.front() = 0x5AU;
    logo_rom.back() = 0xA5U;

    const auto sys = assemble_msx(bios, {}, msx_config{.logo_rom = logo_rom});
    REQUIRE(sys != nullptr);

    CHECK(sys->bus.read8(0x0000U) == 0x11U);
    CHECK(sys->bus.read8(0x7FFFU) == 0x22U);
    CHECK(sys->bus.read8(0x8000U) == 0x5AU);
    CHECK(sys->bus.read8(0xBFFFU) == 0xA5U);
    CHECK(sys->bus.read8(0xC000U) == 0xFFU);

    configure_ppi_outputs(*sys);
    sys->write_io(0xA8U, 0x10U); // page 2 -> cartridge slot 1
    CHECK(sys->bus.read8(0x8000U) == 0xFFU);
}

TEST_CASE("msx PSG port B direction releases joystick trigger output pins",
          "[manifests][msx][io]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(sys != nullptr);
    configure_psg_gpio(*sys);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->write_io(0xA1U, 0x00U);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x0FU);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_mixer);
    sys->write_io(0xA1U, 0x3FU); // port A input, port B input: register 15 pins released
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x3FU);
}

TEST_CASE("msx PSG port A honors register 7 input/output direction", "[manifests][msx][io]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(sys != nullptr);

    mnemos::peripheral::controller_state pad{};
    pad.up = true;
    sys->apply_joystick(0, pad);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    sys->write_io(0xA1U, 0x5AU);
    CHECK(sys->read_io(0xA2U) == 0x5AU);

    configure_psg_gpio(*sys);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x3EU);
}

TEST_CASE("msx mouse protocol clocks port-1 deltas through PSG port A",
          "[manifests][msx][io][mouse]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(sys != nullptr);
    configure_psg_gpio(*sys);
    sys->set_mouse(0, 0x12, -0x34, true, false);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x21U);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->write_io(0xA1U, 0x1FU);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x22U);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_mixer);
    sys->write_io(0xA1U, 0x3FU); // port B input: latch writes no longer clock mouse pin 8
    const std::uint8_t phase_after_release = sys->mouse_ports[0].phase();
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->write_io(0xA1U, 0x0FU);
    sys->write_io(0xA1U, 0x1FU);
    CHECK(sys->mouse_ports[0].phase() == phase_after_release);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_mixer);
    sys->write_io(0xA1U, 0xBFU);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->write_io(0xA1U, 0x0FU);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x2CU);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->write_io(0xA1U, 0x1FU);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x2CU);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->write_io(0xA1U, 0x0FU);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x20U);

    mnemos::peripheral::controller_state pad{};
    pad.up = true;
    sys->apply_joystick(0, pad);
    CHECK((sys->read_io(0xA2U) & 0x3FU) == 0x3EU);
}

TEST_CASE("msx mouse protocol phase round-trips with the machine snapshot",
          "[manifests][msx][io][mouse]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(sys != nullptr);
    configure_psg_gpio(*sys);
    sys->set_mouse(0, 0x45, 0x67, false, true);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->write_io(0xA1U, 0x1FU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    restored->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((restored->read_io(0xA2U) & 0x3FU) == 0x15U);
}

TEST_CASE("msx PSG data port is read from A2 and written through A1", "[manifests][msx][io]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(sys != nullptr);

    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->write_io(0xA1U, 0x40U);
    CHECK(sys->psg.read_reg(mnemos::chips::audio::ssg::reg_port_b) == 0x40U);

    sys->write_io(0xA2U, 0x00U);
    CHECK(sys->psg.read_reg(mnemos::chips::audio::ssg::reg_port_b) == 0x40U);
    CHECK(sys->read_io(0xA1U) == 0xFFU);
    CHECK(sys->read_io(0xA2U) == 0x40U);
}

TEST_CASE("msx cassette input is read through PSG port A bit 7", "[manifests][msx]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(sys != nullptr);
    const std::array<std::uint32_t, 2> pulses{2U, 2U};
    sys->cassette.load_half_cycles(pulses);
    sys->cassette.set_play(true);
    configure_psg_gpio(*sys);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);

    CHECK((sys->read_io(0xA2U) & 0x80U) == 0x80U);
    run_cycles(*sys, 2);
    CHECK((sys->read_io(0xA2U) & 0x80U) == 0x80U);

    sys->write_io(0xABU, 0x08U); // reset PPI port C bit 4: cassette motor on
    CHECK_FALSE(sys->cassette.motor_on());
    configure_ppi_outputs(*sys);
    CHECK(sys->cassette.motor_on());
    run_cycles(*sys, 2);
    CHECK((sys->read_io(0xA2U) & 0x80U) == 0x00U);
}

TEST_CASE("msx optional CAS image advances through the shared cassette chip",
          "[manifests][msx][cassette]") {
    const std::vector<std::uint8_t> cas = make_cas({0x42U});
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {},
                                  msx_config{.cassette_image = cas});
    REQUIRE(sys != nullptr);

    REQUIRE(sys->cassette.loaded());
    CHECK(sys->cassette.playing());
    CHECK_FALSE(sys->cassette.motor_on());
    CHECK(sys->cassette.position_half_cycle() == 0U);

    sys->write_io(0xABU, 0x08U); // BSR reset PC4: cassette motor on
    configure_ppi_outputs(*sys);
    REQUIRE(sys->cassette.motor_on());
    sys->cassette.tick(1U);
    CHECK(sys->cassette.position_half_cycle() > 0U);
}

TEST_CASE("msx plain cartridge maps at $4000-$BFFF through slot 1", "[manifests][msx]") {
    std::vector<std::uint8_t> cart(0x8000U, 0xFFU);
    cart[0] = 0x11U;
    cart[0x4000U] = 0x22U;
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart);
    REQUIRE(sys != nullptr);

    sys->primary_slot_select = 0x14U; // pages 1/2 -> slot 1
    CHECK(sys->bus.read8(0x4000U) == 0x11U);
    CHECK(sys->bus.read8(0x8000U) == 0x22U);
}

TEST_CASE("msx 16 KiB plain cartridge mirrors across both cartridge pages",
          "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> cart(0x4000U, 0xFFU);
    cart[0] = 'A';
    cart[1] = 'B';
    cart[2] = 0x35U;
    cart[3] = 0xBBU;
    cart[0x3FFFU] = 0x22U;
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                  msx_config{.cartridge_mapper = msx_cartridge_mapper::plain});
    REQUIRE(sys != nullptr);

    sys->primary_slot_select = 0x14U; // pages 1/2 -> slot 1
    CHECK(sys->bus.read8(0x4000U) == 'A');
    CHECK(sys->bus.read8(0x7FFFU) == 0x22U);
    CHECK(sys->bus.read8(0x8000U) == 'A');
    CHECK(sys->bus.read8(0xBFFFU) == 0x22U);

    sys->primary_slot_select = 0xD0U; // page 1 -> BIOS, page 2 -> cartridge
    CHECK(sys->bus.read8(0x8000U) == 'A');
    CHECK(sys->bus.read8(0xBFFFU) == 0x22U);
}

TEST_CASE("msx lower-page 16 KiB plain cartridges mirror only when both cart pages are selected",
          "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> cart(0x4000U, 0xFFU);
    cart[0] = 'A';
    cart[1] = 'B';
    cart[2] = 0x0FU;
    cart[3] = 0x40U;
    cart[0x0123U] = 0x66U;
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                  msx_config{.cartridge_mapper = msx_cartridge_mapper::plain});
    REQUIRE(sys != nullptr);
    configure_ppi_outputs(*sys);

    sys->write_io(0xA8U, 0xD0U); // page 1 -> BIOS, page 2 -> cartridge probe
    CHECK(sys->bus.read8(0x8123U) == 0xFFU);

    sys->write_io(0xA8U, 0x14U); // pages 1/2 -> same cartridge slot
    CHECK(sys->bus.read8(0x4123U) == 0x66U);
    CHECK(sys->bus.read8(0x8123U) == 0x66U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                       msx_config{.cartridge_mapper =
                                                      msx_cartridge_mapper::plain});
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(blob);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored->bus.read8(0x8123U) == 0x66U);
}

TEST_CASE("msx upper-page 16 KiB plain cartridges stop at the page-3 RAM boundary",
          "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> cart(0x4000U, 0xFFU);
    cart[0] = 'A';
    cart[1] = 'B';
    cart[2] = 0x04U;
    cart[3] = 0x80U;
    cart[0x3FFFU] = 0xC5U;
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                  msx_config{.cartridge_mapper = msx_cartridge_mapper::plain});
    REQUIRE(sys != nullptr);

    sys->primary_slot_select = 0xD0U; // page 2 -> cartridge, page 3 -> RAM
    CHECK(sys->bus.read8(0x8000U) == 'A');
    CHECK(sys->bus.read8(0xBFFFU) == 0xC5U);

    sys->bus.write8(0xC000U, 0x5AU);
    CHECK(sys->bus.read8(0xC000U) == 0x5AU);
    CHECK(sys->ram[0xC000U] == 0x5AU);
}

TEST_CASE("msx padded plain cartridge maps its payload at $4000", "[manifests][msx]") {
    std::vector<std::uint8_t> cart(0x10000U, 0x00U);
    cart[0x0000U] = 0x4BU;
    cart[0x0001U] = 0xFCU;
    cart[0x4000U] = 'A';
    cart[0x4001U] = 'B';
    cart[0x7FFFU] = 0x7EU;
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                  msx_config{.cartridge_mapper = msx_cartridge_mapper::plain});
    REQUIRE(sys != nullptr);

    sys->primary_slot_select = 0x14U; // pages 1/2 -> slot 1
    CHECK(sys->bus.read8(0x4000U) == 'A');
    CHECK(sys->bus.read8(0x4001U) == 'B');
    CHECK(sys->bus.read8(0x7FFFU) == 0x7EU);
}

TEST_CASE("msx C-BIOS-style plain 32 KiB handoff exposes the upper ROM page",
          "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> cart(0x8000U, 0xFFU);
    cart[0] = 0x11U;
    cart[0x4000U] = 0x22U;
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart);
    REQUIRE(sys != nullptr);

    sys->primary_slot_select = 0xF4U; // page 1 -> cartridge, page 2 -> RAM
    CHECK(sys->bus.read8(0x4000U) == 0x11U);
    CHECK(sys->bus.read8(0x8000U) == 0x22U);

    sys->bus.write8(0x8000U, 0x77U);
    CHECK(sys->ram[0x8000U] == 0x77U);
    CHECK(sys->bus.read8(0x8000U) == 0x22U);
}

TEST_CASE("msx C-BIOS-style plain 32 KiB handoff exposes the lower ROM page",
          "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    bios[0x4000U] = 0x99U;
    std::vector<std::uint8_t> cart(0x8000U, 0xFFU);
    cart[0] = 0x11U;
    cart[0x4000U] = 0x22U;
    const auto sys = assemble_msx(bios, cart);
    REQUIRE(sys != nullptr);
    sys->cartridge_lower_handoff = true;

    sys->primary_slot_select = 0xD0U; // page 1 -> BIOS, page 2 -> cartridge
    CHECK(sys->bus.read8(0x4000U) == 0x11U);
    CHECK(sys->bus.read8(0x8000U) == 0x22U);
}

TEST_CASE("msx C-BIOS-style handoff leaves non-32 KiB plain cartridges unmapped",
          "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> cart(0x4000U, 0xFFU);
    cart[0] = 0x11U;
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart);
    REQUIRE(sys != nullptr);

    sys->primary_slot_select = 0xF4U; // page 1 -> cartridge, page 2 -> RAM
    sys->bus.write8(0x8000U, 0x44U);
    CHECK(sys->bus.read8(0x4000U) == 0x11U);
    CHECK(sys->bus.read8(0x8000U) == 0x44U);

    sys->primary_slot_select = 0xD0U; // page 1 -> BIOS, page 2 -> cartridge
    CHECK(sys->bus.read8(0x4000U) == 0x00U);
}

TEST_CASE("msx C-BIOS-style lower handoff is gated per cartridge", "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    bios[0x4000U] = 0x99U;
    std::vector<std::uint8_t> cart(0x8000U, 0xFFU);
    cart[0] = 0x11U;
    cart[0x4000U] = 0x22U;
    const auto sys = assemble_msx(bios, cart);
    REQUIRE(sys != nullptr);

    sys->primary_slot_select = 0xD0U; // page 1 -> BIOS, page 2 -> cartridge
    CHECK(sys->bus.read8(0x4000U) == 0x99U);
    CHECK(sys->bus.read8(0x8000U) == 0x22U);
}

TEST_CASE("msx maps two plain cartridge slots independently", "[manifests][msx]") {
    std::vector<std::uint8_t> cart1(0x8000U, 0xFFU);
    cart1[0] = 0x11U;
    cart1[0x4000U] = 0x12U;
    std::vector<std::uint8_t> cart2(0x8000U, 0xFFU);
    cart2[0] = 0x21U;
    cart2[0x4000U] = 0x22U;

    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart1, cart2,
                                  std::span<const std::uint8_t>{}, std::span<const std::uint8_t>{},
                                  std::span<const std::uint8_t>{}, msx_config{});
    REQUIRE(sys != nullptr);

    sys->primary_slot_select = 0x14U; // pages 1/2 -> slot 1
    CHECK(sys->bus.read8(0x4000U) == 0x11U);
    CHECK(sys->bus.read8(0x8000U) == 0x12U);

    sys->primary_slot_select = 0x28U; // pages 1/2 -> slot 2
    CHECK(sys->bus.read8(0x4000U) == 0x21U);
    CHECK(sys->bus.read8(0x8000U) == 0x22U);
}

TEST_CASE("msx expanded primary slot uses the complemented subslot register", "[manifests][msx]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {},
                                  msx_config{.expanded_primary_slots = 0x08U,
                                             .ram_primary_slot = 3U,
                                             .ram_secondary_slot = 2U});
    REQUIRE(sys != nullptr);

    sys->primary_slot_select = 0xC0U; // page 3 -> primary slot 3
    sys->bus.write8(0xFFFFU, 0x80U);  // page 3 -> secondary slot 2

    CHECK(sys->bus.read8(0xFFFFU) == 0x7FU);
    sys->bus.write8(0xC000U, 0x5AU);
    CHECK(sys->ram[0xC000U] == 0x5AU);

    sys->bus.write8(0xFFFFU, 0x00U); // page 3 -> secondary slot 0
    CHECK(sys->bus.read8(0xC000U) == 0xFFU);
}

TEST_CASE("msx memory mapper ports remap 16 KiB RAM segments", "[manifests][msx]") {
    const auto plain = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(plain != nullptr);
    CHECK(plain->read_io(0xFCU) == 0xFFU);

    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {},
                                  msx_config{.ram_mapper_segments = 8U});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mapped_ram.size() == 0x20000U);
    CHECK(sys->read_io(0xFCU) == 3U);
    CHECK(sys->read_io(0xFDU) == 2U);
    CHECK(sys->read_io(0xFEU) == 1U);
    CHECK(sys->read_io(0xFFU) == 0U);

    sys->primary_slot_select = 0xFCU; // all pages except BIOS page 0 -> slot 3 RAM
    sys->bus.write8(0x4000U, 0x11U);
    sys->write_io(0xFDU, 5U);
    CHECK(sys->read_io(0xFDU) == 5U);
    sys->bus.write8(0x4000U, 0x55U);

    CHECK(sys->bus.read8(0x4000U) == 0x55U);
    sys->write_io(0xFDU, 2U);
    CHECK(sys->read_io(0xFDU) == 2U);
    CHECK(sys->bus.read8(0x4000U) == 0x11U);
    sys->write_io(0xFDU, 5U);
    CHECK(sys->bus.read8(0x4000U) == 0x55U);
    CHECK(sys->read_io(0xFDU) == 5U);

    sys->write_io(0xFDU, 13U);
    CHECK(sys->read_io(0xFDU) == 5U);
    CHECK(sys->bus.read8(0x4000U) == 0x55U);
}

TEST_CASE("msx auto-detects ASCII8 cartridges from bank-register signatures",
          "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    add_mapper_signatures(cart, {0x6000U, 0x6800U, 0x7000U, 0x7800U});

    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart);
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mapper == msx_cartridge_mapper::ascii8);

    sys->primary_slot_select = 0x14U;
    CHECK(sys->bus.read8(0x6000U) == 0x81U);
    sys->bus.write8(0x6800U, 5U);
    CHECK(sys->bus.read8(0x6000U) == 0x85U);
}

TEST_CASE("msx auto-detects ASCII16 cartridges from two 16 KiB bank registers",
          "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> cart = make_16k_paged_cart(4U);
    add_mapper_signatures(cart, {0x6000U, 0x7000U});

    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart);
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mapper == msx_cartridge_mapper::ascii16);

    sys->primary_slot_select = 0x14U;
    CHECK(sys->bus.read8(0x4000U) == 0x90U);
    sys->bus.write8(0x6000U, 2U);
    CHECK(sys->bus.read8(0x4000U) == 0x92U);
}

TEST_CASE("msx auto-detects Konami SCC cartridges from 8 KiB write windows",
          "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    add_mapper_signatures(cart, {0x5000U, 0x7000U, 0x9000U, 0xB000U});

    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart);
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mapper == msx_cartridge_mapper::konami_scc);

    sys->primary_slot_select = 0x14U;
    CHECK(sys->bus.read8(0x4000U) == 0x80U);
    sys->bus.write8(0x5000U, 4U);
    CHECK(sys->bus.read8(0x4000U) == 0x84U);
}

TEST_CASE("msx forced plain mapper overrides automatic signatures", "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    add_mapper_signatures(cart, {0x6000U, 0x6800U, 0x7000U, 0x7800U});

    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                  msx_config{.cartridge_mapper = msx_cartridge_mapper::plain});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->mapper == msx_cartridge_mapper::plain);

    sys->primary_slot_select = 0x14U;
    CHECK(sys->bus.read8(0x6000U) == 0x81U);
    sys->bus.write8(0x6800U, 5U);
    CHECK(sys->bus.read8(0x6000U) == 0x81U);
}

TEST_CASE("msx ASCII8 mapper switches 8 KiB cartridge banks", "[manifests][msx]") {
    std::vector<std::uint8_t> cart(0x2000U * 8U, 0x00U);
    for (std::size_t bank = 0; bank < 8U; ++bank) {
        cart[bank * 0x2000U] = static_cast<std::uint8_t>(0x80U + bank);
    }
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                  msx_config{.cartridge_mapper = msx_cartridge_mapper::ascii8});
    REQUIRE(sys != nullptr);
    sys->primary_slot_select = 0x14U;

    CHECK(sys->bus.read8(0x4000U) == 0x80U);
    sys->bus.write8(0x6000U, 6U); // ASCII8 register 0 -> $4000-$5FFF
    CHECK(sys->bus.read8(0x4000U) == 0x86U);
}

TEST_CASE("msx Generic8 mapper switches all four 8 KiB cartridge windows",
          "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> cart(0x2000U * 8U, 0x00U);
    for (std::size_t bank = 0; bank < 8U; ++bank) {
        cart[bank * 0x2000U] = static_cast<std::uint8_t>(0x80U + bank);
    }
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                  msx_config{.cartridge_mapper = msx_cartridge_mapper::generic8});
    REQUIRE(sys != nullptr);
    sys->primary_slot_select = 0x14U;

    CHECK(sys->bus.read8(0x4000U) == 0x80U);
    CHECK(sys->bus.read8(0x6000U) == 0x81U);
    CHECK(sys->bus.read8(0x8000U) == 0x82U);
    CHECK(sys->bus.read8(0xA000U) == 0x83U);

    sys->bus.write8(0x4000U, 4U);
    sys->bus.write8(0x6000U, 5U);
    sys->bus.write8(0x8000U, 6U);
    sys->bus.write8(0xA000U, 7U);

    CHECK(sys->bus.read8(0x4000U) == 0x84U);
    CHECK(sys->bus.read8(0x6000U) == 0x85U);
    CHECK(sys->bus.read8(0x8000U) == 0x86U);
    CHECK(sys->bus.read8(0xA000U) == 0x87U);
}

TEST_CASE("msx ASCII16 mapper ignores ASCII8-only half-window writes",
          "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> cart = make_16k_paged_cart(4U);
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                  msx_config{.cartridge_mapper = msx_cartridge_mapper::ascii16});
    REQUIRE(sys != nullptr);
    sys->primary_slot_select = 0x14U;

    CHECK(sys->bus.read8(0x4000U) == 0x90U);
    CHECK(sys->bus.read8(0x8000U) == 0x91U);
    sys->bus.write8(0x6800U, 2U);
    sys->bus.write8(0x7800U, 3U);
    CHECK(sys->bus.read8(0x4000U) == 0x90U);
    CHECK(sys->bus.read8(0x8000U) == 0x91U);

    sys->bus.write8(0x6000U, 2U);
    sys->bus.write8(0x7000U, 3U);
    CHECK(sys->bus.read8(0x4000U) == 0x92U);
    CHECK(sys->bus.read8(0x8000U) == 0x93U);
}

TEST_CASE("msx second cartridge slot keeps independent ASCII8 mapper banks", "[manifests][msx]") {
    std::vector<std::uint8_t> cart1(0x2000U * 8U, 0x00U);
    std::vector<std::uint8_t> cart2(0x2000U * 8U, 0x00U);
    for (std::size_t bank = 0; bank < 8U; ++bank) {
        cart1[bank * 0x2000U] = static_cast<std::uint8_t>(0x80U + bank);
        cart2[bank * 0x2000U] = static_cast<std::uint8_t>(0x40U + bank);
    }

    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart1, cart2,
                                  std::span<const std::uint8_t>{}, std::span<const std::uint8_t>{},
                                  std::span<const std::uint8_t>{},
                                  msx_config{.cartridge_mapper = msx_cartridge_mapper::ascii8,
                                             .cartridge2_mapper = msx_cartridge_mapper::ascii8});
    REQUIRE(sys != nullptr);

    sys->primary_slot_select = 0x14U; // pages 1/2 -> slot 1
    sys->bus.write8(0x6000U, 6U);
    CHECK(sys->bus.read8(0x4000U) == 0x86U);
    CHECK(sys->cart_8k_bank[0] == 6U);
    CHECK(sys->cart2_8k_bank[0] == 0U);

    sys->primary_slot_select = 0x28U; // pages 1/2 -> slot 2
    CHECK(sys->bus.read8(0x4000U) == 0x40U);
    sys->bus.write8(0x6000U, 5U);
    CHECK(sys->bus.read8(0x4000U) == 0x45U);
    CHECK(sys->cart_8k_bank[0] == 6U);
    CHECK(sys->cart2_8k_bank[0] == 5U);
}

TEST_CASE("msx Korean MSX mapper routes fixed and banked pages", "[manifests][msx]") {
    std::vector<std::uint8_t> cart(0x2000U * 8U, 0x00U);
    for (std::size_t bank = 0; bank < 8U; ++bank) {
        std::fill_n(cart.begin() + static_cast<std::ptrdiff_t>(bank * 0x2000U), 0x2000U,
                    static_cast<std::uint8_t>(bank));
    }

    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                  msx_config{.cartridge_mapper = msx_cartridge_mapper::korean_msx});
    REQUIRE(sys != nullptr);
    sys->primary_slot_select = 0x15U; // pages 0/1/2 -> slot 1

    CHECK(sys->bus.read8(0x0000U) == 0U);
    CHECK(sys->bus.read8(0x2000U) == 1U);
    sys->bus.write8(0x0000U, 3U);
    sys->bus.write8(0x0001U, 4U);
    sys->bus.write8(0x0002U, 5U);
    sys->bus.write8(0x0003U, 6U);
    CHECK(sys->bus.read8(0x8000U) == 3U);
    CHECK(sys->bus.read8(0xA000U) == 4U);
    CHECK(sys->bus.read8(0x4000U) == 5U);
    CHECK(sys->bus.read8(0x6000U) == 6U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored =
        assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                     msx_config{.cartridge_mapper = msx_cartridge_mapper::korean_msx});
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(blob);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored->korean_mapper.page(2) == 5U);
    CHECK(restored->bus.read8(0x4000U) == 5U);
}

TEST_CASE("msx Korean MSX Nemesis mapper boots from the last 8 KiB page", "[manifests][msx]") {
    std::vector<std::uint8_t> cart(0x2000U * 8U, 0x00U);
    for (std::size_t bank = 0; bank < 8U; ++bank) {
        std::fill_n(cart.begin() + static_cast<std::ptrdiff_t>(bank * 0x2000U), 0x2000U,
                    static_cast<std::uint8_t>(bank));
    }

    const auto sys =
        assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                     msx_config{.cartridge_mapper = msx_cartridge_mapper::korean_msx_nemesis});
    REQUIRE(sys != nullptr);
    sys->primary_slot_select = 0x01U; // page 0 -> slot 1

    CHECK(sys->bus.read8(0x0000U) == 7U);
    CHECK(sys->bus.read8(0x1FFFU) == 7U);
    CHECK(sys->bus.read8(0x2000U) == 1U);
}

TEST_CASE("msx ASCII8 SRAM mapper exposes battery RAM in upper cartridge pages",
          "[manifests][msx]") {
    std::vector<std::uint8_t> cart(0x2000U * 8U, 0x00U);
    for (std::size_t bank = 0; bank < 8U; ++bank) {
        cart[bank * 0x2000U] = static_cast<std::uint8_t>(0x80U + bank);
    }

    const auto sys =
        assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                     msx_config{.cartridge_mapper = msx_cartridge_mapper::ascii8_sram8});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->battery_ram().size() == 0x2000U);
    sys->primary_slot_select = 0x14U;

    CHECK(sys->bus.read8(0x8000U) == 0x82U);
    sys->bus.write8(0x7000U, 0x80U); // bank register 2 selects SRAM
    CHECK(sys->bus.read8(0x8000U) == 0xFFU);
    sys->bus.write8(0x8000U, 0x5AU);
    CHECK(sys->battery_ram()[0] == 0x5AU);
    CHECK(sys->bus.read8(0x8000U) == 0x5AU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored =
        assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                     msx_config{.cartridge_mapper = msx_cartridge_mapper::ascii8_sram8});
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(blob);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    restored->primary_slot_select = 0x14U;
    CHECK(restored->bus.read8(0x8000U) == 0x5AU);

    restored->bus.write8(0x7000U, 4U);
    CHECK(restored->bus.read8(0x8000U) == 0x84U);
}

TEST_CASE("msx ASCII16 SRAM mapper exposes mirrored 2 KiB battery RAM", "[manifests][msx]") {
    std::vector<std::uint8_t> cart(0x4000U * 4U, 0x00U);
    for (std::size_t bank = 0; bank < 4U; ++bank) {
        cart[bank * 0x4000U] = static_cast<std::uint8_t>(0x90U + bank);
    }

    const auto sys =
        assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                     msx_config{.cartridge_mapper = msx_cartridge_mapper::ascii16_sram2});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->battery_ram().size() == 0x0800U);
    sys->primary_slot_select = 0x14U;

    CHECK(sys->bus.read8(0x8000U) == 0x91U);
    sys->bus.write8(0x7000U, 0x10U); // bank register 1 selects SRAM
    CHECK(sys->bus.read8(0x8000U) == 0xFFU);
    sys->bus.write8(0x87FFU, 0x33U);
    CHECK(sys->battery_ram()[0x07FFU] == 0x33U);
    CHECK(sys->bus.read8(0x87FFU) == 0x33U);
    CHECK(sys->bus.read8(0x8FFFU) == 0x33U);
}

TEST_CASE("msx cartridge mappers preserve bank 255 on full 256-bank ROMs", "[manifests][msx]") {
    std::vector<std::uint8_t> cart8(0x2000U * 256U, 0x00U);
    cart8[0] = 0x10U;
    cart8[0x2000U * 255U] = 0xEFU;
    const auto ascii8 = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart8,
                                     msx_config{.cartridge_mapper = msx_cartridge_mapper::ascii8});
    REQUIRE(ascii8 != nullptr);
    ascii8->primary_slot_select = 0x14U;
    CHECK(ascii8->bus.read8(0x4000U) == 0x10U);
    ascii8->bus.write8(0x6000U, 255U);
    CHECK(ascii8->bus.read8(0x4000U) == 0xEFU);

    std::vector<std::uint8_t> cart16(0x4000U * 256U, 0x00U);
    cart16[0] = 0x20U;
    cart16[0x4000U * 255U] = 0xDFU;
    const auto ascii16 =
        assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart16,
                     msx_config{.cartridge_mapper = msx_cartridge_mapper::ascii16});
    REQUIRE(ascii16 != nullptr);
    ascii16->primary_slot_select = 0x14U;
    CHECK(ascii16->bus.read8(0x4000U) == 0x20U);
    ascii16->bus.write8(0x6000U, 255U);
    CHECK(ascii16->bus.read8(0x4000U) == 0xDFU);
}

TEST_CASE("msx Konami mapper mirrors bank windows into pages 0 and 3", "[manifests][msx][mapper]") {
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                  msx_config{.cartridge_mapper = msx_cartridge_mapper::konami});
    REQUIRE(sys != nullptr);
    sys->primary_slot_select = 0x55U; // all four 16 KiB pages -> cartridge slot 1

    CHECK(sys->bus.read8(0xC000U) == 0x80U);
    CHECK(sys->bus.read8(0xE000U) == 0x81U);

    sys->bus.write8(0xE000U, 4U); // mirror of $6000
    CHECK(sys->bus.read8(0xE000U) == 0x84U);
    CHECK(sys->cart_8k_bank[1] == 4U);

    sys->bus.write8(0x0000U, 5U); // mirror of $8000
    CHECK(sys->bus.read8(0x0000U) == 0x85U);
    CHECK(sys->cart_8k_bank[2] == 5U);

    sys->bus.write8(0x2000U, 6U); // mirror of $A000
    CHECK(sys->bus.read8(0x2000U) == 0x86U);
    CHECK(sys->cart_8k_bank[3] == 6U);
}

TEST_CASE("msx Konami SCC mapper exposes memory-mapped SCC audio registers", "[manifests][msx]") {
    std::vector<std::uint8_t> cart(0x2000U * 8U, 0xFFU);
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                  msx_config{.cartridge_mapper = msx_cartridge_mapper::konami_scc});
    REQUIRE(sys != nullptr);
    sys->primary_slot_select = 0x14U;

    sys->bus.write8(0x9000U, 0x3FU);
    CHECK(sys->scc_window_enabled);

    // Fill channel 0's 32-byte waveform so the sampled output is independent of
    // the oscillator phase, then verify the write reached the SCC waveform RAM.
    for (std::uint16_t i = 0; i < 32U; ++i) {
        sys->bus.write8(static_cast<std::uint16_t>(0x9800U + i), 0x7FU);
    }
    CHECK(sys->scc.wave_sample(0, 0) == 0x7FU);
    CHECK(sys->bus.read8(0x9800U) == 0x7FU);

    // A non-zero frequency keeps channel 0 audible (a zero period silences it).
    sys->bus.write8(0x9880U, 0x10U);
    sys->bus.write8(0x9881U, 0x00U);
    sys->bus.write8(0x988AU, 0x0FU);
    sys->bus.write8(0x988FU, 0x01U);
    sys->scc.set_clock_divider(1);
    sys->scc.tick(1);
    CHECK(sys->scc.last_left() == static_cast<std::int16_t>(0x7F * 15 * 3));
}

TEST_CASE("msx Konami SCC mapper mirrors bank and SCC apertures into page 0",
          "[manifests][msx][mapper][scc]") {
    std::vector<std::uint8_t> cart = make_8k_paged_cart(64U);
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                  msx_config{.cartridge_mapper = msx_cartridge_mapper::konami_scc});
    REQUIRE(sys != nullptr);
    sys->primary_slot_select = 0x55U; // all four 16 KiB pages -> cartridge slot 1

    sys->bus.write8(0x1000U, 0x3FU); // mirror of $9000; enables the SCC window
    CHECK(sys->scc_window_enabled);
    CHECK(sys->cart_8k_bank[2] == 0x3FU);
    CHECK(sys->bus.read8(0x0000U) == 0xBFU);

    sys->bus.write8(0x1800U, 0x5AU); // mirror of $9800
    CHECK(sys->scc.wave_sample(0, 0) == 0x5AU);
    CHECK(sys->bus.read8(0x1800U) == 0x5AU);
}

TEST_CASE("msx second cartridge slot exposes Konami SCC audio registers", "[manifests][msx]") {
    std::vector<std::uint8_t> cart1(0x8000U, 0xFFU);
    std::vector<std::uint8_t> cart2(0x2000U * 8U, 0xFFU);
    const auto sys = assemble_msx(
        std::vector<std::uint8_t>(0x8000U, 0x00U), cart1, cart2, std::span<const std::uint8_t>{},
        std::span<const std::uint8_t>{}, std::span<const std::uint8_t>{},
        msx_config{.cartridge2_mapper = msx_cartridge_mapper::konami_scc});
    REQUIRE(sys != nullptr);
    sys->primary_slot_select = 0x28U; // pages 1/2 -> slot 2

    sys->bus.write8(0x9000U, 0x3FU);
    CHECK(sys->scc2_window_enabled);

    sys->bus.write8(0x9800U, 0x55U);
    CHECK(sys->scc.wave_sample(0, 0) == 0x55U);
    CHECK(sys->bus.read8(0x9800U) == 0x55U);
}

TEST_CASE("msx optional RP-5C01 RTC is decoded at ports $B4/$B5", "[manifests][msx]") {
    const auto plain = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(plain != nullptr);
    plain->write_io(0xB4U, 0x0DU);
    CHECK(plain->read_io(0xB5U) == 0xFFU);

    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {},
                                  msx_config{.rtc_enabled = true});
    REQUIRE(sys != nullptr);
    sys->write_io(0xB4U, 0x0DU);
    CHECK(sys->read_io(0xB5U) == 0x08U);

    sys->write_io(0xB5U, 0x0BU); // timer enabled, block 3
    sys->write_io(0xB4U, 0x01U);
    sys->write_io(0xB5U, 0x7CU);
    CHECK(sys->read_io(0xB5U) == 0x0CU);
    CHECK(sys->rtc.raw_block_register(3, 1) == 0x0CU);

    sys->rtc.set_cycles_per_second(2U);
    sys->rtc.set_time_24h(26U, 1U, 1U, 4U, 0U, 0U, 0U);
    run_cycles(*sys, 2);
    CHECK(sys->rtc.raw_block_register(0, 0) == 1U);
}

TEST_CASE("msx optional MSX-MUSIC OPLL is decoded at ports $7C/$7D", "[manifests][msx]") {
    const auto plain = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(plain != nullptr);
    plain->write_io(0x7CU, 0x30U);
    plain->write_io(0x7DU, 0x70U);
    CHECK(plain->fm.address_latch() == 0U);

    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {},
                                  msx_config{.fm_music_enabled = true});
    REQUIRE(sys != nullptr);
    sys->fm.set_clock_divider(1);
    sys->write_io(0x7CU, 0x30U);
    sys->write_io(0x7DU, 0x70U); // channel 0: preset 7, loud
    sys->write_io(0x7CU, 0x10U);
    sys->write_io(0x7DU, 0xA0U);
    sys->write_io(0x7CU, 0x20U);
    sys->write_io(0x7DU, 0x18U); // key-on, block 4
    CHECK(sys->fm.address_latch() == 0x20U);

    bool any_nonzero = false;
    for (int i = 0; i < 8000; ++i) {
        sys->fm.tick(1);
        any_nonzero = any_nonzero || (sys->fm.last_sample() != 0);
    }
    CHECK(any_nonzero);
}

TEST_CASE("msx optional FM-PAC SRAM unlocks at $5FFE/$5FFF", "[manifests][msx]") {
    std::vector<std::uint8_t> cart(0x8000U, 0xFFU);
    cart[0x0000U] = 0x11U;
    cart[0x1FFEU] = 0x22U;
    cart[0x1FFFU] = 0x33U;

    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
                                  msx_config{.fm_music_enabled = true, .fmpac_sram_enabled = true});
    REQUIRE(sys != nullptr);
    sys->primary_slot_select = 0x14U;
    REQUIRE(sys->battery_ram().size() == 0x2000U);

    CHECK(sys->bus.read8(0x4000U) == 0x11U);
    CHECK(sys->bus.read8(0x5FFEU) == 0x22U);
    sys->bus.write8(0x4000U, 0x55U);
    CHECK(sys->bus.read8(0x4000U) == 0x11U);

    sys->bus.write8(0x5FFEU, 0x4DU);
    sys->bus.write8(0x5FFFU, 0x69U);
    CHECK(sys->bus.read8(0x4000U) == 0xFFU);

    sys->bus.write8(0x4000U, 0x5AU);
    CHECK(sys->bus.read8(0x4000U) == 0x5AU);
    CHECK(sys->battery_ram()[0] == 0x5AU);

    sys->bus.write8(0x5FFFU, 0x00U);
    CHECK(sys->bus.read8(0x4000U) == 0x11U);

    sys->bus.write8(0x5FFFU, 0x69U);
    CHECK(sys->bus.read8(0x4000U) == 0x5AU);
}

TEST_CASE("msx optional Kanji ROM streams JIS level data through ports", "[manifests][msx]") {
    const auto plain = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(plain != nullptr);
    plain->write_io(0xD8U, 0x23U);
    plain->write_io(0xD9U, 0x04U);
    CHECK(plain->read_io(0xD9U) == 0xFFU);

    const auto kanji = make_kanji_rom();
    const auto sys =
        assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {}, std::span<const std::uint8_t>{},
                     std::span<const std::uint8_t>{}, kanji, msx_config{});
    REQUIRE(sys != nullptr);

    sys->write_io(0xD8U, 0x23U);
    sys->write_io(0xD9U, 0x04U);
    CHECK(sys->read_io(0xD9U) == 0x40U);
    CHECK(sys->read_io(0xD9U) == 0x41U);
    for (int i = 0; i < 30; ++i) {
        (void)sys->read_io(0xD9U);
    }
    CHECK(sys->read_io(0xD9U) == 0x40U);

    sys->write_io(0xDAU, 0x05U);
    sys->write_io(0xDBU, 0x01U);
    CHECK(sys->read_io(0xDBU) == 0xA0U);
    CHECK(sys->read_io(0xDBU) == 0xA1U);
}

TEST_CASE("msx disk ROM slot exposes memory-mapped WD1793 registers", "[manifests][msx]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> empty_cart;
    std::vector<std::uint8_t> disk_rom(0x4000U, 0xFFU);
    disk_rom[0] = 0xC9U;
    auto disk = make_dsk();
    const std::size_t off = disk_sector_offset(2U, 1U, 3U);
    disk[off] = 0x42U;

    const auto sys = assemble_msx(bios, empty_cart, disk_rom, disk, msx_config{});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->disk_enabled);
    CHECK((sys->expanded_primary_slots & 0x08U) != 0U);

    sys->primary_slot_select = 0xCCU; // pages 1 and 3 -> expanded primary slot 3
    sys->bus.write8(0xFFFFU, 0x04U);  // page 1 -> secondary slot 1 disk ROM
    CHECK(sys->bus.read8(0x4000U) == 0xC9U);

    sys->bus.write8(0x7FFCU, 1U); // side 1
    sys->bus.write8(0x7FF9U, 2U); // track
    sys->bus.write8(0x7FFAU, 3U); // sector
    sys->bus.write8(0x7FF8U, 0x80U);
    CHECK((sys->bus.read8(0x7FFFU) & 0x80U) != 0U);
    CHECK(read_fdc_memory(*sys, 0x7FFBU) == 0x42U);
}

TEST_CASE("msx WD1793 I/O ports read flat DSK sectors", "[manifests][msx]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> empty_cart;
    auto disk = make_dsk();
    const std::size_t off = disk_sector_offset(1U, 1U, 2U);
    disk[off] = 0xA5U;

    const auto sys = assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{}, disk,
                                  msx_config{.disk_enabled = true});
    REQUIRE(sys != nullptr);

    sys->write_io(0xD4U, 0x10U); // side 1, drive A
    sys->write_io(0xD1U, 1U);
    sys->write_io(0xD2U, 2U);
    sys->write_io(0xD0U, 0x80U);
    CHECK((sys->read_io(0xD4U) & 0x80U) != 0U);
    CHECK(read_fdc_io(*sys) == 0xA5U);
}

TEST_CASE("msx D4 FDC control decodes shared drive and side aliases",
          "[manifests][msx][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> empty_cart;
    auto disk = make_dsk();
    disk[disk_sector_offset(0U, 0U, 1U)] = 0x41U;
    disk[disk_sector_offset(0U, 1U, 1U)] = 0x61U;

    const auto side_alias = assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{}, disk,
                                         msx_config{.disk_enabled = true});
    REQUIRE(side_alias != nullptr);
    side_alias->write_io(0xD4U, 0x04U);
    CHECK(side_alias->fdc.selected_drive() == 0U);
    CHECK(side_alias->fdc.selected_side() == 1U);
    side_alias->write_io(0xD2U, 1U);
    side_alias->write_io(0xD0U, 0x80U);
    REQUIRE(side_alias->fdc.drq());
    CHECK(read_fdc_io(*side_alias) == 0x61U);

    const auto drive_priority =
        assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{}, disk,
                     msx_config{.disk_enabled = true});
    REQUIRE(drive_priority != nullptr);
    drive_priority->write_io(0xD4U, 0x03U);
    CHECK(drive_priority->fdc.selected_drive() == 0U);
    CHECK(drive_priority->fdc.selected_side() == 0U);
    drive_priority->write_io(0xD2U, 1U);
    drive_priority->write_io(0xD0U, 0x80U);
    REQUIRE(drive_priority->fdc.drq());
    CHECK(read_fdc_io(*drive_priority) == 0x41U);

    const auto drive_b = assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{}, disk,
                                      msx_config{.disk_enabled = true});
    REQUIRE(drive_b != nullptr);
    drive_b->write_io(0xD4U, 0x02U);
    drive_b->write_io(0xD2U, 1U);
    drive_b->write_io(0xD0U, 0x80U);
    CHECK_FALSE(drive_b->fdc.drq());
    CHECK((drive_b->read_io(0xD0U) & k_fdc_status_not_ready) != 0U);
}

TEST_CASE("msx WD1793 I/O ports honor MSX-DOS 1DD BPB geometry", "[manifests][msx]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> empty_cart;
    auto disk = make_msx_dos_dsk(1U, 9U);
    disk[disk_sector_offset(1U, 0U, 1U, 1U, 9U)] = 0x6DU;

    const auto sys = assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{}, disk,
                                  msx_config{.disk_enabled = true});
    REQUIRE(sys != nullptr);

    sys->write_io(0xD4U, 0x00U); // side 0, drive A
    sys->write_io(0xD1U, 1U);
    sys->write_io(0xD2U, 1U);
    sys->write_io(0xD0U, 0x80U);
    REQUIRE((sys->read_io(0xD4U) & 0x80U) != 0U);
    CHECK(read_fdc_io(*sys) == 0x6DU);
}

TEST_CASE("msx WD1793 I/O ports stream multi-sector reads", "[manifests][msx]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> empty_cart;
    auto disk = make_dsk();
    disk[disk_sector_offset(1U, 1U, 8U)] = 0xA8U;
    disk[disk_sector_offset(1U, 1U, 9U)] = 0xB9U;

    const auto sys = assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{}, disk,
                                  msx_config{.disk_enabled = true});
    REQUIRE(sys != nullptr);

    sys->write_io(0xD4U, 0x10U);
    sys->write_io(0xD1U, 1U);
    sys->write_io(0xD2U, 8U);
    sys->write_io(0xD0U, 0x90U);

    CHECK(read_fdc_io(*sys) == 0xA8U);
    for (std::size_t i = 1U; i < wd1793::sector_size; ++i) {
        (void)read_fdc_io(*sys);
    }
    CHECK(sys->read_io(0xD2U) == 9U);
    CHECK((sys->read_io(0xD4U) & 0x80U) != 0U);

    CHECK(read_fdc_io(*sys) == 0xB9U);
    for (std::size_t i = 1U; i < wd1793::sector_size; ++i) {
        (void)read_fdc_io(*sys);
    }
    CHECK(sys->read_io(0xD2U) == 10U);
    CHECK((sys->read_io(0xD0U) & k_fdc_status_record_not_found) != 0U);
}

TEST_CASE("msx WD1793 I/O ports stream read-address ID fields", "[manifests][msx]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> empty_cart;
    const auto sys = assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{}, make_dsk(),
                                  msx_config{.disk_enabled = true});
    REQUIRE(sys != nullptr);

    sys->write_io(0xD4U, 0x10U);
    sys->write_io(0xD1U, 6U);
    sys->write_io(0xD2U, 7U);
    sys->write_io(0xD0U, 0xC0U);

    CHECK(read_fdc_io(*sys) == 6U);
    CHECK(read_fdc_io(*sys) == 1U);
    CHECK(read_fdc_io(*sys) == 7U);
    CHECK(read_fdc_io(*sys) == 0x02U);
    CHECK(read_fdc_io(*sys) == 0x70U);
    CHECK(read_fdc_io(*sys) == 0x60U);
    CHECK(sys->read_io(0xD2U) == 6U);
    CHECK((sys->read_io(0xD4U) & 0x40U) != 0U);
}

TEST_CASE("msx WD1793 I/O ports stream synthesized read-track data", "[manifests][msx]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> empty_cart;
    auto disk = make_dsk();
    const std::size_t sector2 = disk_sector_offset(2U, 1U, 2U);
    disk[sector2] = 0x66U;
    disk[sector2 + 1U] = 0x77U;

    const auto sys = assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{}, disk,
                                  msx_config{.disk_enabled = true});
    REQUIRE(sys != nullptr);

    sys->write_io(0xD4U, 0x10U);
    sys->write_io(0xD1U, 2U);
    sys->write_io(0xD0U, 0xE0U);
    CHECK((sys->read_io(0xD4U) & 0x80U) != 0U);

    std::vector<std::uint8_t> stream;
    stream.reserve(wd1793::standard_mfm_track_size);
    for (std::size_t i = 0U; i < wd1793::standard_mfm_track_size; ++i) {
        stream.push_back(read_fdc_io(*sys));
    }

    const std::vector<std::uint8_t> id_field{
        0xA1U, 0xA1U, 0xA1U, 0xFEU, 0x02U, 0x01U, 0x02U, 0x02U,
    };
    const auto id = std::search(stream.begin(), stream.end(), id_field.begin(), id_field.end());
    CHECK(id != stream.end());

    const std::vector<std::uint8_t> data_field{
        0xA1U, 0xA1U, 0xA1U, 0xFBU, 0x66U, 0x77U,
    };
    const auto data =
        std::search(stream.begin(), stream.end(), data_field.begin(), data_field.end());
    CHECK(data != stream.end());
    CHECK(data > id);
    CHECK((sys->read_io(0xD4U) & 0x40U) != 0U);
}

TEST_CASE("msx WD1793 I/O ports write formatted tracks to flat DSK sectors", "[manifests][msx]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> empty_cart;
    const auto sys = assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{}, make_dsk(),
                                  msx_config{.disk_enabled = true});
    REQUIRE(sys != nullptr);

    sys->write_io(0xD4U, 0x10U);
    sys->write_io(0xD1U, 2U);
    sys->write_io(0xD0U, 0xF0U);
    REQUIRE((sys->read_io(0xD4U) & 0x80U) != 0U);

    auto stream = make_write_track_stream(2U, 1U, 4U, 0x24U, 0x68U);
    const auto id_mark = std::find(stream.begin(), stream.end(), 0xFEU);
    REQUIRE(id_mark != stream.end());
    const std::size_t custom_gap = static_cast<std::size_t>(id_mark - stream.begin()) - 20U;
    stream[custom_gap] = 0x35U;
    for (const std::uint8_t value : stream) {
        write_fdc_io(*sys, value);
    }

    CHECK((sys->read_io(0xD4U) & 0x40U) != 0U);

    sys->write_io(0xD0U, 0xE0U);
    REQUIRE((sys->read_io(0xD4U) & 0x80U) != 0U);
    std::vector<std::uint8_t> raw_track;
    raw_track.reserve(wd1793::standard_mfm_track_size);
    for (std::size_t i = 0U; i < wd1793::standard_mfm_track_size; ++i) {
        raw_track.push_back(read_fdc_io(*sys));
    }
    CHECK(std::find(raw_track.begin(), raw_track.end(), static_cast<std::uint8_t>(0x35U)) !=
          raw_track.end());

    sys->write_io(0xD2U, 4U);
    sys->write_io(0xD0U, 0x80U);
    CHECK(read_fdc_io(*sys) == 0x24U);
    CHECK(read_fdc_io(*sys) == 0x68U);
    CHECK(read_fdc_io(*sys) == 0xE5U);
}

TEST_CASE("msx write-protected disk images reject WD1793 sector writes", "[manifests][msx]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> empty_cart;
    auto disk = make_dsk();
    disk[wd1793::sector_size] = 0x51U;
    const auto sys = assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{}, disk,
                                  msx_config{.disk_enabled = true, .disk_write_protected = true});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.write_protected());

    sys->write_io(0xD2U, 2U);
    sys->write_io(0xD0U, 0xA0U);

    CHECK_FALSE(sys->fdc.drq());
    CHECK((sys->read_io(0xD0U) & k_fdc_status_write_protect) != 0U);
    CHECK(sys->fdc.disk_image()[wd1793::sector_size] == 0x51U);
}

TEST_CASE("msx WD1793 I/O ports report deleted-data sector status", "[manifests][msx]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> empty_cart;
    const auto sys = assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{}, make_dsk(),
                                  msx_config{.disk_enabled = true});
    REQUIRE(sys != nullptr);

    sys->write_io(0xD4U, 0x00U); // drive A, side 0
    sys->write_io(0xD1U, 3U);
    sys->write_io(0xD2U, 4U);
    sys->write_io(0xD0U, 0xA1U); // write sector with deleted data mark
    REQUIRE((sys->read_io(0xD4U) & 0x80U) != 0U);
    for (std::size_t i = 0U; i < wd1793::sector_size; ++i) {
        write_fdc_io(*sys, static_cast<std::uint8_t>(0x40U + i));
    }

    sys->write_io(0xD2U, 4U);
    sys->write_io(0xD0U, 0x80U);
    CHECK(read_fdc_io(*sys) == 0x40U);
    for (std::size_t i = 1U; i < wd1793::sector_size; ++i) {
        (void)read_fdc_io(*sys);
    }
    CHECK((sys->read_io(0xD0U) & k_fdc_status_record_type) != 0U);
}

TEST_CASE("msx WD1793 I/O ports route force-interrupt commands", "[manifests][msx]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> empty_cart;
    const auto sys = assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{}, make_dsk(),
                                  msx_config{.disk_enabled = true});
    REQUIRE(sys != nullptr);

    sys->write_io(0xD2U, 1U);
    sys->write_io(0xD0U, 0x80U);
    REQUIRE(sys->fdc.busy());
    sys->write_io(0xD0U, 0xD0U);
    CHECK_FALSE(sys->fdc.busy());
    CHECK_FALSE(sys->fdc.intrq());

    sys->write_io(0xD0U, 0x80U);
    REQUIRE(sys->fdc.busy());
    sys->write_io(0xD0U, 0xD8U);
    CHECK_FALSE(sys->fdc.busy());
    CHECK(sys->fdc.intrq());
}

TEST_CASE("msx system latch state round-trips", "[manifests][msx]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {},
                                  msx_config{.expanded_primary_slots = 0x08U,
                                             .ram_primary_slot = 3U,
                                             .ram_secondary_slot = 2U,
                                             .ram_mapper_segments = 8U});
    REQUIRE(sys != nullptr);
    configure_ppi_outputs(*sys);
    sys->write_io(0xA8U, 0xC0U);
    sys->bus.write8(0xFFFFU, 0x80U);
    sys->write_io(0xFFU, 5U);
    sys->bus.write8(0xC000U, 0x42U);
    sys->set_key(1, 2, true);
    configure_psg_gpio(*sys);
    sys->write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->write_io(0xA1U, 0x40U);
    const std::array<std::uint32_t, 3> tape_pulses{3U, 4U, 5U};
    sys->cassette.load_half_cycles(tape_pulses);
    sys->cassette.set_play(true);
    sys->write_io(0xABU, 0x08U);
    run_cycles(*sys, 5);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {},
                                       msx_config{.expanded_primary_slots = 0x08U,
                                                  .ram_primary_slot = 3U,
                                                  .ram_secondary_slot = 2U,
                                                  .ram_mapper_segments = 8U});
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(blob);
    restored->load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored->primary_slot_select == 0xC0U);
    CHECK(restored->bus.read8(0xFFFFU) == 0x7FU);
    CHECK(restored->bus.read8(0xC000U) == 0x42U);
    CHECK(restored->keyboard_rows[1] == 0xFBU);
    CHECK(restored->psg.read_reg(mnemos::chips::audio::ssg::reg_mixer) == 0xBFU);
    CHECK(restored->psg.read_reg(mnemos::chips::audio::ssg::reg_port_b) == 0x40U);
    CHECK(restored->cassette.half_cycle_count() == tape_pulses.size());
    CHECK(restored->cassette.position_half_cycle() == sys->cassette.position_half_cycle());
    CHECK(restored->cassette.input_high() == sys->cassette.input_high());
    CHECK(restored->cassette.motor_on());
}

TEST_CASE("msx selected video model round-trips with the machine snapshot", "[manifests][msx]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {},
                                  msx_config{.video_model = msx_video_model::v9938});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->msx2_video());

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(blob);
    restored->load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored->video_model == msx_video_model::v9938);
    CHECK(restored->msx2_video());
    CHECK(&restored->active_video() == static_cast<mnemos::chips::ivideo*>(&restored->vdp2));

    restored->write_io(0x99U, 0x01U);
    restored->write_io(0x99U, 0x90U);
    restored->write_io(0x9AU, 0x70U);
    restored->write_io(0x9AU, 0x00U);
    CHECK(restored->vdp2.palette(1) == 0x01C0U);
}

TEST_CASE("msx FDC mounted disk state round-trips with the machine snapshot", "[manifests][msx]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> empty_cart;
    auto disk = make_dsk();
    const auto sys = assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{}, disk,
                                  msx_config{.disk_enabled = true});
    REQUIRE(sys != nullptr);

    sys->write_io(0xD1U, 3U);
    sys->write_io(0xD2U, 4U);
    sys->write_io(0xD0U, 0xA0U);
    REQUIRE(sys->fdc.drq());
    for (std::size_t i = 0U; i < wd1793::sector_size; ++i) {
        write_fdc_io(*sys, static_cast<std::uint8_t>(0x40U + i));
    }

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored =
        assemble_msx(bios, empty_cart, std::span<const std::uint8_t>{},
                     std::span<const std::uint8_t>{}, msx_config{.disk_enabled = true});
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(blob);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(restored->disk_enabled);

    restored->write_io(0xD1U, 3U);
    restored->write_io(0xD2U, 4U);
    restored->write_io(0xD0U, 0x80U);
    CHECK(read_fdc_io(*restored) == 0x40U);
    CHECK(read_fdc_io(*restored) == 0x41U);
}

TEST_CASE("msx MSX-MUSIC state round-trips with the machine snapshot", "[manifests][msx]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {},
                                  msx_config{.fm_music_enabled = true});
    REQUIRE(sys != nullptr);
    sys->fm.set_clock_divider(1);
    sys->write_io(0x7CU, 0x30U);
    sys->write_io(0x7DU, 0x70U);
    sys->write_io(0x7CU, 0x10U);
    sys->write_io(0x7DU, 0xA0U);
    sys->write_io(0x7CU, 0x20U);
    sys->write_io(0x7DU, 0x18U);
    run_cycles(*sys, 8000);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(blob);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(restored->fm_music_enabled);

    std::array<std::int16_t, 256> a{};
    std::array<std::int16_t, 256> b{};
    sys->fm.generate(a);
    restored->fm.generate(b);
    CHECK(a == b);
}

TEST_CASE("msx FM-PAC SRAM state round-trips with the machine snapshot", "[manifests][msx]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U),
                                  std::vector<std::uint8_t>(0x8000U, 0xFFU),
                                  msx_config{.fm_music_enabled = true, .fmpac_sram_enabled = true});
    REQUIRE(sys != nullptr);
    sys->primary_slot_select = 0x14U;
    sys->bus.write8(0x5FFEU, 0x4DU);
    sys->bus.write8(0x5FFFU, 0x69U);
    sys->bus.write8(0x4001U, 0xC3U);
    REQUIRE(sys->bus.read8(0x4001U) == 0xC3U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U),
                                       std::vector<std::uint8_t>(0x8000U, 0xFFU));
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(blob);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    restored->primary_slot_select = 0x14U;

    REQUIRE(restored->fmpac_sram_enabled);
    REQUIRE(restored->battery_ram().size() == 0x2000U);
    CHECK(restored->bus.read8(0x4001U) == 0xC3U);
    CHECK(restored->battery_ram()[1] == 0xC3U);
}

TEST_CASE("msx second cartridge mapper state round-trips", "[manifests][msx]") {
    std::vector<std::uint8_t> cart1(0x2000U * 8U, 0x00U);
    std::vector<std::uint8_t> cart2(0x2000U * 8U, 0x00U);
    for (std::size_t bank = 0; bank < 8U; ++bank) {
        cart1[bank * 0x2000U] = static_cast<std::uint8_t>(0x90U + bank);
        cart2[bank * 0x2000U] = static_cast<std::uint8_t>(0x50U + bank);
    }

    const msx_config config{.cartridge_mapper = msx_cartridge_mapper::ascii8,
                            .cartridge2_mapper = msx_cartridge_mapper::ascii8};
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), cart1, cart2,
                                  std::span<const std::uint8_t>{}, std::span<const std::uint8_t>{},
                                  std::span<const std::uint8_t>{}, config);
    REQUIRE(sys != nullptr);
    sys->primary_slot_select = 0x28U;
    sys->bus.write8(0x6000U, 7U);
    REQUIRE(sys->bus.read8(0x4000U) == 0x57U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx(
        std::vector<std::uint8_t>(0x8000U, 0x00U), cart1, cart2, std::span<const std::uint8_t>{},
        std::span<const std::uint8_t>{}, std::span<const std::uint8_t>{}, config);
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(blob);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    restored->primary_slot_select = 0x28U;

    CHECK(restored->cart2_8k_bank[0] == 7U);
    CHECK(restored->bus.read8(0x4000U) == 0x57U);
}

TEST_CASE("msx Kanji ROM stream state round-trips with the machine snapshot", "[manifests][msx]") {
    const auto kanji = make_kanji_rom();
    const auto sys =
        assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {}, std::span<const std::uint8_t>{},
                     std::span<const std::uint8_t>{}, kanji, msx_config{});
    REQUIRE(sys != nullptr);
    sys->write_io(0xD8U, 0x23U);
    sys->write_io(0xD9U, 0x04U);
    CHECK(sys->read_io(0xD9U) == 0x40U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored =
        assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {}, std::span<const std::uint8_t>{},
                     std::span<const std::uint8_t>{}, kanji, msx_config{});
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(blob);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored->read_io(0xD9U) == 0x41U);
}

TEST_CASE("msx RTC state round-trips with the machine snapshot", "[manifests][msx]") {
    const auto sys = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U), {},
                                  msx_config{.rtc_enabled = true});
    REQUIRE(sys != nullptr);
    sys->rtc.set_cycles_per_second(8U);
    sys->rtc.set_time_24h(26U, 2U, 28U, 5U, 1U, 2U, 3U);
    sys->write_io(0xB4U, 0x0DU);
    sys->write_io(0xB5U, 0x0BU); // block 3
    sys->write_io(0xB4U, 0x04U);
    sys->write_io(0xB5U, 0x09U);
    sys->rtc.tick(3U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx(std::vector<std::uint8_t>(0x8000U, 0x00U));
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(blob);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(restored->rtc_enabled);

    sys->rtc.tick(5U);
    restored->rtc.tick(5U);
    CHECK(restored->rtc.raw_block_register(3, 4) == 0x09U);
    CHECK(restored->rtc.raw_block_register(0, 0) == sys->rtc.raw_block_register(0, 0));
}
