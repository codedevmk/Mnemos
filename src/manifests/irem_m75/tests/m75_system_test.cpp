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
            0x3EU, 0x02U, 0xD3U, 0x04U,        // LD A,$02 ; OUT ($04),A
            0x3EU, 0x5AU, 0xD3U, 0x00U,        // LD A,$5A ; OUT ($00),A
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
            0xDBU, 0x80U, // IN A,($80)
            0xD3U, 0x83U, // OUT ($83),A
            0x3EU, 0xF0U, // LD A,$F0
            0xD3U, 0x82U, // OUT ($82),A
            0x76U         // HALT
        };
        std::copy(std::begin(program), std::end(program), rom.begin());
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_sound_sample_program() {
        std::vector<std::uint8_t> rom(m75::sound_rom_size, 0x00U);
        const std::uint8_t program[] = {
            0x3EU, 0x34U,        // LD A,$34
            0xD3U, 0x80U,        // OUT ($80),A
            0x3EU, 0x12U,        // LD A,$12
            0xD3U, 0x81U,        // OUT ($81),A
            0xDBU, 0x84U,        // IN A,($84)
            0x32U, 0x01U, 0xF0U, // LD ($F001),A
            0xD3U, 0x82U,        // OUT ($82),A
            0xDBU, 0x84U,        // IN A,($84)
            0x32U, 0x02U, 0xF0U, // LD ($F002),A
            0xD3U, 0x82U,        // OUT ($82),A
            0xD3U, 0x83U,        // OUT ($83),A
            0x76U                // HALT
        };
        std::copy(std::begin(program), std::end(program), rom.begin());
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_kikcubic_main_program() {
        std::vector<std::uint8_t> rom(m75::main_rom_size, 0xFFU);
        const std::uint8_t program[] = {
            0xDBU, 0x00U, 0x32U, 0x01U, 0xE0U, // IN A,($00) ; LD ($E001),A
            0xDBU, 0x01U, 0x32U, 0x02U, 0xE0U, // IN A,($01) ; LD ($E002),A
            0xDBU, 0x02U, 0x32U, 0x03U, 0xE0U, // IN A,($02) ; LD ($E003),A
            0xDBU, 0x03U, 0x32U, 0x04U, 0xE0U, // IN A,($03) ; LD ($E004),A
            0xDBU, 0x04U, 0x32U, 0x05U, 0xE0U, // IN A,($04) ; LD ($E005),A
            0x3EU, 0x02U, 0xD3U, 0x04U,        // LD A,$02 ; OUT ($04),A
            0x3EU, 0x5AU, 0xD3U, 0x06U,        // LD A,$5A ; OUT ($06),A
            0x76U                              // HALT
        };
        std::copy(std::begin(program), std::end(program), rom.begin());
        rom[0x10000U] = 0x11U;
        rom[0x18000U] = 0x22U;
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

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_kikcubic_image() {
        auto image = synthetic_m75_image();
        image.regions["maincpu"] = synthetic_kikcubic_main_program();
        image.regions.erase("bgtiles");
        image.regions.erase("plds");
        image.regions["proms"] =
            std::vector<std::uint8_t>(m75::kikcubic_proms_size, 0x2BU);
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

TEST_CASE("m75 executable board maps Z80 RAM, banked ROM, and diagnostic frame", "[m75][board]") {
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

TEST_CASE("m75 board declares Z80/Z80/YM2151/DAC clocks and 256x256 raster", "[m75][board]") {
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

TEST_CASE("m75 kikcubic profile owns its main CPU port layout", "[m75][board]") {
    auto system = m75::assemble_m75(synthetic_kikcubic_image(), m75::board_params_for("kikcubic"));
    REQUIRE(system != nullptr);

    system->set_inputs(0xE7U, 0xB7U, 0xCFU);
    system->run_frame();

    CHECK(system->params.rom_layout == "kikcubic");
    CHECK(system->dsw1 == m75::kikcubic_dsw1_default);
    CHECK(system->dsw2 == m75::kikcubic_dsw2_default);
    CHECK(system->work_ram[1] == m75::kikcubic_dsw1_default);
    CHECK(system->work_ram[2] == m75::kikcubic_dsw2_default);
    CHECK(system->work_ram[3] == 0xE7U);
    CHECK(system->work_ram[4] == 0xCFU);
    CHECK(system->work_ram[5] == 0xB7U);
    CHECK(system->bank_register == 0x02U);
    CHECK(system->sound_latch == 0x5AU);
    CHECK(system->main_bus.read8(0x8000U) == 0x22U);
}

TEST_CASE("m75 palette bus exposes two 5-bit KNA91 banks", "[m75][board][palette]") {
    auto system = m75::assemble_m75(synthetic_m75_image(), m75::board_params_for("vigilant"));
    REQUIRE(system != nullptr);

    system->main_bus.write8(m75::palette_ram_base + 0x000U, 0xE7U);
    CHECK(system->palette_ram[0x000U] == 0x07U);
    CHECK(system->main_bus.read8(m75::palette_ram_base + 0x000U) == 0xE7U);

    system->main_bus.write8(m75::palette_ram_base + 0x100U, 0x32U);
    CHECK(system->palette_ram[0x100U] == 0x12U);
    CHECK(system->main_bus.read8(m75::palette_ram_base + 0x100U) == 0xF2U);

    system->main_bus.write8(m75::palette_ram_base + 0x200U, 0xBAU);
    CHECK(system->palette_ram[0x200U] == 0x1AU);
    CHECK(system->main_bus.read8(m75::palette_ram_base + 0x200U) == 0xFAU);

    system->main_bus.write8(m75::palette_ram_base + 0x404U, 0x1FU);
    system->main_bus.write8(m75::palette_ram_base + 0x504U, 0x00U);
    system->main_bus.write8(m75::palette_ram_base + 0x604U, 0x00U);
    CHECK(system->palette_ram[0x404U] == 0x1FU);
    CHECK(system->main_bus.read8(m75::palette_ram_base + 0x404U) == 0xFFU);
}

TEST_CASE("m75 rear color register selects the rear palette bank and disable bit",
          "[m75][video][palette]") {
    std::vector<std::uint8_t> zeros(1U, 0U);
    std::vector<std::uint8_t> vram(m75::video_ram_size, 0U);
    std::vector<std::uint8_t> palette(m75::palette_ram_size, 0U);
    std::vector<std::uint8_t> sprite_ram(m75::sprite_ram_size, 0U);

    // Pixel 0 resolves to palette index 0x104 when the rear layer is enabled
    // with color code 0, and to 0x64 when bit 6 disables the rear layer.
    palette[0x064U] = 0x1FU; // red, front bank
    palette[0x504U] = 0x1FU; // green, rear bank

    m75::m75_video rear_enabled;
    rear_enabled.compose(zeros, zeros, zeros, zeros, zeros, zeros, zeros, vram, palette, sprite_ram,
                         0U, 0U, 0x00U, "vigilant");
    CHECK(rear_enabled.framebuffer().pixels[0] == 0x0000FF00U);

    m75::m75_video rear_disabled;
    rear_disabled.compose(zeros, zeros, zeros, zeros, zeros, zeros, zeros, vram, palette,
                          sprite_ram, 0U, 0U, 0x40U, "vigilant");
    CHECK(rear_disabled.framebuffer().pixels[0] == 0x00FF0000U);
}

TEST_CASE("m75 Z80 sound CPU acknowledges latch and drives DAC", "[m75][board][audio]") {
    auto system = m75::assemble_m75(synthetic_m75_image(), m75::board_params_for("vigilant"));
    REQUIRE(system != nullptr);

    system->run_frame();

    CHECK(system->sound_latch == 0x5AU);
    CHECK_FALSE(system->sound_latch_irq);
    REQUIRE(system->dac_write_events.size() == 1U);
    CHECK(system->dac_write_events[0].sound_clock > 0U);
    CHECK(system->dac_write_events[0].sound_clock < m75::sound_cycles_per_frame);
    CHECK(system->dac.level() == 0xF0U);
    CHECK(system->dac.output() > 0);
}

TEST_CASE("m75 sound CPU streams sample ROM bytes through the DAC", "[m75][board][audio]") {
    auto image = synthetic_m75_image();
    image.regions["soundcpu"] = synthetic_sound_sample_program();
    image.regions["samples"].assign(m75::sample_rom_size, 0x00U);
    image.regions["samples"][0x1234U] = 0x7DU;
    image.regions["samples"][0x1235U] = 0x41U;

    auto system = m75::assemble_m75(std::move(image), m75::board_params_for("vigilant"));
    REQUIRE(system != nullptr);
    system->sound_latch_irq = true;
    system->update_sound_irq();

    system->sound_cpu.tick(256U);

    CHECK(system->sound_cpu.cpu_registers().halted);
    CHECK(system->sound_ram[0x001U] == 0x7DU);
    CHECK(system->sound_ram[0x002U] == 0x41U);
    CHECK(system->sample_address == 0x1236U);
    CHECK_FALSE(system->sound_latch_irq);
    REQUIRE(system->dac_write_events.size() == 2U);
    CHECK(system->dac_write_events[0].sound_clock > 0U);
    CHECK(system->dac_write_events[1].sound_clock > system->dac_write_events[0].sound_clock);
    CHECK(system->dac.level() == 0x41U);
    CHECK(system->dac.output() < 0);
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
