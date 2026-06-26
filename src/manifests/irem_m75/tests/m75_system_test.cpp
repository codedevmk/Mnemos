#include "m75_system.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

    namespace m75 = mnemos::manifests::irem_m75;

    [[nodiscard]] std::vector<std::uint8_t> synthetic_main_program() {
        std::vector<std::uint8_t> rom(m75::main_rom_size, 0xFFU);
        const std::uint8_t program[] = {
            0x3EU, 0x42U, 0x32U, 0x00U, 0xE0U, // LD A,$42 ; LD ($E000),A
            0x3EU, 0x37U, 0x32U, 0x00U, 0xD0U, // LD A,$37 ; LD ($D000),A
            0x3EU, 0x02U, 0xD3U, 0x04U,       // LD A,$02 ; OUT ($04),A
            0x3EU, 0x5AU, 0xD3U, 0x00U,       // LD A,$5A ; OUT ($00),A
            0x76U                              // HALT
        };
        std::copy(std::begin(program), std::end(program), rom.begin());
        rom[0x10000U] = 0x11U;
        rom[0x18000U] = 0x22U;
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_sound_program() {
        std::vector<std::uint8_t> rom(m75::sound_rom_size, 0x00U);
        const std::uint8_t program[] = {
            0xDBU, 0x80U,       // IN A,($80)
            0xD3U, 0x83U,       // OUT ($83),A
            0x3EU, 0xF0U,       // LD A,$F0
            0xD3U, 0x82U,       // OUT ($82),A
            0x76U               // HALT
        };
        std::copy(std::begin(program), std::end(program), rom.begin());
        return rom;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_m75_image() {
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_main_program());
        image.regions.emplace("soundcpu", synthetic_sound_program());
        image.regions.emplace("chars", std::vector<std::uint8_t>(m75::char_gfx_size, 0x81U));
        image.regions.emplace("sprites", std::vector<std::uint8_t>(m75::sprite_gfx_size, 0x42U));
        image.regions.emplace("bgtiles", std::vector<std::uint8_t>(m75::bg_tile_gfx_size, 0x24U));
        image.regions.emplace("samples", std::vector<std::uint8_t>(m75::sample_rom_size, 0x18U));
        image.regions.emplace("proms", std::vector<std::uint8_t>(m75::proms_size, 0x3FU));
        image.regions.emplace("plds", std::vector<std::uint8_t>(m75::plds_size, 0xFFU));
        return image;
    }

    [[nodiscard]] bool frame_has_nonblack(const mnemos::chips::frame_buffer_view& frame) {
        for (std::uint32_t y = 0; y < frame.height; ++y) {
            for (std::uint32_t x = 0; x < frame.width; ++x) {
                if (frame.pixels[static_cast<std::size_t>(y) * frame.effective_stride() + x] !=
                    0U) {
                    return true;
                }
            }
        }
        return false;
    }

} // namespace

TEST_CASE("m75 executable board maps Z80 RAM, banked ROM, and diagnostic frame",
          "[m75][board]") {
    auto system = m75::assemble_m75(synthetic_m75_image(), m75::board_params_for("vigilant"));
    REQUIRE(system != nullptr);

    CHECK(system->main_bus.read8(0x0000U) == 0x3EU);
    CHECK(system->main_bus.read8(0x8000U) == 0x11U);

    system->main_bus.write8(m75::palette_ram_base, 0x1FU);
    system->main_bus.write8(m75::sprite_ram_base, 0x41U);
    CHECK(system->palette_ram[0] == 0x1FU);
    CHECK(system->sprite_ram[0] == 0x41U);

    system->run_frame();
    CHECK(system->work_ram[0] == 0x42U);
    CHECK(system->video_ram[0] == 0x37U);
    CHECK(system->bank_register == 0x02U);
    CHECK(system->main_bus.read8(0x8000U) == 0x22U);
    CHECK(frame_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m75 board declares Z80/Z80/YM2151/DAC clocks and 256x256 raster",
          "[m75][board]") {
    auto system = m75::assemble_m75(synthetic_m75_image(), m75::board_params_for("vigilant"));
    REQUIRE(system != nullptr);

    CHECK(system->main_cpu.metadata().part_number == "Z80");
    CHECK(system->sound_cpu.metadata().part_number == "Z80");
    CHECK(system->video.metadata().part_number == "m75_video_first_pass");
    CHECK(system->dac.metadata().part_number == "dac8");
    CHECK(m75::main_clock_hz == 3'579'545U);
    CHECK(m75::sound_clock_hz == 3'579'545U);
    CHECK(m75::main_cycles_per_frame == 65'061U);
    CHECK(m75::sound_cycles_per_frame == 65'061U);
    CHECK(m75::visible_width == 256U);
    CHECK(m75::visible_height == 256U);
    CHECK(m75::frame_lines == 284U);
}

TEST_CASE("m75 Z80 sound CPU acknowledges latch and drives DAC", "[m75][board][audio]") {
    auto system = m75::assemble_m75(synthetic_m75_image(), m75::board_params_for("vigilant"));
    REQUIRE(system != nullptr);

    system->run_frame();

    CHECK(system->sound_latch == 0x5AU);
    CHECK_FALSE(system->sound_latch_irq);
    CHECK_FALSE(system->dac_write_events.empty());
    CHECK(system->dac.level() == 0xF0U);
    CHECK(system->dac.output() > 0);
}

TEST_CASE("m75 save state preserves board identity and runtime state", "[m75][board]") {
    auto source = m75::assemble_m75(synthetic_m75_image(), m75::board_params_for("vigilant"));
    source->set_inputs(0xFEU, 0xFDU, 0xFBU);
    source->run_frame();
    source->work_ram[7] = 0x6CU;
    source->palette_ram[3] = 0x25U;

    std::vector<std::uint8_t> state;
    mnemos::chips::state_writer writer(state);
    source->save_state(writer);
    REQUIRE_FALSE(state.empty());

    auto restored = m75::assemble_m75(synthetic_m75_image(), m75::board_params_for("vigilant"));
    mnemos::chips::state_reader reader(state);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored->work_ram[0] == 0x42U);
    CHECK(restored->work_ram[7] == 0x6CU);
    CHECK(restored->palette_ram[3] == 0x25U);
    CHECK(restored->input_p1 == 0xFEU);
    CHECK(restored->bank_register == 0x02U);
    CHECK(restored->main_bus.read8(0x8000U) == 0x22U);
    CHECK(restored->dac.level() == 0xF0U);

    auto wrong = m75::assemble_m75(synthetic_m75_image(), {});
    mnemos::chips::state_reader wrong_reader(state);
    wrong->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
