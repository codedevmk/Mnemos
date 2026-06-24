#pragma once

#include "bus.hpp"
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
        plain,
        ascii8,
        ascii16,
        konami,
        konami_scc,
    };

    struct msx2_config final {
        mnemos::video_region video_region{mnemos::video_region::ntsc};
        msx2_cartridge_mapper cartridge_mapper{msx2_cartridge_mapper::plain};
        std::size_t ram_size{0x20000U}; // 128 KiB RAM mapper, the common MSX2 baseline
        bool msx_music{};
        std::span<const std::uint8_t> sub_bios{};
        std::span<const std::uint8_t> disk_rom{};
        std::span<const std::uint8_t> disk_image{};
        bool disk_write_protected{};
    };

    // Minimal but real MSX2 composition: Z80A + V9938 + AY/YM2149 SSG + RP-5C01
    // RTC + WD1793-compatible disk controller + Konami SCC expansion audio +
    // optional MSX-MUSIC YM2413 + PPI slot control + 16 KiB RAM mapper pages.
    // Slot layout:
    //   slot 0: expanded internal ROMs, subslot 0 = main BIOS, subslot 1 = MSX2
    //           sub-ROM when provided
    //   slot 1: cartridge in pages 1-2, with plain/ASCII/Konami mapper support
    //   slot 2: optional disk interface ROM and memory-mapped FDC windows
    //   slot 3: mapper RAM, segments selected by I/O ports #FC-#FF
    //
    // Raw floppy flux timing, exact CRC byte preservation, and deeper VDP command
    // timing are follow-up hardware slices.
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
        chips::storage::wd1793 fdc;
        chips::audio::scc scc;
        chips::audio::ym2413 music;
        topology::bus bus{16U, topology::endianness::little};

        std::vector<std::uint8_t> bios;
        std::vector<std::uint8_t> sub_bios;
        std::vector<std::uint8_t> disk_rom;
        std::vector<std::uint8_t> cartridge;
        std::vector<std::uint8_t> ram;

        msx2_cartridge_mapper cart_mapper{msx2_cartridge_mapper::plain};

        // PPI primary slot select: two bits per 16 KiB page.
        std::uint8_t primary_slot{0x00U};
        std::array<std::uint8_t, 4> secondary_slot{};
        std::array<bool, 4> expanded_slot{};
        std::uint8_t ppi_c{0x0FU};
        std::uint8_t ppi_control{0x9BU};
        std::array<std::uint8_t, 16> keyboard_rows{};
        std::array<std::uint8_t, 2> joystick_ports{};
        bool cassette_input_high{true};

        // Four 16 KiB RAM mapper windows selected by ports #FC-#FF.
        std::array<std::uint8_t, 4> ram_segment{};

        // Cartridge mapper registers.
        std::array<std::uint8_t, 4> ascii8_bank{};
        std::array<std::uint8_t, 2> ascii16_bank{};
        std::array<std::uint8_t, 4> konami_bank{};
        bool msx_music_active{};

        [[nodiscard]] bool msx_music_enabled() const noexcept { return msx_music_active; }

        void set_key(int row, int bit, bool pressed) noexcept;
        void set_joystick(int port, bool up, bool down, bool left, bool right, bool trigger_a,
                          bool trigger_b) noexcept;
        void set_cassette_input(bool high) noexcept { cassette_input_high = high; }
        [[nodiscard]] bool cassette_motor_on() const noexcept { return (ppi_c & 0x10U) == 0U; }
        [[nodiscard]] bool cassette_output_high() const noexcept { return (ppi_c & 0x20U) != 0U; }
        [[nodiscard]] std::uint8_t cpu_read(std::uint16_t address) noexcept;
        void cpu_write(std::uint16_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t io_read(std::uint16_t port) noexcept;
        void io_write(std::uint16_t port, std::uint8_t value) noexcept;
        [[nodiscard]] std::span<std::uint8_t> ram_view() noexcept { return ram; }

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
        [[nodiscard]] std::uint8_t read_cartridge(std::uint16_t address) const noexcept;
        void write_cartridge(std::uint16_t address, std::uint8_t value) noexcept;
        [[nodiscard]] bool scc_register_selected(std::uint16_t address) const noexcept;
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
