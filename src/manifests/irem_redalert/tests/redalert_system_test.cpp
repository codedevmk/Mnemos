#include "redalert_system.hpp"

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
        namespace red = mnemos::manifests::irem_redalert;

        std::vector<std::uint8_t> rom(red::main_rom_size, 0xFFU);
        const std::vector<std::uint8_t> program{
            0xA9U, 0x42U, 0x8DU, 0x00U, 0x00U, // LDA #$42 ; STA $0000
            0xA9U, 0x07U, 0x8DU, 0x50U, 0xC0U, // LDA #$07 ; STA $C050
            0xA9U, 0x81U, 0x8DU, 0x00U, 0x20U, // LDA #$81 ; STA $2000
            0xA9U, 0xC3U, 0x8DU, 0x00U, 0x40U, // LDA #$C3 ; STA $4000
            0xA9U, 0x00U, 0x8DU, 0x30U, 0xC0U, // LDA #$00 ; STA $C030
            0xA9U, 0x04U, 0x8DU, 0x40U, 0xC0U, // LDA #$04 ; STA $C040
            0xADU, 0x70U, 0xC0U,             // LDA $C070 ; clear IRQ
            0x4CU, 0x21U, 0x50U};             // JMP $5021
        std::copy(program.begin(), program.end(), rom.begin() + red::program_rom_base);
        poke16(rom, red::vector_mirror_source + 0x0FFCU, red::program_rom_base);
        poke16(rom, red::vector_mirror_source + 0x0FFEU, red::program_rom_base);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> prom_ramp() {
        std::vector<std::uint8_t> proms(mnemos::manifests::irem_redalert::proms_size);
        for (std::size_t i = 0; i < proms.size(); ++i) {
            proms[i] = static_cast<std::uint8_t>(i);
        }
        return proms;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_image() {
        namespace red = mnemos::manifests::irem_redalert;
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_program());
        image.regions.emplace("audiocpu", std::vector<std::uint8_t>(red::audio_rom_size, 0xFFU));
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

TEST_CASE("Irem Red Alert system runs the WW III 6502 memory map", "[irem_redalert]") {
    namespace red = mnemos::manifests::irem_redalert;

    auto sys = red::assemble_redalert(synthetic_image(), red::board_params_for("ww3"));
    CHECK(sys->dip_switches == red::ww3_dip_default);
    CHECK(sys->video.framebuffer().width == red::visible_width);
    CHECK(sys->video.framebuffer().height == red::visible_height);

    sys->run_frame();

    CHECK(sys->ram[0] == 0x42U);
    CHECK(sys->bitmap_ram[0] == 0x81U);
    CHECK(sys->bitmap_color_ram[0] == 0x07U);
    CHECK(sys->char_ram[0] == 0xC3U);
    CHECK(sys->audio_command == 0x00U);
    CHECK(sys->audio_command_write_count == 1U);
    CHECK(sys->speaker_output_edge_count == 0U);
    CHECK(sys->speaker_output_high);
    CHECK(sys->video_control == red::video_flip_bit);
    CHECK(sys->video_control_write_count == 1U);
    CHECK(sys->flip_screen());
    CHECK(sys->interrupt_ack_count >= 1U);
    CHECK(sys->speaker.pending_samples() > 0U);
    CHECK(nonblack_pixels(sys->video.framebuffer()) > 0U);
}

TEST_CASE("Irem Red Alert vector mirror fetches reset/IRQ vectors from $8000 ROM window",
          "[irem_redalert]") {
    namespace red = mnemos::manifests::irem_redalert;

    auto sys = red::assemble_redalert(synthetic_image(), red::board_params_for("ww3"));
    CHECK(sys->main_bus.read8(0xFFFCU) == static_cast<std::uint8_t>(red::program_rom_base));
    CHECK(sys->main_bus.read8(0xFFFDU) == static_cast<std::uint8_t>(red::program_rom_base >> 8U));
    CHECK(sys->main_bus.read8(0xF000U) == sys->main_bus.read8(red::vector_mirror_source));
}

TEST_CASE("Irem Red Alert video output is bitmap, char, and PROM driven", "[irem_redalert]") {
    namespace red = mnemos::manifests::irem_redalert;

    std::array<std::uint8_t, red::bitmap_ram_size> bitmap{};
    std::array<std::uint8_t, red::bitmap_color_ram_size> bitmap_colors{};
    std::array<std::uint8_t, red::char_ram_size> chars{};
    const std::vector<std::uint8_t> proms = prom_ramp();

    bitmap[0] = 0x80U;
    bitmap_colors[0] = 0x07U;
    chars[0] = 0xC3U;
    chars[0x0400U | ((0x43U << 3U) | 0x00U)] = 0x80U;
    chars[0x0C00U | ((0x43U << 3U) | 0x00U)] = 0x80U;

    red::redalert_video first;
    first.compose(bitmap, bitmap_colors, chars, proms, red::video_flip_bit, red::video_flip_bit,
                  "redalert_ww3_m27mb");
    red::redalert_video second;
    second.compose(bitmap, bitmap_colors, chars, proms, red::video_flip_bit, red::video_flip_bit,
                   "redalert_ww3_m27mb");

    CHECK(frame_pixels(first.framebuffer()) == frame_pixels(second.framebuffer()));
    CHECK(nonblack_pixels(first.framebuffer()) > 0U);

    red::redalert_video flipped;
    flipped.compose(bitmap, bitmap_colors, chars, proms, 0x00U, red::video_flip_bit,
                    "redalert_ww3_m27mb");
    CHECK(frame_pixels(flipped.framebuffer()) != frame_pixels(first.framebuffer()));
}

TEST_CASE("Irem Red Alert save-state preserves board identity and RAM", "[irem_redalert]") {
    namespace red = mnemos::manifests::irem_redalert;

    auto source = red::assemble_redalert(synthetic_image(), red::board_params_for("ww3"));
    source->run_frame();
    source->ram[0x10] = 0xA5U;

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = red::assemble_redalert(synthetic_image(), red::board_params_for("ww3"));
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->ram[0x10] == 0xA5U);
    CHECK(restored->bitmap_ram[0] == 0x81U);
    CHECK(restored->bitmap_color_ram[0] == 0x07U);
    CHECK(restored->char_ram[0] == 0xC3U);
    CHECK(restored->audio_command == source->audio_command);
    CHECK(restored->video_control == source->video_control);
    CHECK(restored->speaker_output_high == source->speaker_output_high);
    CHECK(restored->main_cpu.cpu_registers().pc == source->main_cpu.cpu_registers().pc);

    auto wrong = red::assemble_redalert(
        synthetic_image(),
        {.cpu_clock_hz = red::cpu_clock_hz,
         .rom_layout = "unknown",
         .dip_default = 0x00U,
         .video_control_xor = 0x00U});
    mnemos::chips::state_reader wrong_reader(snapshot);
    wrong->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
