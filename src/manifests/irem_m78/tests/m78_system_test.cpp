#include "m78_system.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

    namespace m78 = mnemos::manifests::irem_m78;

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m78_program() {
        std::vector<std::uint8_t> rom(m78::main_rom_size, 0x00U);
        const std::vector<std::uint8_t> program{
            0x3EU, 0x42U, 0x32U, 0x00U, 0xE0U,
            0x3EU, 0x11U, 0x01U, 0x00U, 0x80U, 0xEDU, 0x79U,
            0x3EU, 0x01U, 0x01U, 0x00U, 0x90U, 0xEDU, 0x79U,
            0x3EU, 0x03U, 0x01U, 0x00U, 0xA0U, 0xEDU, 0x79U,
            0x3EU, 0x22U, 0x01U, 0x00U, 0xC0U, 0xEDU, 0x79U,
            0x3EU, 0x02U, 0x01U, 0x00U, 0xD0U, 0xEDU, 0x79U,
            0x3EU, 0x04U, 0x01U, 0x00U, 0xE0U, 0xEDU, 0x79U,
            0x3EU, 0x7CU, 0x01U, 0x00U, 0x20U, 0xEDU, 0x79U,
            0xC3U, 0x00U, 0x00U};
        std::copy(program.begin(), program.end(), rom.begin());
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m78_sound_program() {
        std::vector<std::uint8_t> rom(m78::audio_rom_size, 0x00U);
        const std::vector<std::uint8_t> program{
            0xDBU, 0x80U, 0xD3U, 0x83U,
            0x3EU, 0x20U, 0xD3U, 0x00U,
            0x3EU, 0x7FU, 0xD3U, 0x01U,
            0x3EU, 0xF0U, 0xD3U, 0x82U,
            0x76U};
        std::copy(program.begin(), program.end(), rom.begin());
        return rom;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_m78_image() {
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_m78_program());
        image.regions.emplace("audiocpu", synthetic_m78_sound_program());
        std::vector<std::uint8_t> tiles(m78::tiles_rom_size, 0x00U);
        std::vector<std::uint8_t> tiles2(m78::tiles2_rom_size, 0x00U);
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            tiles[i] = static_cast<std::uint8_t>((i * 17U + 0x33U) & 0xFFU);
            tiles2[i] = static_cast<std::uint8_t>((i * 29U + 0x55U) & 0xFFU);
        }
        std::vector<std::uint8_t> proms(m78::proms_size, 0x00U);
        for (std::size_t i = 0; i < proms.size(); ++i) {
            proms[i] = static_cast<std::uint8_t>((i * 11U + 0x21U) & 0xFFU);
        }
        image.regions.emplace("tiles", std::move(tiles));
        image.regions.emplace("tiles2", std::move(tiles2));
        image.regions.emplace("m72_audio", std::vector<std::uint8_t>(m78::m72_audio_rom_size, 0U));
        image.regions.emplace("proms", std::move(proms));
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

TEST_CASE("m78 executable board maps BJ92 dual-Z80 RAM, ports, and video",
          "[m78][board]") {
    auto system = m78::assemble_m78(synthetic_m78_image());
    REQUIRE(system != nullptr);

    CHECK(system->main_bus.read8(0x0000U) == 0x3EU);
    CHECK(system->sound_bus.read8(0x0000U) == 0xDBU);
    CHECK(system->input0 == m78::m78_in0_default);
    CHECK(system->input1 == m78::m78_in1_default);
    CHECK(system->dsw1 == m78::m78_dsw1_default);

    system->run_frame();

    CHECK(system->work_ram[0] == 0x42U);
    CHECK(system->tile_ram[1][0] == 0x11U);
    CHECK(system->attr_ram[1][0] == 0x01U);
    CHECK(system->color_ram[1][0] == 0x03U);
    CHECK(system->tile_ram[0][0] == 0x22U);
    CHECK(system->attr_ram[0][0] == 0x02U);
    CHECK(system->color_ram[0][0] == 0x04U);
    CHECK(system->sound_latch == 0x7CU);
    CHECK(system->sound_command_write_count > 0U);
    CHECK(frame_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m78 board declares dual Z80, YM2151/DAC, and 512x384 vertical raster",
          "[m78][board]") {
    auto system = m78::assemble_m78(synthetic_m78_image());
    REQUIRE(system != nullptr);

    CHECK(system->main_cpu.metadata().part_number == "Z80");
    CHECK(system->sound_cpu.metadata().part_number == "Z80");
    CHECK(system->video.metadata().part_number == "m78_video_first_pass");
    CHECK(system->dac.metadata().part_number == "dac8");
    CHECK(m78::main_clock_hz == 3'579'545U);
    CHECK(m78::sound_clock_hz == 3'579'545U);
    CHECK(m78::visible_width == 512U);
    CHECK(m78::visible_height == 384U);
    CHECK(m78::frame_rate_x1000 == 55000U);
}

TEST_CASE("m78 Z80 sound CPU drives YM2151 register writes and DAC events",
          "[m78][board][audio]") {
    auto system = m78::assemble_m78(synthetic_m78_image());
    REQUIRE(system != nullptr);

    system->run_frame();

    CHECK(system->sound_latch_ack_count > 0U);
    CHECK(system->ym2151_write_count > 0U);
    REQUIRE(system->dac_write_events.size() == 1U);
    CHECK(system->dac_write_events[0].sound_clock > 0U);
    CHECK(system->dac.level() == 0xF0U);
    CHECK(system->dac.output() > 0);
}

TEST_CASE("m78 save state preserves board identity and runtime state", "[m78][board]") {
    auto source = m78::assemble_m78(synthetic_m78_image());
    REQUIRE(source != nullptr);
    source->set_inputs(0xD9U, 0x23U);
    source->run_frame();
    source->vregs[5] = 0x6CU;
    source->layer_control[7] = 0x25U;

    std::vector<std::uint8_t> state;
    mnemos::chips::state_writer writer(state);
    source->save_state(writer);
    REQUIRE_FALSE(state.empty());

    mnemos::chips::state_reader header_reader(state);
    CHECK(header_reader.u32() == m78::m78_system_state_version);

    auto restored = m78::assemble_m78(synthetic_m78_image());
    mnemos::chips::state_reader reader(state);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored->work_ram[0] == 0x42U);
    CHECK(restored->vregs[5] == 0x6CU);
    CHECK(restored->layer_control[7] == 0x25U);
    CHECK(restored->input0 == 0xD9U);
    CHECK(restored->input1 == 0x23U);
    CHECK(restored->dac.level() == 0xF0U);

    auto wrong = m78::assemble_m78({});
    mnemos::chips::state_reader wrong_reader(state);
    wrong->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
