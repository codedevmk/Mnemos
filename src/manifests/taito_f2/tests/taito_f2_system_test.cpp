#include "taito_f2_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace {

    namespace taito = mnemos::manifests::taito_f2;
    using mnemos::manifests::common::rom_set_image;

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

} // namespace

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
    decl.taito_f2_map = "gunfront";
    decl.taito_f2_sprite_policy = "banked";
    decl.taito_f2_sprite_extension_base = 0xC00000U;
    decl.taito_f2_sprite_extension_size = 0x2000U;
    decl.taito_f2_sprite_buffering = "partial_delayed";
    decl.taito_f2_palette_format = "rgbx_444";
    decl.taito_f2_sprite_active_area = "y_word_bit0";
    decl.taito_f2_sprite_hide_pixels = static_cast<std::int16_t>(3);
    decl.taito_f2_sprite_flip_hide_pixels = static_cast<std::int16_t>(-3);

    const auto params = taito::board_params_from_decl(decl);
    CHECK(params.vertical);
    CHECK(params.address_map == taito::taito_f2_address_map::gunfront);
    CHECK(params.sprite_policy == taito::taito_f2_sprite_policy::banked);
    CHECK(params.sprite_buffering == taito::taito_f2_sprite_buffering::partial_delayed);
    CHECK(params.palette_format == taito::taito_f2_palette_format::rgbx_444);
    CHECK(params.sprite_active_area == taito::taito_f2_sprite_active_area::y_word_bit0);
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
    const auto pulirula_params = taito::board_params_from_decl(decl);
    CHECK(pulirula_params.address_map == taito::taito_f2_address_map::pulirula);

    decl.taito_f2_map = "quizhq";
    const auto quizhq_params = taito::board_params_from_decl(decl);
    CHECK(quizhq_params.address_map == taito::taito_f2_address_map::quizhq);

    decl.taito_f2_map = "qtorimon";
    const auto qtorimon_params = taito::board_params_from_decl(decl);
    CHECK(qtorimon_params.address_map == taito::taito_f2_address_map::qtorimon);

    decl.taito_f2_map = "qzchikyu";
    const auto qzchikyu_params = taito::board_params_from_decl(decl);
    CHECK(qzchikyu_params.address_map == taito::taito_f2_address_map::qzchikyu);

    decl.taito_f2_map = "qzquest";
    const auto qzquest_params = taito::board_params_from_decl(decl);
    CHECK(qzquest_params.address_map == taito::taito_f2_address_map::qzquest);

    decl.taito_f2_map = "metalb";
    const auto metalb_params = taito::board_params_from_decl(decl);
    CHECK(metalb_params.address_map == taito::taito_f2_address_map::metalb);

    decl.taito_f2_map = "footchmp";
    const auto footchmp_params = taito::board_params_from_decl(decl);
    CHECK(footchmp_params.address_map == taito::taito_f2_address_map::footchmp);

    decl.taito_f2_map = "deadconx";
    const auto deadconx_params = taito::board_params_from_decl(decl);
    CHECK(deadconx_params.address_map == taito::taito_f2_address_map::deadconx);

    decl.taito_f2_map = "dinorex";
    const auto dinorex_params = taito::board_params_from_decl(decl);
    CHECK(dinorex_params.address_map == taito::taito_f2_address_map::dinorex);

    decl.taito_f2_map = "thundfox";
    const auto thundfox_params = taito::board_params_from_decl(decl);
    CHECK(thundfox_params.address_map == taito::taito_f2_address_map::thundfox);

    decl.taito_f2_map = "growl";
    const auto growl_params = taito::board_params_from_decl(decl);
    CHECK(growl_params.address_map == taito::taito_f2_address_map::growl);

    decl.taito_f2_map = "ninjak";
    const auto ninjak_params = taito::board_params_from_decl(decl);
    CHECK(ninjak_params.address_map == taito::taito_f2_address_map::ninjak);

    decl.taito_f2_map = "solfigtr";
    const auto solfigtr_params = taito::board_params_from_decl(decl);
    CHECK(solfigtr_params.address_map == taito::taito_f2_address_map::solfigtr);
}

TEST_CASE("taito_f2 real F2 map routes TC0100SCN, sprite RAM, sound, and inputs",
          "[taito_f2]") {
    auto system = taito::assemble_taito_f2(
        make_image(),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::gunfront,
         .sprite_policy = taito::taito_f2_sprite_policy::banked,
         .sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed,
         .palette_format = taito::taito_f2_palette_format::rgbx_444});

    CHECK(system->video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::banked);
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

    system->main_bus.write16_be(taito::real_sprite_bank_base + 4U, 0x0001U);
    CHECK(system->sprite_bank_regs[2] == 0x0001U);
    CHECK(system->video.sprite_bank(0U) == 0x0800U);

    system->main_bus.write16_be(taito::real_video_reg_base + 0U, 0x0010U);
    system->main_bus.write16_be(taito::real_video_reg_base + 6U, 0x0020U);
    CHECK(system->video_regs[0] == 0x0010U);
    CHECK(system->video_regs[3] == 0x0020U);

    system->main_bus.write16_be(taito::real_priority_base + 10U, 0x0042U);
    CHECK(system->priority_regs[5] == 0x0042U);
    CHECK(system->video.priority_register(5U) == 0x0042U);
    CHECK(system->main_bus.read16_be(taito::real_priority_base + 10U) == 0x0042U);

    system->main_bus.write16_be(taito::real_sound_comm_base + 2U, 0x005AU);
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
         .sprite_buffering = taito::taito_f2_sprite_buffering::partial_delayed,
         .palette_format = taito::taito_f2_palette_format::rgbx_444,
         .sprite_hide_pixels = 3,
         .sprite_flip_hide_pixels = 3});

    CHECK(system->video.current_tilemap_variant() ==
          mnemos::chips::video::taito_f2_video::tilemap_variant::tc0100scn);
    CHECK(system->video.current_sprite_mode() ==
          mnemos::chips::video::taito_f2_video::sprite_mode::banked);
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

    system->main_bus.write16_be(taito::real_sprite_bank_base + 4U, 0x0001U);
    CHECK(system->sprite_bank_regs[2] == 0x0001U);
    CHECK(system->video.sprite_bank(0U) == 0x0800U);
    CHECK(system->video.sprite_bank(1U) == 0x0C00U);

    system->main_bus.write16_be(taito::real_video_reg_base + 0U, 0x0010U);
    system->main_bus.write16_be(taito::real_video_reg_base + 6U, 0x0020U);
    CHECK(system->video_regs[0] == 0x0010U);
    CHECK(system->video_regs[3] == 0x0020U);

    system->main_bus.write16_be(taito::real_priority_base + 10U, 0x0042U);
    CHECK(system->priority_regs[5] == 0x0042U);
    CHECK(system->video.priority_register(5U) == 0x0042U);

    system->main_bus.write16_be(taito::growl_sound_comm_base + 2U, 0x005AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    system->input_p3 = 0x77U;
    system->input_p4 = 0x88U;
    CHECK(system->main_bus.read8(taito::growl_dip_input_base + 1U) == 0x12U);
    CHECK(system->main_bus.read8(taito::growl_dip_input_base + 3U) == 0x34U);
    CHECK(system->main_bus.read8(taito::growl_player_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::growl_player_input_base + 3U) == 0x55U);
    CHECK(system->main_bus.read8(taito::growl_player_input_base + 5U) == 0x3CU);
    CHECK(system->main_bus.read8(taito::growl_p3_input_base + 1U) == 0x77U);
    CHECK(system->main_bus.read8(taito::growl_p4_input_base + 1U) == 0x88U);
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
    CHECK(system->video.sprite_bank(0U) == 0x0800U);
    CHECK(system->video.sprite_bank(1U) == 0x0C00U);

    system->main_bus.write16_be(taito::real_priority_base + 10U, 0x0042U);
    CHECK(system->priority_regs[5] == 0x0042U);
    CHECK(system->video.priority_register(5U) == 0x0042U);

    system->main_bus.write16_be(taito::ninjak_sound_comm_base + 2U, 0x005AU);
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
    CHECK(system->video.sprite_bank(0U) == 0x0800U);

    system->main_bus.write16_be(taito::growl_sound_comm_base + 2U, 0x005AU);
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

    system->main_bus.write16_be(taito::real_sound_comm_base + 2U, 0x005AU);
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

    system->main_bus.write16_be(taito::quizhq_sound_comm_base + 2U, 0x005AU);
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

    system->main_bus.write16_be(taito::qtorimon_sound_comm_base + 2U, 0x005AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::qtorimon_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::qtorimon_input_base + 3U) == 0x55U);
    CHECK(system->main_bus.read8(taito::real_priority_base + 10U) == 0xFFU);
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

    system->main_bus.write16_be(taito::qzchikyu_sound_comm_base + 2U, 0x005AU);
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

    system->main_bus.write16_be(taito::qzquest_sound_comm_base + 2U, 0x005AU);
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

    system->main_bus.write16_be(taito::metalb_sound_comm_base + 2U, 0x005AU);
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
    CHECK(system->video.sprite_bank(0U) == 0x0800U);
    CHECK(system->video.sprite_bank(1U) == 0x0C00U);

    system->main_bus.write16_be(taito::footchmp_tc0480scp_control_base + 0x1EU, 0x0010U);
    CHECK(system->tc0480scp_control_regs[0x0F] == 0x0010U);
    CHECK(system->video.tc0480scp_control_register(0x0FU) == 0x0010U);

    system->main_bus.write16_be(taito::footchmp_priority_base + 8U, 0x0051U);
    CHECK(system->priority_regs[4] == 0x0051U);
    CHECK(system->video.priority_register(4U) == 0x0051U);

    system->main_bus.write16_be(taito::footchmp_sound_comm_base + 2U, 0x005AU);
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
    CHECK(system->video.sprite_bank(0U) == 0x0800U);
    CHECK(system->video.sprite_bank(1U) == 0x0C00U);

    system->main_bus.write16_be(taito::deadconx_tc0480scp_control_base + 0x1EU,
                                0x0010U);
    CHECK(system->tc0480scp_control_regs[0x0F] == 0x0010U);
    CHECK(system->video.tc0480scp_control_register(0x0FU) == 0x0010U);

    system->main_bus.write16_be(taito::deadconx_priority_base + 8U, 0x0051U);
    CHECK(system->priority_regs[4] == 0x0051U);
    CHECK(system->video.priority_register(4U) == 0x0051U);

    system->main_bus.write16_be(taito::deadconx_sound_comm_base + 2U, 0x005AU);
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

TEST_CASE("taito_f2 Dino Rex map routes type-3 sprite extension and shifted windows",
          "[taito_f2][video]") {
    auto system = taito::assemble_taito_f2(
        make_image(),
        {.vertical = false,
         .address_map = taito::taito_f2_address_map::dinorex,
         .sprite_policy = taito::taito_f2_sprite_policy::extension_3,
         .sprite_buffering = taito::taito_f2_sprite_buffering::immediate,
         .palette_format = taito::taito_f2_palette_format::xrgb_555,
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
          mnemos::chips::video::taito_f2_video::palette_format::xrgb_555);
    CHECK(system->video.sprite_hide_pixels() == 3);
    CHECK(system->video.sprite_flip_hide_pixels() == 3);

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

    system->main_bus.write16_be(taito::dinorex_sound_comm_base + 2U, 0x005AU);
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

    system->main_bus.write16_be(taito::thundfox_sound_comm_base + 2U, 0x005AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);

    system->set_inputs(0xAAU, 0x55U, 0x3CU, 0x12U, 0x34U);
    CHECK(system->main_bus.read8(taito::thundfox_input_base + 1U) == 0xAAU);
    CHECK(system->main_bus.read8(taito::thundfox_input_base + 3U) == 0x55U);

    poke16(system->tile_ram, 0U, 0U);
    poke16(system->tile_ram, 2U, 1U);
    poke16(system->tile_ram_secondary, 0U, 0U);
    poke16(system->tile_ram_secondary, 2U, 1U);
    system->main_bus.write16_be(taito::thundfox_video_reg_base + 0U, 0x0000U);
    system->main_bus.write16_be(taito::thundfox_video_reg_base + 6U, 0x0000U);
    system->main_bus.write16_be(taito::thundfox_secondary_video_reg_base + 0U, 0x0000U);
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

    system->main_bus.write16_be(taito::pulirula_sound_comm_base + 2U, 0x005AU);
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
    poke16(system->palette_ram, (128U * 16U + 3U) * 2U, 0x7C00U);
    set_sprite(system->sprite_ram, 0U, 16U, 20U, 1U, 0U);

    system->run_frame();

    CHECK(system->video.framebuffer().pixels[20U * 320U + 16U] == 0x0000FFU);
    CHECK(system->video.framebuffer().pixels[35U * 320U + 31U] == 0x0000FFU);
    CHECK(system->video.framebuffer().pixels[36U * 320U + 16U] == 0x000000U);
}

TEST_CASE("taito_f2 maps 68K/Z80 sound latches", "[taito_f2][sound]") {
    auto system = taito::assemble_taito_f2(make_image());

    system->main_bus.write8(taito::comm_base + 1U, 0x5AU);
    CHECK(system->latch_68k_to_z80 == 0x5AU);
    CHECK(system->sound_latch_pending);
    CHECK(system->sound_bus.read8(taito::z80_latch_addr) == 0x5AU);
    CHECK_FALSE(system->sound_latch_pending);

    system->sound_bus.write8(taito::z80_reply_addr, 0xC3U);
    CHECK(system->main_bus.read8(taito::comm_base + 3U) == 0xC3U);
}

TEST_CASE("taito_f2 sound Z80 reaches the YM2610 port pair", "[taito_f2][sound]") {
    auto image = make_image();
    auto& sound = image.regions["audiocpu"];
    sound.assign(0x4000U, 0x00U);
    const std::vector<std::uint8_t> program{
        0x3E, 0x08,       // LD A,$08  -- SSG ch-A fine tune
        0x32, 0x00, 0x90, // LD ($9000),A
        0x3E, 0x0F,       // LD A,$0F
        0x32, 0x01, 0x90, // LD ($9001),A
        0x18, 0xFE,       // JR $
    };
    std::copy(program.begin(), program.end(), sound.begin());

    auto system = taito::assemble_taito_f2(std::move(image));
    system->run_frame();

    CHECK(system->opnb.ssg_block().read_reg(0x08U) == 0x0FU);
    CHECK(system->sound_cpu.cpu_registers().pc != 0U);
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
