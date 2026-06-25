#pragma once

#include "bus.hpp"
#include "korean_msx_mapper.hpp"
#include "msx_cassette.hpp"
#include "msx_io_ports.hpp"
#include "msx_kanji_rom.hpp"
#include "region.hpp"
#include "rp5c01.hpp"
#include "scc.hpp"
#include "ssg.hpp"
#include "state.hpp"
#include "v9938.hpp"
#include "wd1793.hpp"
#include "ym2413.hpp"
#include "z80.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mnemos::manifests::msx2 {

    enum class msx2_cartridge_mapper : std::uint8_t {
        automatic,
        plain,
        ascii8,
        ascii8_sram8,
        ascii16,
        ascii16_sram2,
        generic8,
        konami,
        konami_scc,
        korean_msx,
        korean_msx_nemesis,
    };

    struct msx2_config final {
        mnemos::video_region video_region{mnemos::video_region::ntsc};
        msx2_cartridge_mapper cartridge_mapper{msx2_cartridge_mapper::automatic};
        msx2_cartridge_mapper cartridge2_mapper{msx2_cartridge_mapper::automatic};
        std::size_t ram_size{0x20000U}; // 128 KiB RAM mapper, the common MSX2 baseline
        std::uint8_t expanded_primary_slots{};
        std::uint8_t ram_primary_slot{3U};
        std::uint8_t ram_secondary_slot{};
        std::uint8_t sub_bios_primary_slot{};
        std::uint8_t sub_bios_secondary_slot{1U};
        std::uint8_t disk_primary_slot{2U};
        std::uint8_t disk_secondary_slot{};
        std::uint8_t cartridge2_primary_slot{2U};
        std::uint8_t cartridge2_secondary_slot{};
        bool disk_enabled{};
        bool msx_music{};
        bool fmpac_sram{};
        std::span<const std::uint8_t> cartridge2{};
        std::span<const std::uint8_t> sub_bios{};
        std::span<const std::uint8_t> logo_rom{};
        std::span<const std::uint8_t> disk_rom{};
        std::span<const std::uint8_t> kanji_rom{};
        std::span<const std::uint8_t> cassette_image{};
        std::span<const std::uint8_t> disk_image{};
        bool disk_write_protected{};
    };

    // Minimal but real MSX2 composition: Z80A + V9938 + AY/YM2149 SSG + RP-5C01
    // RTC + WD1793-compatible disk controller + Konami SCC expansion audio +
    // optional MSX-MUSIC YM2413 + PPI slot control + 16 KiB RAM mapper pages.
    // Default slot layout:
    //   slot 0: expanded internal ROMs, subslot 0 = main BIOS plus optional
    //           MSX2 logo ROM at $8000-$BFFF, subslot 1 = MSX2 sub-ROM when provided
    //   slot 1: cartridge in pages 1-2, with plain/ASCII/Konami mapper support
    //   slot 2: optional second cartridge, or disk interface ROM/FDC windows
    //   slot 3: mapper RAM, segments selected by I/O ports #FC-#FF
    // Alternate RAM, disk, and second-cartridge primary/subslot layouts are
    // selected through msx2_config for machine profiles that wire expansions
    // into internal subslots.
    //
    // WD1793 media uses deterministic byte-track/index-phase timing; analog flux-cell timing and
    // deeper VDP command timing remain follow-up hardware slices.
    // The public methods below are intentionally system-level controls used by
    // tests and the player adapter.
    struct msx2_system final {
        static constexpr std::size_t page_size = 0x4000U;
        static constexpr std::size_t ascii8_page_size = 0x2000U;
        static constexpr std::size_t ascii16_page_size = 0x4000U;

        chips::cpu::z80 cpu;
        chips::video::v9938 vdp;
        chips::audio::ssg psg;
        chips::peripheral::rp5c01 rtc;
        chips::storage::msx_cassette cassette;
        chips::storage::wd1793 fdc;
        chips::peripheral::msx_kanji_rom kanji;
        chips::mapper::korean_msx_mapper korean_mapper;
        chips::mapper::korean_msx_mapper korean_mapper2;
        chips::audio::scc scc;
        chips::audio::ym2413 music;
        topology::bus bus{16U, topology::endianness::little};

        std::vector<std::uint8_t> bios;
        std::vector<std::uint8_t> sub_bios;
        std::vector<std::uint8_t> logo_rom;
        std::vector<std::uint8_t> disk_rom;
        std::vector<std::uint8_t> cartridge;
        std::vector<std::uint8_t> cartridge2;
        std::vector<std::uint8_t> ram;
        std::vector<std::uint8_t> cart_sram;
        std::vector<std::uint8_t> cart2_sram;

        msx2_cartridge_mapper cart_mapper{msx2_cartridge_mapper::plain};
        msx2_cartridge_mapper cart2_mapper{msx2_cartridge_mapper::plain};

        // PPI primary slot select: two bits per 16 KiB page.
        std::uint8_t primary_slot{0x00U};
        std::uint8_t ppi_a{};
        std::array<std::uint8_t, 4> secondary_slot{};
        std::array<bool, 4> expanded_slot{};
        std::uint8_t ram_primary_slot{3U};
        std::uint8_t ram_secondary_slot{};
        std::uint8_t sub_bios_primary_slot{};
        std::uint8_t sub_bios_secondary_slot{1U};
        std::uint8_t disk_primary_slot{2U};
        std::uint8_t disk_secondary_slot{};
        std::uint8_t cartridge2_primary_slot{2U};
        std::uint8_t cartridge2_secondary_slot{};
        bool disk_active{};
        std::uint8_t ppi_c{0x0FU};
        std::uint8_t ppi_control{0x9BU};
        std::uint8_t ppi_b{0xFFU};
        std::array<std::uint8_t, 16> keyboard_rows{};
        std::array<std::uint8_t, 2> joystick_ports{};
        std::array<common::msx_mouse_port, 2> mouse_ports{};

        // Four 16 KiB RAM mapper windows selected by ports #FC-#FF.
        std::array<std::uint8_t, 4> ram_segment{};

        // Cartridge mapper registers.
        std::array<std::uint8_t, 4> ascii8_bank{};
        std::array<std::uint8_t, 2> ascii16_bank{};
        std::array<std::uint8_t, 4> konami_bank{};
        std::array<std::uint8_t, 4> cart2_ascii8_bank{};
        std::array<std::uint8_t, 2> cart2_ascii16_bank{};
        std::array<std::uint8_t, 4> cart2_konami_bank{};
        bool msx_music_active{};
        bool fmpac_sram_active{};
        std::array<std::uint8_t, 0x2000> fmpac_sram{};
        std::array<std::uint8_t, 2> fmpac_unlock_latch{{0xFFU, 0xFFU}};

        [[nodiscard]] bool msx_music_enabled() const noexcept { return msx_music_active; }
        [[nodiscard]] bool fmpac_sram_enabled() const noexcept { return fmpac_sram_active; }
        [[nodiscard]] bool disk_enabled() const noexcept { return disk_active; }

        void set_key(int row, int bit, bool pressed) noexcept;
        void set_joystick(int port, bool up, bool down, bool left, bool right, bool trigger_a,
                          bool trigger_b) noexcept;
        void set_mouse(int port, std::int16_t delta_x, std::int16_t delta_y, bool left_button,
                       bool right_button) noexcept;
        void set_cassette_input(bool high) noexcept { cassette.set_input_high(high); }
        [[nodiscard]] bool cassette_motor_on() const noexcept { return cassette.motor_on(); }
        [[nodiscard]] bool cassette_output_high() const noexcept { return cassette.output_high(); }
        [[nodiscard]] std::uint8_t cpu_read(std::uint16_t address) noexcept;
        void cpu_write(std::uint16_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t io_read(std::uint16_t port) noexcept;
        void io_write(std::uint16_t port, std::uint8_t value) noexcept;
        [[nodiscard]] std::span<std::uint8_t> ram_view() noexcept { return ram; }
        [[nodiscard]] std::span<std::uint8_t> battery_ram() noexcept;
        void sync_ppi_outputs() noexcept;
        void sync_cassette_control() noexcept;
        void sync_psg_outputs() noexcept;

        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);

      private:
        [[nodiscard]] std::uint8_t slot_for_page(std::uint16_t address) const noexcept;
        [[nodiscard]] std::uint8_t secondary_for_page(std::uint8_t slot,
                                                      std::uint16_t address) const noexcept;
        [[nodiscard]] std::uint8_t read_slot(std::uint8_t slot, std::uint8_t subslot,
                                             std::uint16_t address) noexcept;
        void write_slot(std::uint8_t slot, std::uint8_t subslot, std::uint16_t address,
                        std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t cartridge_slot_index(std::uint8_t slot,
                                                        std::uint8_t subslot) const noexcept;
        [[nodiscard]] std::uint8_t
        plain_32k_handoff_cart_slot(std::uint8_t slot, std::uint8_t subslot,
                                    std::uint16_t address) const noexcept;
        [[nodiscard]] bool plain_16k_lower_page_visible(std::uint8_t slot_index) const noexcept;
        [[nodiscard]] std::uint8_t read_cartridge(std::uint8_t slot_index,
                                                  std::uint16_t address) const noexcept;
        void write_cartridge(std::uint8_t slot_index, std::uint16_t address,
                             std::uint8_t value) noexcept;
        [[nodiscard]] bool fmpac_sram_unlocked() const noexcept;
        [[nodiscard]] bool scc_register_selected(std::uint8_t slot_index,
                                                 std::uint16_t address) const noexcept;
        [[nodiscard]] bool fdc_mmio_selected(std::uint16_t address) const noexcept;
        [[nodiscard]] std::uint8_t read_fdc_mmio(std::uint16_t address) noexcept;
        void write_fdc_mmio(std::uint16_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_psg_data() const noexcept;
        [[nodiscard]] std::uint8_t read_ram(std::uint16_t address) const noexcept;
        void write_ram(std::uint16_t address, std::uint8_t value) noexcept;
    };

    [[nodiscard]] std::unique_ptr<msx2_system>
    assemble_msx2(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> cartridge = {},
                  const msx2_config& config = {});

} // namespace mnemos::manifests::msx2
