#pragma once

#include "bus.hpp"
#include "korean_msx_mapper.hpp"
#include "msx_cassette.hpp"
#include "msx_io_ports.hpp"
#include "msx_kanji_rom.hpp"
#include "peripheral.hpp"
#include "region.hpp"
#include "rp5c01.hpp"
#include "scc.hpp"
#include "ssg.hpp"
#include "state.hpp"
#include "tms9918a.hpp"
#include "v9938.hpp"
#include "wd1793.hpp"
#include "ym2413.hpp"
#include "z80.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mnemos::manifests::msx {

    enum class msx_cartridge_mapper : std::uint8_t {
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

    enum class msx_video_model : std::uint8_t {
        tms9918a,
        v9938,
    };

    struct msx_config final {
        mnemos::video_region video_region{mnemos::video_region::ntsc};
        msx_video_model video_model{msx_video_model::tms9918a};
        msx_cartridge_mapper cartridge_mapper{msx_cartridge_mapper::automatic};
        msx_cartridge_mapper cartridge2_mapper{msx_cartridge_mapper::automatic};
        std::uint8_t expanded_primary_slots{};
        std::uint8_t ram_primary_slot{3U};
        std::uint8_t ram_secondary_slot{};
        std::uint8_t ram_mapper_segments{};
        bool rtc_enabled{};
        bool disk_enabled{};
        bool disk_write_protected{};
        bool fm_music_enabled{};
        bool fmpac_sram_enabled{};
        std::span<const std::uint8_t> cassette_image{};
        std::span<const std::uint8_t> logo_rom{};
        std::uint8_t disk_primary_slot{3U};
        std::uint8_t disk_secondary_slot{1U};
        std::uint8_t cartridge2_primary_slot{2U};
        std::uint8_t cartridge2_secondary_slot{};
    };

    // MSX1 baseline machine: Z80A, TMS9918A-family VDP, AY-3-8910-compatible PSG,
    // 8255-style PPI slot/keyboard controls, a 32 KiB BIOS slot, 64 KiB RAM or
    // memory-mapper RAM slot, expanded primary-slot latches, two cartridge slots,
    // Konami SCC cartridge audio, RP-5C01 RTC, cassette waveform input, optional
    // FM-PAC/PAC SRAM, optional cartridge SRAM, and an optional WD1793-compatible MSX floppy
    // interface.
    // MSX2 configurations select the V9938 path explicitly; MSX1 remains the default.
    struct msx_system final {
        chips::cpu::z80 cpu;
        chips::video::tms9918a vdp;
        chips::video::v9938 vdp2;
        chips::audio::ssg psg;
        chips::audio::scc scc;
        chips::audio::ym2413 fm;
        chips::peripheral::rp5c01 rtc;
        chips::peripheral::msx_kanji_rom kanji;
        chips::storage::msx_cassette cassette;
        chips::storage::wd1793 fdc;
        chips::mapper::korean_msx_mapper korean_mapper;
        chips::mapper::korean_msx_mapper korean_mapper2;
        topology::bus bus{16U, topology::endianness::little};

        std::array<std::uint8_t, 0x8000> bios{};
        std::array<std::uint8_t, 0x10000> ram{};
        std::vector<std::uint8_t> mapped_ram{};
        std::vector<std::uint8_t> logo_rom{};
        std::vector<std::uint8_t> cartridge{};
        std::vector<std::uint8_t> cartridge2{};
        std::vector<std::uint8_t> disk_rom{};
        std::vector<std::uint8_t> cart_sram{};
        std::vector<std::uint8_t> cart2_sram{};
        std::array<std::uint8_t, 0x2000> fmpac_sram{};

        msx_cartridge_mapper mapper{msx_cartridge_mapper::plain};
        msx_cartridge_mapper cartridge2_mapper{msx_cartridge_mapper::plain};
        msx_video_model video_model{msx_video_model::tms9918a};
        std::array<std::uint8_t, 4> cart_8k_bank{{0U, 1U, 2U, 3U}};
        std::array<std::uint8_t, 2> cart_16k_bank{{0U, 1U}};
        std::array<std::uint8_t, 4> cart2_8k_bank{{0U, 1U, 2U, 3U}};
        std::array<std::uint8_t, 2> cart2_16k_bank{{0U, 1U}};
        bool scc_window_enabled{};
        bool scc2_window_enabled{};
        bool cartridge_lower_handoff{};
        bool cartridge2_lower_handoff{};
        bool rtc_enabled{};
        bool disk_enabled{};
        bool fm_music_enabled{};
        bool fmpac_sram_enabled{};
        std::array<std::uint8_t, 2> fmpac_unlock_latch{{0xFFU, 0xFFU}};
        std::uint8_t disk_primary_slot{3U};
        std::uint8_t disk_secondary_slot{1U};
        std::uint8_t cartridge2_primary_slot{2U};
        std::uint8_t cartridge2_secondary_slot{};

        // PPI port A: two primary-slot select bits for each 16 KiB page.
        std::uint8_t primary_slot_select{};
        std::uint8_t ppi_a{};
        // Bit N marks primary slot N as expanded. Each expanded primary slot owns
        // one subslot-select latch at $FFFF.
        std::uint8_t expanded_primary_slots{};
        std::array<std::uint8_t, 4> secondary_slot_select{};
        std::uint8_t ram_primary_slot{3U};
        std::uint8_t ram_secondary_slot{};
        std::array<std::uint8_t, 4> ram_mapper_page{{0U, 1U, 2U, 3U}};
        // PPI port C: low nibble selects the keyboard row; upper bits carry
        // cassette motor/write, caps LED, and click output latches.
        std::uint8_t ppi_c{0xF0U};
        std::uint8_t ppi_control{0x9BU};
        std::uint8_t ppi_b{0xFFU};
        std::array<std::uint8_t, 16> keyboard_rows{};
        std::array<std::uint8_t, 2> joystick_rows{{0x3FU, 0x3FU}}; // active-low low 6 bits
        std::array<common::msx_mouse_port, 2> mouse_ports{};

        void set_key(int row, int bit, bool pressed) noexcept;
        void apply_joystick(int port, const peripheral::controller_state& state) noexcept;
        void set_mouse(int port, std::int16_t delta_x, std::int16_t delta_y, bool left_button,
                       bool right_button) noexcept;

        [[nodiscard]] std::uint8_t read_memory(std::uint16_t address) noexcept;
        void write_memory(std::uint16_t address, std::uint8_t value) noexcept;

        [[nodiscard]] std::uint8_t read_io(std::uint16_t port) noexcept;
        void write_io(std::uint16_t port, std::uint8_t value) noexcept;
        [[nodiscard]] std::span<const std::uint8_t> work_ram() const noexcept;
        [[nodiscard]] std::span<std::uint8_t> battery_ram() noexcept;
        [[nodiscard]] bool msx2_video() const noexcept;
        [[nodiscard]] chips::ivideo& active_video() noexcept;
        [[nodiscard]] const chips::ivideo& active_video() const noexcept;
        [[nodiscard]] chips::frame_buffer_view framebuffer() const noexcept;
        void sync_ppi_outputs() noexcept;
        void sync_cassette_control() noexcept;
        void sync_psg_outputs() noexcept;

        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);

      private:
        struct slot_decode final {
            std::uint8_t primary{};
            std::uint8_t secondary{};
        };

        [[nodiscard]] bool primary_slot_expanded(std::uint8_t primary) const noexcept;
        [[nodiscard]] slot_decode selected_slot(std::uint16_t address) const noexcept;
        [[nodiscard]] slot_decode page3_slot() const noexcept;
        [[nodiscard]] bool is_bios_slot(slot_decode slot) const noexcept;
        [[nodiscard]] std::uint8_t cart_slot_index(slot_decode slot) const noexcept;
        [[nodiscard]] bool is_disk_slot(slot_decode slot) const noexcept;
        [[nodiscard]] bool is_ram_slot(slot_decode slot) const noexcept;
        [[nodiscard]] std::uint8_t
        plain_32k_handoff_cart_slot(slot_decode selected_slot,
                                    std::uint16_t address) const noexcept;
        [[nodiscard]] bool plain_16k_lower_page_visible(std::uint8_t slot_index) const noexcept;
        [[nodiscard]] std::uint8_t read_ram(std::uint16_t address) const noexcept;
        void write_ram(std::uint16_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_disk(std::uint16_t address) noexcept;
        void write_disk(std::uint16_t address, std::uint8_t value) noexcept;
        [[nodiscard]] std::uint8_t read_cart(std::uint8_t slot_index,
                                             std::uint16_t address) noexcept;
        void write_cart(std::uint8_t slot_index, std::uint16_t address,
                        std::uint8_t value) noexcept;
        [[nodiscard]] bool fmpac_sram_unlocked() const noexcept;
        [[nodiscard]] std::uint8_t read_cart_8k(std::span<const std::uint8_t> rom,
                                                std::uint8_t bank,
                                                std::uint16_t offset) const noexcept;
        [[nodiscard]] std::uint8_t read_cart_16k(std::span<const std::uint8_t> rom,
                                                 std::uint8_t bank,
                                                 std::uint16_t offset) const noexcept;
        [[nodiscard]] std::uint8_t psg_port_a() const noexcept;
    };

    [[nodiscard]] std::unique_ptr<msx_system>
    assemble_msx(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> cartridge = {},
                 const msx_config& config = {});
    [[nodiscard]] std::unique_ptr<msx_system> assemble_msx(std::span<const std::uint8_t> bios,
                                                           std::span<const std::uint8_t> cartridge,
                                                           std::span<const std::uint8_t> disk_rom,
                                                           std::span<const std::uint8_t> disk_image,
                                                           const msx_config& config);
    [[nodiscard]] std::unique_ptr<msx_system>
    assemble_msx(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> cartridge,
                 std::span<const std::uint8_t> disk_rom, std::span<const std::uint8_t> disk_image,
                 std::span<const std::uint8_t> kanji_rom, const msx_config& config);
    [[nodiscard]] std::unique_ptr<msx_system>
    assemble_msx(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> cartridge,
                 std::span<const std::uint8_t> cartridge2, std::span<const std::uint8_t> disk_rom,
                 std::span<const std::uint8_t> disk_image, std::span<const std::uint8_t> kanji_rom,
                 const msx_config& config);

} // namespace mnemos::manifests::msx
