#include "m62_system.hpp"

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
        namespace M62 = mnemos::manifests::irem_m62;

        std::vector<std::uint8_t> rom(M62::main_rom_size, 0xFFU);
        const auto lo = [](std::uint16_t value) {
            return static_cast<std::uint8_t>(value & 0x00FFU);
        };
        const auto hi = [](std::uint16_t value) {
            return static_cast<std::uint8_t>(value >> 8U);
        };
        const std::vector<std::uint8_t> program{
            0x3EU, 0x42U, 0x32U, lo(M62::scratch_ram_base), hi(M62::scratch_ram_base),
            0x3EU, 0x81U, 0x32U, lo(M62::video_ram_base), hi(M62::video_ram_base),
            0x3EU, 0x07U, 0x32U, lo(M62::color_ram_base), hi(M62::color_ram_base),
            0x3EU, 0x01U, 0x32U, lo(M62::sound_latch_address), hi(M62::sound_latch_address),
            0x3EU, 0x01U, 0x32U, lo(M62::control_register_address),
            hi(M62::control_register_address),
            0xC3U, 0x1EU, 0x00U}; // JP $001E
        std::copy(program.begin(), program.end(), rom.begin() + M62::program_rom_base);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_sound_program() {
        namespace M62 = mnemos::manifests::irem_m62;

        std::vector<std::uint8_t> rom(M62::sound_rom_size, 0x01U);
        const std::uint8_t program[] = {
            0x96U, 0x02U,                                           // LDAA <$02
            0x97U, 0x06U,                                           // STAA <$06
            0x86U, 0x07U, 0x97U, 0x00U, 0x86U, 0xFEU, 0x97U, 0x01U, // AY0 mixer
            0x86U, 0x08U, 0x97U, 0x00U, 0x86U, 0x0FU, 0x97U, 0x01U, // AY0 level A
            0x86U, 0x00U, 0x97U, 0x00U, 0x86U, 0x1FU, 0x97U, 0x01U, // AY0 tone A
            0x86U, 0x07U, 0x97U, 0x04U, 0x86U, 0xFDU, 0x97U, 0x05U, // AY1 mixer
            0x86U, 0x09U, 0x97U, 0x04U, 0x86U, 0x0CU, 0x97U, 0x05U, // AY1 level B
            0x86U, 0x02U, 0x97U, 0x04U, 0x86U, 0x23U, 0x97U, 0x05U, // AY1 tone B
            0x7EU, static_cast<std::uint8_t>(M62::sound_rom_base >> 8U),
            static_cast<std::uint8_t>(M62::sound_rom_base)};
        std::copy(std::begin(program), std::end(program), rom.begin() + M62::sound_rom_base);
        rom[0xFFFEU] = static_cast<std::uint8_t>(M62::sound_rom_base >> 8U);
        rom[0xFFFFU] = static_cast<std::uint8_t>(M62::sound_rom_base);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> glyph_ramp() {
        std::vector<std::uint8_t> gfx(mnemos::manifests::irem_m62::gfx_rom_size);
        for (std::size_t i = 0; i < gfx.size(); ++i) {
            gfx[i] = static_cast<std::uint8_t>((i * 17U) ^ (i >> 1U));
        }
        return gfx;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_image() {
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_program());
        image.regions.emplace("soundcpu", synthetic_sound_program());
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

TEST_CASE("Irem M62 system runs Z80 and MC6803 first-pass board windows", "[irem_m62]") {
    namespace M62 = mnemos::manifests::irem_m62;

    auto sys = M62::assemble_m62(synthetic_image(), M62::board_params_for("ldrun"));
    CHECK(sys->dip_switches == M62::ldrun_dip_default);
    CHECK(sys->video.framebuffer().width == M62::visible_width);
    CHECK(sys->video.framebuffer().height == M62::visible_height);

    sys->run_frame();

    CHECK(sys->scratch_ram[0] == 0x42U);
    CHECK(sys->video_ram[0] == 0x81U);
    CHECK(sys->color_ram[0] == 0x07U);
    CHECK(sys->sound_latch == 0x01U);
    CHECK(sys->sound_latch_write_count == 1U);
    CHECK(sys->sound_latch_ack_count > 0U);
    CHECK(sys->sound_cpu_psg_write_count > 0U);
    CHECK(sys->sound_cpu_enabled);
    CHECK_FALSE(sys->sound_cpu.reset_line_held());
    CHECK(sys->sound_cpu.metadata().part_number == "MC6803");
    CHECK(sys->ay0.volume(0) == 0x0FU);
    CHECK(sys->ay1.volume(1) == 0x0CU);
    CHECK(sys->speaker_output_edge_count == 1U);
    CHECK(sys->speaker_output_high);
    CHECK(sys->control_register == 0x01U);
    CHECK(sys->control_write_count == 1U);
    CHECK(sys->flip_screen);
    CHECK(sys->speaker.pending_samples() > 0U);
    CHECK(nonblack_pixels(sys->video.framebuffer()) > 0U);
}

TEST_CASE("Irem M62 video output is GFX-ROM and RAM driven", "[irem_m62]") {
    namespace M62 = mnemos::manifests::irem_m62;

    std::array<std::uint8_t, M62::video_ram_size> video_ram{};
    std::array<std::uint8_t, M62::color_ram_size> color_ram{};
    const std::vector<std::uint8_t> gfx = glyph_ramp();
    const std::vector<std::uint8_t> program = synthetic_program();

    video_ram[0] = 0x81U;
    color_ram[0] = 0x07U;

    M62::m62_video first;
    first.compose(video_ram, color_ram, gfx, program, false, "m62_ldrun_z80_raw_media");
    M62::m62_video second;
    second.compose(video_ram, color_ram, gfx, program, false, "m62_ldrun_z80_raw_media");

    CHECK(frame_pixels(first.framebuffer()) == frame_pixels(second.framebuffer()));
    CHECK(nonblack_pixels(first.framebuffer()) > 0U);

    M62::m62_video flipped;
    flipped.compose(video_ram, color_ram, gfx, program, true, "m62_ldrun_z80_raw_media");
    CHECK(frame_pixels(flipped.framebuffer()) != frame_pixels(first.framebuffer()));
}

TEST_CASE("Irem M62 save-state preserves board identity and RAM", "[irem_m62]") {
    namespace M62 = mnemos::manifests::irem_m62;

    auto source = M62::assemble_m62(synthetic_image(), M62::board_params_for("ldrun"));
    source->run_frame();
    source->work_ram[0x10] = 0xA5U;

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = M62::assemble_m62(synthetic_image(), M62::board_params_for("ldrun"));
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->work_ram[0x10] == 0xA5U);
    CHECK(restored->scratch_ram[0] == 0x42U);
    CHECK(restored->video_ram[0] == 0x81U);
    CHECK(restored->sound_latch == source->sound_latch);
    CHECK(restored->sound_latch_ack_count == source->sound_latch_ack_count);
    CHECK(restored->sound_cpu_psg_write_count == source->sound_cpu_psg_write_count);
    CHECK(restored->sound_cpu_enabled == source->sound_cpu_enabled);
    CHECK(restored->sound_cpu.cpu_registers().pc == source->sound_cpu.cpu_registers().pc);
    CHECK(restored->control_register == source->control_register);
    CHECK(restored->speaker_output_high == source->speaker_output_high);
    CHECK(restored->main_cpu.cpu_registers().pc == source->main_cpu.cpu_registers().pc);

    auto wrong = M62::assemble_m62(
        synthetic_image(),
        {.cpu_clock_hz = M62::cpu_clock_hz, .rom_layout = "unknown", .dip_default = 0x00U});
    mnemos::chips::state_reader wrong_reader(snapshot);
    wrong->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
