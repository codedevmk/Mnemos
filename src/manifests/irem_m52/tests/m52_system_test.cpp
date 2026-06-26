#include "m52_system.hpp"

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
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m52::main_rom_size, 0x00U);
        // LD A,$42 ; LD ($8000),A ; OUT ($10),A ; LD A,$05 ; LD ($D000),A ; JP $0000
        const std::uint8_t program[] = {0x3EU, 0x42U, 0x32U, 0x00U, 0x80U, 0xD3U, 0x10U, 0x3EU,
                                        0x05U, 0x32U, 0x00U, 0xD0U, 0xC3U, 0x00U, 0x00U};
        std::copy(std::begin(program), std::end(program), rom.begin());
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_sound_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m52::sound_rom_size, 0x00U);
        // Read and acknowledge the sound latch, then program AY0, AY1, and MSM5205
        // through the modeled sound-CPU ports.
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
        namespace m52 = mnemos::manifests::irem_m52;
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_z80_program());
        image.regions.emplace("soundcpu", synthetic_sound_program());
        image.regions.emplace("tx_gfx", std::vector<std::uint8_t>(m52::tx_gfx_size, 0x81U));
        image.regions.emplace("sprite_gfx", std::vector<std::uint8_t>(m52::sprite_gfx_size, 0x42U));
        image.regions.emplace("proms", std::vector<std::uint8_t>(m52::proms_size, 0x3FU));
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
        std::vector<std::uint8_t> proms(mnemos::manifests::irem_m52::proms_size);
        for (std::size_t i = 0; i < proms.size(); ++i) {
            proms[i] = static_cast<std::uint8_t>(i);
        }
        return proms;
    }
} // namespace

TEST_CASE("Irem M52 system runs Z80 memory and IO windows", "[irem_m52]") {
    namespace m52 = mnemos::manifests::irem_m52;

    auto sys = m52::assemble_m52(synthetic_image(), m52::board_params_for("mpatrol"));
    CHECK(sys->dsw1 == m52::mpatrol_dsw1_default);
    CHECK(sys->dsw2 == m52::mpatrol_dsw2_default);
    sys->run_frame();

    CHECK(sys->video_ram[0] == 0x42U);
    CHECK(sys->scroll_regs[0] == 0x42U);
    CHECK(sys->sound_command == 0x05U);
    CHECK(sys->sound_command_write_count > 0U);
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
    CHECK(sys->msm.last_sample() != 0);
    CHECK(sys->video.framebuffer().width == m52::visible_width);
    CHECK(sys->video.framebuffer().height == m52::visible_height);
    CHECK(nonblack_pixels(sys->video.framebuffer()) > 0U);
}

TEST_CASE("Irem M52 sound command is owned by the sound Z80 ports", "[irem_m52]") {
    namespace m52 = mnemos::manifests::irem_m52;

    auto sys = m52::assemble_m52(synthetic_image(), m52::board_params_for("mpatrol"));
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

TEST_CASE("Irem M52 video output ignores executable ROM entropy", "[irem_m52]") {
    namespace m52 = mnemos::manifests::irem_m52;

    std::array<std::uint8_t, m52::video_ram_size> video_ram{};
    std::array<std::uint8_t, m52::color_ram_size> color_ram{};
    std::array<std::uint8_t, m52::sprite_ram_size> sprite_ram{};
    std::array<std::uint8_t, m52::work_ram_size> work_ram_a{};
    std::array<std::uint8_t, m52::work_ram_size> work_ram_b{};
    std::array<std::uint8_t, 32> scroll_regs{};
    std::array<std::uint8_t, 2> bg_x{0x11U, 0x23U};
    std::array<std::uint8_t, 2> bg_y{0x07U, 0x19U};
    std::vector<std::uint8_t> tx_gfx(m52::tx_gfx_size, 0U);
    std::vector<std::uint8_t> sprite_gfx(m52::sprite_gfx_size, 0U);
    std::vector<std::uint8_t> main_program_a(m52::main_rom_size, 0x00U);
    std::vector<std::uint8_t> main_program_b(m52::main_rom_size, 0xFFU);
    std::vector<std::uint8_t> sound_program_a(m52::sound_rom_size, 0x55U);
    std::vector<std::uint8_t> sound_program_b(m52::sound_rom_size, 0xAAU);
    const std::vector<std::uint8_t> proms = prom_ramp();

    video_ram[0] = 0x02U;
    color_ram[0] = 0x80U;
    tx_gfx[0x10] = 0x80U;
    std::fill(work_ram_b.begin(), work_ram_b.end(), static_cast<std::uint8_t>(0xEEU));
    scroll_regs[0] = 0x2AU;

    m52::m52_video first;
    first.compose(main_program_a, sound_program_a, tx_gfx, sprite_gfx, proms, video_ram,
                  color_ram, sprite_ram, work_ram_a, scroll_regs, bg_x, bg_y, 0x31U, false,
                  "moon_patrol");
    m52::m52_video second;
    second.compose(main_program_b, sound_program_b, tx_gfx, sprite_gfx, proms, video_ram,
                   color_ram, sprite_ram, work_ram_b, scroll_regs, bg_x, bg_y, 0x31U, false,
                   "moon_patrol");

    CHECK(frame_pixels(first.framebuffer()) == frame_pixels(second.framebuffer()));
}

TEST_CASE("Irem M52 video renders sprite RAM records through sprite graphics", "[irem_m52]") {
    namespace m52 = mnemos::manifests::irem_m52;

    std::array<std::uint8_t, m52::video_ram_size> video_ram{};
    std::array<std::uint8_t, m52::color_ram_size> color_ram{};
    std::array<std::uint8_t, m52::sprite_ram_size> blank_sprite_ram{};
    std::array<std::uint8_t, m52::sprite_ram_size> object_sprite_ram{};
    std::array<std::uint8_t, m52::work_ram_size> work_ram{};
    std::array<std::uint8_t, 32> scroll_regs{};
    std::array<std::uint8_t, 2> bg_x{};
    std::array<std::uint8_t, 2> bg_y{};
    std::vector<std::uint8_t> tx_gfx(m52::tx_gfx_size, 0U);
    std::vector<std::uint8_t> sprite_gfx(m52::sprite_gfx_size, 0U);
    std::vector<std::uint8_t> program(m52::main_rom_size, 0U);
    const std::vector<std::uint8_t> proms = prom_ramp();

    for (std::uint32_t row = 0; row < 16U; ++row) {
        sprite_gfx[static_cast<std::size_t>(row) * 2U] = 0x80U;
    }
    object_sprite_ram[0] = static_cast<std::uint8_t>(241U - 40U);
    object_sprite_ram[1] = 0x00U;
    object_sprite_ram[2] = 0x05U;
    object_sprite_ram[3] = 24U;

    m52::m52_video blank;
    blank.compose(program, program, tx_gfx, sprite_gfx, proms, video_ram, color_ram,
                  blank_sprite_ram, work_ram, scroll_regs, bg_x, bg_y, 0x00U, false,
                  "moon_patrol");
    m52::m52_video with_object;
    with_object.compose(program, program, tx_gfx, sprite_gfx, proms, video_ram, color_ram,
                        object_sprite_ram, work_ram, scroll_regs, bg_x, bg_y, 0x00U, false,
                        "moon_patrol");

    const auto blank_pixels = frame_pixels(blank.framebuffer());
    const auto object_pixels = frame_pixels(with_object.framebuffer());
    const auto lit_index = static_cast<std::size_t>(40U) * m52::visible_width + 24U;
    const auto transparent_index = static_cast<std::size_t>(40U) * m52::visible_width + 25U;
    CHECK(object_pixels[lit_index] != blank_pixels[lit_index]);
    CHECK(object_pixels[transparent_index] == blank_pixels[transparent_index]);
}

TEST_CASE("Irem M52 save-state preserves board identity and RAM", "[irem_m52]") {
    namespace m52 = mnemos::manifests::irem_m52;

    auto source = m52::assemble_m52(synthetic_image(), m52::board_params_for("mpatrol"));
    source->run_frame();
    source->work_ram[0x10] = 0xA5U;
    source->sound_ram[0x10] = 0x5AU;

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = m52::assemble_m52(synthetic_image(), m52::board_params_for("mpatrol"));
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

    auto wrong = m52::assemble_m52(synthetic_image(), {});
    mnemos::chips::state_reader wrong_reader(snapshot);
    wrong->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
