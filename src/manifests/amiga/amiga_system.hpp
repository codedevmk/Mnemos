#pragma once

#include "agnus.hpp"
#include "bus.hpp"
#include "cia8520.hpp"
#include "denise.hpp"
#include "expansions/zorro2.hpp"
#include "m68000.hpp"
#include "paula.hpp"
#include "region.hpp"
#include "state.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace mnemos::manifests::amiga {

    enum class amiga_keyboard_layout : std::uint8_t { us, qwerty_international, german, azerty };

    enum class amiga_model : std::uint8_t {
        amiga500,
        amiga500_plus,
        amiga600,
        amiga2000,
        amiga2000_ecs_1m
    };

    struct amiga_config final {
        mnemos::video_region video_region{mnemos::video_region::pal};
        amiga_keyboard_layout keyboard_layout{amiga_keyboard_layout::us};
        amiga_model model{amiga_model::amiga500};
        std::size_t fast_ram_size{};
    };

    // Shared classic Amiga machine assembly: MC68000 + Agnus + Denise + Paula +
    // two CIA 8520s over a 24-bit, big-endian 68K bus, configured by model
    // descriptors for A500/A500+/A600/A2000-style boards. The reusable silicon
    // stays in src/chips; this type owns board wiring, Kickstart overlay, chip
    // RAM, custom register routing, CIA glue, drives, input, and expansions.
    struct amiga_system final {
        static constexpr std::size_t chip_ram_size = 512U * 1024U;
        static constexpr std::size_t chip_ram_size_1m = 1024U * 1024U;
        static constexpr std::size_t fast_ram_size_512k = 512U * 1024U;
        static constexpr std::size_t fast_ram_size_1m = 1024U * 1024U;
        static constexpr std::size_t fast_ram_size_2m = 2U * 1024U * 1024U;
        static constexpr std::size_t fast_ram_size_4m = 4U * 1024U * 1024U;
        static constexpr std::size_t fast_ram_max_size = 8U * 1024U * 1024U;
        static constexpr std::size_t kickstart_window_size = 512U * 1024U;
        static constexpr std::size_t floppy_cylinders = 80U;
        static constexpr std::size_t floppy_heads = 2U;
        static constexpr std::size_t floppy_track_count = floppy_cylinders * floppy_heads;
        static constexpr std::size_t floppy_sectors_per_track = 11U;
        static constexpr std::size_t floppy_sector_size = 512U;
        static constexpr std::size_t floppy_dd_size =
            floppy_cylinders * floppy_heads * floppy_sectors_per_track * floppy_sector_size;
        static constexpr std::size_t floppy_drive_count = 4U;
        static constexpr std::uint8_t no_floppy_drive = 0xFFU;
        static constexpr std::uint32_t floppy_index_pulses_per_second = 5U;
        static constexpr std::size_t keyboard_raw_key_count = 128U;
        static constexpr std::size_t keyboard_queue_capacity = 16U;
        static constexpr std::uint8_t keyboard_reset_warning_code = 0x78U;
        static constexpr std::uint8_t keyboard_last_code_bad_code = 0xF9U;
        static constexpr std::uint8_t keyboard_buffer_overflow_code = 0xFAU;
        static constexpr std::uint8_t keyboard_self_test_fail_code = 0xFCU;
        static constexpr std::uint8_t keyboard_powerup_stream_start_code = 0xFDU;
        static constexpr std::uint8_t keyboard_powerup_stream_end_code = 0xFEU;

        static constexpr std::uint32_t chip_ram_base = 0x000000U;
        static constexpr std::uint32_t fast_ram_base = 0x200000U;
        static constexpr std::uint32_t zorro2_expansion_ram_base = fast_ram_base;
        static constexpr std::uint32_t zorro2_expansion_ram_size =
            static_cast<std::uint32_t>(fast_ram_max_size);
        static constexpr std::uint32_t zorro2_autoconfig_base = 0xE80000U;
        static constexpr std::uint32_t zorro2_autoconfig_size = 0x10000U;
        static constexpr std::uint32_t kickstart_base = 0xF80000U;
        static constexpr std::uint32_t custom_base = 0xDFF000U;
        static constexpr std::uint32_t cia_a_base = 0xBFE000U;
        static constexpr std::uint32_t cia_b_base = 0xBFD000U;

        static constexpr std::uint16_t int_tbe = 1U << 0U;
        static constexpr std::uint16_t int_dskblk = 1U << 1U;
        static constexpr std::uint16_t int_soft = 1U << 2U;
        static constexpr std::uint16_t int_ports = 1U << 3U;
        static constexpr std::uint16_t int_coper = 1U << 4U;
        static constexpr std::uint16_t int_vertb = 1U << 5U;
        static constexpr std::uint16_t int_blit = 1U << 6U;
        static constexpr std::uint16_t int_aud0 = 1U << 7U;
        static constexpr std::uint16_t int_aud1 = 1U << 8U;
        static constexpr std::uint16_t int_aud2 = 1U << 9U;
        static constexpr std::uint16_t int_aud3 = 1U << 10U;
        static constexpr std::uint16_t int_rbf = 1U << 11U;
        static constexpr std::uint16_t int_dsksyn = 1U << 12U;
        static constexpr std::uint16_t int_exter = 1U << 13U;
        static constexpr std::uint16_t int_master = 1U << 14U;
        static constexpr std::uint16_t setclr_bit = 1U << 15U;
        static constexpr std::uint8_t joy_up = 1U << 0U;
        static constexpr std::uint8_t joy_down = 1U << 1U;
        static constexpr std::uint8_t joy_left = 1U << 2U;
        static constexpr std::uint8_t joy_right = 1U << 3U;
        static constexpr std::uint8_t joy_fire = 1U << 4U;
        static constexpr std::uint8_t joy_secondary_fire = 1U << 5U;
        static constexpr std::uint8_t joy_middle_fire = 1U << 6U;

        chips::cpu::m68000 cpu;
        chips::video::agnus agnus;
        chips::video::denise denise;
        chips::audio::paula paula;
        chips::peripheral::cia8520 cia_a;
        chips::peripheral::cia8520 cia_b;
        topology::bus bus{24U, topology::endianness::big};

        std::vector<std::uint8_t> chip_ram = std::vector<std::uint8_t>(chip_ram_size, 0U);
        std::vector<std::uint8_t> fast_ram{};
        std::array<std::uint8_t, kickstart_window_size> kickstart_rom{};

        std::vector<zorro2_expansion_board> zorro2_boards{};
        std::size_t zorro2_autoconfig_index{};
        std::uint8_t zorro2_base_low_nibble{};
        bool zorro2_base_low_nibble_valid{};

        std::array<std::uint16_t, chips::video::agnus::palette_entries> palette_words{};
        std::array<std::uint8_t, chips::video::agnus::palette_entries * 2U> palette_bytes{};
        std::array<std::uint32_t, chips::video::agnus::max_bitplanes> bitplane_pointer{};
        std::array<std::uint32_t, 4> blitter_pointer{};
        std::array<std::int16_t, 4> blitter_modulo{};
        std::array<std::uint16_t, 4> blitter_data{};
        std::uint16_t bltcon0{};
        std::uint16_t bltcon1{};
        std::uint16_t bltafwm{0xFFFFU};
        std::uint16_t bltalwm{0xFFFFU};
        std::uint16_t bltsize{};
        std::uint64_t blitter_cycles_remaining{};
        std::uint32_t cop1lc{};
        std::uint32_t cop2lc{};
        std::uint32_t copper_address_mask{chips::video::agnus::ocs_copper_address_mask};
        std::uint16_t custom_high_latch{};
        std::uint32_t disk_pointer{};
        std::uint16_t disk_length{};
        std::uint16_t disk_sync{0x4489U};
        std::uint16_t disk_adkcon{};
        std::uint16_t disk_data{};
        std::uint16_t disk_last_length_write{};
        std::uint16_t disk_shift{};
        std::uint32_t disk_dma_bytes_remaining{};
        bool disk_dma_armed{};
        bool disk_byte_valid{};
        bool disk_sync_match{};
        bool disk_wordsync_waiting{};
        std::array<std::uint16_t, 2> joydat{};
        std::array<std::uint8_t, 2> joystick_state{};
        std::array<std::uint16_t, 2> pot_counter{0xFFFFU, 0xFFFFU};
        std::array<std::uint16_t, 2> pot_target{0xFFFFU, 0xFFFFU};
        std::uint64_t beam_line_epoch{};
        std::uint64_t pot_start_line_epoch{};
        std::uint16_t potgo{};
        bool pot_counters_running{};

        std::uint16_t intena{};
        std::uint16_t intreq{};
        bool overlay_active{true};
        bool cia_a_irq{};
        bool cia_b_irq{};
        std::uint64_t frame_index{};

        struct floppy_drive_state final {
            std::vector<std::uint8_t> image{};
            std::vector<std::uint8_t> track_stream{};
            std::vector<std::uint8_t> weak_bit_stream{};
            std::array<std::vector<std::uint8_t>, floppy_track_count> raw_track_cache{};
            std::array<std::vector<std::uint8_t>, floppy_track_count> weak_bit_cache{};
            std::size_t stream_offset{};
            std::size_t track_stream_track_index{};
            std::uint8_t stream_bit_offset{};
            std::uint8_t stream_read_shift{};
            std::uint8_t stream_read_bit_count{};
            std::uint8_t stream_write_latch{};
            std::uint8_t stream_write_shift{};
            std::uint8_t stream_write_bits_remaining{};
            std::uint16_t weak_bit_lfsr{0xACE1U};
            std::uint8_t cylinder_pos{};
            bool connected{};
            bool motor_on{};
            bool write_protected{true};
            bool change_latch{true};
            bool track_stream_dirty{};
            std::uint32_t index_line_accumulator{};
            std::uint64_t byte_clock_accumulator{};
        };

        std::array<floppy_drive_state, floppy_drive_count> floppy_drives{};
        std::uint8_t floppy_selected_mask{};
        std::uint8_t floppy_active_drive{no_floppy_drive};
        std::uint8_t floppy_side_pos{};
        bool floppy_motor_on{};
        bool floppy_selected{};
        bool floppy_step_line{true};
        bool floppy_direction_inward{};
        std::array<std::uint8_t, keyboard_queue_capacity> keyboard_queue{};
        std::array<bool, keyboard_raw_key_count> keyboard_key_down{};
        std::size_t keyboard_queue_head{};
        std::size_t keyboard_queue_count{};
        bool keyboard_byte_in_flight{};
        bool keyboard_ack_low_seen{};
        bool keyboard_caps_lock_led{};

        [[nodiscard]] bool kickstart_overlay_active() const noexcept { return overlay_active; }
        [[nodiscard]] bool floppy_loaded() const noexcept { return floppy_loaded(0U); }
        [[nodiscard]] bool floppy_loaded(std::size_t drive) const noexcept;
        [[nodiscard]] std::size_t floppy_size() const noexcept { return floppy_size(0U); }
        [[nodiscard]] std::size_t floppy_size(std::size_t drive) const noexcept;
        [[nodiscard]] std::uint8_t floppy_cylinder() const noexcept { return floppy_cylinder(0U); }
        [[nodiscard]] std::uint8_t floppy_cylinder(std::size_t drive) const noexcept;
        [[nodiscard]] std::uint8_t floppy_side() const noexcept { return floppy_side_pos; }
        [[nodiscard]] std::uint8_t selected_floppy_drive() const noexcept {
            return floppy_active_drive;
        }
        [[nodiscard]] std::uint32_t floppy_index_lines_per_revolution() const noexcept;
        [[nodiscard]] std::size_t keyboard_pending_count() const noexcept {
            return keyboard_queue_count;
        }
        [[nodiscard]] bool keyboard_caps_lock_led_on() const noexcept {
            return keyboard_caps_lock_led;
        }
        [[nodiscard]] zorro2_expansion_board* active_zorro2_autoconfig_board() noexcept;
        [[nodiscard]] const zorro2_expansion_board*
        active_zorro2_autoconfig_board() const noexcept;
        [[nodiscard]] bool zorro2_autoconfig_pending() const noexcept;
        void reset_zorro2_autoconfig() noexcept;

        [[nodiscard]] bool mount_floppy(std::span<const std::uint8_t> adf_image);
        [[nodiscard]] bool mount_floppy(std::size_t drive, std::span<const std::uint8_t> adf_image);
        void unmount_floppy() noexcept;
        void unmount_floppy(std::size_t drive) noexcept;
        void set_floppy_change_latch(std::size_t drive, bool changed) noexcept;
        void set_floppy_write_protected(std::size_t drive, bool write_protected) noexcept;
        void set_joystick(std::size_t hardware_port, std::uint8_t mask) noexcept;
        void set_mouse(std::size_t hardware_port, std::int16_t delta_x, std::int16_t delta_y,
                       bool left_button, bool right_button, bool middle_button) noexcept;
        void set_pot_position(std::size_t hardware_port, std::uint8_t x, std::uint8_t y) noexcept;
        [[nodiscard]] bool enqueue_keyboard_key(std::uint8_t raw_keycode, bool pressed) noexcept;
        [[nodiscard]] bool enqueue_keyboard_control_code(std::uint8_t code) noexcept;
        [[nodiscard]] bool press_caps_lock() noexcept;
        void service_keyboard_queue() noexcept;

        [[nodiscard]] std::uint16_t read_custom_word(std::uint16_t reg) noexcept;
        void write_custom_word(std::uint16_t reg, std::uint16_t value) noexcept;
        [[nodiscard]] std::uint8_t read_custom_byte(std::uint32_t address) noexcept;
        void write_custom_byte(std::uint32_t address, std::uint8_t value) noexcept;

        [[nodiscard]] std::uint8_t cia_a_port_a_inputs() const noexcept;
        [[nodiscard]] std::uint16_t read_potinp() const noexcept;
        [[nodiscard]] std::uint32_t
        cpu_bus_wait_cycles(std::uint32_t address, bool program, bool write,
                            std::uint32_t instruction_cycles_before_access,
                            std::uint32_t instruction_wait_cycles) const noexcept;
        void write_cia_a_sp(bool level) noexcept;
        void write_cia_b_port_b(std::uint8_t value);
        void request_interrupt(std::uint16_t mask) noexcept;
        [[nodiscard]] std::uint16_t visible_intreq() const noexcept;
        void update_irq_level() noexcept;
        void update_overlay_from_cia() noexcept;
        void reset_board(chips::reset_kind kind) noexcept;
        void reset_board_from_cpu() noexcept;
        [[nodiscard]] std::uint16_t read_pot_counter(std::size_t hardware_port) noexcept;
        void start_blitter(std::uint16_t value) noexcept;
        void start_blitter_line(std::uint32_t length) noexcept;
        void tick_blitter_cycle() noexcept;
        [[nodiscard]] bool enqueue_keyboard_code(std::uint8_t code) noexcept;
        void transmit_keyboard_code(std::uint8_t code) noexcept;
        [[nodiscard]] floppy_drive_state* active_floppy_drive_state() noexcept;
        [[nodiscard]] const floppy_drive_state* active_floppy_drive_state() const noexcept;
        void preserve_dirty_floppy_track(std::size_t drive) noexcept;
        void update_floppy_track_stream();
        void update_floppy_track_stream(std::size_t drive);
        [[nodiscard]] bool flush_floppy_track_to_image(std::size_t drive) noexcept;
        [[nodiscard]] std::uint8_t next_floppy_byte() noexcept;
        void accept_floppy_byte(std::uint8_t byte) noexcept;
        [[nodiscard]] bool shift_floppy_read_bit() noexcept;
        void shift_floppy_write_bit() noexcept;
        void tick_floppy_scanline() noexcept;
        void tick_floppy_data_cycle() noexcept;
        void complete_disk_dma_byte() noexcept;
        void transfer_disk_dma_byte(std::uint8_t byte) noexcept;
        void start_disk_dma(std::uint16_t value) noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);
    };

    [[nodiscard]] std::unique_ptr<amiga_system>
    assemble_amiga(std::vector<std::uint8_t> kickstart_rom, const amiga_config& config = {});

} // namespace mnemos::manifests::amiga
