#include "m58_system.hpp"

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
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m58::main_rom_size, 0x00U);
        // LD A,$42 ; LD ($8000),A ; OUT ($10),A ; LD A,$05 ; LD ($D000),A ; JP $0000
        const std::uint8_t program[] = {0x3EU, 0x42U, 0x32U, 0x00U, 0x80U, 0xD3U, 0x10U, 0x3EU,
                                        0x05U, 0x32U, 0x00U, 0xD0U, 0xC3U, 0x00U, 0x00U};
        std::copy(std::begin(program), std::end(program), rom.begin());
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_sound_program() {
        namespace m58 = mnemos::manifests::irem_m58;

        std::vector<std::uint8_t> rom(m58::sound_rom_size, 0x01U);
        // MC6803 code at $8000: read and acknowledge the sound latch, then
        // program both first-pass PSG MMIO surfaces through direct-page stores.
        const std::uint8_t program[] = {
            0x96U, 0x02U,                                           // LDAA <$02
            0x97U, 0x06U,                                           // STAA <$06
            0x86U, 0x07U, 0x97U, 0x00U, 0x86U, 0xFEU, 0x97U, 0x01U, // AY0 mixer
            0x86U, 0x08U, 0x97U, 0x00U, 0x86U, 0x0FU, 0x97U, 0x01U, // AY0 level A
            0x86U, 0x00U, 0x97U, 0x00U, 0x86U, 0x1FU, 0x97U, 0x01U, // AY0 tone A
            0x86U, 0x07U, 0x97U, 0x04U, 0x86U, 0xFDU, 0x97U, 0x05U, // AY1 mixer
            0x86U, 0x09U, 0x97U, 0x04U, 0x86U, 0x0CU, 0x97U, 0x05U, // AY1 level B
            0x86U, 0x02U, 0x97U, 0x04U, 0x86U, 0x23U, 0x97U, 0x05U, // AY1 tone B
            0x7EU, 0x80U, 0x00U};
        std::copy(std::begin(program), std::end(program), rom.begin() + m58::sound_rom_base);
        rom[0xFFFEU] = static_cast<std::uint8_t>(m58::sound_rom_base >> 8U);
        rom[0xFFFFU] = static_cast<std::uint8_t>(m58::sound_rom_base);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> tile_gfx_with_marker() {
        std::vector<std::uint8_t> tiles(mnemos::manifests::irem_m58::tile_gfx_size, 0x00U);
        tiles[1U * 8U + 7U] = 0x01U;
        return tiles;
    }

    [[nodiscard]] std::vector<std::uint8_t> sprite_gfx_with_marker() {
        std::vector<std::uint8_t> sprites(mnemos::manifests::irem_m58::sprite_gfx_size, 0x00U);
        for (std::uint32_t row = 0; row < 16U; ++row) {
            sprites[static_cast<std::size_t>(row) * 2U] = 0x80U;
        }
        return sprites;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_image() {
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_z80_program());
        image.regions.emplace("soundcpu", synthetic_sound_program());
        image.regions.emplace("tiles", tile_gfx_with_marker());
        image.regions.emplace("sprites", sprite_gfx_with_marker());
        image.regions.emplace("proms",
                              std::vector<std::uint8_t>(mnemos::manifests::irem_m58::proms_size,
                                                        0x3FU));
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

    [[nodiscard]] std::vector<std::uint32_t>
    frame_pixels(mnemos::chips::frame_buffer_view view) {
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
        std::vector<std::uint8_t> proms(mnemos::manifests::irem_m58::proms_size);
        for (std::size_t i = 0; i < proms.size(); ++i) {
            proms[i] = static_cast<std::uint8_t>(i);
        }
        return proms;
    }
} // namespace

TEST_CASE("Irem M58 system runs main Z80 and sound MC6803 windows", "[irem_m58]") {
    namespace m58 = mnemos::manifests::irem_m58;

    auto sys = m58::assemble_m58(synthetic_image(), m58::board_params_for("10yard"));
    CHECK(sys->dsw1 == m58::tenyard_dsw1_default);
    CHECK(sys->dsw2 == m58::tenyard_dsw2_default);
    sys->run_frame();

    CHECK(sys->video_ram[0] == 0x42U);
    CHECK(sys->scroll_regs[0] == 0x42U);
    CHECK(sys->sound_command == 0x05U);
    CHECK(sys->sound_command_write_count > 0U);
    CHECK(sys->sound_latch_ack_count > 0U);
    CHECK_FALSE(sys->sound_latch_irq);
    CHECK(sys->sound_cpu.elapsed_cycles() > 0U);
    CHECK_FALSE(sys->sound_cpu.reset_line_held());
    CHECK(sys->sound_cpu.metadata().part_number == "MC6803");
    CHECK(sys->ay0.volume(0) == 0x0FU);
    CHECK(sys->ay1.volume(1) == 0x0CU);
    CHECK(sys->ay0.pending_samples() > 0U);
    CHECK(sys->ay1.pending_samples() > 0U);
    CHECK(sys->sound_cpu_psg_write_count > 0U);
    CHECK(sys->video.framebuffer().width == m58::visible_width);
    CHECK(sys->video.framebuffer().height == m58::visible_height);
    CHECK(nonblack_pixels(sys->video.framebuffer()) > 0U);
}

TEST_CASE("Irem M58 sound command is owned by the sound MC6803 direct-page MMIO",
          "[irem_m58]") {
    namespace m58 = mnemos::manifests::irem_m58;

    auto sys = m58::assemble_m58(synthetic_image(), m58::board_params_for("10yard"));
    CHECK(sys->ay0.volume(0) == 0U);
    CHECK(sys->ay1.volume(1) == 0U);
    CHECK(sys->sound_cpu_psg_write_count == 0U);

    sys->latch_sound_command(0x5AU);

    CHECK(sys->sound_command == 0x5AU);
    CHECK(sys->sound_latch_irq);
    CHECK(sys->ay0.volume(0) == 0U);
    CHECK(sys->ay1.volume(1) == 0U);
    CHECK(sys->sound_cpu_psg_write_count == 0U);

    sys->sound_cpu.tick(4096U);

    CHECK_FALSE(sys->sound_latch_irq);
    CHECK(sys->sound_latch_ack_count > 0U);
    CHECK(sys->ay0.volume(0) == 0x0FU);
    CHECK(sys->ay0.tone_period(0) == 0x001FU);
    CHECK(sys->ay1.volume(1) == 0x0CU);
    CHECK(sys->ay1.tone_period(1) == 0x0023U);
    CHECK(sys->sound_cpu_psg_write_count > 0U);
}

TEST_CASE("Irem M58 video output ignores executable ROM entropy", "[irem_m58]") {
    namespace m58 = mnemos::manifests::irem_m58;

    std::array<std::uint8_t, m58::video_ram_size> video_ram{};
    std::array<std::uint8_t, m58::color_ram_size> color_ram{};
    std::array<std::uint8_t, m58::sprite_ram_size> sprite_ram{};
    std::array<std::uint8_t, 32> scroll_regs{};
    std::vector<std::uint8_t> tiles = tile_gfx_with_marker();
    std::vector<std::uint8_t> sprites = sprite_gfx_with_marker();
    const std::vector<std::uint8_t> proms = prom_ramp();

    video_ram[0] = 1U;
    color_ram[0] = 0x80U;
    scroll_regs[0] = 0x2AU;

    m58::m58_video first;
    first.compose(tiles, sprites, proms, video_ram, color_ram, sprite_ram, scroll_regs, false,
                  "ten_yard_fight");
    m58::m58_video second;
    second.compose(tiles, sprites, proms, video_ram, color_ram, sprite_ram, scroll_regs, false,
                   "ten_yard_fight");

    CHECK(frame_pixels(first.framebuffer()) == frame_pixels(second.framebuffer()));
}

TEST_CASE("Irem M58 video renders sprite RAM records through sprite graphics", "[irem_m58]") {
    namespace m58 = mnemos::manifests::irem_m58;

    std::array<std::uint8_t, m58::video_ram_size> video_ram{};
    std::array<std::uint8_t, m58::color_ram_size> color_ram{};
    std::array<std::uint8_t, m58::sprite_ram_size> blank_sprite_ram{};
    std::array<std::uint8_t, m58::sprite_ram_size> object_sprite_ram{};
    std::array<std::uint8_t, 32> scroll_regs{};
    std::vector<std::uint8_t> tiles(m58::tile_gfx_size, 0U);
    std::vector<std::uint8_t> sprites = sprite_gfx_with_marker();
    const std::vector<std::uint8_t> proms = prom_ramp();

    object_sprite_ram[0] = static_cast<std::uint8_t>(241U - 40U);
    object_sprite_ram[1] = 0x00U;
    object_sprite_ram[2] = 0x05U;
    object_sprite_ram[3] = 24U;

    m58::m58_video blank;
    blank.compose(tiles, sprites, proms, video_ram, color_ram, blank_sprite_ram, scroll_regs,
                  false, "ten_yard_fight");
    m58::m58_video with_object;
    with_object.compose(tiles, sprites, proms, video_ram, color_ram, object_sprite_ram,
                        scroll_regs, false, "ten_yard_fight");

    const auto blank_pixels = frame_pixels(blank.framebuffer());
    const auto object_pixels = frame_pixels(with_object.framebuffer());
    const auto lit_index = static_cast<std::size_t>(40U) * m58::visible_width + 24U;
    const auto transparent_index = static_cast<std::size_t>(40U) * m58::visible_width + 25U;
    CHECK(object_pixels[lit_index] != blank_pixels[lit_index]);
    CHECK(object_pixels[transparent_index] == blank_pixels[transparent_index]);
}

TEST_CASE("Irem M58 video mirrors tile layer position on flip-screen", "[irem_m58]") {
    namespace m58 = mnemos::manifests::irem_m58;

    std::array<std::uint8_t, m58::video_ram_size> blank_video_ram{};
    std::array<std::uint8_t, m58::video_ram_size> text_video_ram{};
    std::array<std::uint8_t, m58::color_ram_size> color_ram{};
    std::array<std::uint8_t, m58::sprite_ram_size> sprite_ram{};
    std::array<std::uint8_t, 32> scroll_regs{};
    std::vector<std::uint8_t> tiles = tile_gfx_with_marker();
    std::vector<std::uint8_t> sprites(m58::sprite_gfx_size, 0U);
    const std::vector<std::uint8_t> proms = prom_ramp();

    const std::uint32_t tile_x = 3U;
    const std::uint32_t tile_y = 2U;
    text_video_ram[static_cast<std::size_t>(tile_y) * 32U + tile_x] = 1U;

    m58::m58_video blank;
    blank.compose(tiles, sprites, proms, blank_video_ram, color_ram, sprite_ram, scroll_regs,
                  true, "ten_yard_fight");
    m58::m58_video with_text;
    with_text.compose(tiles, sprites, proms, text_video_ram, color_ram, sprite_ram, scroll_regs,
                      true, "ten_yard_fight");

    const auto blank_pixels = frame_pixels(blank.framebuffer());
    const auto text_pixels = frame_pixels(with_text.framebuffer());
    const std::uint32_t unflipped_x = tile_x * 8U;
    const std::uint32_t unflipped_y = tile_y * 8U;
    const std::uint32_t flipped_x = m58::visible_width - 1U - unflipped_x;
    const std::uint32_t flipped_y = m58::visible_height - 1U - unflipped_y;
    const auto unflipped_index =
        static_cast<std::size_t>(unflipped_y) * m58::visible_width + unflipped_x;
    const auto flipped_index =
        static_cast<std::size_t>(flipped_y) * m58::visible_width + flipped_x;

    CHECK(text_pixels[flipped_index] != blank_pixels[flipped_index]);
    CHECK(text_pixels[unflipped_index] == blank_pixels[unflipped_index]);
}

TEST_CASE("Irem M58 save-state preserves board identity and RAM", "[irem_m58]") {
    namespace m58 = mnemos::manifests::irem_m58;

    auto source = m58::assemble_m58(synthetic_image(), m58::board_params_for("10yard"));
    source->run_frame();
    source->work_ram[0x10] = 0xA5U;
    source->sound_ram[0x10] = 0x5AU;

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = m58::assemble_m58(synthetic_image(), m58::board_params_for("10yard"));
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->work_ram[0x10] == 0xA5U);
    CHECK(restored->sound_ram[0x10] == 0x5AU);
    CHECK(restored->video.framebuffer().width == source->video.framebuffer().width);
    CHECK(restored->sound_command == source->sound_command);
    CHECK(restored->sound_latch_ack_count == source->sound_latch_ack_count);
    CHECK(restored->sound_cpu_psg_write_count == source->sound_cpu_psg_write_count);
    CHECK(restored->sound_cpu.cpu_registers().pc == source->sound_cpu.cpu_registers().pc);
    CHECK(restored->ay0.read_reg(mnemos::chips::audio::ssg::reg_port_a) ==
          source->ay0.read_reg(mnemos::chips::audio::ssg::reg_port_a));
    CHECK(restored->ay1.tone_period(1) == source->ay1.tone_period(1));

    auto wrong = m58::assemble_m58(
        synthetic_image(),
        {.rom_layout = "unknown", .dsw1_default = 0x00U, .dsw2_default = 0x00U});
    mnemos::chips::state_reader wrong_reader(snapshot);
    wrong->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
