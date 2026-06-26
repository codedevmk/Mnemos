#include "m52_system.hpp"

#include "rom_set.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_image() {
        namespace m52 = mnemos::manifests::irem_m52;
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_z80_program());
        image.regions.emplace("soundcpu", std::vector<std::uint8_t>(m52::sound_rom_size, 0x24U));
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
} // namespace

TEST_CASE("Irem M52 system runs Z80 memory and IO windows", "[irem_m52]") {
    namespace m52 = mnemos::manifests::irem_m52;

    auto sys = m52::assemble_m52(synthetic_image(), m52::board_params_for("mpatrol"));
    sys->run_frame();

    CHECK(sys->video_ram[0] == 0x42U);
    CHECK(sys->scroll_regs[0] == 0x42U);
    CHECK(sys->sound_command == 0x05U);
    CHECK(sys->sound_command_write_count > 0U);
    CHECK(sys->video.framebuffer().width == m52::visible_width);
    CHECK(sys->video.framebuffer().height == m52::visible_height);
    CHECK(nonblack_pixels(sys->video.framebuffer()) > 0U);
}

TEST_CASE("Irem M52 save-state preserves board identity and RAM", "[irem_m52]") {
    namespace m52 = mnemos::manifests::irem_m52;

    auto source = m52::assemble_m52(synthetic_image(), m52::board_params_for("mpatrol"));
    source->run_frame();
    source->work_ram[0x10] = 0xA5U;

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    source->save_state(writer);

    auto restored = m52::assemble_m52(synthetic_image(), m52::board_params_for("mpatrol"));
    mnemos::chips::state_reader reader(snapshot);
    restored->load_state(reader);

    REQUIRE(reader.ok());
    CHECK(restored->work_ram[0x10] == 0xA5U);
    CHECK(restored->video.framebuffer().width == source->video.framebuffer().width);
    CHECK(restored->sound_command == source->sound_command);

    auto wrong = m52::assemble_m52(synthetic_image(), {});
    mnemos::chips::state_reader wrong_reader(snapshot);
    wrong->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
