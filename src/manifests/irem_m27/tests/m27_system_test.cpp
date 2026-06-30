#include "m27_system.hpp"

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
    void poke16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
        REQUIRE(offset + 1U < bytes.size());
        bytes[offset] = static_cast<std::uint8_t>(value);
        bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 8U);
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_program() {
        namespace M27 = mnemos::manifests::irem_m27;

        std::vector<std::uint8_t> rom(M27::main_rom_size, 0xFFU);
        const std::vector<std::uint8_t> program{
            0xA9U, 0x42U, 0x8DU, 0x00U, 0x00U, // LDA #$42 ; STA $0000
            0xA9U, 0x81U, 0x8DU, 0x00U, 0x20U, // LDA #$81 ; STA $2000
            0xA9U, 0x07U, 0x8DU, 0x00U, 0x24U, // LDA #$07 ; STA $2400
            0xA9U, 0x01U, 0x8DU, 0x04U, 0x40U, // LDA #$01 ; STA $4004
            0xA9U, 0x01U, 0x8DU, 0x05U, 0x40U, // LDA #$01 ; STA $4005
            0x4CU, 0x1EU, 0x80U};             // JMP $801E
        std::copy(program.begin(), program.end(), rom.begin() + M27::program_rom_base);
        poke16(rom, 0xFFFCU, M27::program_rom_base);
        poke16(rom, 0xFFFEU, M27::program_rom_base);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> prom_ramp() {
        std::vector<std::uint8_t> proms(mnemos::manifests::irem_m27::proms_size);
        for (std::size_t i = 0; i < proms.size(); ++i) {
            proms[i] = static_cast<std::uint8_t>(i);
        }
        return proms;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_image() {
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_program());
        image.regions.emplace("audiocpu",
                              std::vector<std::uint8_t>(mnemos::manifests::irem_m27::audio_rom_size,
                                                        0xFFU));
        image.regions.emplace("proms", prom_ramp());
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

TEST_CASE("Irem M27 system runs the first-pass 6502 memory map", "[irem_m27]") {
    namespace M27 = mnemos::manifests::irem_m27;

    auto sys = M27::assemble_m27(synthetic_image(), M27::board_params_for("panther"));
    CHECK(sys->dip_switches == M27::panther_dip_default);
    CHECK(sys->video.framebuffer().width == M27::visible_width);
    CHECK(sys->video.framebuffer().height == M27::visible_height);

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

TEST_CASE("Irem M27 video output is RAM and PROM driven", "[irem_m27]") {
    namespace M27 = mnemos::manifests::irem_m27;

    std::array<std::uint8_t, M27::video_ram_size> video_ram{};
    std::array<std::uint8_t, M27::color_ram_size> color_ram{};
    const std::vector<std::uint8_t> proms = prom_ramp();

    video_ram[0] = 0x81U;
    color_ram[0] = 0x07U;

    M27::m27_video first;
    first.compose(video_ram, color_ram, proms, false, "m27_panther_6502");
    M27::m27_video second;
    second.compose(video_ram, color_ram, proms, false, "m27_panther_6502");

    CHECK(frame_pixels(first.framebuffer()) == frame_pixels(second.framebuffer()));
    CHECK(nonblack_pixels(first.framebuffer()) > 0U);

    M27::m27_video flipped;
    flipped.compose(video_ram, color_ram, proms, true, "m27_panther_6502");
    CHECK(frame_pixels(flipped.framebuffer()) != frame_pixels(first.framebuffer()));
}

TEST_CASE("Irem M27 save-state preserves board identity and RAM", "[irem_m27]") {
    namespace M27 = mnemos::manifests::irem_m27;

    auto source = M27::assemble_m27(synthetic_image(), M27::board_params_for("panther"));
    source->run_frame();
    source->work_ram[0x10] = 0xA5U;

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = M27::assemble_m27(synthetic_image(), M27::board_params_for("panther"));
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

    auto wrong = M27::assemble_m27(
        synthetic_image(),
        {.cpu_clock_hz = M27::cpu_clock_hz, .rom_layout = "unknown", .dip_default = 0x00U});
    mnemos::chips::state_reader wrong_reader(snapshot);
    wrong->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
