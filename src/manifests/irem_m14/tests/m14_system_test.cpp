#include "m14_system.hpp"

#include "rom_set.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {
    [[nodiscard]] std::vector<std::uint8_t> synthetic_program() {
        namespace M14 = mnemos::manifests::irem_m14;

        std::vector<std::uint8_t> rom(M14::main_rom_size, 0xFFU);
        const std::vector<std::uint8_t> program{
            0x3EU, 0x42U, 0x32U, 0x00U, 0x20U, // LD A,$42 ; LD ($2000),A
            0x3EU, 0x81U, 0x32U, 0x00U, 0x24U, // LD A,$81 ; LD ($2400),A
            0x3EU, 0x07U, 0x32U, 0x00U, 0x28U, // LD A,$07 ; LD ($2800),A
            0x3EU, 0x01U, 0x32U, 0x04U, 0x40U, // LD A,$01 ; LD ($4004),A
            0x3EU, 0x01U, 0x32U, 0x05U, 0x40U, // LD A,$01 ; LD ($4005),A
            0xC3U, 0x1EU, 0x00U};             // JP $001E
        std::copy(program.begin(), program.end(), rom.begin() + M14::program_rom_base);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> glyph_ramp() {
        std::vector<std::uint8_t> gfx(mnemos::manifests::irem_m14::gfx_rom_size);
        for (std::size_t i = 0; i < gfx.size(); ++i) {
            gfx[i] = static_cast<std::uint8_t>((i * 17U) ^ (i >> 1U));
        }
        return gfx;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_image() {
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_program());
        image.regions.emplace("gfx1", glyph_ramp());
        return image;
    }

    [[nodiscard]] std::size_t nonblack_pixels(mnemos::chips::frame_buffer_view view) {
        std::size_t count = 0U;
        for (std::uint32_t y = 0; y < view.height; ++y) {
            for (std::uint32_t x = 0; x < view.width; ++x) {
                if (view.pixels[static_cast<std::size_t>(y) * view.effective_stride() + x] != 0U) {
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
} // namespace

TEST_CASE("Irem M14 system runs the first-pass 8085 memory map", "[irem_m14]") {
    namespace M14 = mnemos::manifests::irem_m14;

    auto sys = M14::assemble_m14(synthetic_image(), M14::board_params_for("ptrmj"));
    CHECK(sys->dip_switches == M14::ptrmj_dip_default);
    CHECK(sys->video.framebuffer().width == M14::visible_width);
    CHECK(sys->video.framebuffer().height == M14::visible_height);

    sys->run_frame();

    CHECK(sys->scratch_ram[0] == 0x42U);
    CHECK(sys->video_ram[0] == 0x81U);
    CHECK(sys->color_ram[0] == 0x07U);
    CHECK(sys->sound_latch == 0x01U);
    CHECK(sys->sound_latch_write_count == 1U);
    CHECK(sys->speaker_output_edge_count == 1U);
    CHECK(sys->speaker_output_high);
    CHECK(sys->control_register == 0x01U);
    CHECK(sys->control_write_count == 1U);
    CHECK(sys->flip_screen);
    CHECK(sys->speaker.pending_samples() > 0U);
    CHECK(nonblack_pixels(sys->video.framebuffer()) > 0U);
}

TEST_CASE("Irem M14 video output is GFX-ROM and RAM driven", "[irem_m14]") {
    namespace M14 = mnemos::manifests::irem_m14;

    std::array<std::uint8_t, M14::video_ram_size> video_ram{};
    std::array<std::uint8_t, M14::color_ram_size> color_ram{};
    const std::vector<std::uint8_t> gfx = glyph_ramp();
    const std::vector<std::uint8_t> program = synthetic_program();

    video_ram[0] = 0x81U;
    color_ram[0] = 0x07U;

    M14::m14_video first;
    first.compose(video_ram, color_ram, gfx, program, false, "m14_ptrmj_8085");
    M14::m14_video second;
    second.compose(video_ram, color_ram, gfx, program, false, "m14_ptrmj_8085");

    CHECK(frame_pixels(first.framebuffer()) == frame_pixels(second.framebuffer()));
    CHECK(nonblack_pixels(first.framebuffer()) > 0U);

    M14::m14_video flipped;
    flipped.compose(video_ram, color_ram, gfx, program, true, "m14_ptrmj_8085");
    CHECK(frame_pixels(flipped.framebuffer()) != frame_pixels(first.framebuffer()));
}

TEST_CASE("Irem M14 save-state preserves board identity and RAM", "[irem_m14]") {
    namespace M14 = mnemos::manifests::irem_m14;

    auto source = M14::assemble_m14(synthetic_image(), M14::board_params_for("ptrmj"));
    source->run_frame();
    source->work_ram[0x10] = 0xA5U;

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = M14::assemble_m14(synthetic_image(), M14::board_params_for("ptrmj"));
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->work_ram[0x10] == 0xA5U);
    CHECK(restored->scratch_ram[0] == 0x42U);
    CHECK(restored->video_ram[0] == 0x81U);
    CHECK(restored->sound_latch == source->sound_latch);
    CHECK(restored->control_register == source->control_register);
    CHECK(restored->speaker_output_high == source->speaker_output_high);
    CHECK(restored->main_cpu.cpu_registers().pc == source->main_cpu.cpu_registers().pc);

    auto wrong = M14::assemble_m14(
        synthetic_image(),
        {.cpu_clock_hz = M14::cpu_clock_hz, .rom_layout = "unknown", .dip_default = 0x00U});
    mnemos::chips::state_reader wrong_reader(snapshot);
    wrong->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
