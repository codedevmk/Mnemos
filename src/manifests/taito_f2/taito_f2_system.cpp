#include "taito_f2_system.hpp"

#include <algorithm>
#include <span>
#include <string_view>
#include <utility>

namespace mnemos::manifests::taito_f2 {

    namespace {
        [[nodiscard]] std::vector<std::uint8_t>&
        pinned_region(common::rom_set_image& image, std::string_view name, std::size_t size) {
            auto& bytes = image.regions[std::string(name)];
            if (bytes.size() < size) {
                bytes.resize(size, 0xFFU);
            }
            return bytes;
        }

        [[nodiscard]] std::uint16_t merge_word(std::uint16_t word, std::uint32_t address,
                                               std::uint8_t value) noexcept {
            return (address & 1U) == 0U
                       ? static_cast<std::uint16_t>((word & 0x00FFU) |
                                                    (static_cast<std::uint16_t>(value) << 8U))
                       : static_cast<std::uint16_t>((word & 0xFF00U) | value);
        }

        [[nodiscard]] bool has_roz_tilemap(taito_f2_address_map map) noexcept {
            return map == taito_f2_address_map::dondokod ||
                   map == taito_f2_address_map::pulirula;
        }

        [[nodiscard]] bool uses_tc0480scp(taito_f2_address_map map) noexcept {
            return map == taito_f2_address_map::metalb ||
                   map == taito_f2_address_map::footchmp ||
                   map == taito_f2_address_map::deadconx;
        }

        [[nodiscard]] bool uses_dual_tc0100scn(taito_f2_address_map map) noexcept {
            return map == taito_f2_address_map::thundfox;
        }

        [[nodiscard]] bool uses_shifted_quiz_map(taito_f2_address_map map) noexcept {
            return map == taito_f2_address_map::qzchikyu ||
                   map == taito_f2_address_map::qzquest;
        }

        [[nodiscard]] bool has_priority_window(taito_f2_address_map map) noexcept {
            return map != taito_f2_address_map::qtorimon && !uses_shifted_quiz_map(map);
        }

        [[nodiscard]] std::uint32_t work_ram_address(taito_f2_address_map map) noexcept {
            if (map == taito_f2_address_map::dinorex) {
                return dinorex_work_ram_base;
            }
            if (map == taito_f2_address_map::thundfox) {
                return thundfox_work_ram_base;
            }
            if (uses_shifted_quiz_map(map)) {
                return qzchikyu_work_ram_base;
            }
            return map == taito_f2_address_map::pulirula ? pulirula_work_ram_base
                                                         : work_ram_base;
        }

        [[nodiscard]] std::uint32_t palette_ram_address(taito_f2_address_map map) noexcept {
            if (map == taito_f2_address_map::metalb) {
                return metalb_palette_ram_base;
            }
            if (map == taito_f2_address_map::footchmp) {
                return footchmp_palette_ram_base;
            }
            if (map == taito_f2_address_map::deadconx) {
                return deadconx_palette_ram_base;
            }
            if (map == taito_f2_address_map::dinorex) {
                return dinorex_palette_ram_base;
            }
            if (map == taito_f2_address_map::thundfox) {
                return thundfox_palette_ram_base;
            }
            if (uses_shifted_quiz_map(map)) {
                return qzchikyu_palette_ram_base;
            }
            return map == taito_f2_address_map::pulirula ? pulirula_palette_ram_base
                                                         : palette_ram_base;
        }

        [[nodiscard]] std::uint32_t sound_comm_address(taito_f2_address_map map) noexcept {
            if (map == taito_f2_address_map::ninjak) {
                return ninjak_sound_comm_base;
            }
            if (map == taito_f2_address_map::growl ||
                map == taito_f2_address_map::solfigtr) {
                return growl_sound_comm_base;
            }
            if (map == taito_f2_address_map::metalb) {
                return metalb_sound_comm_base;
            }
            if (map == taito_f2_address_map::footchmp) {
                return footchmp_sound_comm_base;
            }
            if (map == taito_f2_address_map::deadconx) {
                return deadconx_sound_comm_base;
            }
            if (map == taito_f2_address_map::dinorex) {
                return dinorex_sound_comm_base;
            }
            if (map == taito_f2_address_map::thundfox) {
                return thundfox_sound_comm_base;
            }
            if (map == taito_f2_address_map::quizhq) {
                return quizhq_sound_comm_base;
            }
            if (map == taito_f2_address_map::qtorimon) {
                return qtorimon_sound_comm_base;
            }
            if (uses_shifted_quiz_map(map)) {
                return qzchikyu_sound_comm_base;
            }
            if (map == taito_f2_address_map::pulirula) {
                return pulirula_sound_comm_base;
            }
            return real_sound_comm_base;
        }

        [[nodiscard]] std::uint32_t priority_address(taito_f2_address_map map) noexcept {
            if (map == taito_f2_address_map::metalb) {
                return metalb_priority_base;
            }
            if (map == taito_f2_address_map::footchmp) {
                return footchmp_priority_base;
            }
            if (map == taito_f2_address_map::deadconx) {
                return deadconx_priority_base;
            }
            if (map == taito_f2_address_map::dinorex) {
                return dinorex_priority_base;
            }
            if (map == taito_f2_address_map::thundfox) {
                return thundfox_priority_base;
            }
            return map == taito_f2_address_map::pulirula ? pulirula_priority_base
                                                         : real_priority_base;
        }

        [[nodiscard]] std::uint32_t input_address(taito_f2_address_map map,
                                                  bool real_map) noexcept {
            if (map == taito_f2_address_map::metalb) {
                return metalb_input_base;
            }
            if (map == taito_f2_address_map::footchmp) {
                return footchmp_input_base;
            }
            if (map == taito_f2_address_map::deadconx) {
                return deadconx_input_base;
            }
            if (map == taito_f2_address_map::dinorex) {
                return dinorex_input_base;
            }
            if (map == taito_f2_address_map::thundfox) {
                return thundfox_input_base;
            }
            if (map == taito_f2_address_map::pulirula) {
                return pulirula_input_base;
            }
            if (map == taito_f2_address_map::qtorimon) {
                return qtorimon_input_base;
            }
            if (uses_shifted_quiz_map(map)) {
                return qzchikyu_input_base;
            }
            return real_map ? real_input_base : input_base;
        }

        [[nodiscard]] std::uint32_t input_window_size(taito_f2_address_map map) noexcept {
            if (map == taito_f2_address_map::footchmp) {
                return footchmp_input_window;
            }
            if (map == taito_f2_address_map::deadconx) {
                return deadconx_input_window;
            }
            if (map == taito_f2_address_map::dinorex) {
                return dinorex_input_window;
            }
            return input_window;
        }

        [[nodiscard]] std::uint32_t tile_ram_address(taito_f2_address_map map,
                                                     bool real_map) noexcept {
            if (map == taito_f2_address_map::metalb) {
                return metalb_tile_ram_base;
            }
            if (map == taito_f2_address_map::footchmp) {
                return footchmp_tile_ram_base;
            }
            if (map == taito_f2_address_map::deadconx) {
                return deadconx_tile_ram_base;
            }
            if (map == taito_f2_address_map::dinorex) {
                return dinorex_tile_ram_base;
            }
            if (map == taito_f2_address_map::thundfox) {
                return thundfox_tile_ram_base;
            }
            if (uses_shifted_quiz_map(map)) {
                return qzchikyu_tile_ram_base;
            }
            return real_map ? real_tile_ram_base : tile_ram_base;
        }

        [[nodiscard]] std::uint32_t sprite_ram_address(taito_f2_address_map map,
                                                       bool real_map) noexcept {
            if (map == taito_f2_address_map::metalb) {
                return metalb_sprite_ram_base;
            }
            if (map == taito_f2_address_map::footchmp) {
                return footchmp_sprite_ram_base;
            }
            if (map == taito_f2_address_map::deadconx) {
                return deadconx_sprite_ram_base;
            }
            if (map == taito_f2_address_map::dinorex) {
                return dinorex_sprite_ram_base;
            }
            if (map == taito_f2_address_map::thundfox) {
                return thundfox_sprite_ram_base;
            }
            if (uses_shifted_quiz_map(map)) {
                return qzchikyu_sprite_ram_base;
            }
            return real_map ? real_sprite_ram_base : sprite_ram_base;
        }

        [[nodiscard]] std::uint32_t video_reg_address(taito_f2_address_map map,
                                                      bool real_map) noexcept {
            if (map == taito_f2_address_map::footchmp) {
                return footchmp_tc0480scp_control_base;
            }
            if (map == taito_f2_address_map::deadconx) {
                return deadconx_tc0480scp_control_base;
            }
            if (map == taito_f2_address_map::dinorex) {
                return dinorex_video_reg_base;
            }
            if (map == taito_f2_address_map::thundfox) {
                return thundfox_video_reg_base;
            }
            if (uses_shifted_quiz_map(map)) {
                return qzchikyu_video_reg_base;
            }
            return map == taito_f2_address_map::metalb
                       ? metalb_tc0480scp_control_base
                       : (real_map ? real_video_reg_base : video_reg_base);
        }

        [[nodiscard]] std::uint32_t sprite_bank_address(taito_f2_address_map map) noexcept {
            if (map == taito_f2_address_map::footchmp) {
                return footchmp_sprite_bank_base;
            }
            if (map == taito_f2_address_map::deadconx) {
                return deadconx_sprite_bank_base;
            }
            return map == taito_f2_address_map::ninjak ? ninjak_sprite_bank_base
                                                       : real_sprite_bank_base;
        }

        [[nodiscard]] std::uint32_t roz_ram_address(taito_f2_address_map map) noexcept {
            return map == taito_f2_address_map::pulirula ? pulirula_roz_ram_base
                                                         : dondokod_roz_ram_base;
        }

        [[nodiscard]] std::uint32_t roz_control_address(taito_f2_address_map map) noexcept {
            return map == taito_f2_address_map::pulirula ? pulirula_roz_control_base
                                                         : dondokod_roz_control_base;
        }
    } // namespace

    taito_f2_board_params board_params_from_decl(const common::rom_set_decl& decl) noexcept {
        taito_f2_board_params params{
            .vertical = decl.orientation == common::screen_orientation::vertical};
        if (decl.taito_f2_map == "dondokod") {
            params.address_map = taito_f2_address_map::dondokod;
        } else if (decl.taito_f2_map == "gunfront") {
            params.address_map = taito_f2_address_map::gunfront;
        } else if (decl.taito_f2_map == "liquidk") {
            params.address_map = taito_f2_address_map::liquidk;
        } else if (decl.taito_f2_map == "pulirula") {
            params.address_map = taito_f2_address_map::pulirula;
        } else if (decl.taito_f2_map == "quizhq") {
            params.address_map = taito_f2_address_map::quizhq;
        } else if (decl.taito_f2_map == "qtorimon") {
            params.address_map = taito_f2_address_map::qtorimon;
        } else if (decl.taito_f2_map == "qzchikyu") {
            params.address_map = taito_f2_address_map::qzchikyu;
        } else if (decl.taito_f2_map == "qzquest") {
            params.address_map = taito_f2_address_map::qzquest;
        } else if (decl.taito_f2_map == "metalb") {
            params.address_map = taito_f2_address_map::metalb;
        } else if (decl.taito_f2_map == "footchmp") {
            params.address_map = taito_f2_address_map::footchmp;
        } else if (decl.taito_f2_map == "deadconx") {
            params.address_map = taito_f2_address_map::deadconx;
        } else if (decl.taito_f2_map == "dinorex") {
            params.address_map = taito_f2_address_map::dinorex;
        } else if (decl.taito_f2_map == "thundfox") {
            params.address_map = taito_f2_address_map::thundfox;
        } else if (decl.taito_f2_map == "growl") {
            params.address_map = taito_f2_address_map::growl;
        } else if (decl.taito_f2_map == "ninjak") {
            params.address_map = taito_f2_address_map::ninjak;
        } else if (decl.taito_f2_map == "solfigtr") {
            params.address_map = taito_f2_address_map::solfigtr;
        }
        if (decl.taito_f2_sprite_policy == "partial_buffer") {
            params.sprite_policy = taito_f2_sprite_policy::partial_buffer;
        } else if (decl.taito_f2_sprite_policy == "banked") {
            params.sprite_policy = taito_f2_sprite_policy::banked;
        } else if (decl.taito_f2_sprite_policy == "extension_1") {
            params.sprite_policy = taito_f2_sprite_policy::extension_1;
        } else if (decl.taito_f2_sprite_policy == "extension_2") {
            params.sprite_policy = taito_f2_sprite_policy::extension_2;
        } else if (decl.taito_f2_sprite_policy == "extension_3") {
            params.sprite_policy = taito_f2_sprite_policy::extension_3;
        }
        if (decl.taito_f2_sprite_active_area == "none") {
            params.sprite_active_area = taito_f2_sprite_active_area::none;
        } else if (decl.taito_f2_sprite_active_area == "control_word_bit0") {
            params.sprite_active_area = taito_f2_sprite_active_area::control_word_bit0;
        } else if (decl.taito_f2_sprite_active_area == "y_word_bit0") {
            params.sprite_active_area = taito_f2_sprite_active_area::y_word_bit0;
        }
        if (decl.taito_f2_sprite_buffering == "full_delayed") {
            params.sprite_buffering = taito_f2_sprite_buffering::full_delayed;
        } else if (decl.taito_f2_sprite_buffering == "partial_delayed") {
            params.sprite_buffering = taito_f2_sprite_buffering::partial_delayed;
        } else if (decl.taito_f2_sprite_buffering == "partial_delayed_thundfox") {
            params.sprite_buffering = taito_f2_sprite_buffering::partial_delayed_thundfox;
        } else if (decl.taito_f2_sprite_buffering == "partial_delayed_qzchikyu") {
            params.sprite_buffering = taito_f2_sprite_buffering::partial_delayed_qzchikyu;
        }
        if (decl.taito_f2_palette_format == "rgbx_444") {
            params.palette_format = taito_f2_palette_format::rgbx_444;
        } else if (decl.taito_f2_palette_format == "xrgb_555") {
            params.palette_format = taito_f2_palette_format::xrgb_555;
        }
        if (decl.taito_f2_sprite_hide_pixels.has_value()) {
            params.sprite_hide_pixels = *decl.taito_f2_sprite_hide_pixels;
        }
        if (decl.taito_f2_sprite_flip_hide_pixels.has_value()) {
            params.sprite_flip_hide_pixels = *decl.taito_f2_sprite_flip_hide_pixels;
        }
        params.sprite_extension_base = decl.taito_f2_sprite_extension_base;
        params.sprite_extension_size = decl.taito_f2_sprite_extension_size;
        return params;
    }

    taito_f2_system::taito_f2_system(common::rom_set_image image,
                                     taito_f2_board_params board_params)
        : roms(std::move(image)), params(board_params) {
        auto& program = pinned_region(roms, "maincpu", main_rom_size);
        main_bus.map_rom(program_rom_base, std::span<const std::uint8_t>(program), 0);
        main_bus.map_ram(work_ram_address(params.address_map), work_ram, 1);
        main_bus.map_ram(palette_ram_address(params.address_map), palette_ram, 1);
        const bool real_map = uses_real_map();
        const std::uint32_t tile_base = tile_ram_address(params.address_map, real_map);
        const std::uint32_t sprite_base = sprite_ram_address(params.address_map, real_map);
        const std::uint32_t video_base = video_reg_address(params.address_map, real_map);
        main_bus.map_ram(tile_base, tile_ram, 1);
        main_bus.map_ram(sprite_base, sprite_ram, 1);
        if (uses_dual_tc0100scn(params.address_map)) {
            main_bus.map_ram(thundfox_secondary_tile_ram_base, tile_ram_secondary, 1);
        }
        if (has_roz_tilemap(params.address_map)) {
            main_bus.map_ram(roz_ram_address(params.address_map), roz_ram, 1);
        }
        if (params.sprite_extension_base.has_value()) {
            const std::uint32_t configured_size = params.sprite_extension_size.value_or(
                static_cast<std::uint32_t>(sprite_extension_ram.size()));
            const std::size_t window_size =
                static_cast<std::size_t>(std::min<std::uint32_t>(
                    configured_size, static_cast<std::uint32_t>(sprite_extension_ram.size())));
            main_bus.map_ram(*params.sprite_extension_base,
                             std::span<std::uint8_t>(sprite_extension_ram).first(window_size),
                             2);
        }

        if (real_map) {
            if (params.sprite_policy == taito_f2_sprite_policy::banked) {
                const std::uint32_t sprite_bank_base = sprite_bank_address(params.address_map);
                main_bus.map_mmio(
                    sprite_bank_base, real_sprite_bank_window,
                    [this, sprite_bank_base](std::uint32_t address) -> std::uint8_t {
                        const std::size_t index = (address - sprite_bank_base) >> 1U;
                        if (index >= sprite_bank_regs.size()) {
                            return 0xFFU;
                        }
                        const std::uint16_t word = sprite_bank_regs[index];
                        return (address & 1U) == 0U ? static_cast<std::uint8_t>(word >> 8U)
                                                    : static_cast<std::uint8_t>(word);
                    },
                    [this, sprite_bank_base](std::uint32_t address, std::uint8_t value) {
                        const std::size_t index = (address - sprite_bank_base) >> 1U;
                        if (index >= sprite_bank_regs.size()) {
                            return;
                        }
                        sprite_bank_regs[index] = merge_word(sprite_bank_regs[index], address,
                                                             value);
                        if ((address & 1U) != 0U) {
                            video.write_sprite_bank_register(
                                static_cast<std::uint32_t>(index), sprite_bank_regs[index]);
                        }
                    },
                    1);
            }
            const std::uint32_t sound_comm_base = sound_comm_address(params.address_map);
            main_bus.map_mmio(
                sound_comm_base, 0x04U,
                [this, sound_comm_base](std::uint32_t address) -> std::uint8_t {
                    const std::uint32_t offset = address - sound_comm_base;
                    return offset >= 0x02U ? latch_z80_to_68k : 0xFFU;
                },
                [this, sound_comm_base](std::uint32_t address, std::uint8_t value) {
                    const std::uint32_t offset = address - sound_comm_base;
                    if (offset >= 0x02U) {
                        latch_68k_to_z80 = value;
                        sound_latch_pending = true;
                        sync_sound_irq();
                    }
                },
                1);
            if (has_priority_window(params.address_map)) {
                const std::uint32_t priority_base = priority_address(params.address_map);
                main_bus.map_mmio(
                    priority_base, real_priority_window,
                    [this, priority_base](std::uint32_t address) -> std::uint8_t {
                        const std::size_t index = (address - priority_base) >> 1U;
                        if (index >= priority_regs.size()) {
                            return 0xFFU;
                        }
                        const std::uint16_t word = priority_regs[index];
                        return (address & 1U) == 0U ? static_cast<std::uint8_t>(word >> 8U)
                                                    : static_cast<std::uint8_t>(word);
                    },
                    [this, priority_base](std::uint32_t address, std::uint8_t value) {
                        const std::size_t index = (address - priority_base) >> 1U;
                        if (index >= priority_regs.size()) {
                            return;
                        }
                        priority_regs[index] = merge_word(priority_regs[index], address, value);
                        if ((address & 1U) != 0U) {
                            video.write_priority_register(static_cast<std::uint32_t>(index),
                                                          priority_regs[index]);
                        }
                    },
                    1);
            }
            if (has_roz_tilemap(params.address_map)) {
                const std::uint32_t roz_control_base =
                    roz_control_address(params.address_map);
                main_bus.map_mmio(
                    roz_control_base, dondokod_roz_control_window,
                    [this, roz_control_base](std::uint32_t address) -> std::uint8_t {
                        const std::size_t index = (address - roz_control_base) >> 1U;
                        if (index >= roz_control_regs.size()) {
                            return 0xFFU;
                        }
                        const std::uint16_t word = roz_control_regs[index];
                        return (address & 1U) == 0U ? static_cast<std::uint8_t>(word >> 8U)
                                                    : static_cast<std::uint8_t>(word);
                    },
                    [this, roz_control_base](std::uint32_t address, std::uint8_t value) {
                        const std::size_t index = (address - roz_control_base) >> 1U;
                        if (index >= roz_control_regs.size()) {
                            return;
                        }
                        roz_control_regs[index] =
                            merge_word(roz_control_regs[index], address, value);
                        if ((address & 1U) != 0U) {
                            video.write_roz_control_register(
                                static_cast<std::uint32_t>(index), roz_control_regs[index]);
                        }
                    },
                    1);
            }
            if (uses_tc0480scp(params.address_map)) {
                const std::uint32_t tc0480scp_control_base = video_base;
                main_bus.map_mmio(
                    tc0480scp_control_base, metalb_tc0480scp_control_window,
                    [this, tc0480scp_control_base](std::uint32_t address) -> std::uint8_t {
                        const std::size_t index =
                            (address - tc0480scp_control_base) >> 1U;
                        if (index >= tc0480scp_control_regs.size()) {
                            return 0xFFU;
                        }
                        const std::uint16_t word = tc0480scp_control_regs[index];
                        return (address & 1U) == 0U ? static_cast<std::uint8_t>(word >> 8U)
                                                    : static_cast<std::uint8_t>(word);
                    },
                    [this, tc0480scp_control_base](std::uint32_t address, std::uint8_t value) {
                        const std::size_t index =
                            (address - tc0480scp_control_base) >> 1U;
                        if (index >= tc0480scp_control_regs.size()) {
                            return;
                        }
                        tc0480scp_control_regs[index] =
                            merge_word(tc0480scp_control_regs[index], address, value);
                        if ((address & 1U) != 0U) {
                            video.write_tc0480scp_control_register(
                                static_cast<std::uint32_t>(index),
                                tc0480scp_control_regs[index]);
                        }
                    },
                    1);
            }
        } else {
            main_bus.map_mmio(
                comm_base, comm_window,
                [this](std::uint32_t address) -> std::uint8_t {
                    switch (address - comm_base) {
                    case 0x03U:
                        return latch_z80_to_68k;
                    default:
                        return 0xFFU;
                    }
                },
                [this](std::uint32_t address, std::uint8_t value) {
                    switch (address - comm_base) {
                    case 0x01U:
                        latch_68k_to_z80 = value;
                        sound_latch_pending = true;
                        sync_sound_irq();
                        break;
                    default:
                        break;
                    }
                },
                1);
        }

        if (!uses_tc0480scp(params.address_map)) {
            main_bus.map_mmio(
                video_base, video_reg_window,
                [this, video_base](std::uint32_t address) -> std::uint8_t {
                    const std::size_t index = (address - video_base) >> 1U;
                    if (index >= video_regs.size()) {
                        return 0xFFU;
                    }
                    const std::uint16_t word = video_regs[index];
                    return (address & 1U) == 0U ? static_cast<std::uint8_t>(word >> 8U)
                                                : static_cast<std::uint8_t>(word);
                },
                [this, video_base](std::uint32_t address, std::uint8_t value) {
                    const std::size_t index = (address - video_base) >> 1U;
                    if (index >= video_regs.size()) {
                        return;
                    }
                    video_regs[index] = merge_word(video_regs[index], address, value);
                    if ((address & 1U) != 0U) {
                        push_video_regs_to_chip();
                        if (index == 6U) {
                            video.latch_sprites();
                        }
                    }
                },
                1);
            if (uses_dual_tc0100scn(params.address_map)) {
                main_bus.map_mmio(
                    thundfox_secondary_video_reg_base, video_reg_window,
                    [this](std::uint32_t address) -> std::uint8_t {
                        const std::size_t index =
                            (address - thundfox_secondary_video_reg_base) >> 1U;
                        if (index >= secondary_video_regs.size()) {
                            return 0xFFU;
                        }
                        const std::uint16_t word = secondary_video_regs[index];
                        return (address & 1U) == 0U ? static_cast<std::uint8_t>(word >> 8U)
                                                    : static_cast<std::uint8_t>(word);
                    },
                    [this](std::uint32_t address, std::uint8_t value) {
                        const std::size_t index =
                            (address - thundfox_secondary_video_reg_base) >> 1U;
                        if (index >= secondary_video_regs.size()) {
                            return;
                        }
                        secondary_video_regs[index] =
                            merge_word(secondary_video_regs[index], address, value);
                        if ((address & 1U) != 0U) {
                            push_video_regs_to_chip();
                            if (index == 6U) {
                                video.latch_sprites();
                            }
                        }
                    },
                    1);
            }
        }

        if (params.address_map == taito_f2_address_map::quizhq) {
            main_bus.map_mmio(
                quizhq_input_a_base, 0x04U,
                [this](std::uint32_t address) -> std::uint8_t {
                    switch ((address - quizhq_input_a_base) & 0x03U) {
                    case 0x00U:
                    case 0x01U:
                        return dip_b;
                    case 0x02U:
                    case 0x03U:
                        return input_p1;
                    default:
                        return 0xFFU;
                    }
                },
                [](std::uint32_t, std::uint8_t) {}, 1);
            main_bus.map_mmio(
                quizhq_input_b_base, 0x06U,
                [this](std::uint32_t address) -> std::uint8_t {
                    switch ((address - quizhq_input_b_base) & 0x07U) {
                    case 0x00U:
                    case 0x01U:
                        return dip_a;
                    case 0x02U:
                    case 0x03U:
                        return input_p2;
                    case 0x04U:
                    case 0x05U:
                        return input_system;
                    default:
                        return 0xFFU;
                    }
                },
                [](std::uint32_t, std::uint8_t) {}, 1);
        } else if (params.address_map == taito_f2_address_map::growl ||
                   params.address_map == taito_f2_address_map::solfigtr) {
            main_bus.map_mmio(
                growl_dip_input_base, 0x06U,
                [this](std::uint32_t address) -> std::uint8_t {
                    switch ((address - growl_dip_input_base) & 0x07U) {
                    case 0x00U:
                    case 0x01U:
                        return dip_a;
                    case 0x02U:
                    case 0x03U:
                        return dip_b;
                    default:
                        return 0xFFU;
                    }
                },
                [](std::uint32_t, std::uint8_t) {}, 1);
            main_bus.map_mmio(
                growl_player_input_base, 0x06U,
                [this](std::uint32_t address) -> std::uint8_t {
                    switch ((address - growl_player_input_base) & 0x07U) {
                    case 0x00U:
                    case 0x01U:
                        return input_p1;
                    case 0x02U:
                    case 0x03U:
                        return input_p2;
                    case 0x04U:
                    case 0x05U:
                        return input_system;
                    default:
                        return 0xFFU;
                    }
                },
                [](std::uint32_t, std::uint8_t) {}, 1);
            main_bus.map_mmio(
                growl_p3_input_base, 0x10U,
                [this](std::uint32_t) -> std::uint8_t { return input_p3; },
                [](std::uint32_t, std::uint8_t) {}, 1);
            main_bus.map_mmio(
                growl_p4_input_base, 0x10U,
                [this](std::uint32_t) -> std::uint8_t { return input_p4; },
                [](std::uint32_t, std::uint8_t) {}, 1);
        } else if (params.address_map == taito_f2_address_map::ninjak) {
            main_bus.map_mmio(
                ninjak_input_base, ninjak_input_window,
                [this](std::uint32_t address) -> std::uint8_t {
                    switch ((address - ninjak_input_base) >> 1U) {
                    case 0x00U:
                        return dip_a;
                    case 0x01U:
                        return dip_b;
                    case 0x02U:
                        return input_p1;
                    case 0x03U:
                        return input_p2;
                    case 0x04U:
                        return input_p3;
                    case 0x05U:
                        return input_p4;
                    case 0x06U:
                        return input_system;
                    default:
                        return 0xFFU;
                    }
                },
                [](std::uint32_t, std::uint8_t) {}, 1);
        } else {
            const std::uint32_t input_window_base = input_address(params.address_map, real_map);
            main_bus.map_mmio(
                input_window_base, input_window_size(params.address_map),
                [this, input_window_base](std::uint32_t address) -> std::uint8_t {
                    switch ((address - input_window_base) & 0x0FU) {
                    case 0x00U:
                    case 0x01U:
                        return input_p1;
                    case 0x02U:
                    case 0x03U:
                        return input_p2;
                    case 0x04U:
                    case 0x05U:
                        return input_system;
                    case 0x06U:
                    case 0x07U:
                        return dip_a;
                    case 0x08U:
                    case 0x09U:
                        return dip_b;
                    default:
                        return 0xFFU;
                    }
                },
                [](std::uint32_t, std::uint8_t) {}, 1);
        }

        main_cpu.attach_bus(main_bus);

        const auto& tiles = pinned_region(roms, "tiles", 0U);
        const auto& tiles_secondary = pinned_region(roms, "tiles_secondary", 0U);
        const auto& sprites = pinned_region(roms, "sprites", 0U);
        const auto& roz = pinned_region(roms, "roz", 0U);

        auto& sound_rom = pinned_region(roms, "audiocpu", z80_fixed_rom_window);
        sound_rom_size = static_cast<std::uint32_t>(sound_rom.size());
        sound_bus.map_rom(z80_fixed_rom_base,
                          std::span<const std::uint8_t>(sound_rom).first(z80_fixed_rom_window),
                          0);
        const std::span<const std::uint8_t> sound_rom_span{sound_rom};
        sound_bus.map_mmio(
            z80_bank_base, static_cast<std::uint32_t>(z80_bank_window),
            [this, sound_rom_span](std::uint32_t address) -> std::uint8_t {
                const std::uint32_t rom_addr = z80_bank_rom_base() + (address - z80_bank_base);
                return rom_addr < sound_rom_span.size() ? sound_rom_span[rom_addr] : 0xFFU;
            },
            [](std::uint32_t, std::uint8_t) {}, 0);
        sound_bus.map_ram(z80_ram_base, z80_ram, 0);
        sound_bus.map_mmio(
            z80_ym_base, 0x04U,
            [this](std::uint32_t) -> std::uint8_t { return opnb.read_status(); },
            [this](std::uint32_t address, std::uint8_t value) {
                switch (address - z80_ym_base) {
                case 0U:
                    opnb.write_address_a(value);
                    break;
                case 1U:
                    opnb.write_data_a(value);
                    break;
                case 2U:
                    opnb.write_address_b(value);
                    break;
                case 3U:
                    opnb.write_data_b(value);
                    break;
                default:
                    break;
                }
            },
            0);
        sound_bus.map_mmio(
            0xA000U, 0x3000U,
            [this](std::uint32_t address) -> std::uint8_t {
                if (address == z80_latch_addr) {
                    sound_latch_pending = false;
                    sync_sound_irq();
                    return latch_68k_to_z80;
                }
                return 0xFFU;
            },
            [this](std::uint32_t address, std::uint8_t value) {
                if (address == z80_reply_addr) {
                    latch_z80_to_68k = value;
                } else if (address == z80_bank_reg) {
                    sound_bank = static_cast<std::uint8_t>(value & 0x0FU);
                }
            },
            0);
        sound_cpu.attach_bus(sound_bus);

        const auto& adpcma = pinned_region(roms, "adpcma", 0U);
        opnb.adpcm_a_block().set_sample_rom(std::span<const std::uint8_t>(adpcma));
        const auto& adpcmb = pinned_region(roms, "adpcmb", 0U);
        auto adpcmb_dst = opnb.adpcm_b_block().sample_rom();
        std::copy_n(adpcmb.begin(), std::min(adpcmb.size(), adpcmb_dst.size()),
                    adpcmb_dst.begin());

        video.attach_tile_ram(tile_ram);
        video.attach_secondary_tile_ram(tile_ram_secondary);
        video.attach_sprite_ram(sprite_ram);
        video.attach_sprite_extension_ram(sprite_extension_ram);
        video.attach_palette(palette_ram);
        video.attach_tile_gfx(std::span<const std::uint8_t>(tiles));
        video.attach_secondary_tile_gfx(std::span<const std::uint8_t>(tiles_secondary));
        video.attach_sprite_gfx(std::span<const std::uint8_t>(sprites));
        video.attach_roz_ram(roz_ram);
        video.attach_roz_gfx(std::span<const std::uint8_t>(roz));

        video.set_vblank_callback([this](std::uint32_t) {
            main_cpu.set_irq_level(2);
            ++vblank_irq_raised;
        });
        main_cpu.set_irq_ack_callback([this](int) {
            main_cpu.set_irq_level(0);
            ++vblank_irq_acked;
        });

        main_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.reset(chips::reset_kind::power_on);
        opnb.reset(chips::reset_kind::power_on);
        video.reset(chips::reset_kind::power_on);
        configure_video_variant();
        push_video_regs_to_chip();
    }

    bool taito_f2_system::uses_real_map() const noexcept {
        return params.address_map != taito_f2_address_map::synthetic;
    }

    std::uint32_t taito_f2_system::z80_bank_rom_base() const noexcept {
        const std::uint32_t base = z80_fixed_rom_window;
        const std::uint32_t bank = sound_bank & 0x0FU;
        return base + bank * static_cast<std::uint32_t>(z80_bank_window);
    }

    void taito_f2_system::sync_sound_irq() noexcept {
        sound_cpu.set_irq_line(sound_latch_pending);
    }

    void taito_f2_system::configure_video_variant() noexcept {
        using video_chip = chips::video::taito_f2_video;
        if (params.address_map == taito_f2_address_map::metalb) {
            video.set_tilemap_variant(video_chip::tilemap_variant::tc0480scp);
            video.set_tc0480scp_palette_bank_base(256U);
            video.set_tc0480scp_priority_model(video_chip::tc0480scp_priority_model::metalb);
            video.set_tc0480scp_offsets(0x32 + 3, -0x04, 1, 0, -1, 0);
        } else if (params.address_map == taito_f2_address_map::footchmp) {
            video.set_tilemap_variant(video_chip::tilemap_variant::tc0480scp);
            video.set_tc0480scp_palette_bank_base(0U);
            video.set_tc0480scp_priority_model(
                video_chip::tc0480scp_priority_model::deadconx_footchmp);
            video.set_tc0480scp_offsets(0x1D + 3, 0x08, -1, 0, -1, 0);
        } else if (params.address_map == taito_f2_address_map::deadconx) {
            video.set_tilemap_variant(video_chip::tilemap_variant::tc0480scp);
            video.set_tc0480scp_palette_bank_base(0U);
            video.set_tc0480scp_priority_model(
                video_chip::tc0480scp_priority_model::deadconx_footchmp);
            video.set_tc0480scp_offsets(0x1E + 3, 0x08, -1, 0, -1, 0);
        } else {
            video.set_tilemap_variant(video_chip::tilemap_variant::tc0100scn);
            video.set_tc0480scp_palette_bank_base(0U);
            video.set_tc0480scp_priority_model(video_chip::tc0480scp_priority_model::metalb);
            video.set_tc0480scp_offsets(0, 0, 0, 0, 0, 0);
        }
        if (uses_dual_tc0100scn(params.address_map)) {
            video.set_tilemap_variant(video_chip::tilemap_variant::dual_tc0100scn);
        }

        switch (params.sprite_policy) {
        case taito_f2_sprite_policy::banked:
            video.set_sprite_mode(video_chip::sprite_mode::banked);
            break;
        case taito_f2_sprite_policy::extension_1:
            video.set_sprite_mode(video_chip::sprite_mode::extension_low);
            break;
        case taito_f2_sprite_policy::extension_2:
            video.set_sprite_mode(video_chip::sprite_mode::extension_high);
            break;
        case taito_f2_sprite_policy::extension_3:
            video.set_sprite_mode(video_chip::sprite_mode::extension_low_as_high);
            break;
        case taito_f2_sprite_policy::standard:
        case taito_f2_sprite_policy::partial_buffer:
            video.set_sprite_mode(video_chip::sprite_mode::standard);
            break;
        }

        switch (params.sprite_active_area) {
        case taito_f2_sprite_active_area::none:
            video.set_sprite_active_area_source(video_chip::sprite_active_area_source::none);
            break;
        case taito_f2_sprite_active_area::control_word_bit0:
            video.set_sprite_active_area_source(
                video_chip::sprite_active_area_source::control_word_bit0);
            break;
        case taito_f2_sprite_active_area::y_word_bit0:
            video.set_sprite_active_area_source(
                video_chip::sprite_active_area_source::y_word_bit0);
            break;
        case taito_f2_sprite_active_area::mode_default:
            video.set_sprite_active_area_source(
                video_chip::sprite_active_area_source::mode_default);
            break;
        }

        switch (params.sprite_buffering) {
        case taito_f2_sprite_buffering::full_delayed:
            video.set_sprite_buffer_policy(video_chip::sprite_buffer_policy::full_delayed);
            break;
        case taito_f2_sprite_buffering::partial_delayed:
            video.set_sprite_buffer_policy(video_chip::sprite_buffer_policy::partial_delayed);
            break;
        case taito_f2_sprite_buffering::partial_delayed_thundfox:
            video.set_sprite_buffer_policy(
                video_chip::sprite_buffer_policy::partial_delayed_thundfox);
            break;
        case taito_f2_sprite_buffering::partial_delayed_qzchikyu:
            video.set_sprite_buffer_policy(
                video_chip::sprite_buffer_policy::partial_delayed_qzchikyu);
            break;
        case taito_f2_sprite_buffering::immediate:
            video.set_sprite_buffer_policy(video_chip::sprite_buffer_policy::immediate);
            break;
        }

        switch (params.palette_format) {
        case taito_f2_palette_format::rgbx_444:
            video.set_palette_format(video_chip::palette_format::rgbx_444);
            break;
        case taito_f2_palette_format::xrgb_555:
            video.set_palette_format(video_chip::palette_format::xrgb_555);
            break;
        case taito_f2_palette_format::xbgr_555:
            video.set_palette_format(video_chip::palette_format::xbgr_555);
            break;
        }

        video.set_sprite_hide_pixels(params.sprite_hide_pixels,
                                     params.sprite_flip_hide_pixels);
        video.set_roz_variant(video_chip::roz_variant::tc0280grd);
        video.set_roz_offsets(0, 0);
        if (params.address_map == taito_f2_address_map::dondokod) {
            video.set_roz_offsets(-16, 0);
        } else if (params.address_map == taito_f2_address_map::pulirula) {
            video.set_roz_variant(video_chip::roz_variant::tc0430grw);
            video.set_roz_offsets(-10, 16);
        }
    }

    void taito_f2_system::push_video_regs_to_chip() noexcept {
        if (uses_tc0480scp(params.address_map)) {
            return;
        }
        video.set_scroll0(video_regs[0], video_regs[3]);
        video.set_scroll1(video_regs[1], video_regs[4]);
        video.set_scroll2(video_regs[2], video_regs[5]);
        video.set_layer_control(video_regs[6]);
        video.set_display_enable((video_regs[7] & 0x8000U) == 0U);
        if (uses_dual_tc0100scn(params.address_map)) {
            video.set_secondary_scroll0(secondary_video_regs[0], secondary_video_regs[3]);
            video.set_secondary_scroll1(secondary_video_regs[1], secondary_video_regs[4]);
            video.set_secondary_scroll2(secondary_video_regs[2], secondary_video_regs[5]);
            video.set_secondary_layer_control(secondary_video_regs[6]);
        }
    }

    void taito_f2_system::run_frame() {
        constexpr std::uint64_t cpu_cycles_per_frame = m68k_clock_hz / frame_rate_hz;
        constexpr std::uint64_t dots_total =
            static_cast<std::uint64_t>(chips::video::taito_f2_video::frame_lines) *
            chips::video::taito_f2_video::line_pixels;
        constexpr std::uint64_t dots_to_vblank =
            static_cast<std::uint64_t>(chips::video::taito_f2_video::vblank_start) *
                chips::video::taito_f2_video::line_pixels +
            1U;
        constexpr std::uint64_t cpu_visible =
            cpu_cycles_per_frame * chips::video::taito_f2_video::vblank_start /
            chips::video::taito_f2_video::frame_lines;

        push_video_regs_to_chip();
        run_cpus(cpu_visible);
        video.tick(dots_to_vblank);
        video.latch_sprites();
        run_cpus(cpu_cycles_per_frame - cpu_visible);
        video.tick(dots_total - dots_to_vblank);
    }

    void taito_f2_system::run_cpus(std::uint64_t cpu_cycles) {
        constexpr std::uint64_t slice_cycles = 256U;
        std::uint64_t remaining = cpu_cycles;
        while (remaining > 0U) {
            const std::uint64_t slice = remaining < slice_cycles ? remaining : slice_cycles;
            remaining -= slice;

            main_cpu.tick(slice);

            z80_cycle_accum_ += slice * z80_clock_hz;
            const std::uint64_t z80_cycles = z80_cycle_accum_ / m68k_clock_hz;
            z80_cycle_accum_ -= z80_cycles * m68k_clock_hz;
            if (z80_cycles > 0U) {
                sound_cpu.tick(z80_cycles);
                sync_sound_irq();
            }

            ym_cycle_accum_ += slice * ym2610_clock_hz;
            const std::uint64_t ym_cycles = ym_cycle_accum_ / m68k_clock_hz;
            ym_cycle_accum_ -= ym_cycles * m68k_clock_hz;
            if (ym_cycles > 0U) {
                opnb.tick(ym_cycles);
            }
        }
    }

    common::rom_set_decl taito_f2_rom_skeleton(std::string set_name) {
        common::rom_set_decl decl;
        decl.name = std::move(set_name);
        decl.board = "taito_f2";
        decl.regions.push_back(
            {.name = "maincpu", .size = main_rom_size, .fill = 0xFFU, .files = {}});
        return decl;
    }

    std::unique_ptr<taito_f2_system> assemble_taito_f2(common::rom_set_image image,
                                                       taito_f2_board_params board_params) {
        return std::make_unique<taito_f2_system>(std::move(image), board_params);
    }

} // namespace mnemos::manifests::taito_f2
