#include "m90_system.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

    namespace m90 = mnemos::manifests::irem_m90;

    [[nodiscard]] std::vector<std::uint8_t>
    make_m90_program(const std::vector<std::uint8_t>& program) {
        std::vector<std::uint8_t> rom(m90::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m90_program() {
        return make_m90_program(
            {0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U, 0x42U, 0xA2U, 0x00U, 0x00U, 0xF4U});
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m90_sound_program() {
        std::vector<std::uint8_t> rom(m90::sound_rom_size, 0x00U);
        rom[0x0000U] = 0x3EU; // LD A,$F0
        rom[0x0001U] = 0xF0U;
        rom[0x0002U] = 0xD3U; // OUT ($82),A
        rom[0x0003U] = static_cast<std::uint8_t>(m90::z80_port_dac);
        rom[0x0004U] = 0x76U; // HALT
        return rom;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_m90_image() {
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_m90_program());
        image.regions.emplace("soundcpu", synthetic_m90_sound_program());
        std::vector<std::uint8_t> samples(m90::sample_rom_size, 0x00U);
        std::fill(samples.begin() + 0x100, samples.begin() + 0x400, std::uint8_t{0x70U});
        image.regions.emplace("samples", std::move(samples));
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

TEST_CASE("m90 executable board maps V35 reset, M90 RAM windows, and diagnostic frame",
          "[m90][board]") {
    auto system = m90::assemble_m90(synthetic_m90_image(), m90::board_params_for("bbmanwj"));
    REQUIRE(system != nullptr);

    CHECK(system->main_bus.read8(0xFFFF0U) == 0xEAU);
    CHECK(system->sound_bus.read8(0x0000U) == 0x3EU);

    system->main_bus.write8(m90::vram_base, 0x37U);
    system->main_bus.write8(m90::sprite_ram_base, 0x41U);
    system->main_bus.write8(m90::palette_ram_base, 0x83U);
    system->main_bus.write8(m90::rowscroll_ram_base, 0x02U);
    CHECK(system->vram[0] == 0x37U);
    CHECK(system->sprite_ram[0] == 0x41U);
    CHECK(system->palette_ram[0] == 0x83U);
    CHECK(system->rowscroll_ram[0] == 0x02U);

    system->run_frame();
    CHECK(system->work_ram[0] == 0x42U);
    CHECK(frame_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m90 board declares V35/Z80/YM2151/DAC clocks and 384x256 raster",
          "[m90][board]") {
    auto system = m90::assemble_m90(synthetic_m90_image(), m90::board_params_for("bbmanwj"));
    REQUIRE(system != nullptr);

    CHECK(system->main_cpu.metadata().part_number == "v35");
    CHECK(system->sound_cpu.metadata().part_number == "Z80");
    CHECK(system->video.metadata().part_number == "GA25_first_pass");
    CHECK(system->dac.metadata().part_number == "dac8");
    CHECK(m90::main_clock_hz == 14'318'181U);
    CHECK(m90::sound_clock_hz == 3'579'545U);
    CHECK(m90::main_cycles_per_frame == 260'245U);
    CHECK(m90::sound_cycles_per_frame == 65'061U);
    CHECK(m90::visible_width == 384U);
    CHECK(m90::visible_height == 256U);
    CHECK(m90::frame_lines == 284U);
}

TEST_CASE("m90 Z80 sound CPU drives the board DAC", "[m90][board][audio]") {
    auto system = m90::assemble_m90(synthetic_m90_image(), m90::board_params_for("bbmanwj"));
    REQUIRE(system != nullptr);

    system->run_frame();

    CHECK_FALSE(system->dac_write_events.empty());
    CHECK(system->dac.level() == 0xF0U);
    CHECK(system->dac.output() > 0);
}

TEST_CASE("m90 save state preserves board identity and runtime state", "[m90][board]") {
    auto source = m90::assemble_m90(synthetic_m90_image(), m90::board_params_for("bbmanwj"));
    source->set_inputs(0xFEU, 0xFDU, 0xFBU);
    source->run_frame();
    source->work_ram[7] = 0x6CU;
    source->rowscroll_ram[3] = 0x25U;

    std::vector<std::uint8_t> state;
    mnemos::chips::state_writer writer(state);
    source->save_state(writer);
    REQUIRE_FALSE(state.empty());

    mnemos::chips::state_reader header_reader(state);
    CHECK(header_reader.u32() == m90::m90_system_state_version);

    std::vector<std::uint8_t> old_version_state = state;
    REQUIRE(old_version_state.size() >= sizeof(std::uint32_t));
    old_version_state[0] = static_cast<std::uint8_t>(m90::m90_system_state_version - 1U);
    auto old_version = m90::assemble_m90(synthetic_m90_image(), m90::board_params_for("bbmanwj"));
    mnemos::chips::state_reader old_reader(old_version_state);
    old_version->load_state(old_reader);
    CHECK_FALSE(old_reader.ok());

    auto restored = m90::assemble_m90(synthetic_m90_image(), m90::board_params_for("bbmanwj"));
    mnemos::chips::state_reader reader(state);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored->work_ram[0] == 0x42U);
    CHECK(restored->work_ram[7] == 0x6CU);
    CHECK(restored->rowscroll_ram[3] == 0x25U);
    CHECK(restored->input_p1 == 0xFEU);
    CHECK(restored->dac.level() == 0xF0U);

    auto wrong_layout =
        m90::assemble_m90(synthetic_m90_image(), m90::board_params_for("newapunk"));
    mnemos::chips::state_reader wrong_reader(state);
    wrong_layout->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
