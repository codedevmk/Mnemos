#include "taito_f2_system.hpp"
#include "taito_f2_game_manifests.hpp"
#include "rom_set_toml.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    namespace taito = mnemos::manifests::taito_f2;
    using mnemos::manifests::common::rom_set_image;

    [[nodiscard]] mnemos::manifests::common::rom_set_decl
    require_embedded_decl(std::string_view set_name, std::string_view toml) {
        REQUIRE_FALSE(toml.empty());
        const auto parsed = mnemos::manifests::common::parse_rom_set_decl(toml, set_name);
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": "
                              << error.message);
        }
        REQUIRE(parsed.ok());
        REQUIRE(parsed.value.has_value());
        return *parsed.value;
    }

    [[nodiscard]] taito::taito_f2_palette_profile
    expected_embedded_palette_profile(std::string_view set_id) noexcept {
        return set_id == "qtorimon" || set_id == "quizhq"
                   ? taito::taito_f2_palette_profile::tc0110pcr_tc0070rgb
                   : taito::taito_f2_palette_profile::tc0260dar;
    }

    void poke16(std::span<std::uint8_t> bytes, std::size_t at, std::uint16_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value);
    }

    void poke32(std::span<std::uint8_t> bytes, std::size_t at, std::uint32_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 24U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value >> 16U);
        bytes[at + 2U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 3U] = static_cast<std::uint8_t>(value);
    }

    [[nodiscard]] std::uint16_t read16_le(std::span<const std::uint8_t> bytes,
                                          std::size_t at) {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[at]) |
            (static_cast<std::uint16_t>(bytes[at + 1U]) << 8U));
    }

    [[nodiscard]] std::uint32_t read32_le(std::span<const std::uint8_t> bytes,
                                          std::size_t at) {
        return static_cast<std::uint32_t>(bytes[at]) |
               (static_cast<std::uint32_t>(bytes[at + 1U]) << 8U) |
               (static_cast<std::uint32_t>(bytes[at + 2U]) << 16U) |
               (static_cast<std::uint32_t>(bytes[at + 3U]) << 24U);
    }

    [[nodiscard]] std::uint64_t read64_le(std::span<const std::uint8_t> bytes,
                                          std::size_t at) {
        std::uint64_t value = 0U;
        for (std::size_t i = 0; i < sizeof(std::uint64_t); ++i) {
            value |= static_cast<std::uint64_t>(bytes[at + i]) << (i * 8U);
        }
        return value;
    }

    [[nodiscard]] rom_set_image make_image() {
        rom_set_image image;
        auto& main = image.regions["maincpu"];
        main.assign(taito::main_rom_size, 0xFFU);
        poke32(main, 0x0U, taito::work_ram_base + taito::work_ram_size);
        poke32(main, 0x4U, 0x00000400U);
        poke16(main, 0x400U, 0x33FCU); // MOVE.W #$4242,($00100000).L
        poke16(main, 0x402U, 0x4242U);
        poke32(main, 0x404U, taito::work_ram_base);
        poke16(main, 0x408U, 0x60FEU); // BRA *
        image.regions["tiles"].assign(0x1000U, 0U);
        image.regions["sprites"].assign(0x2000U, 0U);
        return image;
    }

    void solid_tile(std::vector<std::uint8_t>& gfx, std::uint32_t code, std::uint8_t pen) {
        const std::size_t base = static_cast<std::size_t>(code) * 32U;
        for (std::size_t i = 0; i < 32U; ++i) {
            gfx[base + i] = static_cast<std::uint8_t>((pen << 4U) | pen);
        }
    }

    void program_text_left_column(std::vector<std::uint8_t>& rom, std::uint32_t base,
                                  std::uint8_t code) {
        const std::size_t char_base = static_cast<std::size_t>(base) +
                                      static_cast<std::size_t>(code) * 8U;
        for (std::size_t y = 0; y < 8U; ++y) {
            rom[char_base + y] = 0x80U;
        }
    }

    void solid_tile16_lsb(std::vector<std::uint8_t>& gfx, std::uint32_t code,
                          std::uint8_t pen) {
        const std::size_t base = static_cast<std::size_t>(code) * 128U;
        for (std::size_t i = 0; i < 128U; ++i) {
            gfx[base + i] = static_cast<std::uint8_t>((pen << 4U) | pen);
        }
    }

    void solid_sprite(std::vector<std::uint8_t>& gfx, std::uint32_t code, std::uint8_t pen) {
        const std::size_t base = static_cast<std::size_t>(code) * 128U;
        for (std::size_t i = 0; i < 128U; ++i) {
            gfx[base + i] = static_cast<std::uint8_t>((pen << 4U) | pen);
        }
    }

    [[nodiscard]] std::uint16_t sprite_screen_x(int x) {
        return static_cast<std::uint16_t>(
            (x + static_cast<int>(mnemos::chips::video::taito_f2_video::sprite_screen_x_bias)) &
            0x0FFF);
    }

    [[nodiscard]] std::uint16_t sprite_screen_y(int y) {
        return static_cast<std::uint16_t>(y & 0x0FFF);
    }

    void set_sprite(std::span<std::uint8_t> ram, std::size_t index, int x, int y,
                    std::uint16_t code, std::uint16_t attr) {
        const std::size_t off = index * mnemos::chips::video::taito_f2_video::sprite_entry_bytes;
        poke16(ram, off + 0U, code);
        poke16(ram, off + 2U, 0U);
        poke16(ram, off + 4U, sprite_screen_x(x));
        poke16(ram, off + 6U, sprite_screen_y(y));
        poke16(ram, off + 8U, attr);
        poke16(ram, off + 10U, 0U);
    }

    void write_sound_command(taito::taito_f2_system& system, std::uint32_t base,
                             std::uint8_t value, std::uint8_t port = 0U) {
        system.main_bus.write8(base, port);
        system.main_bus.write8(base + 2U, value);
        system.main_bus.write8(base + 2U,
                               static_cast<std::uint8_t>((value >> 4U) | (value << 4U)));
    }

} // namespace

TEST_CASE("taito_f2 board clocks match the F2 main-board oscillator ratios",
          "[taito_f2][timing]") {
    CHECK(taito::m68k_clock_hz == 12'000'000U);
    CHECK(taito::z80_clock_hz == 4'000'000U);
    CHECK(taito::ym2610_clock_hz == 8'000'000U);
}

TEST_CASE("taito_f2 boots a synthetic 68000 program and renders a frame", "[taito_f2]") {
    auto system = taito::assemble_taito_f2(make_image());

    CHECK(system->main_cpu.cpu_registers().pc == 0x00000400U);
    REQUIRE(system->video.frame_index() == 0U);

    system->run_frame();
    CHECK(system->video.frame_index() == 1U);
    CHECK(system->work_ram[0] == 0x42U);
    CHECK(system->work_ram[1] == 0x42U);
    CHECK(system->video.framebuffer().width == 320U);
    CHECK(system->video.framebuffer().height == 224U);
}

TEST_CASE("taito_f2 ROM skeleton declares the board and program region", "[taito_f2]") {
    const auto decl = taito::taito_f2_rom_skeleton("synthetic");
    CHECK(decl.name == "synthetic");
    CHECK(decl.board == "taito_f2");
    REQUIRE(decl.regions.size() == 1U);
    CHECK(decl.regions[0].name == "maincpu");
    CHECK(decl.regions[0].size == taito::main_rom_size);
}

TEST_CASE("taito_f2 derives real board wiring from a ROM-set declaration", "[taito_f2]") {
    mnemos::manifests::common::rom_set_decl decl;
    decl.orientation = mnemos::manifests::common::screen_orientation::vertical;
    decl.players = 4U;
    decl.taito_f2_map = "gunfront";
    decl.taito_f2_sprite_policy = "banked";
    decl.taito_f2_sprite_extension_base = 0xC00000U;
    decl.taito_f2_sprite_extension_size = 0x2000U;
    decl.taito_f2_sprite_buffering = "partial_delayed";
    decl.taito_f2_palette_format = "rgbx_444";
    decl.taito_f2_sprite_active_area = "y_word_bit0";
    decl.taito_f2_sprite_hide_pixels = static_cast<std::int16_t>(3);
    decl.taito_f2_sprite_flip_hide_pixels = static_cast<std::int16_t>(-3);
    decl.taito_f2_input_profile = "split_tmp82c265";
    decl.taito_f2_text_gfx_source = "program_1bpp";
    decl.taito_f2_text_gfx_base = 0x80000U;
    decl.taito_f2_tc0100scn_bg_x_offset = static_cast<std::int16_t>(16);
    decl.taito_f2_tc0100scn_text_x_offset = static_cast<std::int16_t>(23);
    decl.taito_f2_tc0100scn_text_y_origin = static_cast<std::int16_t>(-12);
    decl.taito_f2_tc0100scn_positive_text_y_origin = static_cast<std::int16_t>(24);
    decl.taito_f2_io_profile = "tc0510nio";
    decl.taito_f2_palette_profile = "tc0110pcr_tc0070rgb";
    decl.taito_f2_priority_profile = "tc0360pri";
    decl.taito_f2_sprite_chip_pair = "tc0200obj_tc0210fbc";
    decl.taito_f2_sound_comm_chip = "tc0140syt";
    decl.taito_f2_video_profile = "tc0100scn";
    decl.taito_f2_tc0480scp_profile = "none";
    decl.taito_f2_roz_x_offset = static_cast<std::int16_t>(-16);
    decl.taito_f2_roz_y_offset = static_cast<std::int16_t>(0);
    decl.taito_f2_aux_profile = "none";
    decl.taito_f2_vblank_irq_level = static_cast<std::uint8_t>(5U);
    decl.taito_f2_sprite_dma_irq_level = static_cast<std::uint8_t>(6U);

    const auto params = taito::board_params_from_decl(decl);
    CHECK(params.vertical);
    CHECK(params.players == 4U);
    CHECK(params.address_map == taito::taito_f2_address_map::gunfront);
    CHECK(params.sprite_policy == taito::taito_f2_sprite_policy::banked);
    CHECK(params.sprite_buffering == taito::taito_f2_sprite_buffering::partial_delayed);
    CHECK(params.palette_format == taito::taito_f2_palette_format::rgbx_444);
    CHECK(params.sprite_active_area == taito::taito_f2_sprite_active_area::y_word_bit0);
    CHECK(params.input_profile == taito::taito_f2_input_profile::split_tmp82c265);
    CHECK(params.text_gfx_source == taito::taito_f2_text_gfx_source::program_1bpp);
    CHECK(params.text_gfx_base == 0x80000U);
    CHECK(params.tc0100scn_bg_x_offset == 16);
    CHECK(params.tc0100scn_text_x_offset == 23);
    CHECK(params.tc0100scn_text_y_origin == -12);
    CHECK(params.tc0100scn_positive_text_y_origin == 24);
    CHECK(params.io_profile == taito::taito_f2_io_profile::tc0510nio);
    CHECK(params.palette_profile ==
          taito::taito_f2_palette_profile::tc0110pcr_tc0070rgb);
    CHECK(params.priority_profile == taito::taito_f2_priority_profile::tc0360pri);
    CHECK(params.sprite_chip_pair ==
          taito::taito_f2_sprite_chip_pair::tc0200obj_tc0210fbc);
    CHECK(params.sound_comm_chip == taito::taito_f2_sound_comm_chip::tc0140syt);
    CHECK(params.video_profile == taito::taito_f2_video_profile::tc0100scn);
    CHECK(params.tc0480scp_profile == taito::taito_f2_tc0480scp_profile::none);
    CHECK(params.roz_x_offset == -16);
    CHECK(params.roz_y_offset == 0);
    CHECK(params.aux_profile == taito::taito_f2_aux_profile::none);
    CHECK(params.vblank_irq_level == 5U);
    CHECK(params.sprite_dma_irq_level == 6U);
    CHECK(params.sprite_hide_pixels == 3);
    CHECK(params.sprite_flip_hide_pixels == -3);
    REQUIRE(params.sprite_extension_base.has_value());
    REQUIRE(params.sprite_extension_size.has_value());
    CHECK(*params.sprite_extension_base == 0xC00000U);
    CHECK(*params.sprite_extension_size == 0x2000U);

    decl.taito_f2_map = "liquidk";
    const auto liquidk_params = taito::board_params_from_decl(decl);
    CHECK(liquidk_params.address_map == taito::taito_f2_address_map::liquidk);

    decl.taito_f2_map = "dondokod";
    const auto dondokod_params = taito::board_params_from_decl(decl);
    CHECK(dondokod_params.address_map == taito::taito_f2_address_map::dondokod);

    decl.taito_f2_map = "pulirula";
    decl.taito_f2_io_profile.reset();
    decl.taito_f2_priority_profile.reset();
    decl.taito_f2_video_profile.reset();
    decl.taito_f2_text_gfx_source.reset();
    decl.taito_f2_text_gfx_base.reset();
    decl.taito_f2_tc0480scp_profile.reset();
    decl.taito_f2_roz_x_offset.reset();
    decl.taito_f2_roz_y_offset.reset();
    const auto pulirula_params = taito::board_params_from_decl(decl);
    CHECK(pulirula_params.address_map == taito::taito_f2_address_map::pulirula);
    CHECK(pulirula_params.video_profile ==
          taito::taito_f2_video_profile::tc0100scn_tc0430grw);
    CHECK(pulirula_params.roz_x_offset == -10);
    CHECK(pulirula_params.roz_y_offset == 16);

    decl.taito_f2_map = "quizhq";
    const auto quizhq_params = taito::board_params_from_decl(decl);
    CHECK(quizhq_params.address_map == taito::taito_f2_address_map::quizhq);
    CHECK(quizhq_params.text_gfx_source == taito::taito_f2_text_gfx_source::program_1bpp);
    CHECK(quizhq_params.text_gfx_base == taito::quizhq_program_text_gfx_base);

    decl.taito_f2_map = "qtorimon";
    const auto qtorimon_params = taito::board_params_from_decl(decl);
    CHECK(qtorimon_params.address_map == taito::taito_f2_address_map::qtorimon);
    CHECK(qtorimon_params.text_gfx_source == taito::taito_f2_text_gfx_source::program_1bpp);
    CHECK(qtorimon_params.text_gfx_base == taito::qtorimon_program_text_gfx_base);

    decl.taito_f2_map = "qzchikyu";
    const auto qzchikyu_params = taito::board_params_from_decl(decl);
    CHECK(qzchikyu_params.address_map == taito::taito_f2_address_map::qzchikyu);
    CHECK(qzchikyu_params.io_profile == taito::taito_f2_io_profile::tc0510nio);
    CHECK(qzchikyu_params.priority_profile == taito::taito_f2_priority_profile::none);

    decl.taito_f2_map = "qzquest";
    const auto qzquest_params = taito::board_params_from_decl(decl);
    CHECK(qzquest_params.address_map == taito::taito_f2_address_map::qzquest);

    decl.taito_f2_map = "metalb";
    const auto metalb_params = taito::board_params_from_decl(decl);
    CHECK(metalb_params.address_map == taito::taito_f2_address_map::metalb);
    CHECK(metalb_params.video_profile == taito::taito_f2_video_profile::tc0480scp);
    CHECK(metalb_params.tc0480scp_profile == taito::taito_f2_tc0480scp_profile::metalb);
    CHECK(metalb_params.io_profile == taito::taito_f2_io_profile::tc0510nio);

    decl.taito_f2_map = "footchmp";
    const auto footchmp_params = taito::board_params_from_decl(decl);
    CHECK(footchmp_params.address_map == taito::taito_f2_address_map::footchmp);
    CHECK(footchmp_params.tc0480scp_profile == taito::taito_f2_tc0480scp_profile::footchmp);

    decl.taito_f2_map = "deadconx";
    const auto deadconx_params = taito::board_params_from_decl(decl);
    CHECK(deadconx_params.address_map == taito::taito_f2_address_map::deadconx);
    CHECK(deadconx_params.tc0480scp_profile == taito::taito_f2_tc0480scp_profile::deadconx);

    decl.taito_f2_map = "dinorex";
    const auto dinorex_params = taito::board_params_from_decl(decl);
    CHECK(dinorex_params.address_map == taito::taito_f2_address_map::dinorex);

    decl.taito_f2_map = "thundfox";
    const auto thundfox_params = taito::board_params_from_decl(decl);
    CHECK(thundfox_params.address_map == taito::taito_f2_address_map::thundfox);
    CHECK(thundfox_params.video_profile == taito::taito_f2_video_profile::dual_tc0100scn);

    decl.taito_f2_map = "growl";
    const auto growl_params = taito::board_params_from_decl(decl);
    CHECK(growl_params.address_map == taito::taito_f2_address_map::growl);
    CHECK(growl_params.input_profile == taito::taito_f2_input_profile::split_tmp82c265);

    decl.taito_f2_map = "ninjak";
    decl.taito_f2_input_profile.reset();
    const auto ninjak_params = taito::board_params_from_decl(decl);
    CHECK(ninjak_params.address_map == taito::taito_f2_address_map::ninjak);
    CHECK(ninjak_params.io_profile == taito::taito_f2_io_profile::te7750);
    CHECK(ninjak_params.input_profile == taito::taito_f2_input_profile::te7750_quad);

    decl.taito_f2_map = "solfigtr";
    const auto solfigtr_params = taito::board_params_from_decl(decl);
    CHECK(solfigtr_params.address_map == taito::taito_f2_address_map::solfigtr);
}

TEST_CASE("taito_f2 embedded manifests stay inside implemented runtime profiles",
          "[taito_f2][manifest]") {
    for (const taito::taito_f2_game_manifest_view& manifest :
         taito::taito_f2_game_manifest_catalog) {
        INFO("set=" << manifest.set_id);
        const auto decl = require_embedded_decl(manifest.set_id, manifest.toml);
        const auto params = taito::board_params_from_decl(decl);

        CHECK(params.palette_profile == expected_embedded_palette_profile(manifest.set_id));
        CHECK(params.sprite_chip_pair ==
              taito::taito_f2_sprite_chip_pair::tc0200obj_tc0210fbc);
        CHECK(params.sound_comm_chip == taito::taito_f2_sound_comm_chip::tc0140syt);
        CHECK(params.aux_profile == taito::taito_f2_aux_profile::none);
        CHECK(params.vblank_irq_level == 5U);
        CHECK(params.sprite_dma_irq_level == 6U);
    }
}

TEST_CASE("taito_f2 partial profile overrides preserve map-derived defaults",
          "[taito_f2][manifest]") {
    mnemos::manifests::common::rom_set_decl decl;
    decl.players = 4U;
    decl.orientation = mnemos::manifests::common::screen_orientation::horizontal;
    decl.taito_f2_map = "growl";
    decl.taito_f2_palette_profile = "tc0110pcr_tc0070rgb";

    const auto params = taito::board_params_from_decl(decl);
    CHECK(params.address_map == taito::taito_f2_address_map::growl);
    CHECK(params.input_profile == taito::taito_f2_input_profile::split_tmp82c265);
    CHECK(params.io_profile == taito::taito_f2_io_profile::tc0510nio);
    CHECK(params.priority_profile == taito::taito_f2_priority_profile::tc0360pri);
    CHECK(params.video_profile == taito::taito_f2_video_profile::tc0100scn);
    CHECK(params.tc0480scp_profile == taito::taito_f2_tc0480scp_profile::none);
    CHECK(params.palette_profile ==
          taito::taito_f2_palette_profile::tc0110pcr_tc0070rgb);
}

TEST_CASE("taito_f2 real F2 map routes TC0100SCN, sprite RAM, sound, and inputs",
          "[taito_f2]") {
    auto system = taito::assemble_taito_f2(
        make_image(),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::gunfront,
         .sprite_policy = taito::taito_f2_sprite_policy::banked,
         .sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed,
         .palette_format = taito::taito_f2_palette_format::rgbx_444,
         .tc0100scn_text_y_origin = -12,
         .tc0100scn_positive_text_y_origin = 14});

    CHECK(system->video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::banked);
    CHECK(system->video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::partial_delayed);
    CHECK(system->video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::rgbx_444);
    CHECK(system->video.tc0100scn_text_y_origin_offset() == -12);
    CHECK(system->video.tc0100scn_positive_text_y_origin_offset() == 14);

    system->main_bus.write16_be(taito::real_tile_ram_base + 2U, 0x1234U);
    CHECK(system->tile_ram[2] == 0x12U);
    CHECK(system->tile_ram[3] == 0x34U);

    system->main_bus.write16_be(taito::real_sprite_ram_base + 6U, 0xABCDU);
    CHECK(system->sprite_ram[6] == 0xABU);
    CHECK(system->sprite_ram[7] == 0xCDU);

    system->main_bus.write16_be(taito::real_sprite_bank_base + 4U, 0x0001U);
    CHECK(system->sprite_bank_regs[2] == 0x0001U);
    system->video.latch_sprites();
    CHECK(system->video.sprite_bank(0U) == 0x0800U);

    system->main_bus.write16_be(taito::real_video_reg_base + 0U, 0x0010U);
    system->main_bus.write16_be(taito::real_video_reg_base + 6U, 0x0020U);
    CHECK(system->video_regs[0] == 0x0010U);
    CHECK(system->video_regs[3] == 0x0020U);

    system->main_bus.write16_be(taito::real_priority_base + 10U, 0x0042U);
    CHECK(system->priority_regs[5] == 0x0042U);
    CHECK(system->video.priority_register(5U) == 0x0042U);
    CHECK(system->main_bus.read16_be(taito::real_priority_base + 10U) == 0x0042U);

    write_sound_command(*system, taito::real_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::real_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::real_input_base + 3U) == 0x55U);
    CHECK(system->main_bus.read8(taito::real_input_base + 5U) == 0x3CU);
    CHECK(system->main_bus.read8(taito::real_input_base + 7U) == 0x12U);
    CHECK(system->main_bus.read8(taito::real_input_base + 9U) == 0x34U);
}

TEST_CASE("taito_f2 Growl map routes TC0190FMC banks and split inputs",
          "[taito_f2][tc0190fmc]") {
    auto system = taito::assemble_taito_f2(
        make_image(),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::growl,
         .sprite_policy = taito::taito_f2_sprite_policy::banked,
         .sprite_active_area = taito::taito_f2_sprite_active_area::y_word_bit0,
         .sprite_buffering = taito::taito_f2_sprite_buffering::immediate,
         .palette_format = taito::taito_f2_palette_format::rrrr_gggg_bbbb_rgbx,
         .sprite_hide_pixels = 3,
         .sprite_flip_hide_pixels = 3});

    CHECK(system->video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
    CHECK(system->video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::banked);
    CHECK(system->video.current_sprite_active_area_source() ==
          mnemos::chips::video::taito_f2_video::sprite_active_area_source::y_word_bit0);
    CHECK(system->video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::immediate);
    CHECK(system->video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::rrrr_gggg_bbbb_rgbx);
    CHECK(system->video.sprite_palette_bank_base() == 0U);

    system->main_bus.write16_be(taito::real_tile_ram_base + 2U, 0x1234U);
    CHECK(system->tile_ram[2] == 0x12U);
    CHECK(system->tile_ram[3] == 0x34U);

    system->main_bus.write16_be(taito::real_sprite_ram_base + 6U, 0xABCDU);
    CHECK(system->sprite_ram[6] == 0xABU);
    CHECK(system->sprite_ram[7] == 0xCDU);

    system->main_bus.write16_be(taito::real_sprite_bank_base + 4U, 0x0001U);
    CHECK(system->sprite_bank_regs[2] == 0x0001U);
    system->video.latch_sprites();
    CHECK(system->video.sprite_bank(0U) == 0x0800U);
    CHECK(system->video.sprite_bank(1U) == 0x0C00U);

    system->main_bus.write16_be(taito::real_video_reg_base + 0U, 0x0010U);
    system->main_bus.write16_be(taito::real_video_reg_base + 6U, 0x0020U);
    CHECK(system->video_regs[0] == 0x0010U);
    CHECK(system->video_regs[3] == 0x0020U);

    system->main_bus.write16_be(taito::real_priority_base + 10U, 0x0042U);
    CHECK(system->priority_regs[5] == 0x0042U);
    CHECK(system->video.priority_register(5U) == 0x0042U);

    write_sound_command(*system, taito::growl_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    system->input_p3 = 0x77U;
    system->input_p4 = 0x88U;
    system->input_coin_extension = 0x99U;
    CHECK(system->main_bus.read8(taito::growl_dip_input_base + 1U) == 0x12U);
    CHECK(system->main_bus.read8(taito::growl_dip_input_base + 3U) == 0x34U);
    CHECK(system->main_bus.read8(taito::growl_player_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::growl_player_input_base + 3U) == 0x55U);
    CHECK(system->main_bus.read8(taito::growl_player_input_base + 5U) == 0x3CU);
    CHECK(system->main_bus.read8(taito::growl_p3_input_base + 0U) == 0x88U);
    CHECK(system->main_bus.read8(taito::growl_p3_input_base + 1U) == 0x77U);
    CHECK(system->main_bus.read8(taito::growl_p4_input_base + 1U) == 0x99U);
}

TEST_CASE("taito_f2 Ninja Kids map routes TE7750 inputs and TC0190FMC banks",
          "[taito_f2][tc0190fmc]") {
    auto system = taito::assemble_taito_f2(
        make_image(),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::ninjak,
         .sprite_policy = taito::taito_f2_sprite_policy::banked,
         .sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed,
         .palette_format = taito::taito_f2_palette_format::rgbx_444});

    CHECK(system->video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
    CHECK(system->video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::banked);
    CHECK(system->video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::partial_delayed);

    system->main_bus.write16_be(taito::real_tile_ram_base + 2U, 0x1234U);
    CHECK(system->tile_ram[2] == 0x12U);
    CHECK(system->tile_ram[3] == 0x34U);

    system->main_bus.write16_be(taito::real_sprite_ram_base + 6U, 0xABCDU);
    CHECK(system->sprite_ram[6] == 0xABU);
    CHECK(system->sprite_ram[7] == 0xCDU);

    system->main_bus.write16_be(taito::ninjak_sprite_bank_base + 4U, 0x0001U);
    CHECK(system->sprite_bank_regs[2] == 0x0001U);
    system->video.latch_sprites();
    CHECK(system->video.sprite_bank(0U) == 0x0800U);
    CHECK(system->video.sprite_bank(1U) == 0x0C00U);

    system->main_bus.write16_be(taito::real_priority_base + 10U, 0x0042U);
    CHECK(system->priority_regs[5] == 0x0042U);
    CHECK(system->video.priority_register(5U) == 0x0042U);

    write_sound_command(*system, taito::ninjak_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    system->input_p3 = 0x77U;
    system->input_p4 = 0x88U;
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 0U) == 0x12U);
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 2U) == 0x34U);
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 4U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 6U) == 0x55U);
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 8U) == 0x77U);
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 10U) == 0x88U);
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 12U) == 0x3CU);
}

TEST_CASE("taito_f2 Solitary Fighter map routes split inputs and flipped hide offsets",
          "[taito_f2][tc0190fmc]") {
    auto system = taito::assemble_taito_f2(
        make_image(),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::solfigtr,
         .sprite_policy = taito::taito_f2_sprite_policy::banked,
         .sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed,
         .palette_format = taito::taito_f2_palette_format::rgbx_444,
         .sprite_hide_pixels = 3,
         .sprite_flip_hide_pixels = -3});

    CHECK(system->video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::banked);
    CHECK(system->video.sprite_hide_pixels() == 3);
    CHECK(system->video.sprite_flip_hide_pixels() == -3);

    system->main_bus.write16_be(taito::real_sprite_bank_base + 4U, 0x0001U);
    CHECK(system->sprite_bank_regs[2] == 0x0001U);
    system->video.latch_sprites();
    CHECK(system->video.sprite_bank(0U) == 0x0800U);

    write_sound_command(*system, taito::growl_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::growl_dip_input_base + 1U) == 0x12U);
    CHECK(system->main_bus.read8(taito::growl_dip_input_base + 3U) == 0x34U);
    CHECK(system->main_bus.read8(taito::growl_player_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::growl_player_input_base + 3U) == 0x55U);
    CHECK(system->main_bus.read8(taito::growl_player_input_base + 5U) == 0x3CU);
}

TEST_CASE("taito_f2 Liquid Kids map uses common F2 video and priority windows",
          "[taito_f2]") {
    auto params = taito::taito_f2_board_params{};
    params.address_map = taito::taito_f2_address_map::liquidk;
    params.sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed;
    params.palette_format = taito::taito_f2_palette_format::rgbx_444;
    params.sprite_hide_pixels = 3;
    params.sprite_flip_hide_pixels = 3;

    auto system = taito::assemble_taito_f2(make_image(), params);

    CHECK(system->video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::standard);
    CHECK(system->video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::partial_delayed);
    CHECK(system->video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::rgbx_444);

    system->main_bus.write16_be(taito::real_tile_ram_base + 2U, 0x1234U);
    CHECK(system->tile_ram[2] == 0x12U);
    CHECK(system->tile_ram[3] == 0x34U);

    system->main_bus.write16_be(taito::real_sprite_ram_base + 6U, 0xABCDU);
    CHECK(system->sprite_ram[6] == 0xABU);
    CHECK(system->sprite_ram[7] == 0xCDU);

    system->main_bus.write16_be(taito::real_video_reg_base + 0U, 0x0010U);
    system->main_bus.write16_be(taito::real_video_reg_base + 6U, 0x0020U);
    CHECK(system->video_regs[0] == 0x0010U);
    CHECK(system->video_regs[3] == 0x0020U);

    system->main_bus.write16_be(taito::real_priority_base + 10U, 0x0042U);
    CHECK(system->priority_regs[5] == 0x0042U);
    CHECK(system->video.priority_register(5U) == 0x0042U);

    write_sound_command(*system, taito::real_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);
}

TEST_CASE("taito_f2 Quiz HQ map routes its split input and sound windows", "[taito_f2]") {
    auto system = taito::assemble_taito_f2(
        make_image(), {.vertical = false, .address_map = taito::taito_f2_address_map::quizhq});

    system->main_bus.write16_be(taito::real_tile_ram_base + 2U, 0x1234U);
    CHECK(system->tile_ram[2] == 0x12U);
    CHECK(system->tile_ram[3] == 0x34U);

    system->main_bus.write16_be(taito::real_sprite_ram_base + 6U, 0xABCDU);
    CHECK(system->sprite_ram[6] == 0xABU);
    CHECK(system->sprite_ram[7] == 0xCDU);

    write_sound_command(*system, taito::quizhq_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::quizhq_input_a_base + 1U) == 0x34U);
    CHECK(system->main_bus.read8(taito::quizhq_input_a_base + 3U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::quizhq_input_b_base + 1U) == 0x12U);
    CHECK(system->main_bus.read8(taito::quizhq_input_b_base + 3U) == 0x55U);
    CHECK(system->main_bus.read8(taito::quizhq_input_b_base + 5U) == 0x3CU);
}

TEST_CASE("taito_f2 Quiz Torimon map routes TC0110PCR palette and shifted IO",
          "[taito_f2]") {
    auto system = taito::assemble_taito_f2(
        make_image(),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::qtorimon,
         .sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed,
         .palette_format = taito::taito_f2_palette_format::xbgr_555});

    CHECK(system->video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
    CHECK(system->video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::partial_delayed);
    CHECK(system->video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::xbgr_555);

    system->main_bus.write16_be(taito::work_ram_base + 2U, 0x2468U);
    CHECK(system->work_ram[2] == 0x24U);
    CHECK(system->work_ram[3] == 0x68U);

    system->main_bus.write16_be(taito::palette_ram_base + 4U, 0x001FU);
    CHECK(system->palette_ram[4] == 0x00U);
    CHECK(system->palette_ram[5] == 0x1FU);

    system->main_bus.write16_be(taito::real_sprite_ram_base + 6U, 0xABCDU);
    CHECK(system->sprite_ram[6] == 0xABU);
    CHECK(system->sprite_ram[7] == 0xCDU);

    system->main_bus.write16_be(taito::real_tile_ram_base + 2U, 0x1234U);
    CHECK(system->tile_ram[2] == 0x12U);
    CHECK(system->tile_ram[3] == 0x34U);

    system->main_bus.write16_be(taito::real_video_reg_base + 0U, 0x0010U);
    system->main_bus.write16_be(taito::real_video_reg_base + 6U, 0x0020U);
    CHECK(system->video_regs[0] == 0x0010U);
    CHECK(system->video_regs[3] == 0x0020U);

    write_sound_command(*system, taito::qtorimon_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::qtorimon_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::qtorimon_input_base + 3U) == 0x55U);
    CHECK(system->main_bus.read8(taito::real_priority_base + 10U) == 0xFFU);
}

TEST_CASE("taito_f2 Quiz Torimon renders program-region 1bpp text glyphs",
          "[taito_f2][video]") {
    auto image = make_image();
    auto& main = image.regions["maincpu"];
    program_text_left_column(main, taito::qtorimon_program_text_gfx_base, 7U);

    auto system = taito::assemble_taito_f2(
        std::move(image),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::qtorimon,
         .palette_format = taito::taito_f2_palette_format::xbgr_555});

    CHECK(system->params.text_gfx_source == taito::taito_f2_text_gfx_source::program_1bpp);
    CHECK(system->params.text_gfx_base == taito::qtorimon_program_text_gfx_base);
    CHECK(system->video.current_text_gfx_source() ==
          mnemos::chips::video::taito_f2_video::text_gfx_source::program_1bpp);
    CHECK(system->video.text_gfx_base_offset() == taito::qtorimon_program_text_gfx_base);

    poke16(system->tile_ram,
           mnemos::chips::video::taito_f2_video::text_tilemap_base,
           0x0207U);
    system->main_bus.write16_be(
        taito::palette_ram_base + (2U * 16U + 1U) * 2U,
        0x001FU);

    system->run_frame();

    const auto pixels = system->video.framebuffer().pixels;
    const std::size_t left_column =
        8U * mnemos::chips::video::taito_f2_video::visible_width + 23U;
    CHECK(pixels[left_column] == 0xFF0000U);
    CHECK(pixels[left_column + 1U] == 0x000000U);
}

TEST_CASE("taito_f2 Quiz Chikyu map routes TC0100SCN and partial sprite buffering",
          "[taito_f2][video]") {
    auto system = taito::assemble_taito_f2(
        make_image(),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::qzchikyu,
         .sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed_qzchikyu,
         .palette_format = taito::taito_f2_palette_format::xrgb_555,
         .sprite_hide_pixels = 0,
         .sprite_flip_hide_pixels = 4});

    CHECK(system->video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
    CHECK(system->video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::standard);
    CHECK(system->video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::
              partial_delayed_qzchikyu);
    CHECK(system->video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::xrgb_555);
    CHECK(system->video.sprite_hide_pixels() == 0);
    CHECK(system->video.sprite_flip_hide_pixels() == 4);

    system->main_bus.write16_be(taito::qzchikyu_work_ram_base + 2U, 0x2468U);
    CHECK(system->work_ram[2] == 0x24U);
    CHECK(system->work_ram[3] == 0x68U);

    system->main_bus.write16_be(taito::qzchikyu_palette_ram_base + 4U, 0x7C00U);
    CHECK(system->palette_ram[4] == 0x7CU);
    CHECK(system->palette_ram[5] == 0x00U);

    system->main_bus.write16_be(taito::qzchikyu_sprite_ram_base + 6U, 0xABCDU);
    CHECK(system->sprite_ram[6] == 0xABU);
    CHECK(system->sprite_ram[7] == 0xCDU);

    system->main_bus.write16_be(taito::qzchikyu_tile_ram_base + 2U, 0x1234U);
    CHECK(system->tile_ram[2] == 0x12U);
    CHECK(system->tile_ram[3] == 0x34U);

    system->main_bus.write16_be(taito::qzchikyu_video_reg_base + 0U, 0x0010U);
    system->main_bus.write16_be(taito::qzchikyu_video_reg_base + 6U, 0x0020U);
    CHECK(system->video_regs[0] == 0x0010U);
    CHECK(system->video_regs[3] == 0x0020U);

    write_sound_command(*system, taito::qzchikyu_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::qzchikyu_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::qzchikyu_input_base + 3U) == 0x55U);
    CHECK(system->main_bus.read8(taito::real_priority_base + 10U) == 0xFFU);
}

TEST_CASE("taito_f2 Quiz Quest map routes shifted TC0100SCN windows",
          "[taito_f2][video]") {
    auto system = taito::assemble_taito_f2(
        make_image(),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::qzquest,
         .sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed,
         .palette_format = taito::taito_f2_palette_format::xrgb_555});

    CHECK(system->video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
    CHECK(system->video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::partial_delayed);
    CHECK(system->video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::xrgb_555);

    system->main_bus.write16_be(taito::qzquest_work_ram_base + 2U, 0x2468U);
    CHECK(system->work_ram[2] == 0x24U);
    CHECK(system->work_ram[3] == 0x68U);

    system->main_bus.write16_be(taito::qzquest_palette_ram_base + 4U, 0x7C00U);
    CHECK(system->palette_ram[4] == 0x7CU);
    CHECK(system->palette_ram[5] == 0x00U);

    system->main_bus.write16_be(taito::qzquest_sprite_ram_base + 6U, 0xABCDU);
    CHECK(system->sprite_ram[6] == 0xABU);
    CHECK(system->sprite_ram[7] == 0xCDU);

    system->main_bus.write16_be(taito::qzquest_tile_ram_base + 2U, 0x1234U);
    CHECK(system->tile_ram[2] == 0x12U);
    CHECK(system->tile_ram[3] == 0x34U);

    system->main_bus.write16_be(taito::qzquest_video_reg_base + 0U, 0x0010U);
    system->main_bus.write16_be(taito::qzquest_video_reg_base + 6U, 0x0020U);
    CHECK(system->video_regs[0] == 0x0010U);
    CHECK(system->video_regs[3] == 0x0020U);

    write_sound_command(*system, taito::qzquest_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::qzquest_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::qzquest_input_base + 3U) == 0x55U);
    CHECK(system->main_bus.read8(taito::real_priority_base + 10U) == 0xFFU);
}

TEST_CASE("taito_f2 Metal Black map routes TC0480SCP video and shifted windows",
          "[taito_f2][video][tc0480scp]") {
    auto image = make_image();
    auto& tiles = image.regions["tiles"];
    tiles.assign(0x400U, 0U);
    solid_tile16_lsb(tiles, 1U, 4U);

    auto system = taito::assemble_taito_f2(
        std::move(image),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::metalb,
         .sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed,
         .palette_format = taito::taito_f2_palette_format::rgbx_444,
         .sprite_hide_pixels = 3,
         .sprite_flip_hide_pixels = 3});

    CHECK(system->video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0480scp);
    CHECK(system->video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::partial_delayed);
    CHECK(system->video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::rgbx_444);
    CHECK(system->video.tc0480scp_palette_bank_base() == 256U);

    system->main_bus.write16_be(taito::metalb_tile_ram_base + 2U, 0x1234U);
    CHECK(system->tile_ram[2] == 0x12U);
    CHECK(system->tile_ram[3] == 0x34U);

    system->main_bus.write16_be(taito::metalb_sprite_ram_base + 6U, 0xABCDU);
    CHECK(system->sprite_ram[6] == 0xABU);
    CHECK(system->sprite_ram[7] == 0xCDU);

    system->main_bus.write16_be(taito::metalb_tc0480scp_control_base + 0x1EU, 0x0010U);
    CHECK(system->tc0480scp_control_regs[0x0F] == 0x0010U);
    CHECK(system->video.tc0480scp_control_register(0x0FU) == 0x0010U);
    CHECK(system->main_bus.read16_be(taito::metalb_tc0480scp_control_base + 0x1EU) ==
          0x0010U);

    system->main_bus.write16_be(taito::metalb_priority_base + 10U, 0x0042U);
    CHECK(system->priority_regs[5] == 0x0042U);
    CHECK(system->video.priority_register(5U) == 0x0042U);

    write_sound_command(*system, taito::metalb_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::metalb_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::metalb_input_base + 3U) == 0x55U);

    system->video.set_tc0480scp_offsets(0, 0, 0, 0, 0, 0);
    system->main_bus.write16_be(taito::metalb_tile_ram_base, 0x0000U);
    system->main_bus.write16_be(taito::metalb_tile_ram_base + 2U, 0x0001U);
    system->main_bus.write16_be(taito::metalb_palette_ram_base + (256U * 16U + 4U) * 2U,
                                0xF000U);
    system->run_frame();

    CHECK(system->video.framebuffer().pixels[0] == 0xFF0000U);
}

TEST_CASE("taito_f2 Football Champ map routes TC0190FMC banks and TC0480SCP video",
          "[taito_f2][video][tc0190fmc][tc0480scp]") {
    auto image = make_image();
    auto& tiles = image.regions["tiles"];
    tiles.assign(0x400U, 0U);
    solid_tile16_lsb(tiles, 1U, 4U);
    image.regions["sprites"].assign(0x200000U, 0U);

    auto system = taito::assemble_taito_f2(
        std::move(image),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::footchmp,
         .sprite_policy = taito::taito_f2_sprite_policy::banked,
         .sprite_active_area = taito::taito_f2_sprite_active_area::y_word_bit0,
         .sprite_buffering = taito::taito_f2_sprite_buffering::full_delayed,
         .palette_format = taito::taito_f2_palette_format::rgbx_444,
         .sprite_hide_pixels = 3,
         .sprite_flip_hide_pixels = 3});

    CHECK(system->video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0480scp);
    CHECK(system->video.current_tc0480scp_priority_model() ==
          mnemos::chips::video::taito_f2_video::tc0480scp_priority_model::
              deadconx_footchmp);
    CHECK(system->video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::banked);
    CHECK(system->video.current_sprite_active_area_source() ==
          mnemos::chips::video::taito_f2_video::sprite_active_area_source::y_word_bit0);
    CHECK(system->video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::full_delayed);

    system->main_bus.write16_be(taito::footchmp_tile_ram_base + 2U, 0x1234U);
    CHECK(system->tile_ram[2] == 0x12U);
    CHECK(system->tile_ram[3] == 0x34U);

    system->main_bus.write16_be(taito::footchmp_sprite_ram_base + 6U, 0xABCDU);
    CHECK(system->sprite_ram[6] == 0xABU);
    CHECK(system->sprite_ram[7] == 0xCDU);

    system->main_bus.write16_be(taito::footchmp_sprite_bank_base + 4U, 0x0001U);
    CHECK(system->sprite_bank_regs[2] == 0x0001U);
    system->video.latch_sprites();
    CHECK(system->video.sprite_bank(0U) == 0x0800U);
    CHECK(system->video.sprite_bank(1U) == 0x0C00U);

    system->main_bus.write16_be(taito::footchmp_tc0480scp_control_base + 0x1EU, 0x0010U);
    CHECK(system->tc0480scp_control_regs[0x0F] == 0x0010U);
    CHECK(system->video.tc0480scp_control_register(0x0FU) == 0x0010U);

    system->main_bus.write16_be(taito::footchmp_priority_base + 8U, 0x0051U);
    CHECK(system->priority_regs[4] == 0x0051U);
    CHECK(system->video.priority_register(4U) == 0x0051U);

    write_sound_command(*system, taito::footchmp_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::footchmp_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::footchmp_input_base + 0x11U) == 0xAAU);

    system->video.set_tc0480scp_offsets(0, 0, 0, 0, 0, 0);
    system->main_bus.write16_be(taito::footchmp_tile_ram_base, 0x0000U);
    system->main_bus.write16_be(taito::footchmp_tile_ram_base + 2U, 0x0001U);
    system->main_bus.write16_be(taito::footchmp_palette_ram_base + 4U * 2U, 0xF000U);
    system->run_frame();

    CHECK(system->video.framebuffer().pixels[0] == 0xFF0000U);
}

TEST_CASE("taito_f2 Dead Connection map routes TC0190FMC banks and TC0480SCP video",
          "[taito_f2][video][tc0190fmc][tc0480scp]") {
    auto image = make_image();
    auto& tiles = image.regions["tiles"];
    tiles.assign(0x400U, 0U);
    solid_tile16_lsb(tiles, 1U, 4U);
    image.regions["sprites"].assign(0x200000U, 0U);

    auto system = taito::assemble_taito_f2(
        std::move(image),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::deadconx,
         .sprite_policy = taito::taito_f2_sprite_policy::banked,
         .sprite_active_area = taito::taito_f2_sprite_active_area::y_word_bit0,
         .sprite_buffering = taito::taito_f2_sprite_buffering::full_delayed,
         .palette_format = taito::taito_f2_palette_format::rgbx_444,
         .sprite_hide_pixels = 3,
         .sprite_flip_hide_pixels = 3});

    CHECK(system->video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0480scp);
    CHECK(system->video.current_tc0480scp_priority_model() ==
          mnemos::chips::video::taito_f2_video::tc0480scp_priority_model::
              deadconx_footchmp);
    CHECK(system->video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::banked);
    CHECK(system->video.current_sprite_active_area_source() ==
          mnemos::chips::video::taito_f2_video::sprite_active_area_source::y_word_bit0);
    CHECK(system->video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::full_delayed);

    system->main_bus.write16_be(taito::deadconx_tile_ram_base + 2U, 0x1234U);
    CHECK(system->tile_ram[2] == 0x12U);
    CHECK(system->tile_ram[3] == 0x34U);

    system->main_bus.write16_be(taito::deadconx_sprite_ram_base + 6U, 0xABCDU);
    CHECK(system->sprite_ram[6] == 0xABU);
    CHECK(system->sprite_ram[7] == 0xCDU);

    system->main_bus.write16_be(taito::deadconx_sprite_bank_base + 4U, 0x0001U);
    CHECK(system->sprite_bank_regs[2] == 0x0001U);
    system->video.latch_sprites();
    CHECK(system->video.sprite_bank(0U) == 0x0800U);
    CHECK(system->video.sprite_bank(1U) == 0x0C00U);

    system->main_bus.write16_be(taito::deadconx_tc0480scp_control_base + 0x1EU,
                                0x0010U);
    CHECK(system->tc0480scp_control_regs[0x0F] == 0x0010U);
    CHECK(system->video.tc0480scp_control_register(0x0FU) == 0x0010U);

    system->main_bus.write16_be(taito::deadconx_priority_base + 8U, 0x0051U);
    CHECK(system->priority_regs[4] == 0x0051U);
    CHECK(system->video.priority_register(4U) == 0x0051U);

    write_sound_command(*system, taito::deadconx_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::deadconx_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::deadconx_input_base + 0x11U) == 0xAAU);

    system->video.set_tc0480scp_offsets(0, 0, 0, 0, 0, 0);
    system->main_bus.write16_be(taito::deadconx_tile_ram_base, 0x0000U);
    system->main_bus.write16_be(taito::deadconx_tile_ram_base + 2U, 0x0001U);
    system->main_bus.write16_be(taito::deadconx_palette_ram_base + 4U * 2U, 0xF000U);
    system->run_frame();

    CHECK(system->video.framebuffer().pixels[0] == 0xFF0000U);
}

TEST_CASE("taito_f2 Dino Rex map routes extension sprites and shifted windows",
          "[taito_f2][video]") {
    auto system = taito::assemble_taito_f2(
        make_image(),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::dinorex,
         .sprite_policy = taito::taito_f2_sprite_policy::extension_3,
         .sprite_buffering = taito::taito_f2_sprite_buffering::immediate,
         .palette_format = taito::taito_f2_palette_format::rrrr_gggg_bbbb_rgbx,
         .sprite_hide_pixels = 3,
         .sprite_flip_hide_pixels = 3,
         .sprite_extension_base = taito::dinorex_sprite_extension_base,
         .sprite_extension_size = taito::dinorex_sprite_extension_size});

    CHECK(system->video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
    CHECK(system->video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::extension_low_as_high);
    CHECK(system->video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::immediate);
    CHECK(system->video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::rrrr_gggg_bbbb_rgbx);
    CHECK(system->video.sprite_hide_pixels() == 3);
    CHECK(system->video.sprite_flip_hide_pixels() == 3);
    CHECK(system->video.sprite_palette_bank_base() == 0U);

    system->main_bus.write16_be(taito::dinorex_work_ram_base + 2U, 0x2468U);
    CHECK(system->work_ram[2] == 0x24U);
    CHECK(system->work_ram[3] == 0x68U);

    system->main_bus.write16_be(taito::dinorex_sprite_extension_base, 0x1200U);
    system->main_bus.write16_be(
        taito::dinorex_sprite_extension_base + taito::dinorex_sprite_extension_size - 2U,
        0xABCDU);
    CHECK(system->sprite_extension_ram[0] == 0x12U);
    CHECK(system->sprite_extension_ram[1] == 0x00U);
    CHECK(system->sprite_extension_ram[taito::dinorex_sprite_extension_size - 2U] ==
          0xABU);
    CHECK(system->sprite_extension_ram[taito::dinorex_sprite_extension_size - 1U] ==
          0xCDU);
    system->main_bus.write16_be(taito::dinorex_sprite_extension_base +
                                    taito::dinorex_sprite_extension_size,
                                0x5555U);
    CHECK(system->sprite_extension_ram[taito::dinorex_sprite_extension_size] == 0U);
    CHECK(system->main_bus.read16_be(taito::dinorex_sprite_extension_base +
                                     taito::dinorex_sprite_extension_size) == 0xFFFFU);

    system->main_bus.write16_be(taito::dinorex_palette_ram_base + 4U, 0x001FU);
    CHECK(system->palette_ram[4] == 0x00U);
    CHECK(system->palette_ram[5] == 0x1FU);

    system->main_bus.write16_be(taito::dinorex_sprite_ram_base + 6U, 0xABCDU);
    CHECK(system->sprite_ram[6] == 0xABU);
    CHECK(system->sprite_ram[7] == 0xCDU);

    system->main_bus.write16_be(taito::dinorex_tile_ram_base + 2U, 0x1234U);
    CHECK(system->tile_ram[2] == 0x12U);
    CHECK(system->tile_ram[3] == 0x34U);

    system->main_bus.write16_be(taito::dinorex_video_reg_base + 0U, 0x0010U);
    system->main_bus.write16_be(taito::dinorex_video_reg_base + 6U, 0x0020U);
    CHECK(system->video_regs[0] == 0x0010U);
    CHECK(system->video_regs[3] == 0x0020U);

    system->main_bus.write16_be(taito::dinorex_priority_base + 10U, 0x0042U);
    CHECK(system->priority_regs[5] == 0x0042U);
    CHECK(system->video.priority_register(5U) == 0x0042U);

    write_sound_command(*system, taito::dinorex_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::dinorex_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::dinorex_input_base + 3U) == 0x55U);
}

TEST_CASE("taito_f2 Thunder Fox map routes dual TC0100SCN tilemaps",
          "[taito_f2][video][tc0100scn]") {
    auto image = make_image();
    auto& tiles = image.regions["tiles"];
    auto& tiles_secondary = image.regions["tiles_secondary"];
    tiles.assign(0x100U, 0U);
    tiles_secondary.assign(0x100U, 0U);
    solid_tile(tiles, 1U, 1U);
    solid_tile(tiles_secondary, 1U, 2U);

    auto system = taito::assemble_taito_f2(
        std::move(image),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::thundfox,
         .sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed_thundfox,
         .palette_format = taito::taito_f2_palette_format::rgbx_444,
         .sprite_hide_pixels = 3,
         .sprite_flip_hide_pixels = -3});

    CHECK(system->video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::dual_tc0100scn);
    CHECK(system->video.current_sprite_buffer_policy() ==
          mnemos::chips::video::taito_f2_video::sprite_buffer_policy::
              partial_delayed_thundfox);
    CHECK(system->video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::rgbx_444);
    CHECK(system->video.sprite_hide_pixels() == 3);
    CHECK(system->video.sprite_flip_hide_pixels() == -3);

    system->main_bus.write16_be(taito::thundfox_work_ram_base + 2U, 0x2468U);
    CHECK(system->work_ram[2] == 0x24U);
    CHECK(system->work_ram[3] == 0x68U);

    system->main_bus.write16_be(taito::thundfox_tile_ram_base + 2U, 0x1234U);
    CHECK(system->tile_ram[2] == 0x12U);
    CHECK(system->tile_ram[3] == 0x34U);

    system->main_bus.write16_be(taito::thundfox_secondary_tile_ram_base + 2U, 0x5678U);
    CHECK(system->tile_ram_secondary[2] == 0x56U);
    CHECK(system->tile_ram_secondary[3] == 0x78U);

    system->main_bus.write16_be(taito::thundfox_video_reg_base + 0U, 0x0010U);
    system->main_bus.write16_be(taito::thundfox_video_reg_base + 6U, 0x0020U);
    CHECK(system->video_regs[0] == 0x0010U);
    CHECK(system->video_regs[3] == 0x0020U);

    system->main_bus.write16_be(taito::thundfox_secondary_video_reg_base + 0U, 0x0030U);
    system->main_bus.write16_be(taito::thundfox_secondary_video_reg_base + 6U, 0x0040U);
    CHECK(system->secondary_video_regs[0] == 0x0030U);
    CHECK(system->secondary_video_regs[3] == 0x0040U);

    system->main_bus.write16_be(taito::thundfox_sprite_ram_base + 6U, 0xABCDU);
    CHECK(system->sprite_ram[6] == 0xABU);
    CHECK(system->sprite_ram[7] == 0xCDU);

    system->main_bus.write16_be(taito::thundfox_priority_base + 10U, 0x0042U);
    CHECK(system->priority_regs[5] == 0x0042U);
    CHECK(system->video.priority_register(5U) == 0x0042U);

    write_sound_command(*system, taito::thundfox_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::thundfox_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::thundfox_input_base + 3U) == 0x55U);

    poke16(system->tile_ram, 0U, 0U);
    poke16(system->tile_ram, 2U, 1U);
    poke16(system->tile_ram_secondary, 0U, 0U);
    poke16(system->tile_ram_secondary, 2U, 1U);
    system->main_bus.write16_be(
        taito::thundfox_video_reg_base + 0U,
        mnemos::chips::video::taito_f2_video::tc0100scn_default_bg_x_offset);
    system->main_bus.write16_be(taito::thundfox_video_reg_base + 6U, 0x0000U);
    system->main_bus.write16_be(
        taito::thundfox_secondary_video_reg_base + 0U,
        mnemos::chips::video::taito_f2_video::tc0100scn_default_bg_x_offset);
    system->main_bus.write16_be(taito::thundfox_secondary_video_reg_base + 6U, 0x0000U);
    system->main_bus.write16_be(taito::thundfox_palette_ram_base + 1U * 2U, 0xF000U);
    system->main_bus.write16_be(taito::thundfox_palette_ram_base + 2U * 2U, 0x00F0U);
    system->main_bus.write16_be(taito::thundfox_priority_base + 10U, 0x0001U);
    system->main_bus.write16_be(taito::thundfox_priority_base + 18U, 0x0002U);
    system->run_frame();

    CHECK(system->video.framebuffer().pixels[0] == 0x0000FFU);
}

TEST_CASE("taito_f2 Dondoko Don map routes TC0280GRD ROZ RAM and controls",
          "[taito_f2][video][roz]") {
    auto image = make_image();
    auto& roz = image.regions["roz"];
    roz.assign(0x1000U, 0U);
    solid_tile(roz, 1U, 4U);

    auto system = taito::assemble_taito_f2(
        std::move(image),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::dondokod,
         .sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed,
         .sprite_hide_pixels = 3,
         .sprite_flip_hide_pixels = 3});

    system->main_bus.write16_be(taito::dondokod_roz_ram_base + 4U, 0x0001U);
    CHECK(system->roz_ram[4] == 0x00U);
    CHECK(system->roz_ram[5] == 0x01U);
    CHECK(system->main_bus.read16_be(taito::dondokod_roz_ram_base + 4U) == 0x0001U);

    system->main_bus.write16_be(taito::dondokod_roz_control_base + 4U, 0x0800U);
    system->main_bus.write16_be(taito::dondokod_roz_control_base + 14U, 0x1000U);
    CHECK(system->roz_control_regs[2] == 0x0800U);
    CHECK(system->roz_control_regs[7] == 0x1000U);
    CHECK(system->video.roz_control_register(2U) == 0x0800U);
    CHECK(system->main_bus.read16_be(taito::dondokod_roz_control_base + 14U) == 0x1000U);

    poke16(system->palette_ram, 4U * 2U, 0x001FU);
    system->run_frame();

    CHECK(system->video.framebuffer().pixels[0] == 0xFF0000U);
}

TEST_CASE("taito_f2 Pulirula map routes TC0430GRW and shifted board windows",
          "[taito_f2][video][roz]") {
    auto image = make_image();
    auto& roz = image.regions["roz"];
    roz.assign(0x1000U, 0U);
    solid_tile(roz, 1U, 4U);

    auto system = taito::assemble_taito_f2(
        std::move(image),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::pulirula,
         .sprite_policy = taito::taito_f2_sprite_policy::extension_2,
         .sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed,
         .palette_format = taito::taito_f2_palette_format::rgbx_444,
         .sprite_hide_pixels = 3,
         .sprite_flip_hide_pixels = 3,
         .sprite_extension_base = taito::pulirula_sprite_extension_base,
         .sprite_extension_size =
             static_cast<std::uint32_t>(taito::sprite_extension_ram_size)});

    CHECK(system->video.current_roz_variant() ==
          mnemos::chips::video::taito_f2_video::roz_variant::tc0430grw);
    CHECK(system->video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::extension_high);
    CHECK(system->video.current_palette_format() ==
          mnemos::chips::video::taito_f2_video::palette_format::rgbx_444);

    system->main_bus.write16_be(taito::pulirula_work_ram_base + 2U, 0x2468U);
    CHECK(system->work_ram[2] == 0x24U);
    CHECK(system->work_ram[3] == 0x68U);

    system->main_bus.write16_be(taito::pulirula_sprite_extension_base, 0x1200U);
    CHECK(system->sprite_extension_ram[0] == 0x12U);
    CHECK(system->sprite_extension_ram[1] == 0x00U);

    system->main_bus.write16_be(taito::pulirula_priority_base + 10U, 0x0042U);
    CHECK(system->priority_regs[5] == 0x0042U);
    CHECK(system->video.priority_register(5U) == 0x0042U);
    system->main_bus.write16_be(taito::pulirula_priority_base + 10U, 0x0000U);

    write_sound_command(*system, taito::pulirula_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::pulirula_input_base + 1U) == 0xAAU);

    constexpr std::uint32_t aligned_tile = 62U * 64U + 1U;
    system->main_bus.write16_be(taito::pulirula_roz_ram_base + aligned_tile * 2U,
                                0x0001U);
    CHECK(system->main_bus.read16_be(taito::pulirula_roz_ram_base + aligned_tile * 2U) ==
          0x0001U);

    system->main_bus.write16_be(taito::pulirula_roz_control_base + 4U, 0x1000U);
    system->main_bus.write16_be(taito::pulirula_roz_control_base + 14U, 0x1000U);
    CHECK(system->roz_control_regs[2] == 0x1000U);
    CHECK(system->video.roz_control_register(2U) == 0x1000U);

    system->main_bus.write16_be(taito::pulirula_palette_ram_base + 8U, 0xF000U);
    system->run_frame();

    CHECK(system->video.framebuffer().pixels[0] == 0xFF0000U);
}

TEST_CASE("taito_f2 routes configured TC0200OBJ sprite extension RAM windows",
          "[taito_f2][video]") {
    constexpr std::uint32_t extension_base = 0xC00000U;
    constexpr std::uint32_t extension_size = 0x1000U;
    auto system = taito::assemble_taito_f2(
        make_image(),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::gunfront,
         .sprite_policy = taito::taito_f2_sprite_policy::extension_1,
         .sprite_extension_base = extension_base,
         .sprite_extension_size = extension_size});

    CHECK(system->video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::extension_low);

    system->main_bus.write16_be(extension_base, 0x1234U);
    system->main_bus.write16_be(extension_base + extension_size - 2U, 0xABCDU);

    CHECK(system->sprite_extension_ram[0] == 0x12U);
    CHECK(system->sprite_extension_ram[1] == 0x34U);
    CHECK(system->sprite_extension_ram[extension_size - 2U] == 0xABU);
    CHECK(system->sprite_extension_ram[extension_size - 1U] == 0xCDU);
    CHECK(system->main_bus.read16_be(extension_base) == 0x1234U);
    CHECK(system->main_bus.read16_be(extension_base + extension_size - 2U) == 0xABCDU);

    system->main_bus.write16_be(extension_base + extension_size, 0x5555U);
    CHECK(system->sprite_extension_ram[extension_size] == 0U);
    CHECK(system->main_bus.read16_be(extension_base + extension_size) == 0xFFFFU);
}

TEST_CASE("taito_f2 routes video RAM, palette, and registers to the renderer",
          "[taito_f2][video]") {
    auto image = make_image();
    auto& tiles = image.regions["tiles"];
    solid_tile(tiles, 1U, 1U);

    auto system = taito::assemble_taito_f2(std::move(image));
    poke16(system->tile_ram, 0U, 0U);
    poke16(system->tile_ram, 2U, 1U);
    poke16(system->palette_ram, 2U, 0x001FU); // bank 0, pen 1 -> red
    CHECK(system->main_bus.read16_be(taito::tile_ram_base) == 0U);
    CHECK(system->main_bus.read16_be(taito::tile_ram_base + 2U) == 1U);
    CHECK(system->video.read_palette(0U, 1U) == 0x001FU);

    system->main_bus.write16_be(taito::video_reg_base + 0U, 0U);
    system->main_bus.write16_be(taito::video_reg_base + 2U, 0U);
    system->run_frame();

    CHECK(system->video.framebuffer().pixels[0] == 0xFF0000U);

    system->main_bus.write16_be(taito::video_reg_base + 0U, 8U); // scroll away from tile 0
    system->run_frame();
    CHECK(system->video.framebuffer().pixels[0] == 0x000000U);
}

TEST_CASE("taito_f2 routes sprite RAM and sprite graphics to TC0200OBJ rendering",
          "[taito_f2][video]") {
    auto image = make_image();
    auto& sprites = image.regions["sprites"];
    solid_sprite(sprites, 1U, 3U);

    auto system = taito::assemble_taito_f2(std::move(image));
    poke16(system->palette_ram, 3U * 2U, 0x7C00U);
    set_sprite(system->sprite_ram, 0U, 16U, 20U, 1U, 0U);

    system->run_frame();

    CHECK(system->video.framebuffer().pixels[20U * 320U + 16U] == 0x0000FFU);
    CHECK(system->video.framebuffer().pixels[35U * 320U + 31U] == 0x0000FFU);
    CHECK(system->video.framebuffer().pixels[36U * 320U + 16U] == 0x000000U);
}

TEST_CASE("taito_f2 maps 68K/Z80 sound latches", "[taito_f2][sound]") {
    auto params = taito::taito_f2_board_params{};
    params.address_map = taito::taito_f2_address_map::gunfront;
    auto system = taito::assemble_taito_f2(make_image(), params);

    system->main_bus.write16_be(taito::real_sound_comm_base, 0x0000U);
    write_sound_command(*system, taito::real_sound_comm_base, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);
    CHECK(system->sound_comm.command_write_count(0U) == 1U);
    system->sound_bus.write8(taito::z80_tc0140syt_port_addr, 0x00U);
    const std::uint8_t command_low = system->sound_bus.read8(taito::z80_tc0140syt_data_addr);
    const std::uint8_t command_high = system->sound_bus.read8(taito::z80_tc0140syt_data_addr);
    CHECK(static_cast<std::uint8_t>((command_high << 4U) | command_low) == 0x5AU);
    CHECK_FALSE(system->sound_latch_pending);
    CHECK(system->sound_comm.command_read_count(0U) == 1U);

    write_sound_command(*system, taito::real_sound_comm_base, 0xA5U, 0x02U);
    CHECK(system->latch_68k_to_z80_port2 == 0xA5U);
    CHECK(system->sound_latch_pending_port2);
    CHECK(system->sound_comm.command_write_count(1U) == 1U);
    system->sound_bus.write8(taito::z80_tc0140syt_port_addr, 0x04U);
    CHECK((system->sound_bus.read8(taito::z80_tc0140syt_data_addr) & 0x02U) != 0U);
    system->sound_bus.write8(taito::z80_tc0140syt_port_addr, 0x02U);
    const std::uint8_t command2_low = system->sound_bus.read8(taito::z80_tc0140syt_data_addr);
    const std::uint8_t command2_high = system->sound_bus.read8(taito::z80_tc0140syt_data_addr);
    CHECK(static_cast<std::uint8_t>((command2_high << 4U) | command2_low) == 0xA5U);
    CHECK_FALSE(system->sound_latch_pending_port2);
    CHECK(system->sound_comm.command_read_count(1U) == 1U);

    system->sound_bus.write8(taito::z80_tc0140syt_port_addr, 0x00U);
    system->sound_bus.write8(taito::z80_tc0140syt_data_addr, 0xC3U);
    system->sound_bus.write8(taito::z80_tc0140syt_data_addr, 0x3CU);
    CHECK(system->sound_comm.reply_write_count(0U) == 1U);
    system->main_bus.write8(taito::real_sound_comm_base, 0x04U);
    CHECK((system->main_bus.read8(taito::real_sound_comm_base + 2U) & 0x04U) != 0U);
    system->main_bus.write8(taito::real_sound_comm_base, 0x00U);
    const std::uint8_t reply_low = system->main_bus.read8(taito::real_sound_comm_base + 2U);
    const std::uint8_t reply_high = system->main_bus.read8(taito::real_sound_comm_base + 2U);
    CHECK(static_cast<std::uint8_t>((reply_high << 4U) | reply_low) == 0xC3U);
    CHECK(system->sound_comm.reply_read_count(0U) == 1U);
    system->main_bus.write8(taito::real_sound_comm_base, 0x04U);
    CHECK((system->main_bus.read8(taito::real_sound_comm_base + 2U) & 0x04U) == 0U);
    system->main_bus.write8(taito::real_sound_comm_base, 0x05U);
    system->main_bus.write8(taito::real_sound_comm_base + 2U, 0x00U);
    CHECK(system->sound_comm.clear_count() == 1U);
}

TEST_CASE("taito_f2 maps TC0140SYT reset control to the sound Z80",
          "[taito_f2][sound][save]") {
    const auto make_reset_image = [] {
        auto image = make_image();
        auto& sound = image.regions["audiocpu"];
        sound.assign(0x4000U, 0x00U);
        sound[0] = 0x00U; // NOP
        sound[1] = 0x00U; // NOP
        sound[2] = 0x18U; // JR $
        sound[3] = 0xFEU;
        return image;
    };

    auto params = taito::taito_f2_board_params{};
    params.address_map = taito::taito_f2_address_map::gunfront;
    auto system = taito::assemble_taito_f2(make_reset_image(), params);

    CHECK(system->sound_reset_state[0] == 1U);
    CHECK(system->sound_reset_state[1] == 0U);
    CHECK(system->sound_reset_state[23] == 1U);
    CHECK(system->sound_reset_state[24] == 1U);

    system->sound_cpu.step_instruction();
    system->sound_cpu.step_instruction();
    REQUIRE(system->sound_cpu.cpu_registers().pc == 2U);

    system->main_bus.write8(taito::real_sound_comm_base, 0x04U);
    system->main_bus.write8(taito::real_sound_comm_base + 2U, 0x01U);

    CHECK(system->sound_cpu.reset_line_held());
    CHECK(system->sound_cpu.cpu_registers().pc == 0U);
    CHECK(system->sound_reset_control_write_count == 1U);
    CHECK(system->sound_reset_assert_count == 1U);
    CHECK(system->sound_reset_release_count == 0U);
    CHECK(system->last_sound_reset_control_address == taito::real_sound_comm_base + 2U);
    CHECK(system->last_sound_reset_control_value == 1U);
    CHECK(system->sound_reset_state[1] == 1U);
    CHECK(read32_le(system->sound_reset_state, 4U) == 1U);
    CHECK(read32_le(system->sound_reset_state, 8U) == 1U);
    CHECK(read32_le(system->sound_reset_state, 12U) == 0U);
    CHECK(read32_le(system->sound_reset_state, 16U) == taito::real_sound_comm_base + 2U);
    CHECK(read16_le(system->sound_reset_state, 20U) == 0U);
    CHECK(system->sound_reset_state[22] == 1U);
    CHECK(system->sound_reset_state[25] == 4U);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    system->save_state(writer);

    auto restored = taito::assemble_taito_f2(make_reset_image(), params);
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->sound_cpu.reset_line_held());
    CHECK(restored->sound_reset_control_write_count == 1U);
    CHECK(restored->sound_reset_assert_count == 1U);
    CHECK(restored->sound_reset_release_count == 0U);
    CHECK(restored->last_sound_reset_control_address == taito::real_sound_comm_base + 2U);
    CHECK(restored->last_sound_reset_control_value == 1U);
    CHECK(restored->sound_reset_state == system->sound_reset_state);

    restored->main_bus.write8(taito::real_sound_comm_base, 0x04U);
    restored->main_bus.write8(taito::real_sound_comm_base + 2U, 0x00U);

    CHECK_FALSE(restored->sound_cpu.reset_line_held());
    CHECK(restored->sound_reset_control_write_count == 2U);
    CHECK(restored->sound_reset_assert_count == 1U);
    CHECK(restored->sound_reset_release_count == 1U);
    CHECK(restored->last_sound_reset_control_value == 0U);
    CHECK(restored->sound_reset_state[1] == 0U);
    CHECK(read32_le(restored->sound_reset_state, 12U) == 1U);

    restored->sound_cpu.step_instruction();
    CHECK(restored->sound_cpu.cpu_registers().pc == 1U);
}

TEST_CASE("taito_f2 TC0140SYT commands pulse the sound Z80 NMI",
          "[taito_f2][sound]") {
    auto image = make_image();
    auto& sound = image.regions["audiocpu"];
    sound.assign(0x4000U, 0x00U);
    const std::vector<std::uint8_t> program{
        0x31, 0x00, 0xD0, // LD SP,$D000
        0x76,             // HALT until the 68K posts a TC0140SYT command
    };
    std::copy(program.begin(), program.end(), sound.begin());

    const std::vector<std::uint8_t> nmi_handler{
        0x3E, 0x00,       // LD A,$00  -- select command port 0
        0x32, 0x00, 0xE2, // LD ($E200),A
        0x3A, 0x01, 0xE2, // LD A,($E201)
        0x32, 0x20, 0xC1, // LD ($C120),A
        0x3A, 0x01, 0xE2, // LD A,($E201)
        0x32, 0x21, 0xC1, // LD ($C121),A
        0x76,             // HALT
    };
    std::copy(nmi_handler.begin(), nmi_handler.end(), sound.begin() + 0x66U);

    auto params = taito::taito_f2_board_params{};
    params.address_map = taito::taito_f2_address_map::gunfront;
    auto system = taito::assemble_taito_f2(std::move(image), params);

    system->sound_cpu.step_instruction();
    system->sound_cpu.step_instruction();
    REQUIRE(system->sound_cpu.cpu_registers().halted);

    write_sound_command(*system, taito::real_sound_comm_base, 0x5AU);
    CHECK(system->sound_comm.command_write_count(0U) == 1U);
    for (int i = 0; i < 10; ++i) {
        system->sound_cpu.step_instruction();
    }

    CHECK(system->sound_bus.read8(0xC120U) == 0x0AU);
    CHECK(system->sound_bus.read8(0xC121U) == 0x05U);
    CHECK_FALSE(system->sound_latch_pending);
    CHECK(system->sound_comm.command_read_count(0U) == 1U);
    CHECK(system->sound_cpu.nmi_accept_count() == 1U);
    CHECK(system->sound_cpu.cpu_registers().halted);
}

TEST_CASE("taito_f2 sound bank register selects 16 KiB Z80 ROM pages",
          "[taito_f2][sound]") {
    const auto make_banked_image = [] {
        auto image = make_image();
        auto& sound = image.regions["audiocpu"];
        sound.assign(0x10000U, 0xFFU);
        sound[0x0000U] = 0xA0U;
        sound[0x4000U] = 0xB1U;
        sound[0x8000U] = 0xC2U;
        sound[0xC000U] = 0xD3U;
        return image;
    };

    auto system = taito::assemble_taito_f2(make_banked_image());
    CHECK(system->z80_sound_bank_page_count() == 4U);
    CHECK(system->z80_sound_bank_page() == 0U);
    CHECK(system->z80_sound_bank_page_valid());
    CHECK(system->sound_bank_state[0] == 0U);
    CHECK(system->sound_bank_state[1] == 0U);
    CHECK(system->sound_bank_state[2] == 4U);
    CHECK(system->sound_bank_state[3] == 1U);

    system->sound_bus.write8(taito::z80_bank_reg, 0x19U);
    CHECK(system->sound_bank == 0x19U);
    CHECK(system->z80_sound_bank_page() == 1U);
    CHECK(system->z80_sound_bank_page_valid());
    CHECK(system->sound_bank_state[0] == 0x19U);
    CHECK(system->sound_bank_state[1] == 1U);
    CHECK(system->sound_bank_state[2] == 4U);
    CHECK(system->sound_bank_state[3] == 1U);
    CHECK(system->sound_bus.read8(taito::z80_bank_base) == 0xB1U);

    system->sound_bus.write8(taito::z80_bank_reg, 0x1CU);
    CHECK(system->sound_bus.read8(taito::z80_bank_base) == 0xA0U);

    system->sound_bus.write8(taito::z80_bank_reg, 0x1FU);
    CHECK(system->sound_bus.read8(taito::z80_bank_base) == 0xD3U);
    system->main_bus.write8(taito::comm_base + 1U, 0x42U);
    system->sound_bus.write8(taito::z80_tc0140syt_port_addr, 0x00U);
    (void)system->sound_bus.read8(taito::z80_tc0140syt_data_addr);
    (void)system->sound_bus.read8(taito::z80_tc0140syt_data_addr);
    CHECK(system->sound_comm.command_write_count(0U) == 1U);
    CHECK(system->sound_comm.command_read_count(0U) == 1U);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    system->save_state(writer);

    auto restored = taito::assemble_taito_f2(make_banked_image());
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->sound_bank == 0x1FU);
    CHECK(restored->z80_sound_bank_page() == 3U);
    CHECK(restored->z80_sound_bank_page_valid());
    CHECK(restored->sound_bank_state[0] == 0x1FU);
    CHECK(restored->sound_bank_state[1] == 3U);
    CHECK(restored->sound_bank_state[2] == 4U);
    CHECK(restored->sound_bank_state[3] == 1U);
    CHECK(restored->sound_bus.read8(taito::z80_bank_base) == 0xD3U);
    CHECK(restored->sound_comm.command_write_count(0U) == 1U);
    CHECK(restored->sound_comm.command_read_count(0U) == 1U);

    auto mismatched_params = taito::taito_f2_board_params{};
    mismatched_params.hardware_profiles_explicit = true;
    mismatched_params.io_profile = taito::taito_f2_io_profile::tc0510nio;
    auto mismatched = taito::assemble_taito_f2(make_banked_image(), mismatched_params);
    mnemos::chips::state_reader mismatch_reader(snapshot);
    mismatched->load_state(mismatch_reader);
    CHECK_FALSE(mismatch_reader.ok());
}

TEST_CASE("taito_f2 sound bank register exposes missing Z80 ROM pages",
          "[taito_f2][sound]") {
    auto image = make_image();
    auto& sound = image.regions["audiocpu"];
    sound.assign(0xC000U, 0xFFU);
    sound[0x0000U] = 0xA0U;
    sound[0x4000U] = 0xB1U;
    sound[0x8000U] = 0xC2U;

    auto system = taito::assemble_taito_f2(std::move(image));
    CHECK(system->z80_sound_bank_page_count() == 3U);

    system->sound_bus.write8(taito::z80_bank_reg, 0x1AU);
    CHECK(system->z80_sound_bank_page() == 2U);
    CHECK(system->z80_sound_bank_page_valid());
    CHECK(system->sound_bank_state[0] == 0x1AU);
    CHECK(system->sound_bank_state[1] == 2U);
    CHECK(system->sound_bank_state[2] == 3U);
    CHECK(system->sound_bank_state[3] == 1U);
    CHECK(system->sound_bus.read8(taito::z80_bank_base) == 0xC2U);

    system->sound_bus.write8(taito::z80_bank_reg, 0x1BU);
    CHECK(system->z80_sound_bank_page() == 3U);
    CHECK_FALSE(system->z80_sound_bank_page_valid());
    CHECK(system->sound_bank_state[0] == 0x1BU);
    CHECK(system->sound_bank_state[1] == 3U);
    CHECK(system->sound_bank_state[2] == 3U);
    CHECK(system->sound_bank_state[3] == 0U);
    CHECK(system->sound_bus.read8(taito::z80_bank_base) == 0xFFU);
}

TEST_CASE("taito_f2 palette writes publish decoded palette state",
          "[taito_f2][palette][save]") {
    auto params = taito::taito_f2_board_params{};
    params.address_map = taito::taito_f2_address_map::qzchikyu;
    params.palette_format = taito::taito_f2_palette_format::xrgb_555;
    auto source = taito::assemble_taito_f2(make_image(), params);

    CHECK(source->palette_write_state[0] == 0U);
    CHECK(source->palette_write_state[1] ==
          static_cast<std::uint8_t>(taito::taito_f2_palette_format::xrgb_555));
    CHECK(source->palette_write_state[20] == 0U);

    source->main_bus.write16_be(taito::qzchikyu_palette_ram_base + 4U, 0x7C00U);

    const std::uint32_t expected_color =
        mnemos::chips::video::taito_f2_video::decode_color(
            mnemos::chips::video::taito_f2_video::palette_format::xrgb_555,
            0x7C00U);
    CHECK(source->main_bus.read16_be(taito::qzchikyu_palette_ram_base + 4U) == 0x7C00U);
    CHECK(source->palette_ram[4] == 0x7CU);
    CHECK(source->palette_ram[5] == 0x00U);
    CHECK(source->palette_write_count == 2U);
    CHECK(source->last_palette_write_address == taito::qzchikyu_palette_ram_base + 5U);
    CHECK(source->last_palette_write_value == 0x00U);
    CHECK(source->last_palette_index == 2U);
    CHECK(source->last_palette_word == 0x7C00U);
    CHECK(source->last_palette_color == expected_color);
    CHECK(source->palette_read_count == 2U);
    CHECK(source->last_palette_read_address == taito::qzchikyu_palette_ram_base + 5U);
    CHECK(source->last_palette_read_value == 0x00U);
    CHECK(source->last_palette_read_index == 2U);
    CHECK(source->last_palette_read_word == 0x7C00U);
    CHECK(source->last_palette_read_color == expected_color);

    CHECK(source->palette_write_state[0] == 1U);
    CHECK(source->palette_write_state[1] ==
          static_cast<std::uint8_t>(taito::taito_f2_palette_format::xrgb_555));
    CHECK(source->palette_write_state[2] == 0x00U);
    CHECK(source->palette_write_state[3] ==
          static_cast<std::uint8_t>(taito::taito_f2_address_map::qzchikyu));
    CHECK(read32_le(source->palette_write_state, 4U) == 2U);
    CHECK(read32_le(source->palette_write_state, 8U) ==
          taito::qzchikyu_palette_ram_base + 5U);
    CHECK(read16_le(source->palette_write_state, 12U) == 0x7C00U);
    CHECK(read16_le(source->palette_write_state, 14U) == 2U);
    CHECK(read32_le(source->palette_write_state, 16U) == expected_color);
    CHECK(source->palette_write_state[20] == 1U);
    CHECK(source->palette_write_state[21] == 0x00U);
    CHECK(source->palette_write_state[22] ==
          static_cast<std::uint8_t>(taito::taito_f2_palette_profile::tc0110pcr_tc0070rgb));
    CHECK(source->palette_write_state[23] == 1U);
    CHECK(read32_le(source->palette_write_state, 24U) == 2U);
    CHECK(read32_le(source->palette_write_state, 28U) ==
          taito::qzchikyu_palette_ram_base + 5U);
    CHECK(read16_le(source->palette_write_state, 32U) == 0x7C00U);
    CHECK(read16_le(source->palette_write_state, 34U) == 2U);
    CHECK(read32_le(source->palette_write_state, 36U) == expected_color);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = taito::assemble_taito_f2(make_image(), params);
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->palette_write_state == source->palette_write_state);
    CHECK(restored->palette_write_count == source->palette_write_count);
    CHECK(restored->last_palette_write_address == source->last_palette_write_address);
    CHECK(restored->last_palette_write_value == source->last_palette_write_value);
    CHECK(restored->last_palette_index == source->last_palette_index);
    CHECK(restored->last_palette_word == source->last_palette_word);
    CHECK(restored->last_palette_color == source->last_palette_color);
    CHECK(restored->palette_read_count == source->palette_read_count);
    CHECK(restored->last_palette_read_address == source->last_palette_read_address);
    CHECK(restored->last_palette_read_value == source->last_palette_read_value);
    CHECK(restored->last_palette_read_index == source->last_palette_read_index);
    CHECK(restored->last_palette_read_word == source->last_palette_read_word);
    CHECK(restored->last_palette_read_color == source->last_palette_read_color);
}

TEST_CASE("taito_f2 sound Z80 reaches the YM2610 port pair", "[taito_f2][sound]") {
    auto image = make_image();
    auto& sound = image.regions["audiocpu"];
    sound.assign(0x4000U, 0x00U);
    const std::vector<std::uint8_t> program{
        0x3E, 0x08,       // LD A,$08  -- SSG ch-A fine tune
        0x32, 0x00, 0xE0, // LD ($E000),A
        0x3E, 0x0F,       // LD A,$0F
        0x32, 0x01, 0xE0, // LD ($E001),A
        0x18, 0xFE,       // JR $
    };
    std::copy(program.begin(), program.end(), sound.begin());

    auto system = taito::assemble_taito_f2(std::move(image));
    system->run_frame();

    CHECK(system->opnb.ssg_block().read_reg(0x08U) == 0x0FU);
    CHECK(system->sound_cpu.cpu_registers().pc != 0U);
}

TEST_CASE("taito_f2 routes YM2610 timer IRQs to the sound Z80", "[taito_f2][sound]") {
    auto image = make_image();
    auto& sound = image.regions["audiocpu"];
    sound.assign(0x4000U, 0x00U);
    const std::vector<std::uint8_t> program{
        0x31, 0x00, 0xD0, // LD SP,$D000
        0xED, 0x56,       // IM 1
        0xFB,             // EI
        0x00,             // NOP, after which IFF1 becomes observable
        0x76,             // HALT until the YM2610 timer asserts /INT
    };
    std::copy(program.begin(), program.end(), sound.begin());

    const std::vector<std::uint8_t> handler{
        0x3E, 0x99,       // LD A,$99
        0x32, 0x00, 0xC1, // LD ($C100),A
        0x76,             // HALT
    };
    std::copy(handler.begin(), handler.end(), sound.begin() + 0x38U);

    auto system = taito::assemble_taito_f2(std::move(image));
    for (int i = 0; i < 5; ++i) {
        system->sound_cpu.step_instruction();
    }
    REQUIRE(system->sound_cpu.cpu_registers().halted);
    CHECK(system->sound_bus.read8(0xC100U) == 0x00U);

    system->sound_bus.write8(taito::z80_ym_base + 0U, 0x24U);
    system->sound_bus.write8(taito::z80_ym_base + 1U, 0xFFU);
    system->sound_bus.write8(taito::z80_ym_base + 0U, 0x25U);
    system->sound_bus.write8(taito::z80_ym_base + 1U, 0x02U);
    system->sound_bus.write8(taito::z80_ym_base + 0U, 0x27U);
    system->sound_bus.write8(taito::z80_ym_base + 1U, 0x05U);
    system->opnb.tick(2U * mnemos::chips::audio::ym2610::default_clock_divider);
    REQUIRE(system->opnb.irq_asserted());

    for (int i = 0; i < 4; ++i) {
        system->sound_cpu.step_instruction();
    }
    CHECK(system->sound_bus.read8(0xC100U) == 0x99U);

    system->sound_bus.write8(taito::z80_ym_base + 0U, 0x27U);
    system->sound_bus.write8(taito::z80_ym_base + 1U, 0x15U);
    CHECK_FALSE(system->opnb.irq_asserted());
}

TEST_CASE("taito_f2 exposes player inputs and dips on the board window", "[taito_f2][input]") {
    auto system = taito::assemble_taito_f2(make_image());
    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);

    CHECK(system->main_bus.read8(taito::input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::input_base + 3U) == 0x55U);
    CHECK(system->main_bus.read8(taito::input_base + 5U) == 0x3CU);
    CHECK(system->main_bus.read8(taito::input_base + 7U) == 0x12U);
    CHECK(system->main_bus.read8(taito::input_base + 9U) == 0x34U);
}

TEST_CASE("taito_f2 I/O windows latch raw output writes and coin meter edges",
          "[taito_f2][input]") {
    auto params = taito::taito_f2_board_params{};
    params.address_map = taito::taito_f2_address_map::gunfront;
    auto system = taito::assemble_taito_f2(make_image(), params);

    CHECK(system->io_output_state[0] == 0U);

    system->main_bus.write8(taito::real_input_base + 1U, 0x11U);
    CHECK(system->io_output_regs[1] == 0x11U);
    CHECK(system->io_output_latch == 0x11U);
    CHECK(system->last_io_output_address == taito::real_input_base + 1U);
    CHECK(system->last_io_output_value == 0x11U);
    CHECK(system->io_output_write_count == 1U);
    CHECK(system->coin_counters[0] == 1U);
    CHECK(system->coin_counters[1] == 0U);
    CHECK(system->coin_counter_lines[0]);
    CHECK_FALSE(system->coin_counter_lines[1]);
    CHECK(system->coin_lockouts[0]);
    CHECK_FALSE(system->coin_lockouts[1]);
    CHECK(system->io_output_state[0] == 1U);
    CHECK(system->io_output_state[1] == 1U);
    CHECK(system->io_output_state[2] == 0x11U);
    CHECK(system->io_output_state[3] == 0x11U);
    CHECK(system->io_output_state[4] == 0x01U);
    CHECK(system->io_output_state[5] == 0x01U);
    CHECK(system->io_output_state[6] == 1U);
    CHECK(system->io_output_state[10] == 1U);
    CHECK(system->io_output_state[26] == 2U);
    CHECK(system->io_output_state[27] == 4U);
    CHECK(system->io_output_state[35] == 0x04U);
    CHECK(system->io_output_state[36] == 0U);
    CHECK(system->io_access_state[56] == 0x04U);
    CHECK(system->io_access_state[57] == 0U);
    CHECK(system->io_access_state[58] == 0U);

    system->main_bus.write8(taito::real_input_base + 1U, 0x11U);
    CHECK(system->io_output_write_count == 2U);
    CHECK(system->coin_counters[0] == 1U);

    system->main_bus.write8(taito::real_input_base + 1U, 0x03U);
    CHECK(system->io_output_latch == 0x03U);
    CHECK(system->coin_counters[0] == 1U);
    CHECK(system->coin_counters[1] == 1U);
    CHECK_FALSE(system->coin_lockouts[0]);
}

TEST_CASE("taito_f2 multi-window I/O writes are retained by offset",
          "[taito_f2][input]") {
    auto params = taito::taito_f2_board_params{};
    params.address_map = taito::taito_f2_address_map::growl;
    params.players = 4U;
    params.input_profile = taito::taito_f2_input_profile::split_tmp82c265;
    auto system = taito::assemble_taito_f2(make_image(), params);

    system->main_bus.write8(taito::growl_p3_input_base + 7U, 0x5AU);
    CHECK(system->io_output_regs[7] == 0x5AU);
    CHECK(system->last_io_output_address == taito::growl_p3_input_base + 7U);
    CHECK(system->last_io_output_value == 0x5AU);
    CHECK(system->io_output_write_count == 1U);
    CHECK(system->io_output_latch == 0U);
    CHECK(system->coin_counters[0] == 0U);
    CHECK(system->io_output_state[26] == 4U);

    system->main_bus.write8(taito::growl_player_input_base + 1U, 0xCFU);
    CHECK(system->io_output_regs[1] == 0xCFU);
    CHECK(system->io_output_latch == 0xCFU);
    CHECK(system->coin_counters[0] == 1U);
    CHECK(system->coin_counters[1] == 1U);
    CHECK(system->coin_counters[2] == 1U);
    CHECK(system->coin_counters[3] == 1U);
    CHECK_FALSE(system->coin_lockouts[0]);
    CHECK_FALSE(system->coin_lockouts[1]);
    CHECK(system->coin_lockouts[2]);
    CHECK(system->coin_lockouts[3]);
    CHECK(system->io_output_state[4] == 0x0FU);
    CHECK(system->io_output_state[5] == 0x0CU);
    CHECK(system->io_output_state[26] == 4U);
    CHECK(system->io_output_state[27] == 4U);
    CHECK(system->io_output_state[35] == 0x01U);
    CHECK(system->io_output_state[36] == 0x0CU);
    CHECK(system->io_output_state[37] == system->input_system);
    CHECK(system->io_output_state[38] == system->input_coin_extension);
    CHECK(system->io_output_state[39] == 1U);
}

TEST_CASE("taito_f2 Ninja Kids TE7750 input exposes four-player service mux",
          "[taito_f2][input]") {
    auto params = taito::taito_f2_board_params{};
    params.address_map = taito::taito_f2_address_map::ninjak;
    params.players = 4U;
    params.input_profile = taito::taito_f2_input_profile::te7750_quad;
    auto system = taito::assemble_taito_f2(make_image(), params);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    system->input_p3 = 0x66U;
    system->input_p4 = 0x99U;
    system->input_coin_extension = 0xE4U;

    CHECK(system->main_bus.read8(taito::ninjak_input_base + 0U) == 0x12U);
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 2U) == 0x34U);
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 4U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 6U) == 0x55U);
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 8U) == 0x66U);
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 10U) == 0x99U);
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 12U) == 0x3CU);
    CHECK(system->main_bus.read8(taito::ninjak_input_base + 14U) == 0xE4U);

    system->main_bus.write8(taito::ninjak_input_base + 1U, 0x0FU);
    CHECK(system->coin_counters[0] == 1U);
    CHECK(system->coin_counters[1] == 1U);
    CHECK(system->coin_counters[2] == 1U);
    CHECK(system->coin_counters[3] == 1U);
    CHECK(system->io_output_state[4] == 0x0FU);
    CHECK(system->io_output_state[26] == 4U);
    CHECK(system->io_output_state[27] == 4U);
    CHECK(system->io_output_state[35] == 0x01U);
    CHECK(system->io_output_state[36] == 0x18U);
    CHECK(system->io_output_state[37] == system->input_system);
    CHECK(system->io_output_state[38] == system->input_coin_extension);
    CHECK(system->io_output_state[39] == 0U);
}

TEST_CASE("taito_f2 I/O output sidecar round-trips through save state",
          "[taito_f2][input][save]") {
    auto params = taito::taito_f2_board_params{};
    params.address_map = taito::taito_f2_address_map::gunfront;
    auto source = taito::assemble_taito_f2(make_image(), params);
    source->main_bus.write8(taito::real_input_base + 1U, 0x03U);
    source->main_bus.write8(taito::real_input_base + 5U, 0x7EU);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = taito::assemble_taito_f2(make_image(), params);
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->io_output_regs == source->io_output_regs);
    CHECK(restored->io_output_state == source->io_output_state);
    CHECK(restored->io_output_latch == source->io_output_latch);
    CHECK(restored->coin_counters == source->coin_counters);
    CHECK(restored->coin_counter_lines == source->coin_counter_lines);
    CHECK(restored->coin_lockouts == source->coin_lockouts);
    CHECK(restored->io_output_write_count == source->io_output_write_count);
    CHECK(restored->last_io_output_address == source->last_io_output_address);
    CHECK(restored->last_io_output_value == source->last_io_output_value);
}

TEST_CASE("taito_f2 I/O access sidecar tracks readback byte lanes and service mux",
          "[taito_f2][input][save]") {
    auto params = taito::taito_f2_board_params{};
    params.address_map = taito::taito_f2_address_map::growl;
    params.players = 4U;
    params.input_profile = taito::taito_f2_input_profile::split_tmp82c265;
    auto source = taito::assemble_taito_f2(make_image(), params);
    source->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    source->input_coin_extension = 0xC3U;

    REQUIRE(source->io_access_state.size() == taito::io_access_state_bytes);
    CHECK(source->io_access_state[0] == 1U);
    CHECK(source->io_access_state[1] == 0U);

    CHECK(source->main_bus.read16_be(taito::growl_dip_input_base) == 0x1212U);
    CHECK(source->io_input_read_count == 2U);
    CHECK(source->io_input_dip_read_count == 2U);
    CHECK(source->io_input_service_read_count == 0U);
    CHECK(source->io_access_read_even_count == 1U);
    CHECK(source->io_access_read_odd_count == 1U);
    CHECK(source->io_access_inferred_read_pair_count == 1U);
    CHECK(read32_le(source->io_access_state, 4U) == source->io_input_read_count);
    CHECK(read32_le(source->io_access_state, 12U) == source->io_input_dip_read_count);
    CHECK(read32_le(source->io_access_state, 36U) ==
          source->io_access_inferred_read_pair_count);
    CHECK(read32_le(source->io_access_state, 44U) == taito::growl_dip_input_base + 1U);
    CHECK(source->io_access_state[48] == 0x12U);
    CHECK(source->io_access_state[49] == 1U);

    CHECK(source->main_bus.read16_be(taito::growl_player_input_base + 4U) == 0x3C3CU);
    CHECK(source->io_input_read_count == 4U);
    CHECK(source->io_input_service_read_count == 2U);
    CHECK(source->io_access_inferred_read_pair_count == 2U);
    CHECK(read32_le(source->io_access_state, 16U) ==
          source->io_input_service_read_count);
    CHECK(source->io_access_state[54] == source->input_system);
    CHECK(source->io_access_state[55] == source->input_coin_extension);
    CHECK(source->io_access_state[56] == 0x01U);
    CHECK(source->io_access_state[57] == 0x0CU);
    CHECK(source->io_access_state[58] == 1U);

    source->main_bus.write16_be(taito::growl_player_input_base, 0x0F0FU);
    CHECK(source->io_output_write_count == 2U);
    CHECK(source->io_access_write_even_count == 1U);
    CHECK(source->io_access_write_odd_count == 1U);
    CHECK(source->io_access_inferred_write_pair_count == 1U);
    CHECK(source->last_io_access_write);
    CHECK(source->last_io_access_pair_inferred);
    CHECK(read32_le(source->io_access_state, 8U) == source->io_output_write_count);
    CHECK(read32_le(source->io_access_state, 28U) ==
          source->io_access_write_even_count);
    CHECK(read32_le(source->io_access_state, 32U) ==
          source->io_access_write_odd_count);
    CHECK(read32_le(source->io_access_state, 40U) ==
          source->io_access_inferred_write_pair_count);
    CHECK(read32_le(source->io_access_state, 44U) == taito::growl_player_input_base + 1U);
    CHECK(source->io_access_state[2] == 1U);
    CHECK(source->io_access_state[48] == 0x0FU);
    CHECK(source->io_access_state[49] == 1U);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = taito::assemble_taito_f2(make_image(), params);
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->io_access_state == source->io_access_state);
    CHECK(restored->io_input_read_count == source->io_input_read_count);
    CHECK(restored->io_input_dip_read_count == source->io_input_dip_read_count);
    CHECK(restored->io_input_service_read_count == source->io_input_service_read_count);
    CHECK(restored->io_access_inferred_read_pair_count ==
          source->io_access_inferred_read_pair_count);
    CHECK(restored->io_access_inferred_write_pair_count ==
          source->io_access_inferred_write_pair_count);
    CHECK(restored->previous_io_access_valid == source->previous_io_access_valid);
}

TEST_CASE("taito_f2 watchdog windows publish per-map diagnostics",
          "[taito_f2][input][watchdog][save]") {
    auto growl_params = taito::taito_f2_board_params{};
    growl_params.address_map = taito::taito_f2_address_map::growl;
    auto growl = taito::assemble_taito_f2(make_image(), growl_params);

    CHECK(growl->watchdog_state[0] == 1U);
    CHECK(growl->watchdog_state[32] == 1U);
    CHECK(growl->watchdog_state[33] == 0U);
    CHECK(growl->watchdog_state[34] == 0U);
    CHECK(growl->watchdog_state[35] == 0U);
    CHECK(read32_le(growl->watchdog_state, 24U) == taito::growl_watchdog_base);
    growl->main_bus.write8(taito::growl_watchdog_base, 0x5AU);
    CHECK(growl->watchdog_write_count == 1U);
    CHECK(growl->watchdog_confirmed_write_count == 1U);
    CHECK(growl->watchdog_suspect_write_count == 0U);
    CHECK(growl->last_watchdog_address == taito::growl_watchdog_base);
    CHECK(growl->last_watchdog_value == 0x5AU);
    CHECK(growl->watchdog_state[1] == 1U);
    CHECK(growl->watchdog_state[2] == 1U);
    CHECK(read32_le(growl->watchdog_state, 4U) == 1U);
    CHECK(read32_le(growl->watchdog_state, 8U) == 1U);
    CHECK(read32_le(growl->watchdog_state, 16U) == taito::growl_watchdog_base);
    CHECK(growl->watchdog_state[20] == 0x5AU);
    CHECK(growl->watchdog_state[21] == 2U);
    CHECK(growl->watchdog_state[44] == 1U);
    CHECK(growl->watchdog_state[46] == taito::watchdog_window);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    growl->save_state(writer);
    auto restored = taito::assemble_taito_f2(make_image(), growl_params);
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored->watchdog_state == growl->watchdog_state);
    CHECK(restored->watchdog_write_count == 1U);
    CHECK(restored->last_watchdog_value == 0x5AU);

    auto quiz_params = taito::taito_f2_board_params{};
    quiz_params.address_map = taito::taito_f2_address_map::quizhq;
    auto quiz = taito::assemble_taito_f2(make_image(), quiz_params);
    CHECK(read32_le(quiz->watchdog_state, 24U) == taito::quizhq_watchdog_base);
    quiz->main_bus.write8(taito::quizhq_input_b_base, 0x33U);
    CHECK(quiz->watchdog_confirmed_write_count == 1U);
    CHECK(quiz->io_output_write_count == 1U);
    CHECK(quiz->watchdog_state[21] == 1U);

    auto ninjak_params = taito::taito_f2_board_params{};
    ninjak_params.address_map = taito::taito_f2_address_map::ninjak;
    auto ninjak = taito::assemble_taito_f2(make_image(), ninjak_params);
    CHECK(read32_le(ninjak->watchdog_state, 24U) == taito::ninjak_watchdog_base);
    ninjak->main_bus.write8(taito::ninjak_watchdog_base + 1U, 0x44U);
    CHECK(ninjak->watchdog_confirmed_write_count == 1U);
    CHECK(ninjak->watchdog_state[21] == 3U);

    auto gunfront_params = taito::taito_f2_board_params{};
    gunfront_params.address_map = taito::taito_f2_address_map::gunfront;
    auto gunfront = taito::assemble_taito_f2(make_image(), gunfront_params);
    CHECK(read32_le(gunfront->watchdog_state, 24U) ==
          taito::real_input_base + 2U);
    CHECK(gunfront->watchdog_state[32] == 1U);
    gunfront->main_bus.write8(taito::real_input_base + 2U, 0x22U);
    CHECK(gunfront->watchdog_confirmed_write_count == 1U);
    CHECK(gunfront->watchdog_suspect_write_count == 0U);
    CHECK(gunfront->last_watchdog_address == taito::real_input_base + 2U);
    CHECK(gunfront->watchdog_state[21] == 8U);
    CHECK(gunfront->watchdog_state[44] == 1U);

    auto footchmp_params = taito::taito_f2_board_params{};
    footchmp_params.address_map = taito::taito_f2_address_map::footchmp;
    auto footchmp = taito::assemble_taito_f2(make_image(), footchmp_params);
    CHECK(read32_le(footchmp->watchdog_state, 24U) ==
          taito::footchmp_watchdog_base);
    CHECK(read32_le(footchmp->watchdog_state, 36U) ==
          taito::footchmp_priority_base + 2U);
    footchmp->main_bus.write8(taito::footchmp_watchdog_base, 0x55U);
    footchmp->main_bus.write8(taito::footchmp_priority_base + 2U, 0x66U);
    CHECK(footchmp->watchdog_confirmed_write_count == 1U);
    CHECK(footchmp->watchdog_suspect_write_count == 1U);
    CHECK(footchmp->watchdog_state[3] == 1U);
    CHECK(footchmp->watchdog_state[21] == 6U);
    CHECK(footchmp->watchdog_state[44] == 0U);

    auto dinorex_params = taito::taito_f2_board_params{};
    dinorex_params.address_map = taito::taito_f2_address_map::dinorex;
    auto dinorex = taito::assemble_taito_f2(make_image(), dinorex_params);
    CHECK(dinorex->watchdog_state[32] == 1U);
    CHECK(dinorex->watchdog_state[33] == 1U);
    CHECK(read32_le(dinorex->watchdog_state, 24U) == taito::real_input_base);
    CHECK(read32_le(dinorex->watchdog_state, 28U) == taito::dinorex_watchdog_base);
    dinorex->main_bus.write8(taito::dinorex_watchdog_base, 0x77U);
    CHECK(dinorex->watchdog_confirmed_write_count == 0U);
    CHECK(dinorex->watchdog_suspect_write_count == 1U);
    CHECK(dinorex->watchdog_state[21] == 5U);
}

TEST_CASE("taito_f2 main bus sidecar tracks byte lanes and unmapped accesses",
          "[taito_f2][bus][save]") {
    auto params = taito::taito_f2_board_params{};
    params.address_map = taito::taito_f2_address_map::growl;
    auto system = taito::assemble_taito_f2(make_image(), params);

    REQUIRE(system->main_bus_state.size() == taito::main_bus_state_bytes);
    CHECK(system->main_bus_state[0] == 1U);
    CHECK(system->main_bus_state[1] == 1U);
    CHECK(system->main_bus_state[38] == 1U);
    CHECK(system->main_bus_state[39] == 0U);

    const std::uint32_t initial_writes = system->main_bus_write_count;
    const std::uint32_t initial_pairs = system->main_bus_inferred_word_pair_count;
    const std::uint32_t initial_odd = system->main_bus_odd_access_count;
    system->main_bus.write16_be(taito::work_ram_base, 0x1234U);
    CHECK(system->work_ram[0] == 0x12U);
    CHECK(system->work_ram[1] == 0x34U);
    CHECK(system->main_bus_write_count >= initial_writes + 2U);
    CHECK(system->main_bus_inferred_word_pair_count >= initial_pairs + 1U);
    CHECK(system->main_bus_odd_access_count >= initial_odd + 1U);
    CHECK(system->last_main_bus_address == taito::work_ram_base + 1U);
    CHECK(system->last_main_bus_value == 0x34U);
    CHECK(system->last_main_bus_write);
    CHECK(system->last_main_bus_mapped);
    CHECK_FALSE(system->last_main_bus_open_bus);
    CHECK(system->last_main_bus_pair_inferred);
    CHECK(read32_le(system->main_bus_state, 8U) == system->main_bus_write_count);
    CHECK(read32_le(system->main_bus_state, 24U) ==
          system->main_bus_inferred_word_pair_count);
    CHECK(read32_le(system->main_bus_state, 28U) == taito::work_ram_base + 1U);
    CHECK(system->main_bus_state[32] == 0x34U);
    CHECK(system->main_bus_state[35] == 1U);
    CHECK(system->main_bus_state[36] ==
          static_cast<std::uint8_t>(taito::taito_f2_address_map::growl));
    CHECK(system->main_bus_state[37] == 1U);

    constexpr std::uint32_t unmapped_base = 0x00F00000U;
    const std::uint32_t initial_reads = system->main_bus_read_count;
    const std::uint32_t initial_open_bus = system->main_bus_open_bus_read_count;
    CHECK(system->main_bus.read8(unmapped_base) == 0xFFU);
    CHECK(system->main_bus_read_count == initial_reads + 1U);
    CHECK(system->main_bus_open_bus_read_count == initial_open_bus + 1U);
    CHECK_FALSE(system->last_main_bus_write);
    CHECK_FALSE(system->last_main_bus_mapped);
    CHECK(system->last_main_bus_open_bus);
    CHECK(read32_le(system->main_bus_state, 12U) ==
          system->main_bus_open_bus_read_count);
    CHECK(read32_le(system->main_bus_state, 28U) == unmapped_base);
    CHECK(system->main_bus_state[33] == 1U);

    const std::uint32_t initial_unmapped_writes =
        system->main_bus_unmapped_write_count;
    system->main_bus.write8(unmapped_base + 2U, 0xA5U);
    CHECK(system->main_bus_unmapped_write_count == initial_unmapped_writes + 1U);
    CHECK(system->last_main_bus_write);
    CHECK_FALSE(system->last_main_bus_mapped);
    CHECK(read32_le(system->main_bus_state, 16U) ==
          system->main_bus_unmapped_write_count);
    CHECK(read32_le(system->main_bus_state, 28U) == unmapped_base + 2U);
    CHECK(system->main_bus_state[32] == 0xA5U);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    system->save_state(writer);

    auto restored = taito::assemble_taito_f2(make_image(), params);
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->main_bus_state == system->main_bus_state);
    CHECK(restored->main_bus_read_count == system->main_bus_read_count);
    CHECK(restored->main_bus_write_count == system->main_bus_write_count);
    CHECK(restored->main_bus_open_bus_read_count ==
          system->main_bus_open_bus_read_count);
    CHECK(restored->main_bus_unmapped_write_count ==
          system->main_bus_unmapped_write_count);
    CHECK(restored->main_bus_inferred_word_pair_count ==
          system->main_bus_inferred_word_pair_count);
    CHECK(restored->last_main_bus_address == system->last_main_bus_address);
    CHECK(restored->last_main_bus_value == system->last_main_bus_value);
    CHECK(restored->previous_main_bus_valid == system->previous_main_bus_valid);
}

TEST_CASE("taito_f2 IRQ sidecar tracks configured VBL assert and CPU ack levels",
          "[taito_f2][irq][save]") {
    auto params = taito::taito_f2_board_params{};
    params.vblank_irq_level = 5U;
    params.sprite_dma_irq_level = 6U;
    auto source = taito::assemble_taito_f2(make_image(), params);

    REQUIRE(source->irq_state.size() == taito::irq_state_bytes);
    CHECK(source->irq_state[0] == 5U);
    CHECK(source->irq_state[1] == 6U);
    CHECK(source->irq_state[2] == 0U);
    CHECK(source->irq_state[3] == 0U);
    CHECK(read64_le(source->irq_state, 4U) == 0U);
    CHECK(read64_le(source->irq_state, 12U) == 0U);
    CHECK(source->irq_state[22] == 0U);
    CHECK(source->irq_state[23] == 0U);
    CHECK(read64_le(source->irq_state, 24U) == 0U);
    CHECK(read64_le(source->irq_state, 32U) == 0U);

    source->run_frame();

    CHECK(source->vblank_irq_raised > 0U);
    CHECK(source->sprite_dma_irq_raised > 0U);
    CHECK(source->last_vblank_irq_level == 5U);
    CHECK(source->last_sprite_dma_irq_level == 6U);
    CHECK(source->irq_state[0] == 5U);
    CHECK(source->irq_state[1] == 6U);
    CHECK(source->irq_state[2] == 5U);
    CHECK(source->irq_state[22] == 6U);
    CHECK(read64_le(source->irq_state, 4U) == source->vblank_irq_raised);
    CHECK(read64_le(source->irq_state, 24U) == source->sprite_dma_irq_raised);
    CHECK(source->irq_state[20] == 1U);
    CHECK(source->irq_state[40] == 1U);
    CHECK(source->irq_state[42] == (source->vblank_irq_pending ? 1U : 0U));
    CHECK(source->irq_state[43] == (source->sprite_dma_irq_pending ? 1U : 0U));
    if (source->vblank_irq_acked != 0U) {
        CHECK(source->last_irq_ack_level == 5U);
        CHECK(source->irq_state[3] == 5U);
        CHECK(source->irq_state[21] == 1U);
    }
    if (source->sprite_dma_irq_acked != 0U) {
        CHECK(source->last_sprite_dma_irq_ack_level == 6U);
        CHECK(source->irq_state[23] == 6U);
        CHECK(source->irq_state[41] == 1U);
    }
    CHECK(read64_le(source->irq_state, 12U) == source->vblank_irq_acked);
    CHECK(read64_le(source->irq_state, 32U) == source->sprite_dma_irq_acked);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = taito::assemble_taito_f2(make_image(), params);
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->vblank_irq_raised == source->vblank_irq_raised);
    CHECK(restored->vblank_irq_acked == source->vblank_irq_acked);
    CHECK(restored->last_vblank_irq_level == source->last_vblank_irq_level);
    CHECK(restored->last_irq_ack_level == source->last_irq_ack_level);
    CHECK(restored->sprite_dma_irq_raised == source->sprite_dma_irq_raised);
    CHECK(restored->sprite_dma_irq_acked == source->sprite_dma_irq_acked);
    CHECK(restored->last_sprite_dma_irq_level == source->last_sprite_dma_irq_level);
    CHECK(restored->last_sprite_dma_irq_ack_level ==
          source->last_sprite_dma_irq_ack_level);
    CHECK(restored->vblank_irq_pending == source->vblank_irq_pending);
    CHECK(restored->sprite_dma_irq_pending == source->sprite_dma_irq_pending);
    CHECK(restored->irq_state == source->irq_state);
}

TEST_CASE("taito_f2 sprite register latch raises configured DMA IRQ",
          "[taito_f2][irq][sprite]") {
    auto params = taito::taito_f2_board_params{};
    params.vblank_irq_level = 5U;
    params.sprite_dma_irq_level = 6U;
    auto system = taito::assemble_taito_f2(make_image(), params);

    system->main_bus.write16_be(taito::video_reg_base + 12U, 0x0001U);

    CHECK(system->vblank_irq_raised == 0U);
    CHECK(system->sprite_dma_irq_raised == 1U);
    CHECK(system->last_sprite_dma_irq_level == 6U);
    CHECK(system->sprite_dma_irq_pending);
    CHECK(system->irq_state[22] == 6U);
    CHECK(read64_le(system->irq_state, 24U) == 1U);
    CHECK(system->irq_state[40] == 1U);
    CHECK(system->irq_state[43] == 1U);
    CHECK(system->irq_state[44] == 6U);
}

TEST_CASE("taito_f2 board profile sidecar exposes machine timing and support gates",
          "[taito_f2][profile][save]") {
    auto params = taito::taito_f2_board_params{};
    params.vertical = true;
    params.players = 4U;
    params.address_map = taito::taito_f2_address_map::gunfront;
    params.sprite_policy = taito::taito_f2_sprite_policy::banked;
    params.sprite_active_area = taito::taito_f2_sprite_active_area::y_word_bit0;
    params.sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed;
    params.palette_format = taito::taito_f2_palette_format::rgbx_444;
    params.palette_profile = taito::taito_f2_palette_profile::tc0260dar;
    params.text_gfx_source = taito::taito_f2_text_gfx_source::program_1bpp;
    params.text_gfx_base = 0x80000U;
    params.input_profile = taito::taito_f2_input_profile::split_tmp82c265;
    params.io_profile = taito::taito_f2_io_profile::tc0510nio;
    params.tc0100scn_bg_x_offset = 16;
    params.tc0100scn_text_x_offset = 23;
    params.tc0100scn_text_y_origin = -12;
    params.tc0100scn_positive_text_y_origin = 24;
    params.roz_x_offset = -16;
    params.roz_y_offset = 0;
    params.hardware_profiles_explicit = true;

    auto source = taito::assemble_taito_f2(make_image(), params);
    REQUIRE(source->board_profile_state.size() == taito::board_profile_state_bytes);
    CHECK(source->board_profile_state[0] == 1U);
    CHECK(source->board_profile_state[1] == 1U);
    CHECK(source->board_profile_state[2] == 4U);
    CHECK(source->board_profile_state[3] ==
          static_cast<std::uint8_t>(taito::taito_f2_address_map::gunfront));
    CHECK(source->board_profile_state[4] ==
          static_cast<std::uint8_t>(taito::taito_f2_sprite_policy::banked));
    CHECK(source->board_profile_state[5] ==
          static_cast<std::uint8_t>(taito::taito_f2_sprite_active_area::y_word_bit0));
    CHECK(source->board_profile_state[6] ==
          static_cast<std::uint8_t>(taito::taito_f2_sprite_buffering::partial_delayed));
    CHECK(source->board_profile_state[7] ==
          static_cast<std::uint8_t>(taito::taito_f2_palette_format::rgbx_444));
    CHECK(source->board_profile_state[8] ==
          static_cast<std::uint8_t>(taito::taito_f2_text_gfx_source::program_1bpp));
    CHECK(source->board_profile_state[9] ==
          static_cast<std::uint8_t>(taito::taito_f2_input_profile::split_tmp82c265));
    CHECK(source->board_profile_state[10] ==
          static_cast<std::uint8_t>(taito::taito_f2_io_profile::tc0510nio));
    CHECK(source->board_profile_state[11] ==
          static_cast<std::uint8_t>(taito::taito_f2_palette_profile::tc0260dar));
    CHECK(source->board_profile_state[20] == 0xBFU);
    CHECK((source->board_profile_state[21] & 0x0FU) == 0x0FU);
    CHECK(source->board_profile_state[22] == taito::z80_sound_bank_mask);
    CHECK(source->board_profile_state[23] == 1U);
    CHECK(read32_le(source->board_profile_state, 24U) == taito::m68k_clock_hz);
    CHECK(read32_le(source->board_profile_state, 28U) == taito::z80_clock_hz);
    CHECK(read32_le(source->board_profile_state, 32U) == taito::ym2610_clock_hz);
    CHECK(read32_le(source->board_profile_state, 36U) == taito::frame_rate_hz);
    CHECK(read32_le(source->board_profile_state, 40U) ==
          mnemos::chips::video::taito_f2_video::visible_width);
    CHECK(read32_le(source->board_profile_state, 44U) ==
          mnemos::chips::video::taito_f2_video::visible_height);
    CHECK(read32_le(source->board_profile_state, 48U) ==
          mnemos::chips::video::taito_f2_video::line_pixels);
    CHECK(read32_le(source->board_profile_state, 52U) ==
          mnemos::chips::video::taito_f2_video::frame_lines);
    CHECK(read32_le(source->board_profile_state, 56U) ==
          mnemos::chips::video::taito_f2_video::vblank_start);
    CHECK(read32_le(source->board_profile_state, 60U) ==
          static_cast<std::uint32_t>(taito::z80_fixed_rom_window));
    CHECK(read32_le(source->board_profile_state, 64U) ==
          static_cast<std::uint32_t>(taito::z80_bank_window));
    CHECK(read32_le(source->board_profile_state, 68U) ==
          static_cast<std::uint32_t>(taito::z80_ram_size));
    CHECK(read32_le(source->board_profile_state, 72U) ==
          static_cast<std::uint32_t>(taito::z80_fixed_rom_window));
    CHECK(read32_le(source->board_profile_state, 76U) == 1U);
    CHECK(read32_le(source->board_profile_state, 80U) == 0x80000U);
    CHECK(static_cast<std::int16_t>(read16_le(source->board_profile_state, 84U)) == 16);
    CHECK(static_cast<std::int16_t>(read16_le(source->board_profile_state, 86U)) == 23);
    CHECK(static_cast<std::int16_t>(read16_le(source->board_profile_state, 88U)) == -12);
    CHECK(static_cast<std::int16_t>(read16_le(source->board_profile_state, 90U)) == 24);
    CHECK(static_cast<std::int16_t>(read16_le(source->board_profile_state, 92U)) == -16);
    CHECK(static_cast<std::int16_t>(read16_le(source->board_profile_state, 94U)) == 0);

    source->sound_bus.write8(taito::z80_bank_reg, 0x1FU);
    CHECK(source->board_profile_state[23] == 0U);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = taito::assemble_taito_f2(make_image(), params);
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->sound_bank == source->sound_bank);
    CHECK(restored->board_profile_state == source->board_profile_state);

    auto unsupported_params = params;
    unsupported_params.sprite_chip_pair =
        taito::taito_f2_sprite_chip_pair::tc0540obn_tc0520tbc;
    unsupported_params.sound_comm_chip = taito::taito_f2_sound_comm_chip::tc0530syc;
    unsupported_params.aux_profile = taito::taito_f2_aux_profile::rtc;
    unsupported_params.vblank_irq_level = 4U;
    unsupported_params.sprite_dma_irq_level = 5U;

    auto unsupported = taito::assemble_taito_f2(make_image(), unsupported_params);
    CHECK(unsupported->board_profile_state[20] == 0x81U);
}
