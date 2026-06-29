#include "travrusa_system.hpp"

#include "rom_set.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {
    [[nodiscard]] std::vector<std::uint8_t> synthetic_z80_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_travrusa::main_rom_size, 0x00U);
        const std::uint8_t program[] = {
            0x3EU, 0x42U, 0x32U, 0x00U, 0x80U, // LD A,$42 ; LD ($8000),A
            0x3EU, 0x84U, 0x32U, 0x01U, 0x80U, // LD A,$84 ; LD ($8001),A
            0x3EU, 0x2AU, 0x32U, 0x00U, 0x90U, // LD A,$2A ; LD ($9000),A
            0x3EU, 0x01U, 0x32U, 0x00U, 0xA0U, // LD A,$01 ; LD ($A000),A
            0x3EU, 0x05U, 0x32U, 0x00U, 0xD0U, // LD A,$05 ; LD ($D000),A
            0x3EU, 0x01U, 0x32U, 0x01U, 0xD0U, // LD A,$01 ; LD ($D001),A
            0xC3U, 0x00U, 0x00U};              // JP $0000
        std::copy(std::begin(program), std::end(program), rom.begin());
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_sound_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_travrusa::sound_rom_size, 0x00U);
        const std::uint8_t program[] = {
            0xDBU, 0x02U,                                           // IN A,($02)
            0xD3U, 0x06U,                                           // OUT ($06),A
            0x3EU, 0x07U, 0xD3U, 0x00U, 0x3EU, 0xFEU, 0xD3U, 0x01U, // AY0 mixer
            0x3EU, 0x08U, 0xD3U, 0x00U, 0x3EU, 0x0FU, 0xD3U, 0x01U, // AY0 level A
            0x3EU, 0x00U, 0xD3U, 0x00U, 0x3EU, 0x1FU, 0xD3U, 0x01U, // AY0 tone A
            0x3EU, 0x07U, 0xD3U, 0x04U, 0x3EU, 0xFDU, 0xD3U, 0x05U, // AY1 mixer
            0x3EU, 0x09U, 0xD3U, 0x04U, 0x3EU, 0x0CU, 0xD3U, 0x05U, // AY1 level B
            0x3EU, 0x02U, 0xD3U, 0x04U, 0x3EU, 0x23U, 0xD3U, 0x05U, // AY1 tone B
            0x3EU, 0x07U, 0xD3U, 0x08U,                             // MSM nibble
            0xC3U, 0x00U, 0x00U};
        std::copy(std::begin(program), std::end(program), rom.begin());
        return rom;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_image() {
        namespace travrusa = mnemos::manifests::irem_travrusa;
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_z80_program());
        image.regions.emplace("soundcpu", synthetic_sound_program());
        image.regions.emplace("tiles", std::vector<std::uint8_t>(travrusa::tiles_size, 0x00U));
        image.regions.emplace("sprites", std::vector<std::uint8_t>(travrusa::sprites_size, 0x00U));
        image.regions.emplace("proms", std::vector<std::uint8_t>(travrusa::proms_size, 0x3FU));
        return image;
    }

    [[nodiscard]] std::size_t nonblack_pixels(mnemos::chips::frame_buffer_view view) {
        std::size_t count = 0U;
        for (std::uint32_t y = 0; y < view.height; ++y) {
            for (std::uint32_t x = 0; x < view.width; ++x) {
                if (view.pixels[static_cast<std::size_t>(y) * view.stride + x] != 0U) {
                    ++count;
                }
            }
        }
        return count;
    }

    [[nodiscard]] std::vector<std::uint32_t> frame_pixels(mnemos::chips::frame_buffer_view view) {
        std::vector<std::uint32_t> pixels;
        pixels.reserve(static_cast<std::size_t>(view.width) * view.height);
        for (std::uint32_t y = 0; y < view.height; ++y) {
            const std::uint32_t* row =
                view.pixels + static_cast<std::size_t>(y) * view.effective_stride();
            pixels.insert(pixels.end(), row, row + view.width);
        }
        return pixels;
    }

    [[nodiscard]] std::vector<std::uint8_t> prom_ramp() {
        std::vector<std::uint8_t> proms(mnemos::manifests::irem_travrusa::proms_size);
        for (std::size_t i = 0; i < proms.size(); ++i) {
            proms[i] = static_cast<std::uint8_t>(i);
        }
        return proms;
    }
} // namespace

TEST_CASE("Irem Traverse USA system runs Z80 memory and IO windows", "[irem_travrusa]") {
    namespace travrusa = mnemos::manifests::irem_travrusa;

    auto sys = travrusa::assemble_travrusa(synthetic_image(), travrusa::board_params_for("travrusa"));
    CHECK(sys->dsw1 == travrusa::travrusa_dsw1_default);
    CHECK(sys->dsw2 == travrusa::travrusa_dsw2_default);
    sys->run_frame();

    CHECK(sys->video_ram[0] == 0x42U);
    CHECK(sys->video_ram[1] == 0x84U);
    CHECK(sys->scroll_x_low == 0x2AU);
    CHECK(sys->scroll_x_high == 0x01U);
    CHECK(sys->scroll_x() == 0x012AU);
    CHECK(sys->scroll_write_count > 0U);
    CHECK(sys->sound_command == 0x05U);
    CHECK(sys->sound_command_write_count > 0U);
    CHECK(sys->flip_screen);
    CHECK(sys->flip_write_count > 0U);
    CHECK(sys->sound_latch_ack_count > 0U);
    CHECK_FALSE(sys->sound_latch_irq);
    CHECK(sys->sound_cpu.elapsed_cycles() > 0U);
    CHECK_FALSE(sys->sound_cpu.reset_line_held());
    CHECK(sys->ay0.volume(0) == 0x0FU);
    CHECK(sys->ay1.volume(1) == 0x0CU);
    CHECK(sys->ay0.pending_samples() > 0U);
    CHECK(sys->ay1.pending_samples() > 0U);
    CHECK(sys->sound_cpu_msm_write_count > 0U);
    CHECK(sys->msm.vclk_count() > 0U);
    CHECK(sys->msm.pending_samples() > 0U);
    CHECK(sys->video.framebuffer().width == travrusa::visible_width);
    CHECK(sys->video.framebuffer().height == travrusa::visible_height);
    CHECK(nonblack_pixels(sys->video.framebuffer()) > 0U);
}

TEST_CASE("Irem Traverse USA sound command is owned by the sound Z80 ports", "[irem_travrusa]") {
    namespace travrusa = mnemos::manifests::irem_travrusa;

    auto sys = travrusa::assemble_travrusa(synthetic_image(), travrusa::board_params_for("travrusa"));
    CHECK(sys->ay0.volume(0) == 0U);
    CHECK(sys->ay1.volume(1) == 0U);
    CHECK(sys->msm.vclk_count() == 0U);
    CHECK(sys->sound_cpu_msm_write_count == 0U);

    sys->latch_sound_command(0x5AU);

    CHECK(sys->sound_command == 0x5AU);
    CHECK(sys->sound_latch_irq);
    CHECK(sys->ay0.volume(0) == 0U);
    CHECK(sys->ay1.volume(1) == 0U);
    CHECK(sys->msm.vclk_count() == 0U);
    CHECK(sys->sound_cpu_msm_write_count == 0U);

    sys->sound_cpu.tick(4096U);

    CHECK_FALSE(sys->sound_latch_irq);
    CHECK(sys->sound_latch_ack_count > 0U);
    CHECK(sys->ay0.volume(0) == 0x0FU);
    CHECK(sys->ay0.tone_period(0) == 0x001FU);
    CHECK(sys->ay1.volume(1) == 0x0CU);
    CHECK(sys->ay1.tone_period(1) == 0x0023U);
    CHECK(sys->sound_cpu_msm_write_count > 0U);
    CHECK(sys->msm.vclk_count() > 0U);
}

TEST_CASE("Irem Traverse USA video output ignores executable ROM entropy", "[irem_travrusa]") {
    namespace travrusa = mnemos::manifests::irem_travrusa;

    std::array<std::uint8_t, travrusa::video_ram_size> video_ram{};
    std::array<std::uint8_t, travrusa::sprite_ram_size> sprite_ram{};
    std::vector<std::uint8_t> tiles(travrusa::tiles_size, 0U);
    std::vector<std::uint8_t> sprites(travrusa::sprites_size, 0U);
    const std::vector<std::uint8_t> proms = prom_ramp();

    video_ram[0] = 0x02U;
    video_ram[1] = 0x80U;
    tiles[0x10] = 0x80U;

    travrusa::travrusa_video first;
    first.compose(tiles, sprites, proms, video_ram, sprite_ram, 0x012AU, false, "travrusa");
    travrusa::travrusa_video second;
    second.compose(tiles, sprites, proms, video_ram, sprite_ram, 0x012AU, false, "travrusa");

    CHECK(frame_pixels(first.framebuffer()) == frame_pixels(second.framebuffer()));
}

TEST_CASE("Irem Traverse USA video renders sprite RAM records through sprite graphics",
          "[irem_travrusa]") {
    namespace travrusa = mnemos::manifests::irem_travrusa;

    std::array<std::uint8_t, travrusa::video_ram_size> video_ram{};
    std::array<std::uint8_t, travrusa::sprite_ram_size> blank_sprite_ram{};
    std::array<std::uint8_t, travrusa::sprite_ram_size> object_sprite_ram{};
    std::vector<std::uint8_t> tiles(travrusa::tiles_size, 0U);
    std::vector<std::uint8_t> sprites(travrusa::sprites_size, 0U);
    const std::vector<std::uint8_t> proms = prom_ramp();

    for (std::uint32_t row = 0; row < 16U; ++row) {
        sprites[static_cast<std::size_t>(row) * 2U] = 0x80U;
    }
    object_sprite_ram[0] = static_cast<std::uint8_t>(241U - 40U);
    object_sprite_ram[1] = 0x00U;
    object_sprite_ram[2] = 0x05U;
    object_sprite_ram[3] = 24U;

    travrusa::travrusa_video blank;
    blank.compose(tiles, sprites, proms, video_ram, blank_sprite_ram, 0U, false, "travrusa");
    travrusa::travrusa_video with_object;
    with_object.compose(tiles, sprites, proms, video_ram, object_sprite_ram, 0U, false, "travrusa");

    const auto blank_pixels = frame_pixels(blank.framebuffer());
    const auto object_pixels = frame_pixels(with_object.framebuffer());
    const auto lit_index = static_cast<std::size_t>(40U) * travrusa::visible_width + 24U;
    const auto transparent_index = static_cast<std::size_t>(40U) * travrusa::visible_width + 25U;
    CHECK(object_pixels[lit_index] != blank_pixels[lit_index]);
    CHECK(object_pixels[transparent_index] == blank_pixels[transparent_index]);
}

TEST_CASE("Irem Traverse USA video renders the interleaved 64x32 tilemap with scroll",
          "[irem_travrusa]") {
    namespace travrusa = mnemos::manifests::irem_travrusa;

    std::array<std::uint8_t, travrusa::video_ram_size> blank_video_ram{};
    std::array<std::uint8_t, travrusa::video_ram_size> text_video_ram{};
    std::array<std::uint8_t, travrusa::sprite_ram_size> sprite_ram{};
    std::vector<std::uint8_t> tiles(travrusa::tiles_size, 0U);
    std::vector<std::uint8_t> sprites(travrusa::sprites_size, 0U);
    const std::vector<std::uint8_t> proms = prom_ramp();

    const std::uint32_t tile_x = 3U;
    const std::uint32_t tile_y = 2U;
    const std::size_t index = (static_cast<std::size_t>(tile_y) * 64U + tile_x) * 2U;
    text_video_ram[index] = 1U;
    text_video_ram[index + 1U] = 0x02U;
    tiles[1U * 8U] = 0x80U;

    travrusa::travrusa_video blank;
    blank.compose(tiles, sprites, proms, blank_video_ram, sprite_ram, 0U, false, "travrusa");
    travrusa::travrusa_video with_text;
    with_text.compose(tiles, sprites, proms, text_video_ram, sprite_ram, 0U, false, "travrusa");

    const auto blank_pixels = frame_pixels(blank.framebuffer());
    const auto text_pixels = frame_pixels(with_text.framebuffer());
    const auto lit_index =
        static_cast<std::size_t>(tile_y * 8U) * travrusa::visible_width + (tile_x * 8U - 8U);
    CHECK(text_pixels[lit_index] != blank_pixels[lit_index]);

    travrusa::travrusa_video scrolled;
    scrolled.compose(tiles, sprites, proms, text_video_ram, sprite_ram, 8U, false, "travrusa");
    const auto scrolled_pixels = frame_pixels(scrolled.framebuffer());
    const auto scrolled_index =
        static_cast<std::size_t>(tile_y * 8U) * travrusa::visible_width + (tile_x * 8U - 16U);
    CHECK(scrolled_pixels[scrolled_index] != blank_pixels[scrolled_index]);
}

TEST_CASE("Irem Traverse USA video mirrors tile position on flip-screen", "[irem_travrusa]") {
    namespace travrusa = mnemos::manifests::irem_travrusa;

    std::array<std::uint8_t, travrusa::video_ram_size> blank_video_ram{};
    std::array<std::uint8_t, travrusa::video_ram_size> text_video_ram{};
    std::array<std::uint8_t, travrusa::sprite_ram_size> sprite_ram{};
    std::vector<std::uint8_t> tiles(travrusa::tiles_size, 0U);
    std::vector<std::uint8_t> sprites(travrusa::sprites_size, 0U);
    const std::vector<std::uint8_t> proms = prom_ramp();

    const std::uint32_t tile_x = 3U;
    const std::uint32_t tile_y = 2U;
    const std::size_t index = (static_cast<std::size_t>(tile_y) * 64U + tile_x) * 2U;
    text_video_ram[index] = 1U;
    tiles[1U * 8U] = 0x80U;

    travrusa::travrusa_video blank;
    blank.compose(tiles, sprites, proms, blank_video_ram, sprite_ram, 0U, true, "travrusa");
    travrusa::travrusa_video with_text;
    with_text.compose(tiles, sprites, proms, text_video_ram, sprite_ram, 0U, true, "travrusa");

    const auto blank_pixels = frame_pixels(blank.framebuffer());
    const auto text_pixels = frame_pixels(with_text.framebuffer());
    const std::uint32_t unflipped_x = tile_x * 8U - 8U;
    const std::uint32_t unflipped_y = tile_y * 8U;
    const std::uint32_t flipped_x = travrusa::visible_width - 1U - unflipped_x;
    const std::uint32_t flipped_y = travrusa::visible_height - 1U - unflipped_y;
    const auto unflipped_index =
        static_cast<std::size_t>(unflipped_y) * travrusa::visible_width + unflipped_x;
    const auto flipped_index =
        static_cast<std::size_t>(flipped_y) * travrusa::visible_width + flipped_x;

    CHECK(text_pixels[flipped_index] != blank_pixels[flipped_index]);
    CHECK(text_pixels[unflipped_index] == blank_pixels[unflipped_index]);
}

TEST_CASE("Irem Traverse USA save-state preserves board identity and RAM", "[irem_travrusa]") {
    namespace travrusa = mnemos::manifests::irem_travrusa;

    auto source = travrusa::assemble_travrusa(synthetic_image(), travrusa::board_params_for("travrusa"));
    source->run_frame();
    source->work_ram[0x10] = 0xA5U;
    source->sound_ram[0x10] = 0x5AU;

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored =
        travrusa::assemble_travrusa(synthetic_image(), travrusa::board_params_for("travrusa"));
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->work_ram[0x10] == 0xA5U);
    CHECK(restored->sound_ram[0x10] == 0x5AU);
    CHECK(restored->video.framebuffer().width == source->video.framebuffer().width);
    CHECK(restored->sound_command == source->sound_command);
    CHECK(restored->sound_latch_ack_count == source->sound_latch_ack_count);
    CHECK(restored->sound_cpu_msm_write_count == source->sound_cpu_msm_write_count);
    CHECK(restored->sound_cpu.cpu_registers().pc == source->sound_cpu.cpu_registers().pc);
    CHECK(restored->ay0.read_reg(mnemos::chips::audio::ssg::reg_port_a) ==
          source->ay0.read_reg(mnemos::chips::audio::ssg::reg_port_a));
    CHECK(restored->ay1.tone_period(1) == source->ay1.tone_period(1));
    CHECK(restored->msm.data_latch() == source->msm.data_latch());
    CHECK(restored->msm.last_sample() == source->msm.last_sample());
    CHECK(restored->msm.step_index() == source->msm.step_index());
    CHECK(restored->scroll_x() == source->scroll_x());

    auto wrong =
        travrusa::assemble_travrusa(synthetic_image(), travrusa::board_params_for("motorace"));
    mnemos::chips::state_reader wrong_reader(snapshot);
    wrong->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
