#include "taito_f2_system.hpp"

#include "crc32.hpp"

#include <algorithm>
#include <array>
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

        constexpr std::uint32_t taito_f2_system_state_version = 23U;
        constexpr std::uint32_t taito_f2_system_min_state_version = 15U;

        enum class watchdog_window_kind : std::uint8_t {
            none,
            quizhq_input_b,
            growl_solfigtr,
            ninjak,
            footchmp_deadconx,
            dinorex_suspect,
            priority_register_suspect,
            tc0220ioc_integrated,
            tc0510nio_integrated
        };

        enum class io_access_window_kind : std::uint8_t {
            none,
            standard,
            quizhq_input_a,
            quizhq_input_b,
            growl_dip,
            growl_player,
            growl_p3_p4,
            growl_coin_service_extension,
            ninjak
        };

        [[nodiscard]] std::uint32_t crc32_u64(std::uint32_t crc,
                                               std::uint64_t value) noexcept {
            std::array<std::uint8_t, 8> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
            return security::cryptography::crc32(bytes, crc);
        }

        [[nodiscard]] std::uint32_t crc32_u32(std::uint32_t crc,
                                               std::uint32_t value) noexcept {
            std::array<std::uint8_t, 4> bytes{};
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                bytes[i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
            return security::cryptography::crc32(bytes, crc);
        }

        [[nodiscard]] std::uint32_t crc32_i32(std::uint32_t crc, int value) noexcept {
            const auto signed_value = static_cast<std::int64_t>(value) + 2'147'483'648LL;
            return crc32_u32(crc, static_cast<std::uint32_t>(signed_value));
        }

        [[nodiscard]] std::uint32_t crc32_u8(std::uint32_t crc, std::uint8_t value) noexcept {
            return security::cryptography::crc32(std::span<const std::uint8_t>(&value, 1U), crc);
        }

        [[nodiscard]] std::uint32_t
        crc32_optional_u32(std::uint32_t crc, const std::optional<std::uint32_t>& value) noexcept {
            crc = crc32_u8(crc, value.has_value() ? 1U : 0U);
            return value.has_value() ? crc32_u32(crc, *value) : crc;
        }

        [[nodiscard]] std::uint32_t
        rom_identity_crc(const common::rom_set_image& roms,
                         const taito_f2_board_params& params) noexcept {
            std::uint32_t crc =
                security::cryptography::crc32(std::string_view{"taito_f2.rom.identity.v1"});
            crc = crc32_u8(crc, params.vertical ? 1U : 0U);
            crc = crc32_u8(crc, params.players);
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.address_map));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.sprite_policy));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.sprite_active_area));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.sprite_buffering));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.palette_format));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.text_gfx_source));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.input_profile));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.io_profile));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.palette_profile));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.priority_profile));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.sprite_chip_pair));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.sound_comm_chip));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.video_profile));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.tc0480scp_profile));
            crc = crc32_u8(crc, static_cast<std::uint8_t>(params.aux_profile));
            crc = crc32_u8(crc, params.vblank_irq_level);
            crc = crc32_u8(crc, params.sprite_dma_irq_level);
            crc = crc32_u32(crc, params.text_gfx_base);
            crc = crc32_i32(crc, params.tc0100scn_bg_x_offset);
            crc = crc32_i32(crc, params.tc0100scn_text_x_offset);
            crc = crc32_i32(crc, params.tc0100scn_text_y_origin);
            crc = crc32_i32(crc, params.tc0100scn_positive_text_y_origin);
            crc = crc32_i32(crc, params.roz_x_offset);
            crc = crc32_i32(crc, params.roz_y_offset);
            crc = crc32_i32(crc, params.sprite_hide_pixels);
            crc = crc32_i32(crc, params.sprite_flip_hide_pixels);
            crc = crc32_optional_u32(crc, params.sprite_extension_base);
            crc = crc32_optional_u32(crc, params.sprite_extension_size);
            crc = crc32_u64(crc, roms.regions.size());
            for (const auto& [name, bytes] : roms.regions) {
                crc = crc32_u64(crc, name.size());
                crc = security::cryptography::crc32(std::string_view{name}, crc);
                crc = crc32_u64(crc, bytes.size());
                crc = security::cryptography::crc32(bytes, crc);
            }
            return crc;
        }

        void write_i32(chips::state_writer& writer, int value) {
            const auto signed_value = static_cast<std::int64_t>(value) + 2'147'483'648LL;
            writer.u32(static_cast<std::uint32_t>(signed_value));
        }

        [[nodiscard]] int read_i32(chips::state_reader& reader) noexcept {
            return static_cast<int>(static_cast<std::int64_t>(reader.u32()) - 2'147'483'648LL);
        }

        void write_optional_u32(chips::state_writer& writer,
                                const std::optional<std::uint32_t>& value) {
            writer.boolean(value.has_value());
            if (value.has_value()) {
                writer.u32(*value);
            }
        }

        [[nodiscard]] bool read_optional_u32_matches(
            chips::state_reader& reader, const std::optional<std::uint32_t>& expected) noexcept {
            const bool saved_has_value = reader.boolean();
            if (saved_has_value != expected.has_value()) {
                return false;
            }
            if (!saved_has_value) {
                return true;
            }
            return reader.u32() == *expected;
        }

        [[nodiscard]] std::uint16_t merge_word(std::uint16_t word, std::uint32_t address,
                                               std::uint8_t value) noexcept {
            return (address & 1U) == 0U
                       ? static_cast<std::uint16_t>((word & 0x00FFU) |
                                                    (static_cast<std::uint16_t>(value) << 8U))
                       : static_cast<std::uint16_t>((word & 0xFF00U) | value);
        }

        [[nodiscard]] taito_f2_io_profile default_io_profile(
            taito_f2_address_map map) noexcept {
            switch (map) {
            case taito_f2_address_map::growl:
            case taito_f2_address_map::solfigtr:
            case taito_f2_address_map::dinorex:
            case taito_f2_address_map::gunfront:
            case taito_f2_address_map::metalb:
            case taito_f2_address_map::qzchikyu:
            case taito_f2_address_map::qzquest:
                return taito_f2_io_profile::tc0510nio;
            case taito_f2_address_map::ninjak:
                return taito_f2_io_profile::te7750;
            case taito_f2_address_map::synthetic:
            case taito_f2_address_map::dondokod:
            case taito_f2_address_map::liquidk:
            case taito_f2_address_map::pulirula:
            case taito_f2_address_map::quizhq:
            case taito_f2_address_map::qtorimon:
            case taito_f2_address_map::footchmp:
            case taito_f2_address_map::deadconx:
            case taito_f2_address_map::thundfox:
                return taito_f2_io_profile::tc0220ioc;
            }
            return taito_f2_io_profile::tc0220ioc;
        }

        [[nodiscard]] taito_f2_input_profile default_input_profile(
            taito_f2_address_map map) noexcept {
            if (map == taito_f2_address_map::growl ||
                map == taito_f2_address_map::solfigtr) {
                return taito_f2_input_profile::split_tmp82c265;
            }
            if (map == taito_f2_address_map::ninjak) {
                return taito_f2_input_profile::te7750_quad;
            }
            return taito_f2_input_profile::standard;
        }

        [[nodiscard]] taito_f2_priority_profile default_priority_profile(
            taito_f2_address_map map) noexcept {
            return map == taito_f2_address_map::qtorimon ||
                           map == taito_f2_address_map::qzchikyu ||
                           map == taito_f2_address_map::qzquest
                       ? taito_f2_priority_profile::none
                       : taito_f2_priority_profile::tc0360pri;
        }

        [[nodiscard]] taito_f2_video_profile default_video_profile(
            taito_f2_address_map map) noexcept {
            if (map == taito_f2_address_map::dondokod) {
                return taito_f2_video_profile::tc0100scn_tc0280grd;
            }
            if (map == taito_f2_address_map::pulirula) {
                return taito_f2_video_profile::tc0100scn_tc0430grw;
            }
            if (map == taito_f2_address_map::metalb ||
                map == taito_f2_address_map::footchmp ||
                map == taito_f2_address_map::deadconx) {
                return taito_f2_video_profile::tc0480scp;
            }
            if (map == taito_f2_address_map::thundfox) {
                return taito_f2_video_profile::dual_tc0100scn;
            }
            return taito_f2_video_profile::tc0100scn;
        }

        [[nodiscard]] taito_f2_tc0480scp_profile default_tc0480scp_profile(
            taito_f2_address_map map) noexcept {
            if (map == taito_f2_address_map::metalb) {
                return taito_f2_tc0480scp_profile::metalb;
            }
            if (map == taito_f2_address_map::footchmp) {
                return taito_f2_tc0480scp_profile::footchmp;
            }
            if (map == taito_f2_address_map::deadconx) {
                return taito_f2_tc0480scp_profile::deadconx;
            }
            return taito_f2_tc0480scp_profile::none;
        }

        void apply_tc0100scn_offset_defaults(taito_f2_board_params& params) noexcept {
            if (params.address_map == taito_f2_address_map::synthetic) {
                params.tc0100scn_bg_x_offset = 0;
                params.tc0100scn_text_x_offset = 0;
                return;
            }
            params.tc0100scn_bg_x_offset =
                chips::video::taito_f2_video::tc0100scn_default_bg_x_offset;
            params.tc0100scn_text_x_offset =
                chips::video::taito_f2_video::tc0100scn_default_text_x_offset;
        }

        void apply_roz_offset_defaults(taito_f2_board_params& params) noexcept {
            params.roz_x_offset = 0;
            params.roz_y_offset = 0;
            if (params.video_profile == taito_f2_video_profile::tc0100scn_tc0280grd &&
                params.address_map == taito_f2_address_map::dondokod) {
                params.roz_x_offset = -16;
            } else if (params.video_profile ==
                           taito_f2_video_profile::tc0100scn_tc0430grw &&
                       params.address_map == taito_f2_address_map::pulirula) {
                params.roz_x_offset = -10;
                params.roz_y_offset = 16;
            }
        }

        [[nodiscard]] bool has_roz_tilemap(const taito_f2_board_params& params) noexcept {
            return params.video_profile == taito_f2_video_profile::tc0100scn_tc0280grd ||
                   params.video_profile == taito_f2_video_profile::tc0100scn_tc0430grw;
        }

        [[nodiscard]] bool uses_tc0480scp(const taito_f2_board_params& params) noexcept {
            return params.video_profile == taito_f2_video_profile::tc0480scp;
        }

        [[nodiscard]] bool uses_dual_tc0100scn(const taito_f2_board_params& params) noexcept {
            return params.video_profile == taito_f2_video_profile::dual_tc0100scn;
        }

        [[nodiscard]] bool
        uses_split_panel_input(const taito_f2_board_params& params) noexcept {
            return params.input_profile == taito_f2_input_profile::split_tmp82c265;
        }

        [[nodiscard]] bool
        uses_te7750_quad_input(const taito_f2_board_params& params) noexcept {
            return params.input_profile == taito_f2_input_profile::te7750_quad;
        }

        [[nodiscard]] std::size_t coin_counter_slot_count(
            const taito_f2_board_params& params) noexcept {
            return std::min<std::size_t>(params.players >= 4U ? 4U : 2U, 4U);
        }

        [[nodiscard]] std::uint8_t cabinet_test_switch_mask(
            const taito_f2_board_params& params) noexcept {
            if (uses_split_panel_input(params) || uses_te7750_quad_input(params)) {
                return 0x01U;
            }
            if (params.players <= 2U) {
                return 0x04U;
            }
            return 0x00U;
        }

        [[nodiscard]] std::uint8_t four_player_service_mask(
            const taito_f2_board_params& params) noexcept {
            if (params.players <= 2U) {
                return 0x00U;
            }
            if (uses_te7750_quad_input(params)) {
                return params.players >= 4U ? 0x18U : 0x08U;
            }
            if (!uses_split_panel_input(params)) {
                return 0x00U;
            }
            return params.players >= 4U ? 0x0CU : 0x04U;
        }

        [[nodiscard]] bool uses_shifted_quiz_map(taito_f2_address_map map) noexcept {
            return map == taito_f2_address_map::qzchikyu ||
                   map == taito_f2_address_map::qzquest;
        }

        [[nodiscard]] bool uses_program_text_gfx(taito_f2_address_map map) noexcept {
            return map == taito_f2_address_map::quizhq ||
                   map == taito_f2_address_map::qtorimon;
        }

        [[nodiscard]] std::uint32_t program_text_gfx_base(
            taito_f2_address_map map) noexcept {
            if (map == taito_f2_address_map::quizhq) {
                return quizhq_program_text_gfx_base;
            }
            if (map == taito_f2_address_map::qtorimon) {
                return qtorimon_program_text_gfx_base;
            }
            return chips::video::taito_f2_video::text_gfx_base;
        }

        [[nodiscard]] taito_f2_board_params
        board_params_with_defaults(taito_f2_board_params params) noexcept {
            if (!params.hardware_profiles_explicit) {
                params.input_profile = default_input_profile(params.address_map);
                params.io_profile = default_io_profile(params.address_map);
                params.priority_profile = default_priority_profile(params.address_map);
                params.video_profile = default_video_profile(params.address_map);
                params.tc0480scp_profile =
                    default_tc0480scp_profile(params.address_map);
            }
            if (params.tc0100scn_bg_x_offset == 0 &&
                params.tc0100scn_text_x_offset == 0 &&
                params.address_map != taito_f2_address_map::synthetic) {
                apply_tc0100scn_offset_defaults(params);
            }
            if (params.roz_x_offset == 0 && params.roz_y_offset == 0) {
                apply_roz_offset_defaults(params);
            }
            if (uses_program_text_gfx(params.address_map)) {
                params.text_gfx_source = taito_f2_text_gfx_source::program_1bpp;
                params.text_gfx_base = program_text_gfx_base(params.address_map);
            }
            return params;
        }

        void apply_decl_hardware_profile_overrides(
            taito_f2_board_params& params, const common::rom_set_decl& decl) noexcept {
            if (decl.taito_f2_input_profile == "split_tmp82c265") {
                params.input_profile = taito_f2_input_profile::split_tmp82c265;
            } else if (decl.taito_f2_input_profile == "te7750_quad") {
                params.input_profile = taito_f2_input_profile::te7750_quad;
            } else if (decl.taito_f2_input_profile == "standard") {
                params.input_profile = taito_f2_input_profile::standard;
            }

            if (decl.taito_f2_text_gfx_source == "program_1bpp") {
                params.text_gfx_source = taito_f2_text_gfx_source::program_1bpp;
            } else if (decl.taito_f2_text_gfx_source == "tc0100scn_ram_2bpp") {
                params.text_gfx_source = taito_f2_text_gfx_source::tc0100scn_ram_2bpp;
                if (!decl.taito_f2_text_gfx_base.has_value()) {
                    params.text_gfx_base =
                        chips::video::taito_f2_video::text_gfx_base;
                }
            }
            if (decl.taito_f2_text_gfx_base.has_value()) {
                params.text_gfx_base = *decl.taito_f2_text_gfx_base;
            }

            if (decl.taito_f2_io_profile == "tc0510nio") {
                params.io_profile = taito_f2_io_profile::tc0510nio;
            } else if (decl.taito_f2_io_profile == "te7750") {
                params.io_profile = taito_f2_io_profile::te7750;
            } else if (decl.taito_f2_io_profile == "tmp82c265") {
                params.io_profile = taito_f2_io_profile::tmp82c265;
            } else if (decl.taito_f2_io_profile == "tc0220ioc") {
                params.io_profile = taito_f2_io_profile::tc0220ioc;
            }

            if (decl.taito_f2_palette_profile == "tc0260dar") {
                params.palette_profile = taito_f2_palette_profile::tc0260dar;
            } else if (decl.taito_f2_palette_profile == "tc0110pcr_tc0070rgb") {
                params.palette_profile = taito_f2_palette_profile::tc0110pcr_tc0070rgb;
            }

            if (decl.taito_f2_priority_profile == "none") {
                params.priority_profile = taito_f2_priority_profile::none;
            } else if (decl.taito_f2_priority_profile == "tc0360pri") {
                params.priority_profile = taito_f2_priority_profile::tc0360pri;
            }

            if (decl.taito_f2_sprite_chip_pair == "tc0540obn_tc0520tbc") {
                params.sprite_chip_pair = taito_f2_sprite_chip_pair::tc0540obn_tc0520tbc;
            } else if (decl.taito_f2_sprite_chip_pair == "tc0200obj_tc0210fbc") {
                params.sprite_chip_pair = taito_f2_sprite_chip_pair::tc0200obj_tc0210fbc;
            }

            if (decl.taito_f2_sound_comm_chip == "tc0530syc") {
                params.sound_comm_chip = taito_f2_sound_comm_chip::tc0530syc;
            } else if (decl.taito_f2_sound_comm_chip == "tc0140syt") {
                params.sound_comm_chip = taito_f2_sound_comm_chip::tc0140syt;
            }

            if (decl.taito_f2_video_profile == "dual_tc0100scn") {
                params.video_profile = taito_f2_video_profile::dual_tc0100scn;
            } else if (decl.taito_f2_video_profile == "tc0100scn_tc0280grd") {
                params.video_profile = taito_f2_video_profile::tc0100scn_tc0280grd;
            } else if (decl.taito_f2_video_profile == "tc0100scn_tc0430grw") {
                params.video_profile = taito_f2_video_profile::tc0100scn_tc0430grw;
            } else if (decl.taito_f2_video_profile == "tc0480scp") {
                params.video_profile = taito_f2_video_profile::tc0480scp;
            } else if (decl.taito_f2_video_profile == "tc0100scn") {
                params.video_profile = taito_f2_video_profile::tc0100scn;
            }

            if (decl.taito_f2_tc0480scp_profile == "metalb") {
                params.tc0480scp_profile = taito_f2_tc0480scp_profile::metalb;
            } else if (decl.taito_f2_tc0480scp_profile == "footchmp") {
                params.tc0480scp_profile = taito_f2_tc0480scp_profile::footchmp;
            } else if (decl.taito_f2_tc0480scp_profile == "deadconx") {
                params.tc0480scp_profile = taito_f2_tc0480scp_profile::deadconx;
            } else if (decl.taito_f2_tc0480scp_profile == "none") {
                params.tc0480scp_profile = taito_f2_tc0480scp_profile::none;
            }

            if (decl.taito_f2_aux_profile == "tc0030cmd_cchip") {
                params.aux_profile = taito_f2_aux_profile::tc0030cmd_cchip;
            } else if (decl.taito_f2_aux_profile == "rtc") {
                params.aux_profile = taito_f2_aux_profile::rtc;
            } else if (decl.taito_f2_aux_profile == "printer") {
                params.aux_profile = taito_f2_aux_profile::printer;
            } else if (decl.taito_f2_aux_profile == "rtc_printer") {
                params.aux_profile = taito_f2_aux_profile::rtc_printer;
            } else if (decl.taito_f2_aux_profile == "none") {
                params.aux_profile = taito_f2_aux_profile::none;
            }

            if (decl.taito_f2_vblank_irq_level.has_value()) {
                params.vblank_irq_level = *decl.taito_f2_vblank_irq_level;
            }
            if (decl.taito_f2_sprite_dma_irq_level.has_value()) {
                params.sprite_dma_irq_level = *decl.taito_f2_sprite_dma_irq_level;
            }
            params.hardware_profiles_explicit =
                decl.taito_f2_input_profile.has_value() ||
                decl.taito_f2_text_gfx_source.has_value() ||
                decl.taito_f2_text_gfx_base.has_value() ||
                decl.taito_f2_io_profile.has_value() ||
                decl.taito_f2_palette_profile.has_value() ||
                decl.taito_f2_priority_profile.has_value() ||
                decl.taito_f2_sprite_chip_pair.has_value() ||
                decl.taito_f2_sound_comm_chip.has_value() ||
                decl.taito_f2_video_profile.has_value() ||
                decl.taito_f2_tc0480scp_profile.has_value() ||
                decl.taito_f2_aux_profile.has_value() ||
                decl.taito_f2_vblank_irq_level.has_value() ||
                decl.taito_f2_sprite_dma_irq_level.has_value();
        }

        [[nodiscard]] bool has_priority_window(const taito_f2_board_params& params) noexcept {
            return params.priority_profile == taito_f2_priority_profile::tc0360pri;
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

        [[nodiscard]] std::uint32_t
        confirmed_watchdog_address(taito_f2_address_map map) noexcept {
            switch (map) {
            case taito_f2_address_map::quizhq:
                return quizhq_watchdog_base;
            case taito_f2_address_map::growl:
                return growl_watchdog_base;
            case taito_f2_address_map::solfigtr:
                return solfigtr_watchdog_base;
            case taito_f2_address_map::ninjak:
                return ninjak_watchdog_base;
            case taito_f2_address_map::footchmp:
                return footchmp_watchdog_base;
            case taito_f2_address_map::deadconx:
                return deadconx_watchdog_base;
            case taito_f2_address_map::synthetic:
            case taito_f2_address_map::dondokod:
            case taito_f2_address_map::gunfront:
            case taito_f2_address_map::liquidk:
            case taito_f2_address_map::pulirula:
            case taito_f2_address_map::qtorimon:
            case taito_f2_address_map::qzchikyu:
            case taito_f2_address_map::qzquest:
            case taito_f2_address_map::metalb:
            case taito_f2_address_map::dinorex:
            case taito_f2_address_map::thundfox:
                return 0U;
            }
            return 0U;
        }

        [[nodiscard]] watchdog_window_kind
        confirmed_watchdog_window(taito_f2_address_map map) noexcept {
            switch (map) {
            case taito_f2_address_map::quizhq:
                return watchdog_window_kind::quizhq_input_b;
            case taito_f2_address_map::growl:
            case taito_f2_address_map::solfigtr:
                return watchdog_window_kind::growl_solfigtr;
            case taito_f2_address_map::ninjak:
                return watchdog_window_kind::ninjak;
            case taito_f2_address_map::footchmp:
            case taito_f2_address_map::deadconx:
                return watchdog_window_kind::footchmp_deadconx;
            case taito_f2_address_map::synthetic:
            case taito_f2_address_map::dondokod:
            case taito_f2_address_map::gunfront:
            case taito_f2_address_map::liquidk:
            case taito_f2_address_map::pulirula:
            case taito_f2_address_map::qtorimon:
            case taito_f2_address_map::qzchikyu:
            case taito_f2_address_map::qzquest:
            case taito_f2_address_map::metalb:
            case taito_f2_address_map::dinorex:
            case taito_f2_address_map::thundfox:
                return watchdog_window_kind::none;
            }
            return watchdog_window_kind::none;
        }

        [[nodiscard]] std::uint32_t input_address(taito_f2_address_map map,
                                                  bool real_map) noexcept;

        [[nodiscard]] bool
        uses_tc0510nio_wordswap(taito_f2_address_map map) noexcept {
            return map == taito_f2_address_map::gunfront ||
                   map == taito_f2_address_map::metalb;
        }

        [[nodiscard]] bool
        io_profile_has_integrated_watchdog(taito_f2_io_profile profile) noexcept {
            return profile == taito_f2_io_profile::tc0220ioc ||
                   profile == taito_f2_io_profile::tc0510nio;
        }

        [[nodiscard]] std::uint32_t
        io_watchdog_byte_offset(const taito_f2_board_params& params) noexcept {
            return params.io_profile == taito_f2_io_profile::tc0510nio &&
                           uses_tc0510nio_wordswap(params.address_map)
                       ? 0x02U
                       : 0x00U;
        }

        [[nodiscard]] std::uint32_t
        integrated_io_watchdog_address(const taito_f2_board_params& params,
                                       bool real_map) noexcept {
            if (!io_profile_has_integrated_watchdog(params.io_profile)) {
                return 0U;
            }
            return input_address(params.address_map, real_map) + io_watchdog_byte_offset(params);
        }

        [[nodiscard]] watchdog_window_kind
        integrated_io_watchdog_window(const taito_f2_board_params& params) noexcept {
            if (params.io_profile == taito_f2_io_profile::tc0220ioc) {
                return watchdog_window_kind::tc0220ioc_integrated;
            }
            if (params.io_profile == taito_f2_io_profile::tc0510nio) {
                return watchdog_window_kind::tc0510nio_integrated;
            }
            return watchdog_window_kind::none;
        }

        [[nodiscard]] std::uint32_t
        confirmed_watchdog_address(const taito_f2_board_params& params,
                                   bool real_map) noexcept {
            const std::uint32_t explicit_address =
                confirmed_watchdog_address(params.address_map);
            return explicit_address != 0U ? explicit_address
                                          : integrated_io_watchdog_address(params, real_map);
        }

        [[nodiscard]] bool
        io_write_resets_integrated_watchdog(const taito_f2_board_params& params,
                                            std::uint32_t window_base,
                                            std::uint32_t address) noexcept {
            if (!io_profile_has_integrated_watchdog(params.io_profile) ||
                address < window_base) {
                return false;
            }
            const std::uint32_t byte_offset = address - window_base;
            std::uint32_t register_index = byte_offset >> 1U;
            if (params.io_profile == taito_f2_io_profile::tc0510nio &&
                uses_tc0510nio_wordswap(params.address_map)) {
                register_index ^= 1U;
            }
            return register_index == 0U;
        }

        [[nodiscard]] std::uint32_t suspect_watchdog_address(
            taito_f2_address_map map) noexcept {
            return map == taito_f2_address_map::dinorex ? dinorex_watchdog_base : 0U;
        }

        [[nodiscard]] std::uint32_t priority_watchdog_like_address(
            taito_f2_address_map map) noexcept {
            switch (map) {
            case taito_f2_address_map::footchmp:
            case taito_f2_address_map::deadconx:
            case taito_f2_address_map::ninjak:
                return priority_address(map) + 2U;
            case taito_f2_address_map::synthetic:
            case taito_f2_address_map::dondokod:
            case taito_f2_address_map::gunfront:
            case taito_f2_address_map::liquidk:
            case taito_f2_address_map::pulirula:
            case taito_f2_address_map::quizhq:
            case taito_f2_address_map::qtorimon:
            case taito_f2_address_map::qzchikyu:
            case taito_f2_address_map::qzquest:
            case taito_f2_address_map::metalb:
            case taito_f2_address_map::dinorex:
            case taito_f2_address_map::thundfox:
            case taito_f2_address_map::growl:
            case taito_f2_address_map::solfigtr:
                return 0U;
            }
            return 0U;
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

        [[nodiscard]] io_access_window_kind
        io_access_window_for(const taito_f2_board_params& params,
                             std::uint32_t window_base) noexcept {
            if (params.address_map == taito_f2_address_map::quizhq) {
                if (window_base == quizhq_input_a_base) {
                    return io_access_window_kind::quizhq_input_a;
                }
                if (window_base == quizhq_input_b_base) {
                    return io_access_window_kind::quizhq_input_b;
                }
            }
            if (params.address_map == taito_f2_address_map::growl ||
                params.address_map == taito_f2_address_map::solfigtr) {
                if (window_base == growl_dip_input_base) {
                    return io_access_window_kind::growl_dip;
                }
                if (window_base == growl_player_input_base) {
                    return io_access_window_kind::growl_player;
                }
                if (window_base == growl_p3_input_base) {
                    return io_access_window_kind::growl_p3_p4;
                }
                if (window_base == growl_p4_input_base) {
                    return io_access_window_kind::growl_coin_service_extension;
                }
            }
            if (params.address_map == taito_f2_address_map::ninjak &&
                window_base == ninjak_input_base) {
                return io_access_window_kind::ninjak;
            }
            return io_access_window_kind::standard;
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
            .vertical = decl.orientation == common::screen_orientation::vertical,
            .players = decl.players};
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
        } else if (decl.taito_f2_palette_format == "rrrr_gggg_bbbb_rgbx" ||
                   decl.taito_f2_palette_format == "rrrrggggbbbbrgbx") {
            params.palette_format = taito_f2_palette_format::rrrr_gggg_bbbb_rgbx;
        }
        if (decl.taito_f2_sprite_hide_pixels.has_value()) {
            params.sprite_hide_pixels = *decl.taito_f2_sprite_hide_pixels;
        }
        if (decl.taito_f2_sprite_flip_hide_pixels.has_value()) {
            params.sprite_flip_hide_pixels = *decl.taito_f2_sprite_flip_hide_pixels;
        }
        if (decl.taito_f2_tc0100scn_bg_x_offset.has_value()) {
            params.tc0100scn_bg_x_offset = *decl.taito_f2_tc0100scn_bg_x_offset;
        }
        if (decl.taito_f2_tc0100scn_text_x_offset.has_value()) {
            params.tc0100scn_text_x_offset = *decl.taito_f2_tc0100scn_text_x_offset;
        }
        if (decl.taito_f2_tc0100scn_text_y_origin.has_value()) {
            params.tc0100scn_text_y_origin = *decl.taito_f2_tc0100scn_text_y_origin;
        }
        if (decl.taito_f2_tc0100scn_positive_text_y_origin.has_value()) {
            params.tc0100scn_positive_text_y_origin =
                *decl.taito_f2_tc0100scn_positive_text_y_origin;
        }
        if (decl.taito_f2_roz_x_offset.has_value()) {
            params.roz_x_offset = *decl.taito_f2_roz_x_offset;
        }
        if (decl.taito_f2_roz_y_offset.has_value()) {
            params.roz_y_offset = *decl.taito_f2_roz_y_offset;
        }
        params.sprite_extension_base = decl.taito_f2_sprite_extension_base;
        params.sprite_extension_size = decl.taito_f2_sprite_extension_size;
        params = board_params_with_defaults(params);
        if (decl.taito_f2_tc0100scn_bg_x_offset.has_value()) {
            params.tc0100scn_bg_x_offset = *decl.taito_f2_tc0100scn_bg_x_offset;
        }
        if (decl.taito_f2_tc0100scn_text_x_offset.has_value()) {
            params.tc0100scn_text_x_offset = *decl.taito_f2_tc0100scn_text_x_offset;
        }
        if (decl.taito_f2_roz_x_offset.has_value()) {
            params.roz_x_offset = *decl.taito_f2_roz_x_offset;
        }
        if (decl.taito_f2_roz_y_offset.has_value()) {
            params.roz_y_offset = *decl.taito_f2_roz_y_offset;
        }
        apply_decl_hardware_profile_overrides(params, decl);
        return params;
    }

    taito_f2_system::taito_f2_system(common::rom_set_image image,
                                     taito_f2_board_params board_params)
        : roms(std::move(image)), params(board_params_with_defaults(board_params)) {
        auto& program = pinned_region(roms, "maincpu", main_rom_size);
        main_bus.map_rom(program_rom_base, std::span<const std::uint8_t>(program), 0);
        main_bus.map_ram(work_ram_address(params.address_map), work_ram, 1);
        const std::uint32_t palette_base = palette_ram_address(params.address_map);
        main_bus.map_mmio(
            palette_base, static_cast<std::uint32_t>(palette_ram.size()),
            [this, palette_base](std::uint32_t address) -> std::uint8_t {
                const std::size_t offset = static_cast<std::size_t>(address - palette_base);
                if (offset >= palette_ram.size()) {
                    return 0xFFU;
                }
                const std::uint8_t value = palette_ram[offset];
                record_palette_read(palette_base, address, value);
                return value;
            },
            [this, palette_base](std::uint32_t address, std::uint8_t value) {
                const std::size_t offset = static_cast<std::size_t>(address - palette_base);
                if (offset >= palette_ram.size()) {
                    return;
                }
                palette_ram[offset] = value;
                record_palette_write(palette_base, address, value);
            },
            1);
        const bool real_map = uses_real_map();
        const std::uint32_t tile_base = tile_ram_address(params.address_map, real_map);
        const std::uint32_t sprite_base = sprite_ram_address(params.address_map, real_map);
        const std::uint32_t video_base = video_reg_address(params.address_map, real_map);
        main_bus.map_ram(tile_base, tile_ram, 1);
        main_bus.map_ram(sprite_base, sprite_ram, 1);
        if (uses_dual_tc0100scn(params)) {
            main_bus.map_ram(thundfox_secondary_tile_ram_base, tile_ram_secondary, 1);
        }
        if (has_roz_tilemap(params)) {
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
                    if (offset < 0x02U) {
                        return sound_main_port;
                    }
                    if (sound_main_port == 4U) {
                        return sound_comm.status();
                    }
                    if (sound_main_port != 0U && sound_main_port != 2U) {
                        return 0xFFU;
                    }
                    const bool port2 = sound_main_port == 2U;
                    const std::uint8_t value =
                        port2 ? latch_z80_to_68k_port2 : latch_z80_to_68k;
                    const std::uint8_t nibble = sound_main_read_high
                                                    ? static_cast<std::uint8_t>(value >> 4U)
                                                    : static_cast<std::uint8_t>(value & 0x0FU);
                    if (sound_main_read_high) {
                        sound_comm.note_reply_read(port2 ? 1U : 0U, value);
                        if (port2) {
                            sound_reply_pending_port2 = false;
                        } else {
                            sound_reply_pending = false;
                        }
                    }
                    sound_main_read_high = !sound_main_read_high;
                    return nibble;
                },
                [this, sound_comm_base](std::uint32_t address, std::uint8_t value) {
                    const std::uint32_t offset = address - sound_comm_base;
                    if (offset < 0x02U) {
                        sound_main_port = static_cast<std::uint8_t>(value & 0x0FU);
                        sound_main_read_high = false;
                        sound_main_write_high = false;
                        return;
                    }
                    if (sound_main_port == 4U) {
                        write_sound_reset_control(address, value);
                        return;
                    }
                    if (sound_main_port == 5U) {
                        sound_comm.clear_all_pending();
                        sync_sound_irq();
                        return;
                    }
                    if (sound_main_port != 0U && sound_main_port != 2U) {
                        return;
                    }
                    auto& latch =
                        sound_main_port == 2U ? latch_68k_to_z80_port2 : latch_68k_to_z80;
                    auto& pending =
                        sound_main_port == 2U ? sound_latch_pending_port2 : sound_latch_pending;
                    const std::uint8_t nibble = static_cast<std::uint8_t>(value & 0x0FU);
                    if (sound_main_write_high) {
                        latch = static_cast<std::uint8_t>((latch & 0x0FU) | (nibble << 4U));
                        pending = true;
                        sound_comm.note_command_write(sound_main_port == 2U ? 1U : 0U,
                                                       latch);
                        pulse_sound_command_nmi();
                        sync_sound_irq();
                    } else {
                        latch = static_cast<std::uint8_t>((latch & 0xF0U) | nibble);
                    }
                    sound_main_write_high = !sound_main_write_high;
                },
                1);
            const auto map_watchdog_window =
                [this](std::uint32_t base, watchdog_window_kind kind, bool confirmed) {
                    main_bus.map_mmio(
                        base, watchdog_window,
                        [](std::uint32_t) -> std::uint8_t { return 0xFFU; },
                        [this, kind, confirmed](std::uint32_t address,
                                                std::uint8_t value) {
                            record_watchdog_write(address, value,
                                                  static_cast<std::uint8_t>(kind),
                                                  confirmed);
                        },
                        1);
                };
            const std::uint32_t watchdog_base =
                confirmed_watchdog_address(params.address_map);
            if (watchdog_base != 0U && watchdog_base != quizhq_watchdog_base) {
                map_watchdog_window(watchdog_base,
                                    confirmed_watchdog_window(params.address_map), true);
            }
            const std::uint32_t suspect_watchdog_base =
                suspect_watchdog_address(params.address_map);
            if (suspect_watchdog_base != 0U) {
                map_watchdog_window(suspect_watchdog_base,
                                    watchdog_window_kind::dinorex_suspect, false);
            }
            if (has_priority_window(params)) {
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
                        const std::uint32_t priority_watchdog_address =
                            priority_watchdog_like_address(params.address_map);
                        if (priority_watchdog_address != 0U &&
                            (address & ~std::uint32_t{1U}) == priority_watchdog_address) {
                            record_watchdog_write(
                                address, value,
                                static_cast<std::uint8_t>(
                                    watchdog_window_kind::priority_register_suspect),
                                false);
                        }
                    },
                    1);
            }
            if (has_roz_tilemap(params)) {
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
            if (uses_tc0480scp(params)) {
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
                        sound_comm.note_command_write(0U, value);
                        pulse_sound_command_nmi();
                        sync_sound_irq();
                        break;
                    default:
                        break;
                    }
                },
                1);
        }

        if (!uses_tc0480scp(params)) {
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
                            board_latch_sprites();
                        }
                    }
                },
                1);
            if (uses_dual_tc0100scn(params)) {
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
                                board_latch_sprites();
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
                    std::uint8_t value = 0xFFU;
                    bool dip_read = false;
                    switch ((address - quizhq_input_a_base) & 0x03U) {
                    case 0x00U:
                    case 0x01U:
                        value = dip_b;
                        dip_read = true;
                        break;
                    case 0x02U:
                    case 0x03U:
                        value = input_p1;
                        break;
                    default:
                        break;
                    }
                    record_io_input_read(quizhq_input_a_base, address, value, dip_read, false);
                    return value;
                },
                [this](std::uint32_t address, std::uint8_t value) {
                    record_io_output_write(quizhq_input_a_base, address, value);
                },
                1);
            main_bus.map_mmio(
                quizhq_input_b_base, 0x06U,
                [this](std::uint32_t address) -> std::uint8_t {
                    std::uint8_t value = 0xFFU;
                    bool dip_read = false;
                    bool service_read = false;
                    switch ((address - quizhq_input_b_base) & 0x07U) {
                    case 0x00U:
                    case 0x01U:
                        value = dip_a;
                        dip_read = true;
                        break;
                    case 0x02U:
                    case 0x03U:
                        value = input_p2;
                        break;
                    case 0x04U:
                    case 0x05U:
                        value = input_system;
                        service_read = true;
                        break;
                    default:
                        break;
                    }
                    record_io_input_read(quizhq_input_b_base, address, value, dip_read,
                                         service_read);
                    return value;
                },
                [this](std::uint32_t address, std::uint8_t value) {
                    if ((address - quizhq_input_b_base) < watchdog_window) {
                        record_watchdog_write(
                            address, value,
                            static_cast<std::uint8_t>(
                                watchdog_window_kind::quizhq_input_b),
                            true);
                    }
                    record_io_output_write(quizhq_input_b_base, address, value);
                },
                1);
        } else if (params.address_map == taito_f2_address_map::growl ||
                   params.address_map == taito_f2_address_map::solfigtr) {
            main_bus.map_mmio(
                growl_dip_input_base, 0x06U,
                [this](std::uint32_t address) -> std::uint8_t {
                    std::uint8_t value = 0xFFU;
                    bool dip_read = false;
                    switch ((address - growl_dip_input_base) & 0x07U) {
                    case 0x00U:
                    case 0x01U:
                        value = dip_a;
                        dip_read = true;
                        break;
                    case 0x02U:
                    case 0x03U:
                        value = dip_b;
                        dip_read = true;
                        break;
                    default:
                        break;
                    }
                    record_io_input_read(growl_dip_input_base, address, value, dip_read,
                                         false);
                    return value;
                },
                [this](std::uint32_t address, std::uint8_t value) {
                    record_io_output_write(growl_dip_input_base, address, value);
                },
                1);
            main_bus.map_mmio(
                growl_player_input_base, 0x06U,
                [this](std::uint32_t address) -> std::uint8_t {
                    std::uint8_t value = 0xFFU;
                    bool service_read = false;
                    switch ((address - growl_player_input_base) & 0x07U) {
                    case 0x00U:
                    case 0x01U:
                        value = input_p1;
                        break;
                    case 0x02U:
                    case 0x03U:
                        value = input_p2;
                        break;
                    case 0x04U:
                    case 0x05U:
                        value = input_system;
                        service_read = true;
                        break;
                    default:
                        break;
                    }
                    record_io_input_read(growl_player_input_base, address, value, false,
                                         service_read);
                    return value;
                },
                [this](std::uint32_t address, std::uint8_t value) {
                    record_io_output_write(growl_player_input_base, address, value);
                },
                1);
            main_bus.map_mmio(
                growl_p3_input_base, 0x10U,
                [this](std::uint32_t address) -> std::uint8_t {
                    const std::uint8_t value =
                        ((address - growl_p3_input_base) & 1U) == 0U ? input_p4
                                                                     : input_p3;
                    record_io_input_read(growl_p3_input_base, address, value, false, false);
                    return value;
                },
                [this](std::uint32_t address, std::uint8_t value) {
                    record_io_output_write(growl_p3_input_base, address, value);
                },
                1);
            main_bus.map_mmio(
                growl_p4_input_base, 0x10U,
                [this](std::uint32_t address) -> std::uint8_t {
                    record_io_input_read(growl_p4_input_base, address, input_coin_extension,
                                         false, true);
                    return input_coin_extension;
                },
                [this](std::uint32_t address, std::uint8_t value) {
                    record_io_output_write(growl_p4_input_base, address, value);
                },
                1);
        } else if (params.address_map == taito_f2_address_map::ninjak) {
            main_bus.map_mmio(
                ninjak_input_base, ninjak_input_window,
                [this](std::uint32_t address) -> std::uint8_t {
                    std::uint8_t value = 0xFFU;
                    bool dip_read = false;
                    bool service_read = false;
                    switch ((address - ninjak_input_base) >> 1U) {
                    case 0x00U:
                        value = dip_a;
                        dip_read = true;
                        break;
                    case 0x01U:
                        value = dip_b;
                        dip_read = true;
                        break;
                    case 0x02U:
                        value = input_p1;
                        break;
                    case 0x03U:
                        value = input_p2;
                        break;
                    case 0x04U:
                        value = input_p3;
                        break;
                    case 0x05U:
                        value = input_p4;
                        break;
                    case 0x06U:
                        value = input_system;
                        service_read = true;
                        break;
                    case 0x07U:
                        value = input_coin_extension;
                        service_read = true;
                        break;
                    default:
                        break;
                    }
                    record_io_input_read(ninjak_input_base, address, value, dip_read,
                                         service_read);
                    return value;
                },
                [this](std::uint32_t address, std::uint8_t value) {
                    record_io_output_write(ninjak_input_base, address, value);
                },
                1);
        } else {
            const std::uint32_t input_window_base = input_address(params.address_map, real_map);
            main_bus.map_mmio(
                input_window_base, input_window_size(params.address_map),
                [this, input_window_base](std::uint32_t address) -> std::uint8_t {
                    std::uint8_t value = 0xFFU;
                    bool dip_read = false;
                    bool service_read = false;
                    switch ((address - input_window_base) & 0x0FU) {
                    case 0x00U:
                    case 0x01U:
                        value = input_p1;
                        break;
                    case 0x02U:
                    case 0x03U:
                        value = input_p2;
                        break;
                    case 0x04U:
                    case 0x05U:
                        value = input_system;
                        service_read = true;
                        break;
                    case 0x06U:
                    case 0x07U:
                        value = dip_a;
                        dip_read = true;
                        break;
                    case 0x08U:
                    case 0x09U:
                        value = dip_b;
                        dip_read = true;
                        break;
                    default:
                        break;
                    }
                    record_io_input_read(input_window_base, address, value, dip_read,
                                         service_read);
                    return value;
                },
                [this, input_window_base](std::uint32_t address, std::uint8_t value) {
                    record_integrated_io_watchdog_write(input_window_base, address, value);
                    record_io_output_write(input_window_base, address, value);
                },
                1);
        }

        main_bus.set_access_observer(
            [this](const topology::access_event& event) { record_main_bus_access(event); });
        main_cpu.attach_bus(main_bus);

        const auto& tiles = pinned_region(roms, "tiles", 0U);
        const auto& tiles_secondary = pinned_region(roms, "tiles_secondary", 0U);
        const auto& sprites = pinned_region(roms, "sprites", 0U);
        const auto& roz = pinned_region(roms, "roz", 0U);

        auto& sound_rom = pinned_region(roms, "audiocpu", z80_fixed_rom_window);
        sound_rom_size = static_cast<std::uint32_t>(sound_rom.size());
        update_sound_bank_state();
        sound_bus.map_rom(z80_fixed_rom_base,
                          std::span<const std::uint8_t>(sound_rom).first(z80_fixed_rom_window),
                          0);
        const std::span<const std::uint8_t> sound_rom_span{sound_rom};
        sound_bus.map_mmio(
            z80_bank_base, static_cast<std::uint32_t>(z80_bank_window),
            [this, sound_rom_span](std::uint32_t address) -> std::uint8_t {
                if (!z80_sound_bank_page_valid()) {
                    return 0xFFU;
                }
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
                    sync_sound_irq();
                    break;
                case 2U:
                    opnb.write_address_b(value);
                    break;
                case 3U:
                    opnb.write_data_b(value);
                    sync_sound_irq();
                    break;
                default:
                    break;
                }
            },
            0);
        sound_bus.map_mmio(
            z80_tc0140syt_port_addr, 0x02U,
            [this](std::uint32_t address) -> std::uint8_t {
                if (address == z80_tc0140syt_port_addr) {
                    return sound_z80_port;
                }
                if (sound_z80_port == 4U) {
                    return sound_comm.status();
                }
                if (sound_z80_port != 0U && sound_z80_port != 2U) {
                    return 0xFFU;
                }
                const bool port2 = sound_z80_port == 2U;
                const std::uint8_t value =
                    port2 ? latch_68k_to_z80_port2 : latch_68k_to_z80;
                const std::uint8_t nibble = sound_z80_read_high
                                                ? static_cast<std::uint8_t>(value >> 4U)
                                                : static_cast<std::uint8_t>(value & 0x0FU);
                if (sound_z80_read_high) {
                    sound_comm.note_command_read(port2 ? 1U : 0U, value);
                    if (port2) {
                        sound_latch_pending_port2 = false;
                    } else {
                        sound_latch_pending = false;
                    }
                    sync_sound_irq();
                }
                sound_z80_read_high = !sound_z80_read_high;
                return nibble;
            },
            [this](std::uint32_t address, std::uint8_t value) {
                if (address == z80_tc0140syt_port_addr) {
                    sound_z80_port = static_cast<std::uint8_t>(value & 0x0FU);
                    sound_z80_read_high = false;
                    sound_z80_write_high = false;
                    return;
                }
                if (sound_z80_port == 4U) {
                    return;
                }
                if (sound_z80_port == 5U) {
                    sound_comm.clear_all_pending();
                    sync_sound_irq();
                    return;
                }
                if (sound_z80_port != 0U && sound_z80_port != 2U) {
                    return;
                }
                auto& latch =
                    sound_z80_port == 2U ? latch_z80_to_68k_port2 : latch_z80_to_68k;
                auto& pending =
                    sound_z80_port == 2U ? sound_reply_pending_port2 : sound_reply_pending;
                const std::uint8_t nibble = static_cast<std::uint8_t>(value & 0x0FU);
                if (sound_z80_write_high) {
                    latch = static_cast<std::uint8_t>((latch & 0x0FU) | (nibble << 4U));
                    pending = true;
                    sound_comm.note_reply_write(sound_z80_port == 2U ? 1U : 0U, latch);
                } else {
                    latch = static_cast<std::uint8_t>((latch & 0xF0U) | nibble);
                }
                sound_z80_write_high = !sound_z80_write_high;
            },
            0);
        sound_bus.map_mmio(
            z80_bank_reg, 0x01U,
            [](std::uint32_t) -> std::uint8_t { return 0xFFU; },
            [this](std::uint32_t, std::uint8_t value) {
                write_sound_bank(value);
            },
            0);
        sound_cpu.attach_bus(sound_bus);

        const auto& adpcma = pinned_region(roms, "adpcma", 0U);
        opnb.adpcm_a_block().set_sample_rom(std::span<const std::uint8_t>(adpcma));
        const auto& adpcmb = pinned_region(roms, "adpcmb", 0U);
        opnb.adpcm_b_block().set_sample_rom(std::span<const std::uint8_t>(adpcmb));

        video.attach_tile_ram(tile_ram);
        video.attach_secondary_tile_ram(tile_ram_secondary);
        video.attach_sprite_ram(sprite_ram);
        video.attach_sprite_extension_ram(sprite_extension_ram);
        video.attach_palette(palette_ram);
        video.attach_tile_gfx(std::span<const std::uint8_t>(tiles));
        video.attach_secondary_tile_gfx(std::span<const std::uint8_t>(tiles_secondary));
        video.attach_sprite_gfx(std::span<const std::uint8_t>(sprites));
        video.attach_text_gfx(std::span<const std::uint8_t>(program));
        video.attach_roz_ram(roz_ram);
        video.attach_roz_gfx(std::span<const std::uint8_t>(roz));

        video.set_vblank_callback([this](std::uint32_t) { raise_vblank_irq(); });
        main_cpu.set_irq_ack_callback([this](int level) {
            acknowledge_main_irq(
                static_cast<std::uint8_t>(level < 0 ? 0 : (level > 7 ? 7 : level)));
        });
        opnb.set_irq([this](bool) { sync_sound_irq(); });

        main_cpu.reset(chips::reset_kind::power_on);
        sound_cpu.reset(chips::reset_kind::power_on);
        opnb.reset(chips::reset_kind::power_on);
        sound_comm.reset(chips::reset_kind::power_on);
        video.reset(chips::reset_kind::power_on);
        configure_video_variant();
        push_video_regs_to_chip();
        update_io_output_state();
        update_io_access_state();
        update_palette_write_state();
        update_irq_state();
        update_board_profile_state();
        update_sound_reset_state();
        update_watchdog_state();
        update_main_bus_state();
    }

    bool taito_f2_system::uses_real_map() const noexcept {
        return params.address_map != taito_f2_address_map::synthetic;
    }

    std::uint32_t taito_f2_system::z80_bank_rom_base() const noexcept {
        return static_cast<std::uint32_t>(z80_sound_bank_page()) *
               static_cast<std::uint32_t>(z80_bank_window);
    }

    void taito_f2_system::record_io_output_write(std::uint32_t window_base,
                                                 std::uint32_t address,
                                                 std::uint8_t value) noexcept {
        const std::uint32_t relative = address - window_base;
        const std::size_t index =
            std::min<std::size_t>(static_cast<std::size_t>(relative),
                                  io_output_regs.size() - 1U);
        io_output_regs[index] = value;
        last_io_output_address = address;
        last_io_output_value = value;
        if (io_output_write_count != 0xFFFFFFFFU) {
            ++io_output_write_count;
        }

        if (relative <= 1U) {
            const std::size_t counter_slots = coin_counter_slot_count(params);
            for (std::uint8_t i = 0U; i < counter_slots; ++i) {
                const auto mask = static_cast<std::uint8_t>(1U << i);
                const bool line = (value & mask) != 0U;
                if (line && !coin_counter_lines[i]) {
                    ++coin_counters[i];
                }
                coin_counter_lines[i] = line;
            }
            for (std::uint8_t i = 0U; i < coin_lockouts.size(); ++i) {
                coin_lockouts[i] = ((value >> (4U + i)) & 1U) != 0U;
            }
            io_output_latch = value;
        }

        update_io_output_state();
        record_io_access(window_base, address, value, true, false, false);
    }

    void taito_f2_system::record_io_input_read(std::uint32_t window_base,
                                               std::uint32_t address,
                                               std::uint8_t value,
                                               bool dip_read,
                                               bool service_read) noexcept {
        record_io_access(window_base, address, value, false, dip_read, service_read);
    }

    void taito_f2_system::record_io_access(std::uint32_t window_base,
                                           std::uint32_t address,
                                           std::uint8_t value,
                                           bool write,
                                           bool dip_read,
                                           bool service_read) noexcept {
        const auto bump = [](std::uint32_t& counter) noexcept {
            if (counter != 0xFFFFFFFFU) {
                ++counter;
            }
        };

        const std::uint8_t window_id =
            static_cast<std::uint8_t>(io_access_window_for(params, window_base));
        const bool inferred_pair =
            previous_io_access_valid && previous_io_access_write == write &&
            previous_io_access_window == window_id &&
            previous_io_access_address + 1U == address &&
            (previous_io_access_address & 1U) == 0U;

        if (write) {
            if ((address & 1U) == 0U) {
                bump(io_access_write_even_count);
            } else {
                bump(io_access_write_odd_count);
            }
            if (inferred_pair) {
                bump(io_access_inferred_write_pair_count);
            }
        } else {
            bump(io_input_read_count);
            if (dip_read) {
                bump(io_input_dip_read_count);
            }
            if (service_read) {
                bump(io_input_service_read_count);
            }
            if ((address & 1U) == 0U) {
                bump(io_access_read_even_count);
            } else {
                bump(io_access_read_odd_count);
            }
            if (inferred_pair) {
                bump(io_access_inferred_read_pair_count);
            }
        }

        last_io_access_address = address;
        last_io_access_value = value;
        last_io_access_window = window_id;
        last_io_access_write = write;
        last_io_access_pair_inferred = inferred_pair;
        previous_io_access_address = address;
        previous_io_access_window = window_id;
        previous_io_access_write = write;
        previous_io_access_valid = true;
        update_io_access_state();
    }

    void taito_f2_system::update_io_output_state() noexcept {
        io_output_state.fill(0U);
        io_output_state[0] = io_output_write_count != 0U ? 1U : 0U;
        io_output_state[1] = static_cast<std::uint8_t>(last_io_output_address & 0x1FU);
        io_output_state[2] = last_io_output_value;
        io_output_state[3] = io_output_latch;
        const std::size_t counter_slots = coin_counter_slot_count(params);
        for (std::uint8_t i = 0U; i < counter_slots; ++i) {
            if (coin_counter_lines[i]) {
                io_output_state[4] = static_cast<std::uint8_t>(io_output_state[4] | (1U << i));
            }
        }
        for (std::uint8_t i = 0U; i < coin_lockouts.size(); ++i) {
            if (coin_lockouts[i]) {
                io_output_state[5] = static_cast<std::uint8_t>(io_output_state[5] | (1U << i));
            }
        }
        auto put_u32 = [this](std::size_t at, std::uint32_t value) {
            io_output_state[at + 0U] = static_cast<std::uint8_t>(value);
            io_output_state[at + 1U] = static_cast<std::uint8_t>(value >> 8U);
            io_output_state[at + 2U] = static_cast<std::uint8_t>(value >> 16U);
            io_output_state[at + 3U] = static_cast<std::uint8_t>(value >> 24U);
        };
        put_u32(6U, io_output_write_count);
        put_u32(10U, coin_counters[0]);
        put_u32(14U, coin_counters[1]);
        put_u32(18U, coin_counters[2]);
        put_u32(22U, coin_counters[3]);
        io_output_state[26] = static_cast<std::uint8_t>(counter_slots);
        io_output_state[27] = static_cast<std::uint8_t>(coin_lockouts.size());
        put_u32(28U, last_io_output_address);
        io_output_state[32] = static_cast<std::uint8_t>(params.address_map);
        io_output_state[33] = static_cast<std::uint8_t>(params.io_profile);
        io_output_state[34] = static_cast<std::uint8_t>(params.aux_profile);
        io_output_state[35] = cabinet_test_switch_mask(params);
        io_output_state[36] = four_player_service_mask(params);
        io_output_state[37] = input_system;
        io_output_state[38] = input_coin_extension;
        io_output_state[39] = uses_split_panel_input(params) ? 1U : 0U;
    }

    void taito_f2_system::update_io_access_state() noexcept {
        io_access_state.fill(0U);

        const auto put_u32 = [this](std::size_t at, std::uint32_t value) {
            io_access_state[at + 0U] = static_cast<std::uint8_t>(value);
            io_access_state[at + 1U] = static_cast<std::uint8_t>(value >> 8U);
            io_access_state[at + 2U] = static_cast<std::uint8_t>(value >> 16U);
            io_access_state[at + 3U] = static_cast<std::uint8_t>(value >> 24U);
        };

        io_access_state[0] = 1U;
        io_access_state[1] = previous_io_access_valid ? 1U : 0U;
        io_access_state[2] = last_io_access_write ? 1U : 0U;
        io_access_state[3] = last_io_access_window;
        put_u32(4U, io_input_read_count);
        put_u32(8U, io_output_write_count);
        put_u32(12U, io_input_dip_read_count);
        put_u32(16U, io_input_service_read_count);
        put_u32(20U, io_access_read_even_count);
        put_u32(24U, io_access_read_odd_count);
        put_u32(28U, io_access_write_even_count);
        put_u32(32U, io_access_write_odd_count);
        put_u32(36U, io_access_inferred_read_pair_count);
        put_u32(40U, io_access_inferred_write_pair_count);
        put_u32(44U, last_io_access_address);
        io_access_state[48] = last_io_access_value;
        io_access_state[49] = last_io_access_pair_inferred ? 1U : 0U;
        io_access_state[50] = static_cast<std::uint8_t>(params.address_map);
        io_access_state[51] = static_cast<std::uint8_t>(params.io_profile);
        io_access_state[52] = static_cast<std::uint8_t>(params.input_profile);
        io_access_state[53] = static_cast<std::uint8_t>(params.aux_profile);
        io_access_state[54] = input_system;
        io_access_state[55] = input_coin_extension;
        io_access_state[56] = cabinet_test_switch_mask(params);
        io_access_state[57] = four_player_service_mask(params);
        io_access_state[58] = uses_split_panel_input(params) ? 1U : 0U;
        io_access_state[59] = previous_io_access_valid ? 1U : 0U;
        put_u32(60U, previous_io_access_address);
    }

    void taito_f2_system::record_palette_write(std::uint32_t window_base,
                                               std::uint32_t address,
                                               std::uint8_t value) noexcept {
        const std::size_t relative = static_cast<std::size_t>(address - window_base);
        const std::size_t word_offset = relative & ~std::size_t{1U};

        last_palette_write_address = address;
        last_palette_write_value = value;
        if (palette_write_count != 0xFFFFFFFFU) {
            ++palette_write_count;
        }

        if (word_offset + 1U < palette_ram.size()) {
            last_palette_index = static_cast<std::uint16_t>(word_offset / 2U);
            last_palette_word = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(palette_ram[word_offset]) << 8U) |
                palette_ram[word_offset + 1U]);
            last_palette_color = chips::video::taito_f2_video::decode_color(
                active_video_palette_format(), last_palette_word);
        } else {
            last_palette_index = 0U;
            last_palette_word = 0U;
            last_palette_color = 0U;
        }

        update_palette_write_state();
    }

    void taito_f2_system::record_palette_read(std::uint32_t window_base,
                                              std::uint32_t address,
                                              std::uint8_t value) noexcept {
        const std::size_t relative = static_cast<std::size_t>(address - window_base);
        const std::size_t word_offset = relative & ~std::size_t{1U};

        last_palette_read_address = address;
        last_palette_read_value = value;
        if (palette_read_count != 0xFFFFFFFFU) {
            ++palette_read_count;
        }

        if (word_offset + 1U < palette_ram.size()) {
            last_palette_read_index = static_cast<std::uint16_t>(word_offset / 2U);
            last_palette_read_word = static_cast<std::uint16_t>(
                (static_cast<std::uint16_t>(palette_ram[word_offset]) << 8U) |
                palette_ram[word_offset + 1U]);
            last_palette_read_color = chips::video::taito_f2_video::decode_color(
                active_video_palette_format(), last_palette_read_word);
        } else {
            last_palette_read_index = 0U;
            last_palette_read_word = 0U;
            last_palette_read_color = 0U;
        }

        update_palette_write_state();
    }

    void taito_f2_system::update_palette_write_state() noexcept {
        palette_write_state.fill(0U);
        palette_write_state[0] = palette_write_count != 0U ? 1U : 0U;
        palette_write_state[1] = static_cast<std::uint8_t>(params.palette_format);
        palette_write_state[2] = last_palette_write_value;
        palette_write_state[3] = static_cast<std::uint8_t>(params.address_map);
        palette_write_state[20] = palette_read_count != 0U ? 1U : 0U;
        palette_write_state[21] = last_palette_read_value;
        palette_write_state[22] = static_cast<std::uint8_t>(params.palette_profile);
        palette_write_state[23] =
            (palette_write_count != 0U && palette_read_count != 0U &&
             last_palette_read_index == last_palette_index &&
             last_palette_read_word == last_palette_word)
                ? 1U
                : 0U;
        auto put_u16 = [this](std::size_t at, std::uint16_t value) {
            palette_write_state[at + 0U] = static_cast<std::uint8_t>(value);
            palette_write_state[at + 1U] = static_cast<std::uint8_t>(value >> 8U);
        };
        auto put_u32 = [this](std::size_t at, std::uint32_t value) {
            palette_write_state[at + 0U] = static_cast<std::uint8_t>(value);
            palette_write_state[at + 1U] = static_cast<std::uint8_t>(value >> 8U);
            palette_write_state[at + 2U] = static_cast<std::uint8_t>(value >> 16U);
            palette_write_state[at + 3U] = static_cast<std::uint8_t>(value >> 24U);
        };
        put_u32(4U, palette_write_count);
        put_u32(8U, last_palette_write_address);
        put_u16(12U, last_palette_word);
        put_u16(14U, last_palette_index);
        put_u32(16U, last_palette_color);
        put_u32(24U, palette_read_count);
        put_u32(28U, last_palette_read_address);
        put_u16(32U, last_palette_read_word);
        put_u16(34U, last_palette_read_index);
        put_u32(36U, last_palette_read_color);
    }

    void taito_f2_system::update_irq_state() noexcept {
        irq_state.fill(0U);
        irq_state[0] = params.vblank_irq_level;
        irq_state[1] = params.sprite_dma_irq_level;
        irq_state[2] = last_vblank_irq_level;
        irq_state[3] = last_irq_ack_level;
        irq_state[22] = last_sprite_dma_irq_level;
        irq_state[23] = last_sprite_dma_irq_ack_level;
        auto put_u64 = [this](std::size_t at, std::uint64_t value) {
            for (std::size_t i = 0; i < sizeof(std::uint64_t); ++i) {
                irq_state[at + i] =
                    static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
            }
        };
        put_u64(4U, vblank_irq_raised);
        put_u64(12U, vblank_irq_acked);
        put_u64(24U, sprite_dma_irq_raised);
        put_u64(32U, sprite_dma_irq_acked);
        irq_state[20] = vblank_irq_raised != 0U ? 1U : 0U;
        irq_state[21] = vblank_irq_acked != 0U ? 1U : 0U;
        irq_state[40] = sprite_dma_irq_raised != 0U ? 1U : 0U;
        irq_state[41] = sprite_dma_irq_acked != 0U ? 1U : 0U;
        irq_state[42] = vblank_irq_pending ? 1U : 0U;
        irq_state[43] = sprite_dma_irq_pending ? 1U : 0U;
        std::uint8_t line_level = 0U;
        if (vblank_irq_pending && params.vblank_irq_level > line_level) {
            line_level = params.vblank_irq_level;
        }
        if (sprite_dma_irq_pending && params.sprite_dma_irq_level > line_level) {
            line_level = params.sprite_dma_irq_level;
        }
        irq_state[44] = line_level;
    }

    void taito_f2_system::update_main_irq_line() noexcept {
        std::uint8_t level = 0U;
        if (vblank_irq_pending && params.vblank_irq_level > level) {
            level = params.vblank_irq_level;
        }
        if (sprite_dma_irq_pending && params.sprite_dma_irq_level > level) {
            level = params.sprite_dma_irq_level;
        }
        main_cpu.set_irq_level(level);
    }

    void taito_f2_system::raise_vblank_irq() noexcept {
        if (params.vblank_irq_level == 0U) {
            return;
        }
        last_vblank_irq_level = params.vblank_irq_level;
        vblank_irq_pending = true;
        ++vblank_irq_raised;
        update_main_irq_line();
        update_irq_state();
    }

    void taito_f2_system::raise_sprite_dma_irq() noexcept {
        if (params.sprite_dma_irq_level == 0U) {
            return;
        }
        last_sprite_dma_irq_level = params.sprite_dma_irq_level;
        sprite_dma_irq_pending = true;
        ++sprite_dma_irq_raised;
        update_main_irq_line();
        update_irq_state();
    }

    void taito_f2_system::acknowledge_main_irq(std::uint8_t level) noexcept {
        if (vblank_irq_pending && level == params.vblank_irq_level) {
            vblank_irq_pending = false;
            last_irq_ack_level = level;
            ++vblank_irq_acked;
        }
        if (sprite_dma_irq_pending && level == params.sprite_dma_irq_level) {
            sprite_dma_irq_pending = false;
            last_sprite_dma_irq_ack_level = level;
            ++sprite_dma_irq_acked;
        }
        update_main_irq_line();
        update_irq_state();
    }

    void taito_f2_system::board_latch_sprites(bool assert_dma_irq) noexcept {
        video.latch_sprites();
        if (assert_dma_irq) {
            raise_sprite_dma_irq();
        }
    }

    void taito_f2_system::update_board_profile_state() noexcept {
        board_profile_state.fill(0U);

        const bool palette_supported =
            params.palette_profile == taito_f2_palette_profile::tc0110pcr_tc0070rgb ||
            params.palette_profile == taito_f2_palette_profile::tc0260dar;
        const bool sprite_pair_supported =
            params.sprite_chip_pair == taito_f2_sprite_chip_pair::tc0200obj_tc0210fbc;
        const bool sound_comm_supported =
            params.sound_comm_chip == taito_f2_sound_comm_chip::tc0140syt;
        const bool aux_supported = params.aux_profile == taito_f2_aux_profile::none;
        const bool irq_supported =
            params.vblank_irq_level == 5U && params.sprite_dma_irq_level == 6U;
        const bool clock_profile_supported =
            m68k_clock_hz == 12'000'000U && z80_clock_hz == 4'000'000U &&
            ym2610_clock_hz == 8'000'000U;
        const bool implemented_profile =
            palette_supported && sprite_pair_supported && sound_comm_supported &&
            aux_supported && irq_supported;

        auto put_u16 = [this](std::size_t at, std::uint16_t value) {
            board_profile_state[at + 0U] = static_cast<std::uint8_t>(value);
            board_profile_state[at + 1U] = static_cast<std::uint8_t>(value >> 8U);
        };
        auto put_i16 = [&put_u16](std::size_t at, int value) {
            put_u16(at, static_cast<std::uint16_t>(static_cast<std::int16_t>(value)));
        };
        auto put_u32 = [this](std::size_t at, std::uint32_t value) {
            board_profile_state[at + 0U] = static_cast<std::uint8_t>(value);
            board_profile_state[at + 1U] = static_cast<std::uint8_t>(value >> 8U);
            board_profile_state[at + 2U] = static_cast<std::uint8_t>(value >> 16U);
            board_profile_state[at + 3U] = static_cast<std::uint8_t>(value >> 24U);
        };

        board_profile_state[0] = 1U;
        board_profile_state[1] = params.vertical ? 1U : 0U;
        board_profile_state[2] = params.players;
        board_profile_state[3] = static_cast<std::uint8_t>(params.address_map);
        board_profile_state[4] = static_cast<std::uint8_t>(params.sprite_policy);
        board_profile_state[5] = static_cast<std::uint8_t>(params.sprite_active_area);
        board_profile_state[6] = static_cast<std::uint8_t>(params.sprite_buffering);
        board_profile_state[7] = static_cast<std::uint8_t>(params.palette_format);
        board_profile_state[8] = static_cast<std::uint8_t>(params.text_gfx_source);
        board_profile_state[9] = static_cast<std::uint8_t>(params.input_profile);
        board_profile_state[10] = static_cast<std::uint8_t>(params.io_profile);
        board_profile_state[11] = static_cast<std::uint8_t>(params.palette_profile);
        board_profile_state[12] = static_cast<std::uint8_t>(params.priority_profile);
        board_profile_state[13] = static_cast<std::uint8_t>(params.sprite_chip_pair);
        board_profile_state[14] = static_cast<std::uint8_t>(params.sound_comm_chip);
        board_profile_state[15] = static_cast<std::uint8_t>(params.video_profile);
        board_profile_state[16] = static_cast<std::uint8_t>(params.tc0480scp_profile);
        board_profile_state[17] = static_cast<std::uint8_t>(params.aux_profile);
        board_profile_state[18] = params.vblank_irq_level;
        board_profile_state[19] = params.sprite_dma_irq_level;
        board_profile_state[20] =
            static_cast<std::uint8_t>((palette_supported ? 0x01U : 0U) |
                                      (sprite_pair_supported ? 0x02U : 0U) |
                                      (sound_comm_supported ? 0x04U : 0U) |
                                      (aux_supported ? 0x08U : 0U) |
                                      (irq_supported ? 0x10U : 0U) |
                                      (implemented_profile ? 0x20U : 0U) |
                                      (clock_profile_supported ? 0x80U : 0U));
        board_profile_state[21] =
            static_cast<std::uint8_t>((params.vertical ? 0x01U : 0U) |
                                      0x02U |
                                      (params.vertical ? 0x04U : 0U) |
                                      (uses_real_map() ? 0x08U : 0U));
        board_profile_state[22] = z80_sound_bank_mask;
        board_profile_state[23] = z80_sound_bank_page_valid() ? 1U : 0U;
        put_u32(24U, m68k_clock_hz);
        put_u32(28U, z80_clock_hz);
        put_u32(32U, ym2610_clock_hz);
        put_u32(36U, frame_rate_hz);
        put_u32(40U, chips::video::taito_f2_video::visible_width);
        put_u32(44U, chips::video::taito_f2_video::visible_height);
        put_u32(48U, chips::video::taito_f2_video::line_pixels);
        put_u32(52U, chips::video::taito_f2_video::frame_lines);
        put_u32(56U, chips::video::taito_f2_video::vblank_start);
        put_u32(60U, static_cast<std::uint32_t>(z80_fixed_rom_window));
        put_u32(64U, static_cast<std::uint32_t>(z80_bank_window));
        put_u32(68U, static_cast<std::uint32_t>(z80_ram_size));
        put_u32(72U, sound_rom_size);
        put_u32(76U, z80_sound_bank_page_count());
        put_u32(80U, params.text_gfx_base);
        put_i16(84U, params.tc0100scn_bg_x_offset);
        put_i16(86U, params.tc0100scn_text_x_offset);
        put_i16(88U, params.tc0100scn_text_y_origin);
        put_i16(90U, params.tc0100scn_positive_text_y_origin);
        put_i16(92U, params.roz_x_offset);
        put_i16(94U, params.roz_y_offset);
    }

    std::uint8_t taito_f2_system::z80_sound_bank_page() const noexcept {
        return static_cast<std::uint8_t>(sound_bank & z80_sound_bank_mask);
    }

    std::uint32_t taito_f2_system::z80_sound_bank_page_count() const noexcept {
        const std::uint32_t page_count = std::max<std::uint32_t>(
            1U, (sound_rom_size + static_cast<std::uint32_t>(z80_bank_window) - 1U) /
                    static_cast<std::uint32_t>(z80_bank_window));
        return page_count;
    }

    bool taito_f2_system::z80_sound_bank_page_valid() const noexcept {
        return static_cast<std::uint32_t>(z80_sound_bank_page()) < z80_sound_bank_page_count();
    }

    void taito_f2_system::write_sound_bank(std::uint8_t value) noexcept {
        sound_bank = value;
        update_sound_bank_state();
        update_board_profile_state();
    }

    void taito_f2_system::update_sound_bank_state() noexcept {
        const std::uint32_t page_count = z80_sound_bank_page_count();
        sound_bank_state[0] = sound_bank;
        sound_bank_state[1] = z80_sound_bank_page();
        sound_bank_state[2] = static_cast<std::uint8_t>(std::min<std::uint32_t>(page_count, 0xFFU));
        sound_bank_state[3] = z80_sound_bank_page_valid() ? 1U : 0U;
    }

    void taito_f2_system::write_sound_reset_control(std::uint32_t address,
                                                    std::uint8_t value) noexcept {
        const auto bump = [](std::uint32_t& counter) noexcept {
            if (counter != 0xFFFFFFFFU) {
                ++counter;
            }
        };
        const bool asserted = (value & 0x0FU) != 0U;
        const bool was_asserted = sound_cpu.reset_line_held();

        last_sound_reset_control_address = address;
        last_sound_reset_control_value = static_cast<std::uint8_t>(value & 0x0FU);
        bump(sound_reset_control_write_count);

        if (asserted != was_asserted) {
            if (asserted) {
                bump(sound_reset_assert_count);
            } else {
                bump(sound_reset_release_count);
            }
            sound_cpu.set_reset_line(asserted);
            z80_cycle_accum_ = 0U;
            sync_sound_irq();
        }

        update_sound_reset_state();
    }

    void taito_f2_system::update_sound_reset_state() noexcept {
        sound_reset_state.fill(0U);
        const auto regs = sound_cpu.cpu_registers();
        const auto put_u16 = [this](std::size_t at, std::uint16_t value) {
            sound_reset_state[at + 0U] = static_cast<std::uint8_t>(value);
            sound_reset_state[at + 1U] = static_cast<std::uint8_t>(value >> 8U);
        };
        const auto put_u32 = [this](std::size_t at, std::uint32_t value) {
            sound_reset_state[at + 0U] = static_cast<std::uint8_t>(value);
            sound_reset_state[at + 1U] = static_cast<std::uint8_t>(value >> 8U);
            sound_reset_state[at + 2U] = static_cast<std::uint8_t>(value >> 16U);
            sound_reset_state[at + 3U] = static_cast<std::uint8_t>(value >> 24U);
        };
        const auto clamp_u64 = [](std::uint64_t value) noexcept -> std::uint32_t {
            return value > 0xFFFFFFFFULL ? 0xFFFFFFFFU : static_cast<std::uint32_t>(value);
        };

        sound_reset_state[0] = 1U;
        sound_reset_state[1] = sound_cpu.reset_line_held() ? 1U : 0U;
        sound_reset_state[2] = last_sound_reset_control_value;
        sound_reset_state[3] = sound_reset_control_write_count != 0U ? 1U : 0U;
        put_u32(4U, sound_reset_control_write_count);
        put_u32(8U, sound_reset_assert_count);
        put_u32(12U, sound_reset_release_count);
        put_u32(16U, last_sound_reset_control_address);
        put_u16(20U, regs.pc);
        sound_reset_state[22] = sound_cpu.reset_line_held() ? 1U : 0U;
        sound_reset_state[23] =
            params.sound_comm_chip == taito_f2_sound_comm_chip::tc0140syt ? 1U : 0U;
        sound_reset_state[24] = sound_rom_size >= z80_fixed_rom_window ? 1U : 0U;
        sound_reset_state[25] = sound_main_port;
        sound_reset_state[26] = sound_z80_port;
        sound_reset_state[27] = z80_sound_bank_page();
        put_u32(28U, clamp_u64(sound_cpu.nmi_accept_count()));
        put_u32(32U, clamp_u64(sound_cpu.irq_accept_count()));
        sound_reset_state[36] = sound_comm.status();
        sound_reset_state[37] = sound_comm.command_pending_mask();
        sound_reset_state[38] = sound_comm.reply_pending_mask();
        sound_reset_state[39] = z80_sound_bank_page_valid() ? 1U : 0U;
    }

    void taito_f2_system::record_watchdog_write(std::uint32_t address,
                                                std::uint8_t value,
                                                std::uint8_t window_id,
                                                bool confirmed) noexcept {
        const auto bump = [](std::uint32_t& counter) noexcept {
            if (counter != 0xFFFFFFFFU) {
                ++counter;
            }
        };

        bump(watchdog_write_count);
        if (confirmed) {
            bump(watchdog_confirmed_write_count);
        } else {
            bump(watchdog_suspect_write_count);
        }
        last_watchdog_address = address;
        last_watchdog_value = value;
        last_watchdog_window = window_id;
        last_watchdog_confirmed = confirmed;
        update_watchdog_state();
    }

    void taito_f2_system::record_integrated_io_watchdog_write(
        std::uint32_t window_base, std::uint32_t address, std::uint8_t value) noexcept {
        if (!io_write_resets_integrated_watchdog(params, window_base, address)) {
            return;
        }
        record_watchdog_write(address, value,
                              static_cast<std::uint8_t>(
                                  integrated_io_watchdog_window(params)),
                              true);
    }

    void taito_f2_system::update_watchdog_state() noexcept {
        watchdog_state.fill(0U);

        const auto put_u32 = [this](std::size_t at, std::uint32_t value) {
            watchdog_state[at + 0U] = static_cast<std::uint8_t>(value);
            watchdog_state[at + 1U] = static_cast<std::uint8_t>(value >> 8U);
            watchdog_state[at + 2U] = static_cast<std::uint8_t>(value >> 16U);
            watchdog_state[at + 3U] = static_cast<std::uint8_t>(value >> 24U);
        };

        const std::uint32_t confirmed_address =
            confirmed_watchdog_address(params, uses_real_map());
        const std::uint32_t suspect_address =
            suspect_watchdog_address(params.address_map);
        const std::uint32_t priority_suspect_address =
            priority_watchdog_like_address(params.address_map);
        const bool confirmed_window_present = confirmed_address != 0U;
        const bool suspect_window_present =
            suspect_address != 0U || priority_suspect_address != 0U;

        watchdog_state[0] = 1U;
        watchdog_state[1] = watchdog_write_count != 0U ? 1U : 0U;
        watchdog_state[2] = watchdog_confirmed_write_count != 0U ? 1U : 0U;
        watchdog_state[3] = watchdog_suspect_write_count != 0U ? 1U : 0U;
        put_u32(4U, watchdog_write_count);
        put_u32(8U, watchdog_confirmed_write_count);
        put_u32(12U, watchdog_suspect_write_count);
        put_u32(16U, last_watchdog_address);
        watchdog_state[20] = last_watchdog_value;
        watchdog_state[21] = last_watchdog_window;
        watchdog_state[22] = static_cast<std::uint8_t>(params.address_map);
        watchdog_state[23] = static_cast<std::uint8_t>(params.io_profile);
        put_u32(24U, confirmed_address);
        put_u32(28U, suspect_address != 0U ? suspect_address : priority_suspect_address);
        watchdog_state[32] = confirmed_window_present ? 1U : 0U;
        watchdog_state[33] = suspect_window_present ? 1U : 0U;
        watchdog_state[34] = 0U;
        watchdog_state[35] = 0U;
        put_u32(36U, priority_suspect_address);
        put_u32(40U, 0U);
        watchdog_state[44] = last_watchdog_confirmed ? 1U : 0U;
        watchdog_state[45] = 0U;
        watchdog_state[46] = static_cast<std::uint8_t>(watchdog_window);
        watchdog_state[47] = uses_real_map() ? 1U : 0U;
    }

    bool taito_f2_system::main_bus_address_mapped(std::uint32_t address,
                                                  bool) const noexcept {
        const auto contains = [address](std::uint32_t base, std::uint32_t size) noexcept {
            return size != 0U && address >= base && (address - base) < size;
        };

        const bool real_map = uses_real_map();
        if (contains(program_rom_base, static_cast<std::uint32_t>(main_rom_size)) ||
            contains(work_ram_address(params.address_map),
                     static_cast<std::uint32_t>(work_ram.size())) ||
            contains(palette_ram_address(params.address_map),
                     static_cast<std::uint32_t>(palette_ram.size())) ||
            contains(tile_ram_address(params.address_map, real_map),
                     static_cast<std::uint32_t>(tile_ram.size())) ||
            contains(sprite_ram_address(params.address_map, real_map),
                     static_cast<std::uint32_t>(sprite_ram.size()))) {
            return true;
        }

        if (uses_dual_tc0100scn(params) &&
            contains(thundfox_secondary_tile_ram_base,
                     static_cast<std::uint32_t>(tile_ram_secondary.size()))) {
            return true;
        }
        if (has_roz_tilemap(params) &&
            contains(roz_ram_address(params.address_map),
                     static_cast<std::uint32_t>(roz_ram.size()))) {
            return true;
        }
        if (params.sprite_extension_base.has_value()) {
            const std::uint32_t configured_size = params.sprite_extension_size.value_or(
                static_cast<std::uint32_t>(sprite_extension_ram.size()));
            const std::uint32_t window_size = std::min<std::uint32_t>(
                configured_size, static_cast<std::uint32_t>(sprite_extension_ram.size()));
            if (contains(*params.sprite_extension_base, window_size)) {
                return true;
            }
        }

        const std::uint32_t video_base = video_reg_address(params.address_map, real_map);
        if (!uses_tc0480scp(params)) {
            if (contains(video_base, video_reg_window)) {
                return true;
            }
            if (uses_dual_tc0100scn(params) &&
                contains(thundfox_secondary_video_reg_base, video_reg_window)) {
                return true;
            }
        }

        if (real_map) {
            if (params.sprite_policy == taito_f2_sprite_policy::banked &&
                contains(sprite_bank_address(params.address_map), real_sprite_bank_window)) {
                return true;
            }
            if (contains(sound_comm_address(params.address_map), 0x04U)) {
                return true;
            }

            const std::uint32_t watchdog_base =
                confirmed_watchdog_address(params.address_map);
            if (watchdog_base != 0U && watchdog_base != quizhq_watchdog_base &&
                contains(watchdog_base, watchdog_window)) {
                return true;
            }
            const std::uint32_t suspect_watchdog_base =
                suspect_watchdog_address(params.address_map);
            if (suspect_watchdog_base != 0U &&
                contains(suspect_watchdog_base, watchdog_window)) {
                return true;
            }
            if (has_priority_window(params) &&
                contains(priority_address(params.address_map), real_priority_window)) {
                return true;
            }
            if (has_roz_tilemap(params) &&
                contains(roz_control_address(params.address_map),
                         dondokod_roz_control_window)) {
                return true;
            }
            if (uses_tc0480scp(params) &&
                contains(video_base, metalb_tc0480scp_control_window)) {
                return true;
            }
        } else if (contains(comm_base, comm_window)) {
            return true;
        }

        if (params.address_map == taito_f2_address_map::quizhq) {
            return contains(quizhq_input_a_base, 0x04U) ||
                   contains(quizhq_input_b_base, 0x06U);
        }
        if (params.address_map == taito_f2_address_map::growl ||
            params.address_map == taito_f2_address_map::solfigtr) {
            return contains(growl_dip_input_base, 0x06U) ||
                   contains(growl_player_input_base, 0x06U) ||
                   contains(growl_p3_input_base, 0x10U) ||
                   contains(growl_p4_input_base, 0x10U);
        }
        if (params.address_map == taito_f2_address_map::ninjak) {
            return contains(ninjak_input_base, ninjak_input_window);
        }
        return contains(input_address(params.address_map, real_map),
                        input_window_size(params.address_map));
    }

    void taito_f2_system::record_main_bus_access(
        const topology::access_event& event) noexcept {
        const auto bump = [](std::uint32_t& counter) noexcept {
            if (counter != 0xFFFFFFFFU) {
                ++counter;
            }
        };

        const bool mapped = main_bus_address_mapped(event.address, event.write);
        const bool open_bus_read = !event.write && !mapped && event.value == 0xFFU;
        const bool inferred_pair =
            previous_main_bus_valid && previous_main_bus_write == event.write &&
            previous_main_bus_address + 1U == event.address &&
            (previous_main_bus_address & 1U) == 0U;

        if (event.write) {
            bump(main_bus_write_count);
            if (!mapped) {
                bump(main_bus_unmapped_write_count);
            }
        } else {
            bump(main_bus_read_count);
            if (open_bus_read) {
                bump(main_bus_open_bus_read_count);
            }
        }
        if ((event.address & 1U) != 0U) {
            bump(main_bus_odd_access_count);
        }
        if (inferred_pair) {
            bump(main_bus_inferred_word_pair_count);
        }

        last_main_bus_address = event.address;
        last_main_bus_value = event.value;
        last_main_bus_write = event.write;
        last_main_bus_mapped = mapped;
        last_main_bus_open_bus = open_bus_read;
        last_main_bus_pair_inferred = inferred_pair;

        previous_main_bus_address = event.address;
        previous_main_bus_value = event.value;
        previous_main_bus_write = event.write;
        previous_main_bus_mapped = mapped;
        previous_main_bus_valid = true;
        update_main_bus_state();
    }

    void taito_f2_system::update_main_bus_state() noexcept {
        main_bus_state.fill(0U);

        const auto put_u32 = [this](std::size_t at, std::uint32_t value) {
            main_bus_state[at + 0U] = static_cast<std::uint8_t>(value);
            main_bus_state[at + 1U] = static_cast<std::uint8_t>(value >> 8U);
            main_bus_state[at + 2U] = static_cast<std::uint8_t>(value >> 16U);
            main_bus_state[at + 3U] = static_cast<std::uint8_t>(value >> 24U);
        };

        main_bus_state[0] = 1U;
        main_bus_state[1] = 1U;
        main_bus_state[2] = last_main_bus_write ? 1U : 0U;
        main_bus_state[3] = last_main_bus_mapped ? 1U : 0U;
        put_u32(4U, main_bus_read_count);
        put_u32(8U, main_bus_write_count);
        put_u32(12U, main_bus_open_bus_read_count);
        put_u32(16U, main_bus_unmapped_write_count);
        put_u32(20U, main_bus_odd_access_count);
        put_u32(24U, main_bus_inferred_word_pair_count);
        put_u32(28U, last_main_bus_address);
        main_bus_state[32] = last_main_bus_value;
        main_bus_state[33] = last_main_bus_open_bus ? 1U : 0U;
        main_bus_state[34] = previous_main_bus_valid ? 1U : 0U;
        main_bus_state[35] = last_main_bus_pair_inferred ? 1U : 0U;
        main_bus_state[36] = static_cast<std::uint8_t>(params.address_map);
        main_bus_state[37] = uses_real_map() ? 1U : 0U;
        main_bus_state[38] = 1U;
        main_bus_state[39] = 0U;
        put_u32(40U, previous_main_bus_address);
        main_bus_state[44] = previous_main_bus_value;
        main_bus_state[45] = previous_main_bus_write ? 1U : 0U;
        main_bus_state[46] = previous_main_bus_mapped ? 1U : 0U;
    }

    chips::video::taito_f2_video::palette_format
    taito_f2_system::active_video_palette_format() const noexcept {
        using video_format = chips::video::taito_f2_video::palette_format;
        switch (params.palette_format) {
        case taito_f2_palette_format::rgbx_444:
            return video_format::rgbx_444;
        case taito_f2_palette_format::xrgb_555:
            return video_format::xrgb_555;
        case taito_f2_palette_format::rrrr_gggg_bbbb_rgbx:
            return video_format::rrrr_gggg_bbbb_rgbx;
        case taito_f2_palette_format::xbgr_555:
            return video_format::xbgr_555;
        }
        return video_format::xbgr_555;
    }

    void taito_f2_system::sync_sound_irq() noexcept {
        sound_cpu.set_irq_line(opnb.irq_asserted());
    }

    void taito_f2_system::pulse_sound_command_nmi() noexcept {
        sound_comm.note_command_nmi_pulse();
        sound_cpu.set_nmi_line(true);
        sound_cpu.set_nmi_line(false);
    }

    void taito_f2_system::configure_video_variant() noexcept {
        using video_chip = chips::video::taito_f2_video;
        video.set_text_base(video_chip::text_tilemap_base, params.text_gfx_base);
        video.set_text_gfx_source(params.text_gfx_source ==
                                          taito_f2_text_gfx_source::program_1bpp
                                      ? video_chip::text_gfx_source::program_1bpp
                                      : video_chip::text_gfx_source::tc0100scn_ram_2bpp);
        video.set_tc0100scn_text_y_origins(params.tc0100scn_text_y_origin,
                                           params.tc0100scn_positive_text_y_origin);
        if (uses_tc0480scp(params)) {
            video.set_tilemap_variant(video_chip::tilemap_variant::tc0480scp);
            switch (params.tc0480scp_profile) {
            case taito_f2_tc0480scp_profile::metalb:
                video.set_tc0480scp_palette_bank_base(256U);
                video.set_tc0480scp_priority_model(video_chip::tc0480scp_priority_model::metalb);
                video.set_tc0480scp_offsets(0x32 + 3, -0x04, 1, 0, -1, 0);
                break;
            case taito_f2_tc0480scp_profile::footchmp:
                video.set_tc0480scp_palette_bank_base(0U);
                video.set_tc0480scp_priority_model(
                    video_chip::tc0480scp_priority_model::deadconx_footchmp);
                video.set_tc0480scp_offsets(0x1D + 3, 0x08, -1, 0, -1, 0);
                break;
            case taito_f2_tc0480scp_profile::deadconx:
                video.set_tc0480scp_palette_bank_base(0U);
                video.set_tc0480scp_priority_model(
                    video_chip::tc0480scp_priority_model::deadconx_footchmp);
                video.set_tc0480scp_offsets(0x1E + 3, 0x08, -1, 0, -1, 0);
                break;
            case taito_f2_tc0480scp_profile::none:
                video.set_tc0480scp_palette_bank_base(0U);
                video.set_tc0480scp_priority_model(video_chip::tc0480scp_priority_model::metalb);
                video.set_tc0480scp_offsets(0, 0, 0, 0, 0, 0);
                break;
            }
        } else {
            video.set_tilemap_variant(video_chip::tilemap_variant::tc0100scn);
            video.set_tc0100scn_offsets(params.tc0100scn_bg_x_offset,
                                        params.tc0100scn_text_x_offset);
            video.set_tc0480scp_palette_bank_base(0U);
            video.set_tc0480scp_priority_model(video_chip::tc0480scp_priority_model::metalb);
            video.set_tc0480scp_offsets(0, 0, 0, 0, 0, 0);
        }
        video.set_sprite_palette_bank_base(0U);
        if (uses_dual_tc0100scn(params)) {
            video.set_tilemap_variant(video_chip::tilemap_variant::dual_tc0100scn);
            video.set_tc0100scn_offsets(params.tc0100scn_bg_x_offset,
                                        params.tc0100scn_text_x_offset);
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
        case taito_f2_palette_format::rrrr_gggg_bbbb_rgbx:
            video.set_palette_format(video_chip::palette_format::rrrr_gggg_bbbb_rgbx);
            break;
        case taito_f2_palette_format::xbgr_555:
            video.set_palette_format(video_chip::palette_format::xbgr_555);
            break;
        }

        video.set_sprite_hide_pixels(params.sprite_hide_pixels,
                                     params.sprite_flip_hide_pixels);
        video.set_roz_variant(video_chip::roz_variant::tc0280grd);
        video.set_roz_offsets(params.roz_x_offset, params.roz_y_offset);
        if (params.video_profile == taito_f2_video_profile::tc0100scn_tc0280grd) {
            video.set_roz_offsets(params.roz_x_offset, params.roz_y_offset);
        } else if (params.video_profile == taito_f2_video_profile::tc0100scn_tc0430grw) {
            video.set_roz_variant(video_chip::roz_variant::tc0430grw);
            video.set_roz_offsets(params.roz_x_offset, params.roz_y_offset);
        }
    }

    void taito_f2_system::push_video_regs_to_chip() noexcept {
        if (uses_tc0480scp(params)) {
            return;
        }
        video.set_scroll0(video_regs[0], video_regs[3]);
        video.set_scroll1(video_regs[1], video_regs[4]);
        video.set_scroll2(video_regs[2], video_regs[5]);
        video.set_layer_control(video_regs[6]);
        video.set_display_enable((video_regs[7] & 0x8000U) == 0U);
        if (uses_dual_tc0100scn(params)) {
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
        board_latch_sprites();
        run_cpus(cpu_cycles_per_frame - cpu_visible);
        video.tick(dots_total - dots_to_vblank);
        update_sound_reset_state();
        update_watchdog_state();
        update_main_bus_state();
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

    void taito_f2_system::save_state(chips::state_writer& writer) const {
        writer.u32(taito_f2_system_state_version);

        writer.u32(rom_identity_crc(roms, params));
        writer.boolean(params.vertical);
        writer.u8(params.players);
        writer.u8(static_cast<std::uint8_t>(params.address_map));
        writer.u8(static_cast<std::uint8_t>(params.sprite_policy));
        writer.u8(static_cast<std::uint8_t>(params.sprite_active_area));
        writer.u8(static_cast<std::uint8_t>(params.sprite_buffering));
        writer.u8(static_cast<std::uint8_t>(params.palette_format));
        writer.u8(static_cast<std::uint8_t>(params.text_gfx_source));
        writer.u8(static_cast<std::uint8_t>(params.input_profile));
        writer.u8(static_cast<std::uint8_t>(params.io_profile));
        writer.u8(static_cast<std::uint8_t>(params.palette_profile));
        writer.u8(static_cast<std::uint8_t>(params.priority_profile));
        writer.u8(static_cast<std::uint8_t>(params.sprite_chip_pair));
        writer.u8(static_cast<std::uint8_t>(params.sound_comm_chip));
        writer.u8(static_cast<std::uint8_t>(params.video_profile));
        writer.u8(static_cast<std::uint8_t>(params.tc0480scp_profile));
        writer.u8(static_cast<std::uint8_t>(params.aux_profile));
        writer.u8(params.vblank_irq_level);
        writer.u8(params.sprite_dma_irq_level);
        writer.u32(params.text_gfx_base);
        write_i32(writer, params.tc0100scn_bg_x_offset);
        write_i32(writer, params.tc0100scn_text_x_offset);
        write_i32(writer, params.tc0100scn_text_y_origin);
        write_i32(writer, params.tc0100scn_positive_text_y_origin);
        write_i32(writer, params.roz_x_offset);
        write_i32(writer, params.roz_y_offset);
        write_i32(writer, params.sprite_hide_pixels);
        write_i32(writer, params.sprite_flip_hide_pixels);
        write_optional_u32(writer, params.sprite_extension_base);
        write_optional_u32(writer, params.sprite_extension_size);
        writer.u32(sound_rom_size);

        main_cpu.save_state(writer);
        sound_cpu.save_state(writer);
        video.save_state(writer);
        opnb.save_state(writer);

        writer.bytes(work_ram);
        writer.bytes(palette_ram);
        writer.bytes(tile_ram);
        writer.bytes(tile_ram_secondary);
        writer.bytes(sprite_ram);
        writer.bytes(sprite_extension_ram);
        writer.bytes(roz_ram);
        writer.bytes(z80_ram);
        for (const std::uint16_t value : video_regs) {
            writer.u16(value);
        }
        for (const std::uint16_t value : secondary_video_regs) {
            writer.u16(value);
        }
        for (const std::uint16_t value : sprite_bank_regs) {
            writer.u16(value);
        }
        for (const std::uint16_t value : priority_regs) {
            writer.u16(value);
        }
        for (const std::uint16_t value : roz_control_regs) {
            writer.u16(value);
        }
        for (const std::uint16_t value : tc0480scp_control_regs) {
            writer.u16(value);
        }

        writer.u8(input_p1);
        writer.u8(input_p2);
        writer.u8(input_p3);
        writer.u8(input_p4);
        writer.u8(input_system);
        writer.u8(input_coin_extension);
        writer.u8(dip_a);
        writer.u8(dip_b);
        writer.u8(latch_68k_to_z80);
        writer.u8(latch_z80_to_68k);
        writer.boolean(sound_latch_pending);
        writer.boolean(sound_reply_pending);
        writer.u8(sound_bank);
        writer.u8(sound_main_port);
        writer.u8(sound_z80_port);
        writer.boolean(sound_main_read_high);
        writer.boolean(sound_z80_read_high);
        writer.boolean(sound_main_write_high);
        writer.boolean(sound_z80_write_high);
        writer.u8(latch_68k_to_z80_port2);
        writer.u8(latch_z80_to_68k_port2);
        writer.boolean(sound_latch_pending_port2);
        writer.boolean(sound_reply_pending_port2);
        writer.u64(vblank_irq_raised);
        writer.u64(vblank_irq_acked);
        writer.u8(last_vblank_irq_level);
        writer.u8(last_irq_ack_level);
        writer.u64(sprite_dma_irq_raised);
        writer.u64(sprite_dma_irq_acked);
        writer.u8(last_sprite_dma_irq_level);
        writer.u8(last_sprite_dma_irq_ack_level);
        writer.boolean(vblank_irq_pending);
        writer.boolean(sprite_dma_irq_pending);
        writer.u64(z80_cycle_accum_);
        writer.u64(ym_cycle_accum_);
        writer.u64(sound_comm.command_nmi_pulses());
        writer.bytes(io_output_regs);
        writer.u8(io_output_latch);
        for (const std::uint32_t counter : coin_counters) {
            writer.u32(counter);
        }
        for (const bool line : coin_counter_lines) {
            writer.boolean(line);
        }
        for (const bool lockout : coin_lockouts) {
            writer.boolean(lockout);
        }
        writer.u32(io_output_write_count);
        writer.u32(last_io_output_address);
        writer.u8(last_io_output_value);
        writer.u32(palette_write_count);
        writer.u32(last_palette_write_address);
        writer.u8(last_palette_write_value);
        writer.u16(last_palette_word);
        writer.u16(last_palette_index);
        writer.u32(last_palette_color);
        writer.u32(palette_read_count);
        writer.u32(last_palette_read_address);
        writer.u8(last_palette_read_value);
        writer.u16(last_palette_read_word);
        writer.u16(last_palette_read_index);
        writer.u32(last_palette_read_color);
        writer.u32(sound_reset_control_write_count);
        writer.u32(sound_reset_assert_count);
        writer.u32(sound_reset_release_count);
        writer.u32(last_sound_reset_control_address);
        writer.u8(last_sound_reset_control_value);
        writer.u32(watchdog_write_count);
        writer.u32(watchdog_confirmed_write_count);
        writer.u32(watchdog_suspect_write_count);
        writer.u32(last_watchdog_address);
        writer.u8(last_watchdog_value);
        writer.u8(last_watchdog_window);
        writer.boolean(last_watchdog_confirmed);
        writer.u32(main_bus_read_count);
        writer.u32(main_bus_write_count);
        writer.u32(main_bus_open_bus_read_count);
        writer.u32(main_bus_unmapped_write_count);
        writer.u32(main_bus_odd_access_count);
        writer.u32(main_bus_inferred_word_pair_count);
        writer.u32(last_main_bus_address);
        writer.u8(last_main_bus_value);
        writer.boolean(last_main_bus_write);
        writer.boolean(last_main_bus_mapped);
        writer.boolean(last_main_bus_open_bus);
        writer.boolean(last_main_bus_pair_inferred);
        writer.u32(previous_main_bus_address);
        writer.u8(previous_main_bus_value);
        writer.boolean(previous_main_bus_write);
        writer.boolean(previous_main_bus_mapped);
        writer.boolean(previous_main_bus_valid);
        writer.u32(io_input_read_count);
        writer.u32(io_input_dip_read_count);
        writer.u32(io_input_service_read_count);
        writer.u32(io_access_read_even_count);
        writer.u32(io_access_read_odd_count);
        writer.u32(io_access_write_even_count);
        writer.u32(io_access_write_odd_count);
        writer.u32(io_access_inferred_read_pair_count);
        writer.u32(io_access_inferred_write_pair_count);
        writer.u32(last_io_access_address);
        writer.u8(last_io_access_value);
        writer.u8(last_io_access_window);
        writer.boolean(last_io_access_write);
        writer.boolean(last_io_access_pair_inferred);
        writer.u32(previous_io_access_address);
        writer.u8(previous_io_access_window);
        writer.boolean(previous_io_access_write);
        writer.boolean(previous_io_access_valid);
        sound_comm.save_state(writer);
    }

    void taito_f2_system::load_state(chips::state_reader& reader) {
        const std::uint32_t version = reader.u32();
        if (version < taito_f2_system_min_state_version ||
            version > taito_f2_system_state_version) {
            reader.fail();
            return;
        }

        const std::uint32_t saved_rom_identity = reader.u32();
        const bool saved_vertical = reader.boolean();
        const std::uint8_t saved_players = reader.u8();
        const auto saved_map = static_cast<taito_f2_address_map>(reader.u8());
        const auto saved_sprite_policy = static_cast<taito_f2_sprite_policy>(reader.u8());
        const auto saved_sprite_active_area =
            static_cast<taito_f2_sprite_active_area>(reader.u8());
        const auto saved_sprite_buffering = static_cast<taito_f2_sprite_buffering>(reader.u8());
        const auto saved_palette_format = static_cast<taito_f2_palette_format>(reader.u8());
        const auto saved_text_gfx_source = static_cast<taito_f2_text_gfx_source>(reader.u8());
        const auto saved_input_profile = static_cast<taito_f2_input_profile>(reader.u8());
        const auto saved_io_profile = static_cast<taito_f2_io_profile>(reader.u8());
        const auto saved_palette_profile =
            static_cast<taito_f2_palette_profile>(reader.u8());
        const auto saved_priority_profile =
            static_cast<taito_f2_priority_profile>(reader.u8());
        const auto saved_sprite_chip_pair =
            static_cast<taito_f2_sprite_chip_pair>(reader.u8());
        const auto saved_sound_comm_chip =
            static_cast<taito_f2_sound_comm_chip>(reader.u8());
        const auto saved_video_profile = static_cast<taito_f2_video_profile>(reader.u8());
        const auto saved_tc0480scp_profile =
            static_cast<taito_f2_tc0480scp_profile>(reader.u8());
        const auto saved_aux_profile = static_cast<taito_f2_aux_profile>(reader.u8());
        const std::uint8_t saved_vblank_irq_level = reader.u8();
        const std::uint8_t saved_sprite_dma_irq_level = reader.u8();
        const std::uint32_t saved_text_gfx_base = reader.u32();
        const int saved_tc0100scn_bg_x_offset = read_i32(reader);
        const int saved_tc0100scn_text_x_offset = read_i32(reader);
        const int saved_text_y_origin = read_i32(reader);
        const int saved_positive_text_y_origin = read_i32(reader);
        const int saved_roz_x_offset = read_i32(reader);
        const int saved_roz_y_offset = read_i32(reader);
        const int saved_hide_pixels = read_i32(reader);
        const int saved_flip_hide_pixels = read_i32(reader);
        const bool extension_base_matches =
            read_optional_u32_matches(reader, params.sprite_extension_base);
        const bool extension_size_matches =
            read_optional_u32_matches(reader, params.sprite_extension_size);
        const std::uint32_t saved_sound_rom_size = reader.u32();
        if (!reader.ok() || saved_rom_identity != rom_identity_crc(roms, params) ||
            saved_vertical != params.vertical || saved_map != params.address_map ||
            saved_players != params.players ||
            saved_sprite_policy != params.sprite_policy ||
            saved_sprite_active_area != params.sprite_active_area ||
            saved_sprite_buffering != params.sprite_buffering ||
            saved_palette_format != params.palette_format ||
            saved_text_gfx_source != params.text_gfx_source ||
            saved_input_profile != params.input_profile ||
            saved_io_profile != params.io_profile ||
            saved_palette_profile != params.palette_profile ||
            saved_priority_profile != params.priority_profile ||
            saved_sprite_chip_pair != params.sprite_chip_pair ||
            saved_sound_comm_chip != params.sound_comm_chip ||
            saved_video_profile != params.video_profile ||
            saved_tc0480scp_profile != params.tc0480scp_profile ||
            saved_aux_profile != params.aux_profile ||
            saved_vblank_irq_level != params.vblank_irq_level ||
            saved_sprite_dma_irq_level != params.sprite_dma_irq_level ||
            saved_text_gfx_base != params.text_gfx_base ||
            saved_tc0100scn_bg_x_offset != params.tc0100scn_bg_x_offset ||
            saved_tc0100scn_text_x_offset != params.tc0100scn_text_x_offset ||
            saved_text_y_origin != params.tc0100scn_text_y_origin ||
            saved_positive_text_y_origin != params.tc0100scn_positive_text_y_origin ||
            saved_roz_x_offset != params.roz_x_offset ||
            saved_roz_y_offset != params.roz_y_offset ||
            saved_hide_pixels != params.sprite_hide_pixels ||
            saved_flip_hide_pixels != params.sprite_flip_hide_pixels ||
            !extension_base_matches || !extension_size_matches ||
            saved_sound_rom_size != sound_rom_size) {
            reader.fail();
            return;
        }

        main_cpu.load_state(reader);
        sound_cpu.load_state(reader);
        video.load_state(reader);
        opnb.load_state(reader);

        reader.bytes(work_ram);
        reader.bytes(palette_ram);
        reader.bytes(tile_ram);
        reader.bytes(tile_ram_secondary);
        reader.bytes(sprite_ram);
        reader.bytes(sprite_extension_ram);
        reader.bytes(roz_ram);
        reader.bytes(z80_ram);
        for (std::uint16_t& value : video_regs) {
            value = reader.u16();
        }
        for (std::uint16_t& value : secondary_video_regs) {
            value = reader.u16();
        }
        for (std::uint16_t& value : sprite_bank_regs) {
            value = reader.u16();
        }
        for (std::uint16_t& value : priority_regs) {
            value = reader.u16();
        }
        for (std::uint16_t& value : roz_control_regs) {
            value = reader.u16();
        }
        for (std::uint16_t& value : tc0480scp_control_regs) {
            value = reader.u16();
        }

        input_p1 = reader.u8();
        input_p2 = reader.u8();
        input_p3 = reader.u8();
        input_p4 = reader.u8();
        input_system = reader.u8();
        if (version >= 6U) {
            input_coin_extension = reader.u8();
        } else {
            input_coin_extension = 0xFFU;
        }
        dip_a = reader.u8();
        dip_b = reader.u8();
        latch_68k_to_z80 = reader.u8();
        latch_z80_to_68k = reader.u8();
        sound_latch_pending = reader.boolean();
        if (version >= 4U) {
            sound_reply_pending = reader.boolean();
        } else {
            sound_reply_pending = false;
        }
        write_sound_bank(reader.u8());
        if (version >= 3U) {
            sound_main_port = reader.u8();
            sound_z80_port = reader.u8();
            sound_main_read_high = reader.boolean();
            sound_z80_read_high = reader.boolean();
            if (version >= 4U) {
                sound_main_write_high = reader.boolean();
                sound_z80_write_high = reader.boolean();
            } else {
                sound_main_write_high = false;
                sound_z80_write_high = false;
            }
        } else {
            sound_main_port = 0U;
            sound_z80_port = 0U;
            sound_main_read_high = false;
            sound_z80_read_high = false;
            sound_main_write_high = false;
            sound_z80_write_high = false;
        }
        if (version >= 5U) {
            latch_68k_to_z80_port2 = reader.u8();
            latch_z80_to_68k_port2 = reader.u8();
            sound_latch_pending_port2 = reader.boolean();
            sound_reply_pending_port2 = reader.boolean();
        } else {
            latch_68k_to_z80_port2 = 0xFFU;
            latch_z80_to_68k_port2 = 0xFFU;
            sound_latch_pending_port2 = false;
            sound_reply_pending_port2 = false;
        }
        vblank_irq_raised = reader.u64();
        vblank_irq_acked = reader.u64();
        if (version >= 16U) {
            last_vblank_irq_level = reader.u8();
            last_irq_ack_level = reader.u8();
        } else {
            last_vblank_irq_level = vblank_irq_raised != 0U ? params.vblank_irq_level : 0U;
            last_irq_ack_level = 0U;
        }
        if (version >= 18U) {
            sprite_dma_irq_raised = reader.u64();
            sprite_dma_irq_acked = reader.u64();
            last_sprite_dma_irq_level = reader.u8();
            last_sprite_dma_irq_ack_level = reader.u8();
            vblank_irq_pending = reader.boolean();
            sprite_dma_irq_pending = reader.boolean();
        } else {
            sprite_dma_irq_raised = 0U;
            sprite_dma_irq_acked = 0U;
            last_sprite_dma_irq_level = 0U;
            last_sprite_dma_irq_ack_level = 0U;
            vblank_irq_pending = false;
            sprite_dma_irq_pending = false;
        }
        z80_cycle_accum_ = reader.u64();
        ym_cycle_accum_ = reader.u64();
        if (version >= 8U) {
            sound_comm.set_command_nmi_pulses(reader.u64());
        } else {
            sound_comm.set_command_nmi_pulses(0U);
        }
        if (version >= 10U) {
            reader.bytes(io_output_regs);
            io_output_latch = reader.u8();
            const std::size_t saved_coin_slots = version >= 17U ? coin_counters.size() : 2U;
            coin_counters.fill(0U);
            coin_counter_lines.fill(false);
            for (std::size_t i = 0U; i < saved_coin_slots; ++i) {
                coin_counters[i] = reader.u32();
            }
            for (std::size_t i = 0U; i < saved_coin_slots; ++i) {
                coin_counter_lines[i] = reader.boolean();
            }
            for (bool& lockout : coin_lockouts) {
                lockout = reader.boolean();
            }
            io_output_write_count = reader.u32();
            last_io_output_address = reader.u32();
            last_io_output_value = reader.u8();
        } else {
            io_output_regs.fill(0U);
            io_output_latch = 0U;
            coin_counters.fill(0U);
            coin_counter_lines.fill(false);
            coin_lockouts.fill(false);
            io_output_write_count = 0U;
            last_io_output_address = 0U;
            last_io_output_value = 0U;
        }
        if (version >= 11U) {
            palette_write_count = reader.u32();
            last_palette_write_address = reader.u32();
            last_palette_write_value = reader.u8();
            last_palette_word = reader.u16();
            last_palette_index = reader.u16();
            last_palette_color = reader.u32();
        } else {
            palette_write_count = 0U;
            last_palette_write_address = 0U;
            last_palette_write_value = 0U;
            last_palette_word = 0U;
            last_palette_index = 0U;
            last_palette_color = 0U;
        }
        if (version >= 19U) {
            palette_read_count = reader.u32();
            last_palette_read_address = reader.u32();
            last_palette_read_value = reader.u8();
            last_palette_read_word = reader.u16();
            last_palette_read_index = reader.u16();
            last_palette_read_color = reader.u32();
        } else {
            palette_read_count = 0U;
            last_palette_read_address = 0U;
            last_palette_read_value = 0U;
            last_palette_read_word = 0U;
            last_palette_read_index = 0U;
            last_palette_read_color = 0U;
        }
        if (version >= 20U) {
            sound_reset_control_write_count = reader.u32();
            sound_reset_assert_count = reader.u32();
            sound_reset_release_count = reader.u32();
            last_sound_reset_control_address = reader.u32();
            last_sound_reset_control_value = reader.u8();
        } else {
            sound_reset_control_write_count = 0U;
            sound_reset_assert_count = 0U;
            sound_reset_release_count = 0U;
            last_sound_reset_control_address = 0U;
            last_sound_reset_control_value = 0U;
        }
        if (version >= 21U) {
            watchdog_write_count = reader.u32();
            watchdog_confirmed_write_count = reader.u32();
            watchdog_suspect_write_count = reader.u32();
            last_watchdog_address = reader.u32();
            last_watchdog_value = reader.u8();
            last_watchdog_window = reader.u8();
            last_watchdog_confirmed = reader.boolean();
        } else {
            watchdog_write_count = 0U;
            watchdog_confirmed_write_count = 0U;
            watchdog_suspect_write_count = 0U;
            last_watchdog_address = 0U;
            last_watchdog_value = 0U;
            last_watchdog_window = 0U;
            last_watchdog_confirmed = false;
        }
        if (version >= 22U) {
            main_bus_read_count = reader.u32();
            main_bus_write_count = reader.u32();
            main_bus_open_bus_read_count = reader.u32();
            main_bus_unmapped_write_count = reader.u32();
            main_bus_odd_access_count = reader.u32();
            main_bus_inferred_word_pair_count = reader.u32();
            last_main_bus_address = reader.u32();
            last_main_bus_value = reader.u8();
            last_main_bus_write = reader.boolean();
            last_main_bus_mapped = reader.boolean();
            last_main_bus_open_bus = reader.boolean();
            last_main_bus_pair_inferred = reader.boolean();
            previous_main_bus_address = reader.u32();
            previous_main_bus_value = reader.u8();
            previous_main_bus_write = reader.boolean();
            previous_main_bus_mapped = reader.boolean();
            previous_main_bus_valid = reader.boolean();
        } else {
            main_bus_read_count = 0U;
            main_bus_write_count = 0U;
            main_bus_open_bus_read_count = 0U;
            main_bus_unmapped_write_count = 0U;
            main_bus_odd_access_count = 0U;
            main_bus_inferred_word_pair_count = 0U;
            last_main_bus_address = 0U;
            last_main_bus_value = 0U;
            last_main_bus_write = false;
            last_main_bus_mapped = false;
            last_main_bus_open_bus = false;
            last_main_bus_pair_inferred = false;
            previous_main_bus_address = 0U;
            previous_main_bus_value = 0U;
            previous_main_bus_write = false;
            previous_main_bus_mapped = false;
            previous_main_bus_valid = false;
        }
        if (version >= 23U) {
            io_input_read_count = reader.u32();
            io_input_dip_read_count = reader.u32();
            io_input_service_read_count = reader.u32();
            io_access_read_even_count = reader.u32();
            io_access_read_odd_count = reader.u32();
            io_access_write_even_count = reader.u32();
            io_access_write_odd_count = reader.u32();
            io_access_inferred_read_pair_count = reader.u32();
            io_access_inferred_write_pair_count = reader.u32();
            last_io_access_address = reader.u32();
            last_io_access_value = reader.u8();
            last_io_access_window = reader.u8();
            last_io_access_write = reader.boolean();
            last_io_access_pair_inferred = reader.boolean();
            previous_io_access_address = reader.u32();
            previous_io_access_window = reader.u8();
            previous_io_access_write = reader.boolean();
            previous_io_access_valid = reader.boolean();
        } else {
            io_input_read_count = 0U;
            io_input_dip_read_count = 0U;
            io_input_service_read_count = 0U;
            io_access_read_even_count = 0U;
            io_access_read_odd_count = 0U;
            io_access_write_even_count = 0U;
            io_access_write_odd_count = 0U;
            io_access_inferred_read_pair_count = 0U;
            io_access_inferred_write_pair_count = 0U;
            last_io_access_address = 0U;
            last_io_access_value = 0U;
            last_io_access_window = 0U;
            last_io_access_write = false;
            last_io_access_pair_inferred = false;
            previous_io_access_address = 0U;
            previous_io_access_window = 0U;
            previous_io_access_write = false;
            previous_io_access_valid = false;
        }
        if (version >= 13U) {
            sound_comm.load_state(reader);
        }

        if (reader.ok()) {
            update_io_output_state();
            update_io_access_state();
            update_palette_write_state();
            update_main_irq_line();
            update_irq_state();
            update_board_profile_state();
            update_sound_reset_state();
            update_watchdog_state();
            update_main_bus_state();
            sync_sound_irq();
            push_video_regs_to_chip();
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
