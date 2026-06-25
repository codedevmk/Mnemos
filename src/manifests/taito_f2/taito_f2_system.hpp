#pragma once

#include "bus.hpp"
#include "m68000.hpp"
#include "rom_set.hpp"
#include "state.hpp"
#include "tc0140syt.hpp"
#include "taito_f2_video.hpp"
#include "ym2610.hpp"
#include "z80.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mnemos::manifests::taito_f2 {

    inline constexpr std::uint32_t program_rom_base = 0x000000U;
    inline constexpr std::size_t main_rom_size = 0x100000U;

    inline constexpr std::uint32_t work_ram_base = 0x100000U;
    inline constexpr std::size_t work_ram_size = 0x10000U;
    inline constexpr std::uint32_t palette_ram_base = 0x200000U;
    inline constexpr std::size_t palette_ram_size = 0x4000U;
    inline constexpr std::uint32_t comm_base = 0x300000U;
    inline constexpr std::uint32_t comm_window = 0x10U;
    inline constexpr std::uint32_t tile_ram_base = 0x400000U;
    inline constexpr std::size_t tile_ram_size = 0x10000U;
    inline constexpr std::uint32_t sprite_ram_base = 0x500000U;
    inline constexpr std::size_t sprite_ram_size = 0x10000U;
    inline constexpr std::size_t sprite_extension_ram_size = 0x4000U;
    inline constexpr std::uint32_t video_reg_base = 0x600000U;
    inline constexpr std::uint32_t video_reg_window = 0x20U;
    inline constexpr std::uint32_t input_base = 0x700000U;
    inline constexpr std::uint32_t input_window = 0x10U;

    inline constexpr std::uint32_t real_input_base = 0x300000U;
    inline constexpr std::uint32_t real_sound_comm_base = 0x320000U;
    inline constexpr std::uint32_t real_sprite_bank_base = 0x500000U;
    inline constexpr std::uint32_t real_sprite_bank_window = 0x10U;
    inline constexpr std::uint32_t real_tile_ram_base = 0x800000U;
    inline constexpr std::uint32_t real_video_reg_base = 0x820000U;
    inline constexpr std::uint32_t real_sprite_ram_base = 0x900000U;
    inline constexpr std::uint32_t real_priority_base = 0xB00000U;
    inline constexpr std::uint32_t real_priority_window = 0x20U;
    inline constexpr std::uint32_t growl_dip_input_base = 0x300000U;
    inline constexpr std::uint32_t growl_player_input_base = 0x320000U;
    inline constexpr std::uint32_t growl_sound_comm_base = 0x400000U;
    inline constexpr std::uint32_t growl_watchdog_base = 0x340000U;
    inline constexpr std::uint32_t growl_p3_input_base = 0x508000U;
    inline constexpr std::uint32_t growl_p4_input_base = 0x50C000U;
    inline constexpr std::uint32_t solfigtr_watchdog_base = growl_watchdog_base;
    inline constexpr std::uint32_t ninjak_input_base = 0x300000U;
    inline constexpr std::uint32_t ninjak_input_window = 0x20U;
    inline constexpr std::uint32_t ninjak_sound_comm_base = 0x400000U;
    inline constexpr std::uint32_t ninjak_watchdog_base = 0x380000U;
    inline constexpr std::uint32_t ninjak_sprite_bank_base = 0x600000U;
    inline constexpr std::uint32_t dondokod_roz_ram_base = 0xA00000U;
    inline constexpr std::size_t dondokod_roz_ram_size =
        chips::video::taito_f2_video::roz_ram_bytes;
    inline constexpr std::uint32_t dondokod_roz_control_base = 0xA02000U;
    inline constexpr std::uint32_t dondokod_roz_control_window = 0x10U;
    inline constexpr std::uint32_t pulirula_sound_comm_base = 0x200000U;
    inline constexpr std::uint32_t pulirula_work_ram_base = 0x300000U;
    inline constexpr std::uint32_t pulirula_roz_ram_base = 0x400000U;
    inline constexpr std::uint32_t pulirula_roz_control_base = 0x402000U;
    inline constexpr std::uint32_t pulirula_roz_control_window = 0x10U;
    inline constexpr std::uint32_t pulirula_sprite_extension_base = 0x600000U;
    inline constexpr std::uint32_t pulirula_palette_ram_base = 0x700000U;
    inline constexpr std::uint32_t pulirula_priority_base = 0xA00000U;
    inline constexpr std::uint32_t pulirula_input_base = 0xB00000U;
    inline constexpr std::uint32_t quizhq_input_a_base = 0x500000U;
    inline constexpr std::uint32_t quizhq_input_b_base = 0x580000U;
    inline constexpr std::uint32_t quizhq_watchdog_base = quizhq_input_b_base;
    inline constexpr std::uint32_t quizhq_sound_comm_base = 0x600000U;
    inline constexpr std::uint32_t quizhq_program_text_gfx_base = 0x80000U;
    inline constexpr std::uint32_t qtorimon_input_base = 0x500000U;
    inline constexpr std::uint32_t qtorimon_sound_comm_base = 0x600000U;
    inline constexpr std::uint32_t qtorimon_program_text_gfx_base = 0x40000U;
    inline constexpr std::uint32_t qzchikyu_input_base = 0x200000U;
    inline constexpr std::uint32_t qzchikyu_sound_comm_base = 0x300000U;
    inline constexpr std::uint32_t qzchikyu_palette_ram_base = 0x400000U;
    inline constexpr std::uint32_t qzchikyu_work_ram_base = 0x500000U;
    inline constexpr std::uint32_t qzchikyu_sprite_ram_base = 0x600000U;
    inline constexpr std::uint32_t qzchikyu_tile_ram_base = 0x700000U;
    inline constexpr std::uint32_t qzchikyu_video_reg_base = 0x720000U;
    inline constexpr std::uint32_t qzquest_input_base = qzchikyu_input_base;
    inline constexpr std::uint32_t qzquest_sound_comm_base = qzchikyu_sound_comm_base;
    inline constexpr std::uint32_t qzquest_palette_ram_base = qzchikyu_palette_ram_base;
    inline constexpr std::uint32_t qzquest_work_ram_base = qzchikyu_work_ram_base;
    inline constexpr std::uint32_t qzquest_sprite_ram_base = qzchikyu_sprite_ram_base;
    inline constexpr std::uint32_t qzquest_tile_ram_base = qzchikyu_tile_ram_base;
    inline constexpr std::uint32_t qzquest_video_reg_base = qzchikyu_video_reg_base;
    inline constexpr std::uint32_t metalb_sprite_ram_base = 0x300000U;
    inline constexpr std::uint32_t metalb_tile_ram_base = 0x500000U;
    inline constexpr std::uint32_t metalb_tc0480scp_control_base = 0x530000U;
    inline constexpr std::uint32_t metalb_tc0480scp_control_window = 0x30U;
    inline constexpr std::uint32_t metalb_priority_base = 0x600000U;
    inline constexpr std::uint32_t metalb_palette_ram_base = 0x700000U;
    inline constexpr std::uint32_t metalb_input_base = 0x800000U;
    inline constexpr std::uint32_t metalb_sound_comm_base = 0x900000U;
    inline constexpr std::uint32_t footchmp_sprite_ram_base = 0x200000U;
    inline constexpr std::uint32_t footchmp_sprite_bank_base = 0x300000U;
    inline constexpr std::uint32_t footchmp_tile_ram_base = 0x400000U;
    inline constexpr std::uint32_t footchmp_tc0480scp_control_base = 0x430000U;
    inline constexpr std::uint32_t footchmp_priority_base = 0x500000U;
    inline constexpr std::uint32_t footchmp_palette_ram_base = 0x600000U;
    inline constexpr std::uint32_t footchmp_input_base = 0x700000U;
    inline constexpr std::uint32_t footchmp_input_window = 0x20U;
    inline constexpr std::uint32_t footchmp_watchdog_base = 0x800000U;
    inline constexpr std::uint32_t footchmp_sound_comm_base = 0xA00000U;
    inline constexpr std::uint32_t deadconx_sprite_ram_base = footchmp_sprite_ram_base;
    inline constexpr std::uint32_t deadconx_sprite_bank_base = footchmp_sprite_bank_base;
    inline constexpr std::uint32_t deadconx_tile_ram_base = footchmp_tile_ram_base;
    inline constexpr std::uint32_t deadconx_tc0480scp_control_base =
        footchmp_tc0480scp_control_base;
    inline constexpr std::uint32_t deadconx_priority_base = footchmp_priority_base;
    inline constexpr std::uint32_t deadconx_palette_ram_base = footchmp_palette_ram_base;
    inline constexpr std::uint32_t deadconx_input_base = footchmp_input_base;
    inline constexpr std::uint32_t deadconx_input_window = footchmp_input_window;
    inline constexpr std::uint32_t deadconx_watchdog_base = footchmp_watchdog_base;
    inline constexpr std::uint32_t deadconx_sound_comm_base = footchmp_sound_comm_base;
    inline constexpr std::uint32_t dinorex_input_base = real_input_base;
    inline constexpr std::uint32_t dinorex_input_window = input_window;
    inline constexpr std::uint32_t dinorex_sprite_extension_base = 0x400000U;
    inline constexpr std::uint32_t dinorex_sprite_extension_size = 0x1000U;
    inline constexpr std::uint32_t dinorex_palette_ram_base = 0x500000U;
    inline constexpr std::uint32_t dinorex_work_ram_base = 0x600000U;
    inline constexpr std::uint32_t dinorex_priority_base = 0x700000U;
    inline constexpr std::uint32_t dinorex_sprite_ram_base = 0x800000U;
    inline constexpr std::uint32_t dinorex_tile_ram_base = 0x900000U;
    inline constexpr std::uint32_t dinorex_video_reg_base = 0x920000U;
    inline constexpr std::uint32_t dinorex_sound_comm_base = 0xA00000U;
    inline constexpr std::uint32_t dinorex_watchdog_base = 0xB00000U;
    inline constexpr std::uint32_t thundfox_palette_ram_base = 0x100000U;
    inline constexpr std::uint32_t thundfox_input_base = 0x200000U;
    inline constexpr std::uint32_t thundfox_sound_comm_base = 0x220000U;
    inline constexpr std::uint32_t thundfox_work_ram_base = 0x300000U;
    inline constexpr std::uint32_t thundfox_tile_ram_base = 0x400000U;
    inline constexpr std::uint32_t thundfox_video_reg_base = 0x420000U;
    inline constexpr std::uint32_t thundfox_secondary_tile_ram_base = 0x500000U;
    inline constexpr std::uint32_t thundfox_secondary_video_reg_base = 0x520000U;
    inline constexpr std::uint32_t thundfox_sprite_ram_base = 0x600000U;
    inline constexpr std::uint32_t thundfox_priority_base = 0x800000U;

    inline constexpr std::uint16_t z80_fixed_rom_base = 0x0000U;
    inline constexpr std::size_t z80_fixed_rom_window = 0x4000U;
    inline constexpr std::uint16_t z80_bank_base = 0x4000U;
    inline constexpr std::size_t z80_bank_window = 0x4000U;
    inline constexpr std::uint8_t z80_sound_bank_mask = 0x03U;
    inline constexpr std::uint16_t z80_ram_base = 0xC000U;
    inline constexpr std::size_t z80_ram_size = 0x2000U;
    inline constexpr std::size_t sound_bank_state_bytes = 4U;
    inline constexpr std::size_t sound_reset_state_bytes = 40U;
    inline constexpr std::uint32_t watchdog_window = 0x02U;
    inline constexpr std::size_t watchdog_state_bytes = 48U;
    inline constexpr std::size_t main_bus_state_bytes = 64U;
    inline constexpr std::size_t palette_write_state_bytes = 40U;
    inline constexpr std::size_t io_output_reg_count = 0x20U;
    inline constexpr std::size_t io_output_state_bytes = 40U;
    inline constexpr std::size_t io_access_state_bytes = 64U;
    inline constexpr std::size_t irq_state_bytes = 48U;
    inline constexpr std::size_t board_profile_state_bytes = 96U;
    inline constexpr std::uint16_t z80_ym_base = 0xE000U;
    inline constexpr std::uint16_t z80_tc0140syt_port_addr = 0xE200U;
    inline constexpr std::uint16_t z80_tc0140syt_data_addr = 0xE201U;
    inline constexpr std::uint16_t z80_bank_reg = 0xF200U;

    inline constexpr std::uint32_t m68k_clock_hz = 12'000'000U;
    inline constexpr std::uint32_t z80_clock_hz = 4'000'000U;
    inline constexpr std::uint32_t ym2610_clock_hz = 8'000'000U;
    inline constexpr std::uint32_t frame_rate_hz = 60U;
    inline constexpr std::size_t video_reg_count = video_reg_window / 2U;

    enum class taito_f2_address_map : std::uint8_t {
        synthetic,
        dondokod,
        gunfront,
        liquidk,
        pulirula,
        quizhq,
        qtorimon,
        qzchikyu,
        qzquest,
        metalb,
        footchmp,
        deadconx,
        dinorex,
        thundfox,
        growl,
        ninjak,
        solfigtr
    };
    enum class taito_f2_sprite_policy : std::uint8_t {
        standard,
        partial_buffer,
        banked,
        extension_1,
        extension_2,
        extension_3
    };
    enum class taito_f2_sprite_active_area : std::uint8_t {
        mode_default,
        none,
        control_word_bit0,
        y_word_bit0
    };
    enum class taito_f2_sprite_buffering : std::uint8_t {
        immediate,
        full_delayed,
        partial_delayed,
        partial_delayed_thundfox,
        partial_delayed_qzchikyu
    };
    enum class taito_f2_palette_format : std::uint8_t {
        xbgr_555,
        rgbx_444,
        xrgb_555,
        rrrr_gggg_bbbb_rgbx
    };
    enum class taito_f2_text_gfx_source : std::uint8_t {
        tc0100scn_ram_2bpp,
        program_1bpp
    };
    enum class taito_f2_input_profile : std::uint8_t {
        standard,
        split_tmp82c265,
        te7750_quad
    };
    enum class taito_f2_io_profile : std::uint8_t {
        tc0220ioc,
        tc0510nio,
        te7750,
        tmp82c265
    };
    enum class taito_f2_palette_profile : std::uint8_t {
        tc0110pcr_tc0070rgb,
        tc0260dar
    };
    enum class taito_f2_priority_profile : std::uint8_t {
        none,
        tc0360pri
    };
    enum class taito_f2_sprite_chip_pair : std::uint8_t {
        tc0200obj_tc0210fbc,
        tc0540obn_tc0520tbc
    };
    enum class taito_f2_sound_comm_chip : std::uint8_t {
        tc0140syt,
        tc0530syc
    };
    enum class taito_f2_video_profile : std::uint8_t {
        tc0100scn,
        dual_tc0100scn,
        tc0100scn_tc0280grd,
        tc0100scn_tc0430grw,
        tc0480scp
    };
    enum class taito_f2_tc0480scp_profile : std::uint8_t {
        none,
        metalb,
        footchmp,
        deadconx
    };
    enum class taito_f2_aux_profile : std::uint8_t {
        none,
        tc0030cmd_cchip,
        rtc,
        printer,
        rtc_printer
    };

    struct taito_f2_board_params final {
        bool vertical{};
        std::uint8_t players{2U};
        taito_f2_address_map address_map{taito_f2_address_map::synthetic};
        taito_f2_sprite_policy sprite_policy{taito_f2_sprite_policy::standard};
        taito_f2_sprite_active_area sprite_active_area{
            taito_f2_sprite_active_area::mode_default};
        taito_f2_sprite_buffering sprite_buffering{
            taito_f2_sprite_buffering::immediate};
        taito_f2_palette_format palette_format{taito_f2_palette_format::xbgr_555};
        taito_f2_text_gfx_source text_gfx_source{
            taito_f2_text_gfx_source::tc0100scn_ram_2bpp};
        taito_f2_input_profile input_profile{taito_f2_input_profile::standard};
        taito_f2_io_profile io_profile{taito_f2_io_profile::tc0220ioc};
        taito_f2_palette_profile palette_profile{
            taito_f2_palette_profile::tc0110pcr_tc0070rgb};
        taito_f2_priority_profile priority_profile{taito_f2_priority_profile::tc0360pri};
        taito_f2_sprite_chip_pair sprite_chip_pair{
            taito_f2_sprite_chip_pair::tc0200obj_tc0210fbc};
        taito_f2_sound_comm_chip sound_comm_chip{taito_f2_sound_comm_chip::tc0140syt};
        taito_f2_video_profile video_profile{taito_f2_video_profile::tc0100scn};
        taito_f2_tc0480scp_profile tc0480scp_profile{
            taito_f2_tc0480scp_profile::none};
        taito_f2_aux_profile aux_profile{taito_f2_aux_profile::none};
        bool hardware_profiles_explicit{};
        std::uint8_t vblank_irq_level{5U};
        std::uint8_t sprite_dma_irq_level{6U};
        std::uint32_t text_gfx_base{
            chips::video::taito_f2_video::text_gfx_base};
        int tc0100scn_bg_x_offset{};
        int tc0100scn_text_x_offset{};
        int tc0100scn_text_y_origin{
            chips::video::taito_f2_video::tc0100scn_default_text_y_origin};
        int tc0100scn_positive_text_y_origin{
            chips::video::taito_f2_video::tc0100scn_positive_text_y_origin};
        int roz_x_offset{};
        int roz_y_offset{};
        int sprite_hide_pixels{};
        int sprite_flip_hide_pixels{};
        std::optional<std::uint32_t> sprite_extension_base;
        std::optional<std::uint32_t> sprite_extension_size;
    };

    [[nodiscard]] taito_f2_board_params
    board_params_from_decl(const common::rom_set_decl& decl) noexcept;

    struct taito_f2_system final {
        chips::cpu::m68000 main_cpu;
        chips::cpu::z80 sound_cpu;
        chips::video::taito_f2_video video;
        chips::audio::ym2610 opnb;
        chips::bus_controller::tc0140syt sound_comm;
        topology::bus main_bus{24U, topology::endianness::big};
        topology::bus sound_bus{16U, topology::endianness::little};

        common::rom_set_image roms;
        taito_f2_board_params params;

        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, palette_ram_size> palette_ram{};
        std::array<std::uint8_t, palette_write_state_bytes> palette_write_state{};
        std::array<std::uint8_t, tile_ram_size> tile_ram{};
        std::array<std::uint8_t, tile_ram_size> tile_ram_secondary{};
        std::array<std::uint8_t, sprite_ram_size> sprite_ram{};
        std::array<std::uint8_t, sprite_extension_ram_size> sprite_extension_ram{};
        std::array<std::uint8_t, dondokod_roz_ram_size> roz_ram{};
        std::array<std::uint8_t, z80_ram_size> z80_ram{};
        std::array<std::uint16_t, video_reg_count> video_regs{};
        std::array<std::uint16_t, video_reg_count> secondary_video_regs{};
        std::array<std::uint16_t, chips::video::taito_f2_video::sprite_bank_count>
            sprite_bank_regs{};
        std::array<std::uint16_t, chips::video::taito_f2_video::priority_reg_count>
            priority_regs{};
        std::array<std::uint16_t, chips::video::taito_f2_video::roz_control_reg_count>
            roz_control_regs{};
        std::array<std::uint16_t, chips::video::taito_f2_video::tc0480scp_control_reg_count>
            tc0480scp_control_regs{};

        std::uint8_t input_p1{0xFFU};
        std::uint8_t input_p2{0xFFU};
        std::uint8_t input_p3{0xFFU};
        std::uint8_t input_p4{0xFFU};
        std::uint8_t input_system{0xFFU};
        std::uint8_t input_coin_extension{0xFFU};
        std::uint8_t dip_a{0xFFU};
        std::uint8_t dip_b{0xFFU};
        std::array<std::uint8_t, io_output_reg_count> io_output_regs{};
        std::array<std::uint8_t, io_output_state_bytes> io_output_state{};
        std::array<std::uint8_t, io_access_state_bytes> io_access_state{};
        std::uint8_t io_output_latch{};
        std::array<std::uint32_t, 4> coin_counters{};
        std::array<bool, 4> coin_counter_lines{};
        std::array<bool, 4> coin_lockouts{};
        std::uint32_t io_output_write_count{};
        std::uint32_t last_io_output_address{};
        std::uint8_t last_io_output_value{};
        std::uint32_t io_input_read_count{};
        std::uint32_t io_input_dip_read_count{};
        std::uint32_t io_input_service_read_count{};
        std::uint32_t io_access_read_even_count{};
        std::uint32_t io_access_read_odd_count{};
        std::uint32_t io_access_write_even_count{};
        std::uint32_t io_access_write_odd_count{};
        std::uint32_t io_access_inferred_read_pair_count{};
        std::uint32_t io_access_inferred_write_pair_count{};
        std::uint32_t last_io_access_address{};
        std::uint8_t last_io_access_value{};
        std::uint8_t last_io_access_window{};
        bool last_io_access_write{};
        bool last_io_access_pair_inferred{};
        std::uint32_t previous_io_access_address{};
        std::uint8_t previous_io_access_window{};
        bool previous_io_access_write{};
        bool previous_io_access_valid{};
        std::uint32_t palette_write_count{};
        std::uint32_t last_palette_write_address{};
        std::uint8_t last_palette_write_value{};
        std::uint16_t last_palette_word{};
        std::uint16_t last_palette_index{};
        std::uint32_t last_palette_color{};
        std::uint32_t palette_read_count{};
        std::uint32_t last_palette_read_address{};
        std::uint8_t last_palette_read_value{};
        std::uint16_t last_palette_read_word{};
        std::uint16_t last_palette_read_index{};
        std::uint32_t last_palette_read_color{};

        std::uint8_t& latch_68k_to_z80{sound_comm.command_latch(0U)};
        std::uint8_t& latch_z80_to_68k{sound_comm.reply_latch(0U)};
        std::uint8_t& latch_68k_to_z80_port2{sound_comm.command_latch(1U)};
        std::uint8_t& latch_z80_to_68k_port2{sound_comm.reply_latch(1U)};
        bool& sound_latch_pending{sound_comm.command_pending(0U)};
        bool& sound_reply_pending{sound_comm.reply_pending(0U)};
        bool& sound_latch_pending_port2{sound_comm.command_pending(1U)};
        bool& sound_reply_pending_port2{sound_comm.reply_pending(1U)};
        std::uint8_t sound_bank{};
        std::array<std::uint8_t, sound_bank_state_bytes> sound_bank_state{};
        std::uint8_t& sound_main_port{sound_comm.main_port()};
        std::uint8_t& sound_z80_port{sound_comm.sound_port()};
        bool& sound_main_read_high{sound_comm.main_read_high()};
        bool& sound_z80_read_high{sound_comm.sound_read_high()};
        bool& sound_main_write_high{sound_comm.main_write_high()};
        bool& sound_z80_write_high{sound_comm.sound_write_high()};
        std::uint32_t sound_rom_size{};
        std::array<std::uint8_t, sound_reset_state_bytes> sound_reset_state{};
        std::uint32_t sound_reset_control_write_count{};
        std::uint32_t sound_reset_assert_count{};
        std::uint32_t sound_reset_release_count{};
        std::uint32_t last_sound_reset_control_address{};
        std::uint8_t last_sound_reset_control_value{};
        std::array<std::uint8_t, watchdog_state_bytes> watchdog_state{};
        std::uint32_t watchdog_write_count{};
        std::uint32_t watchdog_confirmed_write_count{};
        std::uint32_t watchdog_suspect_write_count{};
        std::uint32_t last_watchdog_address{};
        std::uint8_t last_watchdog_value{};
        std::uint8_t last_watchdog_window{};
        bool last_watchdog_confirmed{};
        std::array<std::uint8_t, main_bus_state_bytes> main_bus_state{};
        std::uint32_t main_bus_read_count{};
        std::uint32_t main_bus_write_count{};
        std::uint32_t main_bus_open_bus_read_count{};
        std::uint32_t main_bus_unmapped_write_count{};
        std::uint32_t main_bus_odd_access_count{};
        std::uint32_t main_bus_inferred_word_pair_count{};
        std::uint32_t last_main_bus_address{};
        std::uint8_t last_main_bus_value{};
        bool last_main_bus_write{};
        bool last_main_bus_mapped{};
        bool last_main_bus_open_bus{};
        bool last_main_bus_pair_inferred{};
        std::uint32_t previous_main_bus_address{};
        std::uint8_t previous_main_bus_value{};
        bool previous_main_bus_write{};
        bool previous_main_bus_mapped{};
        bool previous_main_bus_valid{};

        std::uint64_t vblank_irq_raised{};
        std::uint64_t vblank_irq_acked{};
        std::uint8_t last_vblank_irq_level{};
        std::uint8_t last_irq_ack_level{};
        std::uint64_t sprite_dma_irq_raised{};
        std::uint64_t sprite_dma_irq_acked{};
        std::uint8_t last_sprite_dma_irq_level{};
        std::uint8_t last_sprite_dma_irq_ack_level{};
        bool vblank_irq_pending{};
        bool sprite_dma_irq_pending{};
        std::array<std::uint8_t, irq_state_bytes> irq_state{};
        std::array<std::uint8_t, board_profile_state_bytes> board_profile_state{};

        explicit taito_f2_system(common::rom_set_image image,
                                 taito_f2_board_params board_params = {});

        void run_frame();
        void run_palette_readback_probe() noexcept;
        void set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system,
                        std::uint8_t dsw_a, std::uint8_t dsw_b) noexcept {
            input_p1 = p1;
            input_p2 = p2;
            input_system = system;
            input_coin_extension = 0xFFU;
            dip_a = dsw_a;
            dip_b = dsw_b;
        }
        void sync_sound_irq() noexcept;
        [[nodiscard]] std::uint8_t z80_sound_bank_page() const noexcept;
        [[nodiscard]] std::uint32_t z80_sound_bank_page_count() const noexcept;
        [[nodiscard]] bool z80_sound_bank_page_valid() const noexcept;
        void save_state(chips::state_writer& writer) const;
        void load_state(chips::state_reader& reader);

      private:
        void run_cpus(std::uint64_t cpu_cycles);
        void pulse_sound_command_nmi() noexcept;
        void configure_video_variant() noexcept;
        void push_video_regs_to_chip() noexcept;
        void record_io_output_write(std::uint32_t window_base,
                                    std::uint32_t address,
                                    std::uint8_t value) noexcept;
        void update_io_output_state() noexcept;
        void record_io_input_read(std::uint32_t window_base,
                                  std::uint32_t address,
                                  std::uint8_t value,
                                  bool dip_read,
                                  bool service_read) noexcept;
        void record_io_access(std::uint32_t window_base,
                              std::uint32_t address,
                              std::uint8_t value,
                              bool write,
                              bool dip_read,
                              bool service_read) noexcept;
        void update_io_access_state() noexcept;
        void record_palette_write(std::uint32_t window_base,
                                  std::uint32_t address,
                                  std::uint8_t value) noexcept;
        void record_palette_read(std::uint32_t window_base,
                                 std::uint32_t address,
                                 std::uint8_t value) noexcept;
        void update_palette_write_state() noexcept;
        void update_irq_state() noexcept;
        void update_main_irq_line() noexcept;
        void raise_vblank_irq() noexcept;
        void raise_sprite_dma_irq() noexcept;
        void acknowledge_main_irq(std::uint8_t level) noexcept;
        void board_latch_sprites(bool assert_dma_irq = true) noexcept;
        void update_board_profile_state() noexcept;
        void write_sound_bank(std::uint8_t value) noexcept;
        void update_sound_bank_state() noexcept;
        void write_sound_reset_control(std::uint32_t address, std::uint8_t value) noexcept;
        void update_sound_reset_state() noexcept;
        void record_watchdog_write(std::uint32_t address,
                                   std::uint8_t value,
                                   std::uint8_t window_id,
                                   bool confirmed) noexcept;
        void record_integrated_io_watchdog_write(std::uint32_t window_base,
                                                 std::uint32_t address,
                                                 std::uint8_t value) noexcept;
        void update_watchdog_state() noexcept;
        void record_main_bus_access(const topology::access_event& event) noexcept;
        void update_main_bus_state() noexcept;
        [[nodiscard]] bool main_bus_address_mapped(std::uint32_t address,
                                                   bool write) const noexcept;
        [[nodiscard]] std::uint32_t z80_bank_rom_base() const noexcept;
        [[nodiscard]] chips::video::taito_f2_video::palette_format
        active_video_palette_format() const noexcept;
        [[nodiscard]] bool uses_real_map() const noexcept;

        std::uint64_t z80_cycle_accum_{};
        std::uint64_t ym_cycle_accum_{};
    };

    [[nodiscard]] common::rom_set_decl taito_f2_rom_skeleton(std::string set_name);
    [[nodiscard]] std::unique_ptr<taito_f2_system>
    assemble_taito_f2(common::rom_set_image image, taito_f2_board_params board_params = {});

} // namespace mnemos::manifests::taito_f2
