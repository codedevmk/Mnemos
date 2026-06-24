#pragma once

#include "bus.hpp"
#include "m68000.hpp"
#include "rom_set.hpp"
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
    inline constexpr std::uint32_t growl_p3_input_base = 0x508000U;
    inline constexpr std::uint32_t growl_p4_input_base = 0x50C000U;
    inline constexpr std::uint32_t ninjak_input_base = 0x300000U;
    inline constexpr std::uint32_t ninjak_input_window = 0x20U;
    inline constexpr std::uint32_t ninjak_sound_comm_base = 0x400000U;
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
    inline constexpr std::uint32_t quizhq_sound_comm_base = 0x600000U;
    inline constexpr std::uint32_t qtorimon_input_base = 0x500000U;
    inline constexpr std::uint32_t qtorimon_sound_comm_base = 0x600000U;
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
    inline constexpr std::uint16_t z80_ram_base = 0x8000U;
    inline constexpr std::size_t z80_ram_size = 0x1000U;
    inline constexpr std::uint16_t z80_ym_base = 0x9000U;
    inline constexpr std::uint16_t z80_latch_addr = 0xA001U;
    inline constexpr std::uint16_t z80_reply_addr = 0xB001U;
    inline constexpr std::uint16_t z80_bank_reg = 0xC000U;

    inline constexpr std::uint32_t m68k_clock_hz = 16'000'000U;
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
        xrgb_555
    };

    struct taito_f2_board_params final {
        bool vertical{};
        taito_f2_address_map address_map{taito_f2_address_map::synthetic};
        taito_f2_sprite_policy sprite_policy{taito_f2_sprite_policy::standard};
        taito_f2_sprite_active_area sprite_active_area{
            taito_f2_sprite_active_area::mode_default};
        taito_f2_sprite_buffering sprite_buffering{
            taito_f2_sprite_buffering::immediate};
        taito_f2_palette_format palette_format{taito_f2_palette_format::xbgr_555};
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
        topology::bus main_bus{24U, topology::endianness::big};
        topology::bus sound_bus{16U, topology::endianness::little};

        common::rom_set_image roms;
        taito_f2_board_params params;

        std::array<std::uint8_t, work_ram_size> work_ram{};
        std::array<std::uint8_t, palette_ram_size> palette_ram{};
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
        std::uint8_t dip_a{0xFFU};
        std::uint8_t dip_b{0xFFU};

        std::uint8_t latch_68k_to_z80{0xFFU};
        std::uint8_t latch_z80_to_68k{0xFFU};
        bool sound_latch_pending{};
        std::uint8_t sound_bank{};
        std::uint32_t sound_rom_size{};

        std::uint64_t vblank_irq_raised{};
        std::uint64_t vblank_irq_acked{};

        explicit taito_f2_system(common::rom_set_image image,
                                 taito_f2_board_params board_params = {});

        void run_frame();
        void set_inputs(std::uint8_t p1, std::uint8_t p2, std::uint8_t system,
                        std::uint8_t dsw_a, std::uint8_t dsw_b) noexcept {
            input_p1 = p1;
            input_p2 = p2;
            input_system = system;
            dip_a = dsw_a;
            dip_b = dsw_b;
        }
        void sync_sound_irq() noexcept;

      private:
        void run_cpus(std::uint64_t cpu_cycles);
        void configure_video_variant() noexcept;
        void push_video_regs_to_chip() noexcept;
        [[nodiscard]] std::uint32_t z80_bank_rom_base() const noexcept;
        [[nodiscard]] bool uses_real_map() const noexcept;

        std::uint64_t z80_cycle_accum_{};
        std::uint64_t ym_cycle_accum_{};
    };

    [[nodiscard]] common::rom_set_decl taito_f2_rom_skeleton(std::string set_name);
    [[nodiscard]] std::unique_ptr<taito_f2_system>
    assemble_taito_f2(common::rom_set_image image, taito_f2_board_params board_params = {});

} // namespace mnemos::manifests::taito_f2
