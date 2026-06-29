#include "m102_system.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

    namespace m102 = mnemos::manifests::irem_m102;

    [[nodiscard]] std::vector<std::uint8_t> synthetic_program() {
        std::vector<std::uint8_t> rom(m102::main_rom_size, 0x00U);
        const std::vector<std::uint8_t> program{
            0x3EU, 0x42U, 0x32U, 0x00U, 0xE0U, // LD A,$42 ; LD ($E000),A
            0x3EU, 0x81U, 0x32U, 0x00U, 0xC0U, // LD A,$81 ; LD ($C000),A
            0x3EU, 0x24U, 0x32U, 0x00U, 0xD0U, // LD A,$24 ; LD ($D000),A
            0x3EU, 0x02U, 0xD3U, 0x40U,       // LD A,$02 ; OUT ($40),A
            0x3EU, 0x01U, 0xD3U, 0x00U,       // GA20 ch0 start=$0010
            0x3EU, 0x00U, 0xD3U, 0x01U,
            0x3EU, 0x08U, 0xD3U, 0x02U,       // GA20 ch0 end=$0080
            0x3EU, 0x00U, 0xD3U, 0x03U,
            0x3EU, 0x10U, 0xD3U, 0x04U,       // rate
            0x3EU, 0xFFU, 0xD3U, 0x05U,       // volume
            0x3EU, 0x02U, 0xD3U, 0x06U,       // key on
            0x3EU, 0x5AU, 0xD3U, 0x50U,       // output latch
            0xC3U, 0x31U, 0x00U};             // loop on output latch write
        std::copy(program.begin(), program.end(), rom.begin());
        for (std::size_t i = 0x8000U; i < 0xC000U; ++i) {
            rom[i] = static_cast<std::uint8_t>((i * 13U) & 0xFFU);
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_ga20() {
        std::vector<std::uint8_t> rom(m102::ga20_rom_size, 0x80U);
        for (std::size_t i = 0x10U; i < 0x80U; ++i) {
            rom[i] = static_cast<std::uint8_t>(0x90U + (i & 0x1FU));
        }
        rom[0x80U] = 0x00U;
        return rom;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_image() {
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_program());
        image.regions.emplace("ga20", synthetic_ga20());
        image.regions.emplace("plds", std::vector<std::uint8_t>(m102::pld_region_size, 0x00U));
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

TEST_CASE("m102 executable board maps Hill Climber Z80 RAM, ports, GA20, and video",
          "[m102][board]") {
    auto system = m102::assemble_m102(synthetic_image(), m102::board_params_for("hclimber"));
    REQUIRE(system != nullptr);

    CHECK(system->main_bus.read8(0x0000U) == 0x3EU);
    CHECK(system->input0 == m102::input0_default);
    CHECK(system->input1 == m102::input1_default);
    CHECK(system->dsw1 == m102::dsw1_default);
    CHECK(system->dsw2 == m102::dsw2_default);

    system->run_frame();

    CHECK(system->work_ram[0] == 0x42U);
    CHECK(system->video_ram[0] == 0x81U);
    CHECK(system->medal_ram[0] == 0x24U);
    CHECK(system->bank_select == 0x02U);
    CHECK(system->bank_switch_count == 1U);
    CHECK(system->output_latch == 0x5AU);
    CHECK(system->output_write_count > 0U);
    CHECK(system->ga20_register_write_count >= 7U);
    CHECK(system->ga20_key_on_count > 0U);
    CHECK(system->ga20.last_sample() != 0);
    CHECK(frame_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m102 board exposes D70008-compatible Z80, GA20, and diagnostic raster",
          "[m102][board]") {
    auto system = m102::assemble_m102(synthetic_image(), m102::board_params_for("hclimber"));
    REQUIRE(system != nullptr);

    CHECK(system->main_cpu.metadata().part_number == "Z80");
    CHECK(system->ga20.metadata().part_number == "GA20");
    CHECK(system->video.metadata().part_number == "m102_medal_video_first_pass");
    CHECK(m102::cpu_clock_hz == 3'579'545U);
    CHECK(m102::ga20_clock_hz == 3'579'545U);
    CHECK(m102::visible_width == 320U);
    CHECK(m102::visible_height == 240U);
}

TEST_CASE("m102 save state preserves board identity and runtime state", "[m102][board]") {
    auto source = m102::assemble_m102(synthetic_image(), m102::board_params_for("hclimber"));
    REQUIRE(source != nullptr);
    source->set_inputs(0xF0U, 0x0FU);
    source->run_frame();
    source->video_ram[3] = 0x66U;
    source->medal_ram[5] = 0x77U;

    std::vector<std::uint8_t> state;
    mnemos::chips::state_writer writer(state);
    source->save_state(writer);
    REQUIRE_FALSE(state.empty());

    mnemos::chips::state_reader header_reader(state);
    CHECK(header_reader.u32() == m102::m102_system_state_version);

    auto restored = m102::assemble_m102(synthetic_image(), m102::board_params_for("hclimber"));
    mnemos::chips::state_reader reader(state);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored->work_ram[0] == 0x42U);
    CHECK(restored->video_ram[3] == 0x66U);
    CHECK(restored->medal_ram[5] == 0x77U);
    CHECK(restored->input0 == 0xF0U);
    CHECK(restored->input1 == 0x0FU);
    CHECK(restored->bank_select == 0x02U);
    CHECK(restored->output_latch == 0x5AU);

    auto wrong = m102::assemble_m102({});
    mnemos::chips::state_reader wrong_reader(state);
    wrong->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
