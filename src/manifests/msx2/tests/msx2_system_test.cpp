#include "msx2_system.hpp"

#include "msx_cassette.hpp"
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
    using wd1793 = mnemos::chips::storage::wd1793;
    using mnemos::chips::video::v9938;
    using mnemos::manifests::msx2::assemble_msx2;
    using mnemos::manifests::msx2::msx2_cartridge_mapper;
    using mnemos::manifests::msx2::msx2_config;
    using mnemos::manifests::msx2::msx2_system;

    constexpr std::uint8_t k_fdc_status_record_type = 0x20U;
    constexpr std::uint8_t k_fdc_status_not_ready = 0x80U;

    void run_frame(msx2_system& sys) {
        const std::uint64_t cycles = static_cast<std::uint64_t>(v9938::cycles_per_line) *
                                     static_cast<std::uint64_t>(sys.vdp.total_scanlines());
        for (std::uint64_t i = 0; i < cycles; ++i) {
            sys.vdp.tick(1);
            sys.cpu.tick(1);
            sys.psg.tick(1);
            sys.rtc.tick(1);
            sys.cassette.tick(1);
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

    std::vector<std::uint8_t> make_16k_paged_cart(std::size_t pages) {
        std::vector<std::uint8_t> cart(pages * msx2_system::ascii16_page_size, 0x00U);
        for (std::size_t page = 0; page < pages; ++page) {
            cart[page * msx2_system::ascii16_page_size] = static_cast<std::uint8_t>(0x90U + page);
        }
        return cart;
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

    std::vector<std::uint8_t> make_dsk() {
        std::vector<std::uint8_t> disk(
            static_cast<std::size_t>(80U) * 2U * 9U * wd1793::sector_size, 0xE5U);
        disk[0] = 0x41U;
        disk[1] = 0x42U;
        disk[wd1793::sector_size] = 0x51U;
        disk[9U * wd1793::sector_size] = 0x61U;
        return disk;
    }

    void put_le16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
        REQUIRE(offset + 1U < bytes.size());
        bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
        bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    }

    std::vector<std::uint8_t> make_msx_dos_dsk(std::uint8_t sides, std::uint8_t sectors_per_track) {
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

    [[nodiscard]] std::size_t disk_sector_offset(std::uint8_t track, std::uint8_t side,
                                                 std::uint8_t sector, std::uint8_t sides,
                                                 std::uint8_t sectors_per_track) noexcept {
        return (((static_cast<std::size_t>(track) * sides) + side) * sectors_per_track +
                (sector - 1U)) *
               wd1793::sector_size;
    }

    std::vector<std::uint8_t> make_cas(std::initializer_list<std::uint8_t> bytes) {
        std::vector<std::uint8_t> cas(
            mnemos::chips::storage::msx_cassette::cas_header_magic.begin(),
            mnemos::chips::storage::msx_cassette::cas_header_magic.end());
        cas.insert(cas.end(), bytes.begin(), bytes.end());
        return cas;
    }

    std::vector<std::uint8_t> make_kanji_rom() {
        std::vector<std::uint8_t> rom(0x40000U, 0xFFU);
        const std::size_t level1_char = 0x0123U;
        const std::size_t level2_char = 0x0045U;
        for (std::size_t i = 0; i < 32U; ++i) {
            rom[(level1_char * 32U) + i] = static_cast<std::uint8_t>(0x40U + i);
            rom[0x20000U + (level2_char * 32U) + i] = static_cast<std::uint8_t>(0xA0U + i);
        }
        return rom;
    }

    void append_repeat(std::vector<std::uint8_t>& out, std::uint8_t value, std::size_t count) {
        out.insert(out.end(), count, value);
    }

    void wait_for_fdc_drq(msx2_system& sys) {
        for (std::uint64_t i = 0; i <= wd1793::nominal_mfm_byte_cycles; ++i) {
            if (sys.fdc.drq()) {
                return;
            }
            sys.fdc.tick(1U);
        }
        REQUIRE(sys.fdc.drq());
    }

    std::uint8_t read_fdc_io(msx2_system& sys) {
        wait_for_fdc_drq(sys);
        return sys.io_read(0xD3U);
    }

    void write_fdc_io(msx2_system& sys, std::uint8_t value) {
        wait_for_fdc_drq(sys);
        sys.io_write(0xD3U, value);
    }

    std::uint8_t read_fdc_memory(msx2_system& sys, std::uint16_t address) {
        wait_for_fdc_drq(sys);
        return sys.bus.read8(address);
    }

    void configure_ppi_outputs(msx2_system& sys) { sys.io_write(0xABU, 0x82U); }

    void configure_psg_gpio(msx2_system& sys) {
        sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_mixer);
        sys.io_write(0xA1U, 0xBFU); // port A input, port B output, tone/noise disabled
        sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
        sys.io_write(0xA1U, 0x0FU); // port 1 selected; trigger pins released for input
    }

    void select_primary_slots(msx2_system& sys, std::uint8_t value) {
        configure_ppi_outputs(sys);
        sys.io_write(0xA8U, value);
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
        // The canonical wd1793 write-track transfer is exactly one standard MFM
        // track; pad the trailing gap so DRQ drains and the command completes.
        if (out.size() < mnemos::chips::storage::wd1793::standard_mfm_track_size) {
            append_repeat(out, 0x4EU,
                          mnemos::chips::storage::wd1793::standard_mfm_track_size - out.size());
        }
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
    const std::array<std::uint8_t, 11> bios_prog = {
        0x3E, 0x82,       // LD A,$82  ; PPI PA/PC output, PB input
        0xD3, 0xAB,       // OUT ($AB),A
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
    for (int i = 0; i < 8; ++i) {
        (void)sys->cpu.step_instruction();
    }

    CHECK(sys->primary_slot == 0xF4U);
    CHECK(sys->bus.read8(0xC000U) == 0x42U);
}

TEST_CASE("msx2 C-BIOS-style plain 32 KiB handoff exposes the upper ROM page",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart(0x8000U, 0xFFU);
    cart[0] = 0x11U;
    cart[0x4000U] = 0x22U;

    const auto sys = assemble_msx2(bios, cart);
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0xF4U); // page 1 -> cartridge, page 2 -> RAM

    CHECK(sys->bus.read8(0x4000U) == 0x11U);
    CHECK(sys->bus.read8(0x8000U) == 0x22U);

    sys->bus.write8(0x8000U, 0x77U);
    CHECK(sys->ram[msx2_system::page_size] == 0x77U);
    CHECK(sys->bus.read8(0x8000U) == 0x22U);
}

TEST_CASE("msx2 C-BIOS-style plain 32 KiB lower handoff is gated per cartridge",
          "[manifests][msx2][mapper]") {
    std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    bios[0x4000U] = 0x99U;
    std::vector<std::uint8_t> cart(0x8000U, 0xFFU);
    cart[0] = 0x11U;
    cart[0x4000U] = 0x22U;

    const auto sys = assemble_msx2(bios, cart);
    REQUIRE(sys != nullptr);

    select_primary_slots(*sys, 0xD0U); // page 1 -> BIOS, page 2 -> cartridge
    CHECK(sys->bus.read8(0x4000U) == 0x99U);
    CHECK(sys->bus.read8(0x8000U) == 0x22U);

    sys->cartridge_lower_handoff = true;
    CHECK(sys->bus.read8(0x4000U) == 0x11U);
    CHECK(sys->bus.read8(0x8000U) == 0x22U);
}

TEST_CASE("msx2 C-BIOS-style handoff leaves non-32 KiB plain cartridges unmapped",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart(0x4000U, 0xFFU);
    cart[0] = 0x11U;

    const auto sys = assemble_msx2(bios, cart);
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0xF4U); // page 1 -> cartridge, page 2 -> RAM

    sys->bus.write8(0x8000U, 0x44U);
    CHECK(sys->bus.read8(0x4000U) == 0x11U);
    CHECK(sys->bus.read8(0x8000U) == 0x44U);
}

TEST_CASE("msx2 upper-page 16 KiB plain cartridges stop at the page-3 RAM boundary",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart(0x4000U, 0xFFU);
    cart[0] = 'A';
    cart[1] = 'B';
    cart[2] = 0x04U;
    cart[3] = 0x80U;
    cart[0x3FFFU] = 0xC5U;

    const auto sys = assemble_msx2(bios, cart);
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0xD0U); // page 2 -> cartridge, page 3 -> RAM

    CHECK(sys->bus.read8(0x8000U) == 'A');
    CHECK(sys->bus.read8(0xBFFFU) == 0xC5U);

    sys->bus.write8(0xC000U, 0x5AU);
    CHECK(sys->bus.read8(0xC000U) == 0x5AU);
    CHECK(sys->ram[0] == 0x5AU);
}

TEST_CASE("msx2 C-BIOS slot profile keeps upper-page cart and mapper RAM separated",
          "[manifests][msx2][mapper][slots]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> sub_bios(0x4000U, 0x00U);
    sub_bios[0] = 0x5AU;
    std::vector<std::uint8_t> cart(0x4000U, 0xFFU);
    cart[0] = 'A';
    cart[1] = 'B';
    cart[2] = 0x04U;
    cart[3] = 0x80U;
    cart[0x3FFFU] = 0xC5U;

    const auto sys = assemble_msx2(
        bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::plain,
                                .ram_size = 0x80000U,
                                .expanded_primary_slots = 0x08U,
                                .ram_primary_slot = 3U,
                                .ram_secondary_slot = 2U,
                                .sub_bios_primary_slot = 3U,
                                .sub_bios_secondary_slot = 0U,
                                .sub_bios = sub_bios});
    REQUIRE(sys != nullptr);

    CHECK(sys->expanded_slot[3]);
    CHECK(sys->ram.size() == 0x80000U);
    CHECK(sys->ram_segment[0] == 3U);
    CHECK(sys->ram_segment[1] == 2U);
    CHECK(sys->ram_segment[2] == 1U);
    CHECK(sys->ram_segment[3] == 0U);

    sys->primary_slot = 0xD0U;       // pages 0/1 -> BIOS, page 2 -> cart, page 3 -> slot 3
    sys->ppi_a = sys->primary_slot;  // keep PPI port-A latch coherent with the slot output
    sys->secondary_slot[3] = 0xA0U;  // page 3 -> subslot 2 mapper RAM

    CHECK(sys->cpu_read(0x8000U) == 'A');
    CHECK(sys->cpu_read(0x8001U) == 'B');
    CHECK(sys->cpu_read(0xBFFFU) == 0xC5U);

    sys->cpu_write(0xC000U, 0xA5U);
    CHECK(sys->cpu_read(0xC000U) == 0xA5U);
    CHECK(sys->ram[0] == 0xA5U);
}

TEST_CASE("msx2 RAM mapper ports select independent 16 KiB segments", "[manifests][msx2]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);
    CHECK(sys->io_read(0xFCU) == 3U);
    CHECK(sys->io_read(0xFDU) == 2U);
    CHECK(sys->io_read(0xFEU) == 1U);
    CHECK(sys->io_read(0xFFU) == 0U);
    select_primary_slots(*sys, 0xFFU); // all four pages read/write RAM

    sys->io_write(0xFFU, 3U);
    sys->bus.write8(0xC000U, 0x11U);
    sys->io_write(0xFFU, 4U);
    sys->bus.write8(0xC000U, 0x22U);

    sys->io_write(0xFFU, 3U);
    CHECK(sys->bus.read8(0xC000U) == 0x11U);
    sys->io_write(0xFFU, 4U);
    CHECK(sys->bus.read8(0xC000U) == 0x22U);

    sys->io_write(0xFFU, 11U);
    CHECK(sys->io_read(0xFFU) == 3U);
    CHECK(sys->bus.read8(0xC000U) == 0x11U);
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

TEST_CASE("msx2 configurable internal sub-ROM slot maps C-BIOS-style subslot",
          "[manifests][msx2][slots]") {
    std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    bios[0x0000U] = 0x11U;
    std::vector<std::uint8_t> sub_bios(0x4000U, 0x00U);
    sub_bios[0x0000U] = 0x5AU;

    const auto sys = assemble_msx2(
        bios, {}, msx2_config{.expanded_primary_slots = 0x08U,
                              .ram_primary_slot = 3U,
                              .ram_secondary_slot = 2U,
                              .sub_bios_primary_slot = 3U,
                              .sub_bios_secondary_slot = 0U,
                              .sub_bios = sub_bios});
    REQUIRE(sys != nullptr);

    CHECK(sys->expanded_slot[3]);
    CHECK(sys->bus.read8(0x0000U) == 0x11U);
    select_primary_slots(*sys, 0x03U); // page 0 -> primary slot 3, secondary slot 0

    CHECK(sys->bus.read8(0x0000U) == 0x5AU);
}

TEST_CASE("msx2 overlapping sub-ROM and RAM slot keeps sub-ROM page write-protected",
          "[manifests][msx2][slots]") {
    std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> sub_bios(0x4000U, 0x00U);
    sub_bios[0x0000U] = 0x5AU;

    const auto sys = assemble_msx2(
        bios, {}, msx2_config{.expanded_primary_slots = 0x08U,
                              .ram_primary_slot = 3U,
                              .ram_secondary_slot = 0U,
                              .sub_bios_primary_slot = 3U,
                              .sub_bios_secondary_slot = 0U,
                              .sub_bios = sub_bios});
    REQUIRE(sys != nullptr);

    select_primary_slots(*sys, 0xFFU); // all pages -> expanded primary slot 3, subslot 0
    CHECK(static_cast<unsigned>(sys->cpu_read(0x0000U)) == 0x5AU);

    sys->cpu_write(0x0000U, 0xA5U);
    CHECK(static_cast<unsigned>(sys->cpu_read(0x0000U)) == 0x5AU);
    CHECK(static_cast<unsigned>(sys->ram[3U * msx2_system::page_size]) == 0x00U);

    sys->cpu_write(0xC000U, 0x66U);
    CHECK(static_cast<unsigned>(sys->cpu_read(0xC000U)) == 0x66U);
    CHECK(static_cast<unsigned>(sys->ram[0]) == 0x66U);
}

TEST_CASE("msx2 optional logo ROM maps at slot 0 page 2", "[manifests][msx2][slots]") {
    std::vector<std::uint8_t> bios(0xC000U, 0x11U);
    bios[0x0000U] = 0x22U;
    bios[0x8000U] = 0x33U;
    std::vector<std::uint8_t> logo_rom(0x4000U, 0x44U);
    logo_rom[0x0000U] = 0x5AU;
    logo_rom[0x3FFFU] = 0xA5U;

    const auto sys = assemble_msx2(bios, {}, msx2_config{.logo_rom = logo_rom});
    REQUIRE(sys != nullptr);

    CHECK(sys->bus.read8(0x0000U) == 0x22U);
    CHECK(sys->bus.read8(0x8000U) == 0x5AU);
    CHECK(sys->bus.read8(0xBFFFU) == 0xA5U);

    const auto legacy = assemble_msx2(bios);
    REQUIRE(legacy != nullptr);
    CHECK(legacy->bus.read8(0x8000U) == 0x33U);
}

TEST_CASE("msx2 secondary slot register is only visible through page 3 selected primary slot",
          "[manifests][msx2][slots]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> sub_bios(0x4000U, 0x00U);
    const auto sys = assemble_msx2(bios, {}, msx2_config{.sub_bios = sub_bios});
    REQUIRE(sys != nullptr);

    select_primary_slots(*sys, 0xC0U); // page 3 -> RAM primary slot 3, not expanded slot 0
    sys->bus.write8(0xFFFFU, 0xAAU);

    CHECK(sys->secondary_slot[0] == 0x00U);
    CHECK(sys->ram[0x3FFFU] == 0xAAU);
}

TEST_CASE("msx2 configurable expanded RAM slot maps mapper RAM", "[manifests][msx2][slots]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys =
        assemble_msx2(bios, {}, msx2_config{.ram_primary_slot = 3U, .ram_secondary_slot = 2U});
    REQUIRE(sys != nullptr);

    CHECK(sys->expanded_slot[3]);
    select_primary_slots(*sys, 0xC0U); // page 3 -> primary slot 3
    sys->bus.write8(0xFFFFU, 0x80U);   // page 3 -> secondary slot 2
    sys->bus.write8(0xC000U, 0x5AU);

    CHECK(sys->ram[0] == 0x5AU);
    CHECK(sys->bus.read8(0xC000U) == 0x5AU);

    sys->bus.write8(0xFFFFU, 0x00U); // page 3 -> secondary slot 0
    CHECK(sys->bus.read8(0xC000U) == 0xFFU);
}

TEST_CASE("msx2 configurable disk slot maps disk ROM through expanded subslot",
          "[manifests][msx2][slots][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> disk_rom(0x4000U, 0xFFU);
    disk_rom[0] = 0xD6U;
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {},
                                   msx2_config{.disk_primary_slot = 3U,
                                               .disk_secondary_slot = 1U,
                                               .disk_rom = disk_rom,
                                               .disk_image = disk});
    REQUIRE(sys != nullptr);

    CHECK(sys->expanded_slot[3]);
    select_primary_slots(*sys, 0xC0U); // page 3 -> primary slot 3
    sys->bus.write8(0xFFFFU, 0x04U);   // page 1 -> secondary slot 1
    select_primary_slots(*sys, 0x0CU); // page 1 -> primary slot 3
    CHECK(sys->bus.read8(0x4000U) == 0xD6U);

    sys->bus.write8(0x7FFDU, 0x00U); // drive A
    sys->bus.write8(0x7FFCU, 0x00U); // side 0
    sys->bus.write8(0x7FF9U, 0U);    // track
    sys->bus.write8(0x7FFAU, 1U);    // sector
    sys->bus.write8(0x7FF8U, 0x80U);
    CHECK(read_fdc_memory(*sys, 0x7FFBU) == 0x41U);
}

TEST_CASE("msx2 configurable second cartridge slot maps through expanded subslot",
          "[manifests][msx2][slots][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> cart1(0x8000U, 0xFFU);
    std::vector<std::uint8_t> cart2(0x8000U, 0xFFU);
    cart2[0] = 0x62U;
    const auto sys = assemble_msx2(bios, cart1,
                                   msx2_config{.cartridge2_primary_slot = 3U,
                                               .cartridge2_secondary_slot = 2U,
                                               .cartridge2 = cart2});
    REQUIRE(sys != nullptr);

    CHECK(sys->expanded_slot[3]);
    select_primary_slots(*sys, 0xC0U); // page 3 -> primary slot 3
    sys->bus.write8(0xFFFFU, 0x08U);   // page 1 -> secondary slot 2
    select_primary_slots(*sys, 0x0CU); // page 1 -> primary slot 3
    CHECK(sys->bus.read8(0x4000U) == 0x62U);
}

TEST_CASE("msx2 ASCII8 mapper banks the cartridge window", "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    const auto sys =
        assemble_msx2(bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::ascii8});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x04U); // page 1 -> cartridge slot

    CHECK(sys->bus.read8(0x4000U) == 0x80U);
    sys->bus.write8(0x6000U, 4U);
    CHECK(sys->bus.read8(0x4000U) == 0x84U);
}

TEST_CASE("msx2 Generic8 mapper switches all four 8 KiB cartridge windows",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    const auto sys = assemble_msx2(
        bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::generic8});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x14U);

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

TEST_CASE("msx2 auto-detects ASCII8 cartridges from bank-register signatures",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    add_mapper_signatures(cart, {0x6000U, 0x6800U, 0x7000U, 0x7800U});

    const auto sys = assemble_msx2(bios, cart);
    REQUIRE(sys != nullptr);
    REQUIRE(sys->cart_mapper == msx2_cartridge_mapper::ascii8);

    select_primary_slots(*sys, 0x14U);
    CHECK(sys->bus.read8(0x6000U) == 0x81U);
    sys->bus.write8(0x6800U, 5U);
    CHECK(sys->bus.read8(0x6000U) == 0x85U);
}

TEST_CASE("msx2 auto-detects ASCII16 cartridges from two 16 KiB bank registers",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_16k_paged_cart(4U);
    add_mapper_signatures(cart, {0x6000U, 0x7000U});

    const auto sys = assemble_msx2(bios, cart);
    REQUIRE(sys != nullptr);
    REQUIRE(sys->cart_mapper == msx2_cartridge_mapper::ascii16);

    select_primary_slots(*sys, 0x14U);
    CHECK(sys->bus.read8(0x4000U) == 0x90U);
    sys->bus.write8(0x6000U, 2U);
    CHECK(sys->bus.read8(0x4000U) == 0x92U);
}

TEST_CASE("msx2 auto-detects Konami SCC cartridges from 8 KiB write windows",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    add_mapper_signatures(cart, {0x5000U, 0x7000U, 0x9000U, 0xB000U});

    const auto sys = assemble_msx2(bios, cart);
    REQUIRE(sys != nullptr);
    REQUIRE(sys->cart_mapper == msx2_cartridge_mapper::konami_scc);

    select_primary_slots(*sys, 0x14U);
    CHECK(sys->bus.read8(0x4000U) == 0x80U);
    sys->bus.write8(0x5000U, 4U);
    CHECK(sys->bus.read8(0x4000U) == 0x84U);
}

TEST_CASE("msx2 forced plain mapper overrides automatic signatures", "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    add_mapper_signatures(cart, {0x6000U, 0x6800U, 0x7000U, 0x7800U});

    const auto sys =
        assemble_msx2(bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::plain});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->cart_mapper == msx2_cartridge_mapper::plain);

    select_primary_slots(*sys, 0x14U);
    CHECK(sys->bus.read8(0x6000U) == 0x81U);
    sys->bus.write8(0x6800U, 5U);
    CHECK(sys->bus.read8(0x6000U) == 0x81U);
}

TEST_CASE("msx2 larger plain cartridges expose selected pages outside the canonical window",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart(0x10000U, 0xFFU);
    cart[0x0000U] = 0x10U;
    cart[0x4000U] = 0x40U;
    cart[0x8000U] = 0x80U;
    cart[0xC000U] = 0xC0U;

    const auto sys =
        assemble_msx2(bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::plain});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x55U); // all four pages -> cartridge slot 1

    CHECK(sys->bus.read8(0x0000U) == 0x10U);
    CHECK(sys->bus.read8(0x4000U) == 0x10U);
    CHECK(sys->bus.read8(0x8000U) == 0x40U);
    CHECK(sys->bus.read8(0xC000U) == 0xC0U);
}

TEST_CASE("msx2 16 KiB plain cartridge mirrors across both cartridge pages",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart(0x4000U, 0xFFU);
    cart[0] = 'A';
    cart[1] = 'B';
    cart[2] = 0x35U;
    cart[3] = 0xBBU;
    cart[0x3FFFU] = 0x22U;

    const auto sys =
        assemble_msx2(bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::plain});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x14U); // pages 1 and 2 -> cartridge slot 1

    CHECK(sys->bus.read8(0x4000U) == 'A');
    CHECK(sys->bus.read8(0x7FFFU) == 0x22U);
    CHECK(sys->bus.read8(0x8000U) == 'A');
    CHECK(sys->bus.read8(0xBFFFU) == 0x22U);

    select_primary_slots(*sys, 0xD0U); // page 1 -> BIOS, page 2 -> cartridge
    CHECK(sys->bus.read8(0x8000U) == 'A');
    CHECK(sys->bus.read8(0xBFFFU) == 0x22U);
}

TEST_CASE("msx2 lower-page 16 KiB plain cartridges mirror only when both cart pages are selected",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart(0x4000U, 0xFFU);
    cart[0] = 'A';
    cart[1] = 'B';
    cart[2] = 0x0FU;
    cart[3] = 0x40U;
    cart[0x0123U] = 0x66U;

    const auto sys =
        assemble_msx2(bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::plain});
    REQUIRE(sys != nullptr);

    select_primary_slots(*sys, 0xD0U); // page 1 -> BIOS, page 2 -> cartridge probe
    CHECK(sys->bus.read8(0x8123U) == 0xFFU);

    select_primary_slots(*sys, 0x14U); // pages 1 and 2 -> same cartridge slot
    CHECK(sys->bus.read8(0x4123U) == 0x66U);
    CHECK(sys->bus.read8(0x8123U) == 0x66U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored =
        assemble_msx2(bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::plain});
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(blob);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored->bus.read8(0x8123U) == 0x66U);
}

TEST_CASE("msx2 padded plain cartridge maps its payload at $4000",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart(0x10000U, 0x00U);
    cart[0x0000U] = 0x4BU;
    cart[0x0001U] = 0xFCU;
    cart[0x4000U] = 'A';
    cart[0x4001U] = 'B';
    cart[0x7FFFU] = 0x7EU;

    const auto sys =
        assemble_msx2(bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::plain});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x14U); // pages 1 and 2 -> cartridge slot 1

    CHECK(sys->bus.read8(0x4000U) == 'A');
    CHECK(sys->bus.read8(0x4001U) == 'B');
    CHECK(sys->bus.read8(0x7FFFU) == 0x7EU);
}

TEST_CASE("msx2 slot 2 cartridge maps independently from slot 1", "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart1(0x8000U, 0xFFU);
    std::vector<std::uint8_t> cart2(0x8000U, 0xFFU);
    cart1[0] = 0x11U;
    cart1[0x4000U] = 0x12U;
    cart2[0] = 0x21U;
    cart2[0x4000U] = 0x22U;

    const auto sys = assemble_msx2(bios, cart1, msx2_config{.cartridge2 = cart2});
    REQUIRE(sys != nullptr);

    select_primary_slots(*sys, 0x14U); // pages 1 and 2 -> cartridge slot 1
    CHECK(sys->bus.read8(0x4000U) == 0x11U);
    CHECK(sys->bus.read8(0x8000U) == 0x12U);

    select_primary_slots(*sys, 0x28U); // pages 1 and 2 -> cartridge slot 2
    CHECK(sys->bus.read8(0x4000U) == 0x21U);
    CHECK(sys->bus.read8(0x8000U) == 0x22U);
}

TEST_CASE("msx2 slot 2 larger plain cartridge exposes selected pages outside the canonical window",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> cart1(0x8000U, 0xFFU);
    std::vector<std::uint8_t> cart2(0x10000U, 0xFFU);
    cart2[0x0000U] = 0x21U;
    cart2[0x4000U] = 0x42U;
    cart2[0x8000U] = 0x63U;
    cart2[0xC000U] = 0x84U;

    const auto sys = assemble_msx2(
        bios, cart1,
        msx2_config{.cartridge2_mapper = msx2_cartridge_mapper::plain, .cartridge2 = cart2});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0xAAU); // all four pages -> cartridge slot 2

    CHECK(sys->bus.read8(0x0000U) == 0x21U);
    CHECK(sys->bus.read8(0x4000U) == 0x21U);
    CHECK(sys->bus.read8(0x8000U) == 0x42U);
    CHECK(sys->bus.read8(0xC000U) == 0x84U);
}

TEST_CASE("msx2 slot 2 ASCII8 mapper banks and round-trips state", "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart1 = make_8k_paged_cart(8U);
    std::vector<std::uint8_t> cart2 = make_8k_paged_cart(8U);
    for (std::size_t bank = 0; bank < 8U; ++bank) {
        cart2[bank * msx2_system::ascii8_page_size] = static_cast<std::uint8_t>(0x40U + bank);
    }

    auto sys = assemble_msx2(
        bios, cart1,
        msx2_config{.cartridge2_mapper = msx2_cartridge_mapper::ascii8, .cartridge2 = cart2});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x28U);

    CHECK(sys->bus.read8(0x4000U) == 0x40U);
    sys->bus.write8(0x6000U, 5U);
    CHECK(sys->bus.read8(0x4000U) == 0x45U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    auto restored = assemble_msx2(
        bios, cart1,
        msx2_config{.cartridge2_mapper = msx2_cartridge_mapper::ascii8, .cartridge2 = cart2});
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());
    select_primary_slots(*restored, 0x28U);

    CHECK(restored->cart2_ascii8_bank[0] == 5U);
    CHECK(restored->bus.read8(0x4000U) == 0x45U);
}

TEST_CASE("msx2 ASCII8 SRAM mapper exposes battery RAM in upper cartridge pages",
          "[manifests][msx2][mapper][sram]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    const auto sys = assemble_msx2(
        bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::ascii8_sram8});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->battery_ram().size() == 0x2000U);
    select_primary_slots(*sys, 0x14U); // pages 1 and 2 -> cartridge slot

    CHECK(sys->bus.read8(0x8000U) == 0x82U);
    sys->bus.write8(0x7000U, 0x80U); // bank register 2 selects SRAM
    CHECK(sys->bus.read8(0x8000U) == 0xFFU);
    sys->bus.write8(0x8000U, 0x5AU);
    CHECK(sys->battery_ram()[0] == 0x5AU);
    CHECK(sys->bus.read8(0x8000U) == 0x5AU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx2(
        bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::ascii8_sram8});
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());
    select_primary_slots(*restored, 0x14U);
    CHECK(restored->bus.read8(0x8000U) == 0x5AU);

    restored->bus.write8(0x7000U, 4U);
    CHECK(restored->bus.read8(0x8000U) == 0x84U);
}

TEST_CASE("msx2 ASCII16 mapper ignores ASCII8-only half-window writes",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_16k_paged_cart(4U);
    const auto sys = assemble_msx2(
        bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::ascii16});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x14U);

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

TEST_CASE("msx2 ASCII16 SRAM mapper exposes mirrored 2 KiB battery RAM",
          "[manifests][msx2][mapper][sram]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart(0x4000U * 4U, 0x00U);
    for (std::size_t bank = 0; bank < 4U; ++bank) {
        cart[bank * 0x4000U] = static_cast<std::uint8_t>(0x90U + bank);
    }

    const auto sys = assemble_msx2(
        bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::ascii16_sram2});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->battery_ram().size() == 0x0800U);
    select_primary_slots(*sys, 0x14U); // pages 1 and 2 -> cartridge slot

    CHECK(sys->bus.read8(0x8000U) == 0x91U);
    sys->bus.write8(0x7000U, 0x10U); // bank register 1 selects SRAM
    CHECK(sys->bus.read8(0x8000U) == 0xFFU);
    sys->bus.write8(0x87FFU, 0x33U);
    CHECK(sys->battery_ram()[0x07FFU] == 0x33U);
    CHECK(sys->bus.read8(0x87FFU) == 0x33U);
    CHECK(sys->bus.read8(0x8FFFU) == 0x33U);
}

TEST_CASE("msx2 slot 2 ASCII8 SRAM mapper exposes battery RAM", "[manifests][msx2][mapper][sram]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> cart1(0x8000U, 0xFFU);
    std::vector<std::uint8_t> cart2 = make_8k_paged_cart(8U);
    const auto sys = assemble_msx2(
        bios, cart1,
        msx2_config{.cartridge2_mapper = msx2_cartridge_mapper::ascii8_sram8, .cartridge2 = cart2});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->battery_ram().size() == 0x2000U);
    select_primary_slots(*sys, 0x28U);

    CHECK(sys->bus.read8(0x8000U) == 0x82U);
    sys->bus.write8(0x7000U, 0x80U);
    CHECK(sys->bus.read8(0x8000U) == 0xFFU);
    sys->bus.write8(0x8000U, 0xA9U);
    CHECK(sys->battery_ram()[0] == 0xA9U);
    CHECK(sys->bus.read8(0x8000U) == 0xA9U);
}

TEST_CASE("msx2 cartridge mappers preserve bank 255 on full 256-bank ROMs",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);

    std::vector<std::uint8_t> cart8(0x2000U * 256U, 0x00U);
    cart8[0] = 0x10U;
    cart8[0x2000U * 255U] = 0xEFU;
    const auto ascii8 =
        assemble_msx2(bios, cart8, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::ascii8});
    REQUIRE(ascii8 != nullptr);
    select_primary_slots(*ascii8, 0x14U);
    CHECK(ascii8->bus.read8(0x4000U) == 0x10U);
    ascii8->bus.write8(0x6000U, 255U);
    CHECK(ascii8->bus.read8(0x4000U) == 0xEFU);

    std::vector<std::uint8_t> cart16(0x4000U * 256U, 0x00U);
    cart16[0] = 0x20U;
    cart16[0x4000U * 255U] = 0xDFU;
    const auto ascii16 = assemble_msx2(
        bios, cart16, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::ascii16});
    REQUIRE(ascii16 != nullptr);
    select_primary_slots(*ascii16, 0x14U);
    CHECK(ascii16->bus.read8(0x4000U) == 0x20U);
    ascii16->bus.write8(0x6000U, 255U);
    CHECK(ascii16->bus.read8(0x4000U) == 0xDFU);

    std::vector<std::uint8_t> scc_cart(0x2000U * 256U, 0x00U);
    scc_cart[0] = 0x30U;
    scc_cart[0x2000U * 255U] = 0xCFU;
    const auto scc = assemble_msx2(
        bios, scc_cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::konami_scc});
    REQUIRE(scc != nullptr);
    select_primary_slots(*scc, 0x14U);
    CHECK(scc->bus.read8(0x4000U) == 0x30U);
    scc->bus.write8(0x5000U, 255U);
    CHECK(scc->konami_bank[0] == 255U);
    CHECK(scc->bus.read8(0x4000U) == 0xCFU);
}

TEST_CASE("msx2 Korean MSX mapper routes fixed and banked pages",
          "[manifests][msx2][mapper][korean]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart(0x2000U * 8U, 0x00U);
    for (std::size_t bank = 0; bank < 8U; ++bank) {
        std::fill_n(cart.begin() + static_cast<std::ptrdiff_t>(bank * 0x2000U), 0x2000U,
                    static_cast<std::uint8_t>(bank));
    }

    auto sys = assemble_msx2(bios, cart,
                             msx2_config{.cartridge_mapper = msx2_cartridge_mapper::korean_msx});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x15U); // pages 0/1/2 -> cartridge slot

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

    auto restored = assemble_msx2(
        bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::korean_msx});
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());
    select_primary_slots(*restored, 0x15U);

    CHECK(restored->korean_mapper.page(2) == 5U);
    CHECK(restored->bus.read8(0x4000U) == 5U);
}

TEST_CASE("msx2 Korean MSX Nemesis mapper boots from the last 8 KiB page",
          "[manifests][msx2][mapper][korean]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart(0x2000U * 8U, 0x00U);
    for (std::size_t bank = 0; bank < 8U; ++bank) {
        std::fill_n(cart.begin() + static_cast<std::ptrdiff_t>(bank * 0x2000U), 0x2000U,
                    static_cast<std::uint8_t>(bank));
    }

    const auto sys = assemble_msx2(
        bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::korean_msx_nemesis});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x01U); // page 0 -> cartridge slot

    CHECK(sys->bus.read8(0x0000U) == 7U);
    CHECK(sys->bus.read8(0x1FFFU) == 7U);
    CHECK(sys->bus.read8(0x2000U) == 1U);
}

TEST_CASE("msx2 Konami mapper banks the 8 KiB game windows", "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    const auto sys =
        assemble_msx2(bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::konami});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x14U); // pages 1 and 2 -> cartridge slot

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

TEST_CASE("msx2 Konami mapper mirrors bank windows into pages 0 and 3",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    const auto sys =
        assemble_msx2(bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::konami});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x55U); // all four 16 KiB pages -> cartridge slot 1

    CHECK(sys->bus.read8(0xC000U) == 0x80U);
    CHECK(sys->bus.read8(0xE000U) == 0x81U);

    sys->bus.write8(0xE000U, 4U); // mirror of $6000
    CHECK(sys->bus.read8(0xE000U) == 0x84U);
    CHECK(sys->konami_bank[1] == 4U);

    sys->bus.write8(0x0000U, 5U); // mirror of $8000
    CHECK(sys->bus.read8(0x0000U) == 0x85U);
    CHECK(sys->konami_bank[2] == 5U);

    sys->bus.write8(0x2000U, 6U); // mirror of $A000
    CHECK(sys->bus.read8(0x2000U) == 0x86U);
    CHECK(sys->konami_bank[3] == 6U);
}

TEST_CASE("msx2 Konami SCC mapper exposes banks and SCC register aperture",
          "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(64U);
    const auto sys = assemble_msx2(
        bios, cart, msx2_config{.cartridge_mapper = msx2_cartridge_mapper::konami_scc});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x14U); // pages 1 and 2 -> cartridge slot

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

TEST_CASE("msx2 Konami SCC mapper mirrors bank and SCC apertures in slot 2",
          "[manifests][msx2][mapper][scc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart1(0x8000U, 0xFFU);
    std::vector<std::uint8_t> cart2 = make_8k_paged_cart(64U);
    const auto sys = assemble_msx2(
        bios, cart1,
        msx2_config{.cartridge2_mapper = msx2_cartridge_mapper::konami_scc, .cartridge2 = cart2});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0xAAU); // all four 16 KiB pages -> cartridge slot 2

    sys->bus.write8(0x1000U, 0x3FU); // mirror of $9000; enables the SCC window
    CHECK(sys->cart2_konami_bank[2] == 0x3FU);
    CHECK(sys->bus.read8(0x0000U) == 0xBFU);

    sys->bus.write8(0x1800U, 0x5AU); // mirror of $9800
    CHECK(sys->scc.wave_sample(0, 0) == 0x5AU);
    CHECK(sys->bus.read8(0x1800U) == 0x5AU);
}

TEST_CASE("msx2 mapper state round-trips Konami and SCC registers", "[manifests][msx2][mapper]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart = make_8k_paged_cart(64U);
    auto sys = assemble_msx2(bios, cart,
                             msx2_config{.cartridge_mapper = msx2_cartridge_mapper::konami_scc});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x14U);
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
    CHECK(sys->io_read(0xA9U) == 0xFFU);
    configure_ppi_outputs(*sys);
    CHECK(sys->io_read(0xA9U) == 0xFDU);
}

TEST_CASE("msx2 VDP frame IRQ wakes the Z80 from HALT", "[manifests][msx2][irq]") {
    std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::array<std::uint8_t, 4> program = {
        0xEDU, 0x56U, // IM 1
        0xFBU,       // EI
        0x76U,       // HALT
    };
    std::copy(program.begin(), program.end(), bios.begin());

    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    sys->io_write(0x99U, 0x60U);
    sys->io_write(0x99U, 0x81U); // VDP R#1 = display + frame IRQ enable

    (void)sys->cpu.step_instruction(); // IM 1
    (void)sys->cpu.step_instruction(); // EI
    (void)sys->cpu.step_instruction(); // HALT with IFF1 enabled after EI delay
    auto regs = sys->cpu.cpu_registers();
    REQUIRE(regs.halted);
    REQUIRE(regs.iff1);

    sys->vdp.tick(static_cast<std::uint64_t>(v9938::cycles_per_line) *
                  static_cast<std::uint64_t>(v9938::display_height_192 + 1));
    REQUIRE(sys->vdp.irq_asserted());

    (void)sys->cpu.step_instruction();
    regs = sys->cpu.cpu_registers();
    CHECK_FALSE(regs.halted);
    CHECK(regs.pc == 0x0038U);
    CHECK_FALSE(regs.iff1);
}

TEST_CASE("msx2 primary slot register follows PPI port A direction",
          "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);
    REQUIRE(sys->ppi_control == 0x9BU);

    CHECK(sys->io_read(0xA8U) == 0x00U);

    sys->io_write(0xA8U, 0xC0U);
    CHECK(sys->ppi_a == 0xC0U);
    CHECK(sys->io_read(0xA8U) == 0x00U);

    sys->io_write(0xABU, 0x82U);
    CHECK(sys->io_read(0xA8U) == 0xC0U);
    sys->bus.write8(0xC000U, 0xA5U);
    CHECK(sys->ram[0x0000U] == 0xA5U);
}

TEST_CASE("msx2 PSG data port is read from A2 and written through A1", "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x40U);
    CHECK(sys->psg.read_reg(mnemos::chips::audio::ssg::reg_port_b) == 0x40U);

    sys->io_write(0xA2U, 0x00U);
    CHECK(sys->psg.read_reg(mnemos::chips::audio::ssg::reg_port_b) == 0x40U);
    CHECK(sys->io_read(0xA1U) == 0xFFU);
    CHECK(sys->io_read(0xA2U) == 0x40U);
}

TEST_CASE("msx2 routes joystick input through PSG GPIO ports", "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);
    configure_psg_gpio(*sys);

    sys->set_joystick(0, true, false, true, false, true, false);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x0FU); // select joystick port 1
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x2AU);
    CHECK((sys->io_read(0xA2U) & 0xC0U) == 0xC0U);

    sys->set_joystick(1, false, true, false, true, false, true);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x4FU); // select joystick port 2 for register 14 reads
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x15U);
}

TEST_CASE("msx2 PSG register 15 gates joystick trigger input pins", "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);
    configure_psg_gpio(*sys);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x3FU);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x00U);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x0FU);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x4CU);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x3FU);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x40U);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x0FU);
}

TEST_CASE("msx2 PSG port B direction releases joystick trigger output pins",
          "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);
    configure_psg_gpio(*sys);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x00U);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x0FU);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_mixer);
    sys->io_write(0xA1U, 0x3FU); // port A input, port B input: register 15 pins released
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x3FU);
}

TEST_CASE("msx2 PSG port A honors register 7 input/output direction", "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    sys->set_joystick(0, true, false, false, false, false, false);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    sys->io_write(0xA1U, 0x5AU);
    CHECK(sys->io_read(0xA2U) == 0x5AU);

    configure_psg_gpio(*sys);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x3EU);
}

TEST_CASE("msx2 mouse protocol clocks port-1 deltas through PSG port A",
          "[manifests][msx2][io][mouse]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);
    configure_psg_gpio(*sys);
    sys->set_mouse(0, 0x12, -0x34, true, false);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x21U);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x1FU);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x22U);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_mixer);
    sys->io_write(0xA1U, 0x3FU); // port B input: latch writes no longer clock mouse pin 8
    const std::uint8_t phase_after_release = sys->mouse_ports[0].phase();
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x0FU);
    sys->io_write(0xA1U, 0x1FU);
    CHECK(sys->mouse_ports[0].phase() == phase_after_release);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_mixer);
    sys->io_write(0xA1U, 0xBFU);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x0FU);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x2CU);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x1FU);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x2CU);

    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x0FU);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x20U);

    sys->set_joystick(0, true, false, false, false, false, false);
    CHECK((sys->io_read(0xA2U) & 0x3FU) == 0x3EU);
}

TEST_CASE("msx2 mouse protocol phase round-trips through the system save state",
          "[manifests][msx2][io][mouse]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);
    configure_psg_gpio(*sys);
    sys->set_mouse(0, 0x45, 0x67, false, true);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys->io_write(0xA1U, 0x1FU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx2(bios);
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    restored->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((restored->io_read(0xA2U) & 0x3FU) == 0x15U);
}

TEST_CASE("msx2 routes cassette input through PSG port A", "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    configure_psg_gpio(*sys);
    sys->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    sys->set_cassette_input(false);
    CHECK_FALSE(sys->cassette.input_high());
    CHECK((sys->io_read(0xA2U) & 0x80U) == 0x00U);
    CHECK((sys->io_read(0xA2U) & 0x40U) == 0x40U);

    sys->set_cassette_input(true);
    CHECK(sys->cassette.input_high());
    CHECK((sys->io_read(0xA2U) & 0x80U) == 0x80U);
}

TEST_CASE("msx2 PPI port C controls cassette motor and output lines", "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    sys->io_write(0xAAU, 0x30U);
    CHECK_FALSE(sys->cassette_motor_on());
    CHECK(sys->cassette_output_high());
    configure_ppi_outputs(*sys);
    CHECK_FALSE(sys->cassette_motor_on());
    CHECK(sys->cassette_output_high());

    sys->io_write(0xABU, 0x08U); // reset PPI C bit 4: motor on
    sys->io_write(0xABU, 0x0AU); // reset PPI C bit 5: output low
    CHECK(sys->cassette_motor_on());
    CHECK_FALSE(sys->cassette_output_high());
    CHECK(sys->cassette.motor_on());
    CHECK_FALSE(sys->cassette.output_high());
}

TEST_CASE("msx2 PPI mode control survives BSR writes and save state", "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    CHECK(sys->ppi_control == 0x9BU);
    sys->io_write(0xABU, 0x82U); // mode set: PA/PC output, PB input
    CHECK(sys->ppi_control == 0x82U);
    sys->io_write(0xABU, 0x08U); // BSR reset PC4: cassette motor on
    CHECK(sys->ppi_control == 0x82U);
    CHECK((sys->ppi_c & 0x10U) == 0x00U);
    CHECK(sys->cassette_motor_on());

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx2(bios);
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored->ppi_control == 0x82U);
    CHECK((restored->ppi_c & 0x10U) == 0x00U);
    CHECK(restored->cassette_motor_on());
}

TEST_CASE("msx2 PPI port B output latch is selected by mode control", "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    sys->set_key(2, 1, true);
    sys->io_write(0xAAU, 0x02U);
    configure_ppi_outputs(*sys);
    CHECK(sys->io_read(0xA9U) == 0xFDU);

    sys->io_write(0xA9U, 0x5AU);
    CHECK(sys->io_read(0xA9U) == 0xFDU);

    sys->io_write(0xABU, 0x80U); // mode set: all ports output
    CHECK(sys->io_read(0xA9U) == 0x5AU);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx2(bios);
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored->io_read(0xA9U) == 0x5AU);
    restored->io_write(0xABU, 0x82U); // mode set: PA/PC output, PB input
    CHECK(restored->io_read(0xA9U) == 0xFDU);
}

TEST_CASE("msx2 optional CAS image advances through the shared cassette chip",
          "[manifests][msx2][cassette]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto cas = make_cas({0x42U});
    const auto sys = assemble_msx2(bios, {}, msx2_config{.cassette_image = cas});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->cassette.loaded());
    CHECK(sys->cassette.playing());

    configure_ppi_outputs(*sys);
    sys->io_write(0xAAU, 0x0FU); // motor on
    CHECK(sys->cassette.motor_on());
    CHECK(sys->cassette.position_half_cycle() == 0U);
    sys->cassette.tick(1U);
    CHECK(sys->cassette.position_half_cycle() > 0U);
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
    REQUIRE(sys->disk_enabled());
    REQUIRE(sys->fdc.mounted());

    sys->io_write(0xD4U, 0x01U); // drive A selected, motor latched on
    sys->io_write(0xD1U, 0U);    // track 0
    sys->io_write(0xD2U, 1U);    // sector 1
    sys->io_write(0xD0U, 0x80U); // read sector

    CHECK((sys->io_read(0xD4U) & 0x40U) != 0U);
    CHECK(read_fdc_io(*sys) == 0x41U);
    CHECK(read_fdc_io(*sys) == 0x42U);
}

TEST_CASE("msx2 D4 FDC control selects drive readiness deterministically",
          "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.mounted());

    sys->io_write(0xD4U, 0x02U); // drive B selected; only drive A has mounted media
    sys->io_write(0xD1U, 0U);
    sys->io_write(0xD2U, 1U);
    sys->io_write(0xD0U, 0x80U);
    CHECK_FALSE(sys->fdc.drq());
    CHECK((sys->io_read(0xD0U) & k_fdc_status_not_ready) != 0U);

    sys->io_write(0xD4U, 0x03U); // both drive bits asserted; drive A has priority
    CHECK(sys->fdc.selected_drive() == 0U);
    sys->io_write(0xD4U, 0x04U); // alternate side-select bit used by some disk BIOSes
    CHECK(sys->fdc.selected_drive() == 0U);
    CHECK(sys->fdc.selected_side() == 1U);

    sys->io_write(0xD4U, 0x01U); // drive A selected
    sys->io_write(0xD0U, 0x80U);
    REQUIRE(sys->fdc.drq());
    CHECK(read_fdc_io(*sys) == 0x41U);
}

TEST_CASE("msx2 diskless profile leaves WD1793 ports unmapped", "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);
    REQUIRE_FALSE(sys->disk_enabled());

    CHECK(sys->io_read(0xD0U) == 0xFFU);
    CHECK(sys->io_read(0xD4U) == 0xFFU);
    sys->io_write(0xD1U, 7U);
    CHECK(sys->io_read(0xD1U) == 0xFFU);
}

TEST_CASE("msx2 routes WD1793 multiple sector reads through D0-D4 I/O ports",
          "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.mounted());

    sys->io_write(0xD4U, 0x01U); // drive A selected
    sys->io_write(0xD1U, 0U);    // track 0
    sys->io_write(0xD2U, 1U);    // sector 1
    sys->io_write(0xD0U, 0x90U); // read multiple sectors

    CHECK(read_fdc_io(*sys) == 0x41U);
    CHECK(read_fdc_io(*sys) == 0x42U);
    for (std::uint16_t i = 2U; i < mnemos::chips::storage::wd1793::sector_size; ++i) {
        (void)read_fdc_io(*sys);
    }

    CHECK(sys->io_read(0xD2U) == 2U);
    CHECK(read_fdc_io(*sys) == 0x51U);
    sys->io_write(0xD0U, 0xD0U); // BIOS may stop a multi-sector transfer early
    CHECK((sys->fdc.composed_status() & 0x11U) == 0x00U);
    CHECK_FALSE(sys->fdc.drq());
}

TEST_CASE("msx2 WD1793 I/O ports route force-interrupt commands",
          "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.mounted());

    sys->io_write(0xD4U, 0x01U); // drive A selected
    sys->io_write(0xD2U, 1U);
    sys->io_write(0xD0U, 0x80U);
    REQUIRE(sys->fdc.busy());
    REQUIRE(sys->fdc.drq());

    sys->io_write(0xD0U, 0xD0U);
    CHECK_FALSE(sys->fdc.busy());
    CHECK_FALSE(sys->fdc.drq());
    CHECK_FALSE(sys->fdc.intrq());
    CHECK((sys->io_read(0xD4U) & 0x80U) != 0U);

    sys->io_write(0xD0U, 0x80U);
    REQUIRE(sys->fdc.busy());
    sys->io_write(0xD0U, 0xD8U);
    CHECK_FALSE(sys->fdc.busy());
    CHECK_FALSE(sys->fdc.drq());
    CHECK(sys->fdc.intrq());
    CHECK((sys->io_read(0xD4U) & 0x80U) != 0U);

    (void)sys->io_read(0xD0U);
    CHECK_FALSE(sys->fdc.intrq());
}

TEST_CASE("msx2 routes WD1793 read-address ID fields through D0-D4 I/O ports",
          "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.mounted());

    sys->io_write(0xD4U, 0x05U); // drive A, side 1
    sys->io_write(0xD1U, 6U);
    sys->io_write(0xD2U, 7U);
    sys->io_write(0xD0U, 0xC0U);

    CHECK(read_fdc_io(*sys) == 6U);
    CHECK(read_fdc_io(*sys) == 1U);
    CHECK(read_fdc_io(*sys) == 7U);
    CHECK(read_fdc_io(*sys) == 0x02U);
    CHECK(read_fdc_io(*sys) == 0x70U);
    CHECK(read_fdc_io(*sys) == 0x60U);
    CHECK(sys->io_read(0xD2U) == 6U);
    CHECK((sys->io_read(0xD4U) & 0x80U) != 0U);
}

TEST_CASE("msx2 preserves WD1793 write-protect status through D0-D4 I/O ports",
          "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys =
        assemble_msx2(bios, {}, msx2_config{.disk_image = disk, .disk_write_protected = true});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.mounted());
    REQUIRE(sys->fdc.write_protected());

    sys->io_write(0xD4U, 0x01U); // drive A selected
    sys->io_write(0xD1U, 0U);    // track 0
    sys->io_write(0xD2U, 2U);    // sector 2
    sys->io_write(0xD0U, 0xA0U); // write sector

    CHECK_FALSE(sys->fdc.drq());
    CHECK((sys->io_read(0xD0U) & 0x40U) != 0U);
    CHECK(sys->fdc.disk_image()[mnemos::chips::storage::wd1793::sector_size] == 0x51U);
}

TEST_CASE("msx2 WD1793 I/O ports report deleted-data sector status", "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.mounted());

    sys->io_write(0xD4U, 0x01U); // drive A, side 0
    sys->io_write(0xD1U, 0U);
    sys->io_write(0xD2U, 2U);
    sys->io_write(0xD0U, 0xA1U); // write sector with deleted data mark
    REQUIRE((sys->io_read(0xD4U) & 0x40U) != 0U);
    for (std::size_t i = 0U; i < mnemos::chips::storage::wd1793::sector_size; ++i) {
        write_fdc_io(*sys, static_cast<std::uint8_t>(0x70U + i));
    }

    sys->io_write(0xD2U, 2U);
    sys->io_write(0xD0U, 0x80U);
    CHECK(read_fdc_io(*sys) == 0x70U);
    for (std::size_t i = 1U; i < mnemos::chips::storage::wd1793::sector_size; ++i) {
        (void)read_fdc_io(*sys);
    }
    CHECK((sys->io_read(0xD0U) & k_fdc_status_record_type) != 0U);
}

TEST_CASE("msx2 preserves WD1793 side compare through D0-D4 I/O ports",
          "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.mounted());

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
    CHECK(read_fdc_io(*sys) == 0x61U);
}

TEST_CASE("msx2 formats WD1793 DSK sectors through D0-D4 I/O ports", "[manifests][msx2][io][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->fdc.mounted());

    std::vector<std::uint8_t> stream = make_mfm_format_track(4U, 1U, 9U);
    const std::size_t first_id = find_mfm_id_mark(stream, 4U, 1U, 1U);
    REQUIRE(first_id < stream.size());
    const std::size_t custom_gap = first_id - 20U;
    stream[custom_gap] = 0x36U;
    sys->io_write(0xD4U, 0x05U); // drive A, side 1
    sys->io_write(0xD1U, 4U);
    sys->io_write(0xD0U, 0xF0U);

    REQUIRE((sys->io_read(0xD4U) & 0x40U) != 0U);
    for (const std::uint8_t value : stream) {
        write_fdc_io(*sys, value);
    }

    CHECK((sys->io_read(0xD4U) & 0x80U) != 0U);
    CHECK_FALSE(sys->fdc.drq());

    sys->io_write(0xD0U, 0xE0U);
    REQUIRE((sys->io_read(0xD4U) & 0x40U) != 0U);
    std::vector<std::uint8_t> raw_track;
    raw_track.reserve(wd1793::standard_mfm_track_size);
    for (std::size_t i = 0U; i < wd1793::standard_mfm_track_size; ++i) {
        raw_track.push_back(read_fdc_io(*sys));
    }
    CHECK(std::find(raw_track.begin(), raw_track.end(), static_cast<std::uint8_t>(0x36U)) !=
          raw_track.end());

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
    REQUIRE(sys->fdc.mounted());

    sys->io_write(0xD4U, 0x05U); // drive A, side 1
    sys->io_write(0xD1U, 0U);
    sys->io_write(0xD0U, 0xE0U);

    REQUIRE((sys->io_read(0xD4U) & 0x40U) != 0U);
    std::vector<std::uint8_t> stream;
    stream.reserve(wd1793::standard_mfm_track_size);
    for (std::size_t i = 0U; i < wd1793::standard_mfm_track_size; ++i) {
        stream.push_back(read_fdc_io(*sys));
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

    select_primary_slots(*sys, 0x28U); // pages 1 and 2 -> disk interface slot 2
    CHECK(sys->bus.read8(0x4000U) == 0xD5U);

    sys->bus.write8(0xBFFDU, 0x00U); // drive A
    sys->bus.write8(0xBFFCU, 0x00U); // side 0
    sys->bus.write8(0xBFF9U, 0U);    // track
    sys->bus.write8(0xBFFAU, 1U);    // sector
    sys->bus.write8(0xBFF8U, 0x80U);

    CHECK((sys->bus.read8(0xBFFFU) & 0x80U) != 0U);
    CHECK(read_fdc_memory(*sys, 0xBFFBU) == 0x41U);
}

TEST_CASE("msx2 disk interface exposes memory-mapped FDC without disk ROM",
          "[manifests][msx2][slots][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::vector<std::uint8_t> disk = make_dsk();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->disk_enabled());

    select_primary_slots(*sys, 0x28U); // pages 1 and 2 -> disk interface slot 2
    CHECK(sys->bus.read8(0x4000U) == 0xFFU);
    sys->bus.write8(0xBFFDU, 0x00U); // drive A
    sys->bus.write8(0xBFFCU, 0x00U); // side 0
    sys->bus.write8(0xBFF9U, 0U);    // track
    sys->bus.write8(0xBFFAU, 1U);    // sector
    sys->bus.write8(0xBFF8U, 0x80U);

    CHECK((sys->bus.read8(0xBFFFU) & 0x80U) != 0U);
    CHECK(read_fdc_memory(*sys, 0xBFFBU) == 0x41U);
}

TEST_CASE("msx2 FDC I/O ports honor MSX-DOS 8-sector BPB geometry", "[manifests][msx2][fdc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    auto disk = make_msx_dos_dsk(2U, 8U);
    disk[disk_sector_offset(2U, 1U, 8U, 2U, 8U)] = 0x82U;
    const auto sys = assemble_msx2(bios, {}, msx2_config{.disk_image = disk});
    REQUIRE(sys != nullptr);
    REQUIRE(sys->disk_enabled());

    sys->io_write(0xD4U, 0x10U); // side 1, drive A
    sys->io_write(0xD1U, 2U);
    sys->io_write(0xD2U, 8U);
    sys->io_write(0xD0U, 0x80U);
    REQUIRE((sys->io_read(0xD4U) & 0x40U) != 0U);
    CHECK(read_fdc_io(*sys) == 0x82U);
}

TEST_CASE("msx2 RTC state round-trips through the system save state", "[manifests][msx2][rtc]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);

    const std::uint32_t first_half =
        mnemos::chips::peripheral::rp5c01::default_cycles_per_second / 2U;
    const std::uint32_t second_half =
        mnemos::chips::peripheral::rp5c01::default_cycles_per_second - first_half;
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

    CHECK(restored->rtc.block() == 3U);
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
    CHECK(read_fdc_io(*sys) == 0x41U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    auto restored = assemble_msx2(bios);
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored->disk_enabled());
    CHECK(restored->fdc.mounted());
    CHECK(read_fdc_io(*restored) == 0x42U);
}

TEST_CASE("msx2 joystick state round-trips through the system save state",
          "[manifests][msx2][io]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);
    sys->set_joystick(0, true, false, false, true, true, false);
    configure_psg_gpio(*sys);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    auto restored = assemble_msx2(bios);
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    restored->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    restored->io_write(0xA1U, 0x0FU);
    restored->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((restored->io_read(0xA2U) & 0x3FU) == 0x26U);
}

TEST_CASE("msx2 cassette chip state round-trips through the system save state",
          "[manifests][msx2][io][cassette]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const std::array<std::uint32_t, 3> pulses{3U, 4U, 5U};
    auto sys = assemble_msx2(bios);
    REQUIRE(sys != nullptr);
    configure_psg_gpio(*sys);
    sys->cassette.load_half_cycles(pulses);
    sys->cassette.set_play(true);
    configure_ppi_outputs(*sys);
    sys->io_write(0xAAU, 0x30U);
    sys->io_write(0xABU, 0x08U); // motor on
    sys->io_write(0xABU, 0x0AU); // output low
    sys->cassette.tick(4U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    auto restored = assemble_msx2(bios);
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    restored->io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((restored->io_read(0xA2U) & 0x80U) == (sys->cassette.input_high() ? 0x80U : 0x00U));
    CHECK(restored->cassette.half_cycle_count() == pulses.size());
    CHECK(restored->cassette.position_half_cycle() == sys->cassette.position_half_cycle());
    CHECK(restored->cassette.countdown() == sys->cassette.countdown());
    CHECK(restored->cassette.playing());
    CHECK(restored->cassette_motor_on());
    CHECK_FALSE(restored->cassette_output_high());
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

TEST_CASE("msx2 optional FM-PAC SRAM unlocks at $5FFE/$5FFF", "[manifests][msx2]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    std::vector<std::uint8_t> cart(0x8000U, 0xFFU);
    cart[0x0000U] = 0x11U;
    cart[0x1FFEU] = 0x22U;
    cart[0x1FFFU] = 0x33U;

    const auto sys = assemble_msx2(bios, cart, msx2_config{.msx_music = true, .fmpac_sram = true});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x14U); // pages 1 and 2 -> cartridge slot
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

TEST_CASE("msx2 FM-PAC SRAM state round-trips with the machine snapshot", "[manifests][msx2]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    auto sys = assemble_msx2(bios, std::vector<std::uint8_t>(0x8000U, 0xFFU),
                             msx2_config{.msx_music = true, .fmpac_sram = true});
    REQUIRE(sys != nullptr);
    select_primary_slots(*sys, 0x14U);
    sys->bus.write8(0x5FFEU, 0x4DU);
    sys->bus.write8(0x5FFFU, 0x69U);
    sys->bus.write8(0x4001U, 0xC3U);
    REQUIRE(sys->bus.read8(0x4001U) == 0xC3U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    const auto restored = assemble_msx2(bios, std::vector<std::uint8_t>(0x8000U, 0xFFU));
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());
    select_primary_slots(*restored, 0x14U);

    REQUIRE(restored->fmpac_sram_enabled());
    REQUIRE(restored->battery_ram().size() == 0x2000U);
    CHECK(restored->bus.read8(0x4001U) == 0xC3U);
    CHECK(restored->battery_ram()[1] == 0xC3U);
}

TEST_CASE("msx2 optional Kanji ROM streams JIS level data through ports",
          "[manifests][msx2][kanji]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto plain = assemble_msx2(bios);
    REQUIRE(plain != nullptr);
    plain->io_write(0xD8U, 0x23U);
    plain->io_write(0xD9U, 0x04U);
    CHECK(plain->io_read(0xD9U) == 0xFFU);

    const auto kanji = make_kanji_rom();
    const auto sys = assemble_msx2(bios, {}, msx2_config{.kanji_rom = kanji});
    REQUIRE(sys != nullptr);

    sys->io_write(0xD8U, 0x23U);
    sys->io_write(0xD9U, 0x04U);
    CHECK(sys->io_read(0xD9U) == 0x40U);
    CHECK(sys->io_read(0xD9U) == 0x41U);
    for (int i = 0; i < 30; ++i) {
        (void)sys->io_read(0xD9U);
    }
    CHECK(sys->io_read(0xD9U) == 0x40U);

    sys->io_write(0xDAU, 0x05U);
    sys->io_write(0xDBU, 0x01U);
    CHECK(sys->io_read(0xDBU) == 0xA0U);
    CHECK(sys->io_read(0xDBU) == 0xA1U);
}

TEST_CASE("msx2 Kanji ROM stream state round-trips", "[manifests][msx2][kanji][state]") {
    const std::vector<std::uint8_t> bios(0x8000U, 0x00U);
    const auto kanji = make_kanji_rom();
    auto sys = assemble_msx2(bios, {}, msx2_config{.kanji_rom = kanji});
    REQUIRE(sys != nullptr);

    sys->io_write(0xD8U, 0x23U);
    sys->io_write(0xD9U, 0x04U);
    CHECK(sys->io_read(0xD9U) == 0x40U);

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    sys->save_state(writer);

    auto restored = assemble_msx2(bios, {}, msx2_config{.kanji_rom = kanji});
    REQUIRE(restored != nullptr);
    mnemos::chips::state_reader reader(std::span<const std::uint8_t>{blob});
    restored->load_state(reader);
    REQUIRE(reader.ok());

    CHECK(restored->io_read(0xD9U) == 0x41U);
}
