#include "m92_system.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

    namespace m92 = mnemos::manifests::irem_m92;

    [[nodiscard]] std::vector<std::uint8_t>
    make_m92_program(const std::vector<std::uint8_t>& program) {
        std::vector<std::uint8_t> rom(m92::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m92_program() {
        return make_m92_program(
            {0xB8U, 0x00U, 0xE0U, 0x8EU, 0xD8U, 0xB0U, 0x42U, 0xA2U, 0x00U, 0x00U, 0xF4U});
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m92_sound_program() {
        std::vector<std::uint8_t> rom(m92::sound_rom_size, 0xFFU);
        rom[0x1FFF0U] = 0xEAU; // JMP E000:0200 through the mirrored sound ROM window
        rom[0x1FFF1U] = 0x00U;
        rom[0x1FFF2U] = 0x02U;
        rom[0x1FFF3U] = 0x00U;
        rom[0x1FFF4U] = 0xE0U;
        rom[0x0200U] = 0xF4U;
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m92_sound_ga20_program() {
        std::vector<std::uint8_t> rom(m92::sound_rom_size, 0xFFU);
        rom[0x1FFF0U] = 0xEAU; // JMP E000:0200 through the mirrored sound ROM window
        rom[0x1FFF1U] = 0x00U;
        rom[0x1FFF2U] = 0x02U;
        rom[0x1FFF3U] = 0x00U;
        rom[0x1FFF4U] = 0xE0U;
        const std::array<std::uint8_t, 41> program = {
            0xB8U, 0x00U, 0xA8U, 0x8EU, 0xD8U, // MOV DS,A800
            0xB0U, 0x10U, 0xA2U, 0x00U, 0x00U, // GA20 ch0 start low  -> 0x100
            0xB0U, 0x00U, 0xA2U, 0x01U, 0x00U, // GA20 ch0 start high
            0xB0U, 0x40U, 0xA2U, 0x02U, 0x00U, // GA20 ch0 end low    -> 0x400
            0xB0U, 0x00U, 0xA2U, 0x03U, 0x00U, // GA20 ch0 end high
            0xB0U, 0x00U, 0xA2U, 0x04U, 0x00U, // slowest byte advance
            0xB0U, 0xF6U, 0xA2U, 0x05U, 0x00U, // audible volume
            0xB0U, 0x02U, 0xA2U, 0x06U, 0x00U, // control bit 1 = key-on
            0xF4U};                            // HLT
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x0200U + i] = program[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m92_sound_command_program() {
        std::vector<std::uint8_t> rom(m92::sound_rom_size, 0xFFU);
        const std::size_t intp1_vector =
            static_cast<std::size_t>(m92::sound_irq_vector_command_latch) * 4U;
        rom[intp1_vector + 0U] = 0x00U; // IVT[INTP1] -> 0000:0300
        rom[intp1_vector + 1U] = 0x03U;
        rom[intp1_vector + 2U] = 0x00U;
        rom[intp1_vector + 3U] = 0x00U;
        rom[0x1FFF0U] = 0xEAU; // JMP E000:0200 through the mirrored sound ROM window
        rom[0x1FFF1U] = 0x00U;
        rom[0x1FFF2U] = 0x02U;
        rom[0x1FFF3U] = 0x00U;
        rom[0x1FFF4U] = 0xE0U;
        const std::vector<std::uint8_t> wait_program{
            0xFBU, // STI
            0xF4U, // HLT, then wake on the command-latch IRQ
            0xF4U  // HLT again if the handler ever returns
        };
        for (std::size_t i = 0; i < wait_program.size(); ++i) {
            rom[0x0200U + i] = wait_program[i];
        }
        const std::vector<std::uint8_t> handler{
            0xB8U, 0x00U, 0xA8U, // MOV AX,A800
            0x8EU, 0xD8U,        // MOV DS,AX
            0xA0U, 0x44U, 0x00U, // MOV AL,[0044] (main sound command latch)
            0x88U, 0xC3U,        // MOV BL,AL
            0xA2U, 0x44U, 0x00U, // MOV [0044],AL (acknowledge command IRQ)
            0xA2U, 0x46U, 0x00U, // MOV [0046],AL (sound reply)
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000
            0x8EU, 0xD8U,        // MOV DS,AX
            0x8AU, 0xC3U,        // MOV AL,BL
            0xA2U, 0x00U, 0x00U, // MOV [0000],AL
            0xF4U                // HLT
        };
        for (std::size_t i = 0; i < handler.size(); ++i) {
            rom[0x0300U + i] = handler[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m92_sound_ym_irq_program() {
        std::vector<std::uint8_t> rom(m92::sound_rom_size, 0xFFU);
        const std::size_t intp0_vector =
            static_cast<std::size_t>(m92::sound_irq_vector_ym2151) * 4U;
        rom[intp0_vector + 0U] = 0x00U; // IVT[INTP0] -> 0000:0300
        rom[intp0_vector + 1U] = 0x03U;
        rom[intp0_vector + 2U] = 0x00U;
        rom[intp0_vector + 3U] = 0x00U;
        rom[0x1FFF0U] = 0xEAU; // JMP E000:0200 through the mirrored sound ROM window
        rom[0x1FFF1U] = 0x00U;
        rom[0x1FFF2U] = 0x02U;
        rom[0x1FFF3U] = 0x00U;
        rom[0x1FFF4U] = 0xE0U;
        const std::vector<std::uint8_t> wait_program{
            0xFBU, // STI
            0xF4U, // HLT, then wake on the YM2151 INTP0 IRQ
            0xF4U  // HLT again if the handler ever returns
        };
        for (std::size_t i = 0; i < wait_program.size(); ++i) {
            rom[0x0200U + i] = wait_program[i];
        }
        const std::vector<std::uint8_t> handler{
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000
            0x8EU, 0xD8U,        // MOV DS,AX
            0xB0U, 0xA5U,        // MOV AL,A5
            0xA2U, 0x01U, 0x00U, // MOV [0001],AL
            0xF4U                // HLT
        };
        for (std::size_t i = 0; i < handler.size(); ++i) {
            rom[0x0300U + i] = handler[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m92_sound_irq_priority_program() {
        std::vector<std::uint8_t> rom(m92::sound_rom_size, 0xFFU);
        const std::size_t intp0_vector =
            static_cast<std::size_t>(m92::sound_irq_vector_ym2151) * 4U;
        const std::size_t intp1_vector =
            static_cast<std::size_t>(m92::sound_irq_vector_command_latch) * 4U;
        rom[intp0_vector + 0U] = 0x00U; // IVT[INTP0] -> 0000:0300
        rom[intp0_vector + 1U] = 0x03U;
        rom[intp0_vector + 2U] = 0x00U;
        rom[intp0_vector + 3U] = 0x00U;
        rom[intp1_vector + 0U] = 0x20U; // IVT[INTP1] -> 0000:0320
        rom[intp1_vector + 1U] = 0x03U;
        rom[intp1_vector + 2U] = 0x00U;
        rom[intp1_vector + 3U] = 0x00U;
        rom[0x1FFF0U] = 0xEAU; // JMP E000:0200 through the mirrored sound ROM window
        rom[0x1FFF1U] = 0x00U;
        rom[0x1FFF2U] = 0x02U;
        rom[0x1FFF3U] = 0x00U;
        rom[0x1FFF4U] = 0xE0U;
        const std::vector<std::uint8_t> wait_program{
            0xFBU, // STI
            0xF4U, // HLT, then wake on the selected sound IRQ
            0xF4U  // HLT again if the handler ever returns
        };
        for (std::size_t i = 0; i < wait_program.size(); ++i) {
            rom[0x0200U + i] = wait_program[i];
        }
        const std::vector<std::uint8_t> intp0_handler{
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000
            0x8EU, 0xD8U,        // MOV DS,AX
            0xB0U, 0xA0U,        // MOV AL,A0
            0xA2U, 0x02U, 0x00U, // MOV [0002],AL
            0xF4U                // HLT
        };
        const std::vector<std::uint8_t> intp1_handler{
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000
            0x8EU, 0xD8U,        // MOV DS,AX
            0xB0U, 0xC1U,        // MOV AL,C1
            0xA2U, 0x02U, 0x00U, // MOV [0002],AL
            0xF4U                // HLT
        };
        for (std::size_t i = 0; i < intp0_handler.size(); ++i) {
            rom[0x0300U + i] = intp0_handler[i];
        }
        for (std::size_t i = 0; i < intp1_handler.size(); ++i) {
            rom[0x0320U + i] = intp1_handler[i];
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_m92_sound_irq_priority_chain_program() {
        std::vector<std::uint8_t> rom(m92::sound_rom_size, 0xFFU);
        const std::size_t intp0_vector =
            static_cast<std::size_t>(m92::sound_irq_vector_ym2151) * 4U;
        const std::size_t intp1_vector =
            static_cast<std::size_t>(m92::sound_irq_vector_command_latch) * 4U;
        rom[intp0_vector + 0U] = 0x00U; // IVT[INTP0] -> 0000:0300
        rom[intp0_vector + 1U] = 0x03U;
        rom[intp0_vector + 2U] = 0x00U;
        rom[intp0_vector + 3U] = 0x00U;
        rom[intp1_vector + 0U] = 0x40U; // IVT[INTP1] -> 0000:0340
        rom[intp1_vector + 1U] = 0x03U;
        rom[intp1_vector + 2U] = 0x00U;
        rom[intp1_vector + 3U] = 0x00U;
        rom[0x1FFF0U] = 0xEAU; // JMP E000:0200 through the mirrored sound ROM window
        rom[0x1FFF1U] = 0x00U;
        rom[0x1FFF2U] = 0x02U;
        rom[0x1FFF3U] = 0x00U;
        rom[0x1FFF4U] = 0xE0U;
        const std::vector<std::uint8_t> wait_program{
            0xFBU, // STI
            0xF4U, // HLT, then wake on the selected sound IRQ
            0xF4U  // HLT again if the handlers ever return
        };
        for (std::size_t i = 0; i < wait_program.size(); ++i) {
            rom[0x0200U + i] = wait_program[i];
        }
        const std::vector<std::uint8_t> intp0_handler{
            0xB8U, 0x00U, 0xA8U, // MOV AX,A800
            0x8EU, 0xD8U,        // MOV DS,AX
            0xB0U, 0x14U,        // MOV AL,14
            0xA2U, 0x40U, 0x00U, // MOV [0040],AL (YM2151 timer control address)
            0xB0U, 0x15U,        // MOV AL,15 (reset Timer A flag, keep Timer A/IRQ enabled)
            0xA2U, 0x41U, 0x00U, // MOV [0041],AL
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000
            0x8EU, 0xD8U,        // MOV DS,AX
            0xB0U, 0xA0U,        // MOV AL,A0
            0xA2U, 0x02U, 0x00U, // MOV [0002],AL
            0xFBU,               // STI, then let the pending lower-priority source through
            0xF4U                // HLT
        };
        const std::vector<std::uint8_t> intp1_handler{
            0xB8U, 0x00U, 0xA8U, // MOV AX,A800
            0x8EU, 0xD8U,        // MOV DS,AX
            0xA0U, 0x44U, 0x00U, // MOV AL,[0044] (main sound command latch)
            0x88U, 0xC3U,        // MOV BL,AL
            0xA2U, 0x44U, 0x00U, // MOV [0044],AL (acknowledge command IRQ)
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000
            0x8EU, 0xD8U,        // MOV DS,AX
            0x8AU, 0xC3U,        // MOV AL,BL
            0xA2U, 0x03U, 0x00U, // MOV [0003],AL
            0xF4U                // HLT
        };
        for (std::size_t i = 0; i < intp0_handler.size(); ++i) {
            rom[0x0300U + i] = intp0_handler[i];
        }
        for (std::size_t i = 0; i < intp1_handler.size(); ++i) {
            rom[0x0340U + i] = intp1_handler[i];
        }
        return rom;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_m92_image() {
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_m92_program());
        image.regions.emplace("soundcpu", synthetic_m92_sound_program());

        std::vector<std::uint8_t> tiles(0x4000U, 0x00U);
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            tiles[i] = static_cast<std::uint8_t>((i * 37U) & 0xFFU);
        }
        std::vector<std::uint8_t> sprites(0x4000U, 0x00U);
        for (std::size_t i = 0; i < sprites.size(); ++i) {
            sprites[i] = static_cast<std::uint8_t>((i * 19U + 3U) & 0xFFU);
        }
        std::vector<std::uint8_t> samples(0x1000U, 0x00U);
        std::fill(samples.begin() + 0x100, samples.begin() + 0x400, std::uint8_t{0x55U});

        image.regions.emplace("tiles", std::move(tiles));
        image.regions.emplace("sprites", std::move(sprites));
        image.regions.emplace("samples", std::move(samples));
        image.regions.emplace("plds", std::vector<std::uint8_t>(m92::plds_rom_size, 0xA5U));
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

TEST_CASE("m92 executable board maps V33 reset, M92 RAM windows, and diagnostic frame",
          "[m92][board]") {
    auto system = m92::assemble_m92(synthetic_m92_image(), m92::board_params_for("bmaster"));
    REQUIRE(system != nullptr);

    CHECK(system->main_bus.read8(0xFFFF0U) == 0xEAU);
    CHECK(system->sound_bus.read8(0xFFFF0U) == 0xEAU);

    system->main_bus.write8(m92::vram_base, 0x37U);
    system->main_bus.write8(m92::sprite_ram_base, 0x41U);
    system->main_bus.write8(m92::palette_ram_base, 0x83U);
    CHECK(system->vram[0] == 0x37U);
    CHECK(system->sprite_ram[0] == 0x41U);
    CHECK(system->palette_ram[0] == 0x83U);

    system->run_frame();
    CHECK(system->work_ram[0] == 0x42U);
    CHECK(frame_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m92 board declares M92 V33/V35 clocks and 320x240 raster", "[m92][board]") {
    auto system = m92::assemble_m92(synthetic_m92_image(), m92::board_params_for("bmaster"));
    REQUIRE(system != nullptr);

    CHECK(system->main_cpu.metadata().part_number == "v33");
    CHECK(system->sound_cpu.metadata().part_number == "v35");
    CHECK(m92::main_clock_hz == 9'000'000U);
    CHECK(m92::sound_cpu_clock_hz == 14'318'181U);
    CHECK(m92::main_cycles_per_frame == 149'972U);
    CHECK(m92::sound_cycles_per_frame == 238'592U);
    CHECK(m92::visible_width == 320U);
    CHECK(m92::visible_height == 240U);
    CHECK(m92::frame_lines == 262U);
    CHECK(m92::sound_irq_vector_ym2151 == 24U);
    CHECK(m92::sound_irq_vector_command_latch == 25U);
}

TEST_CASE("m92 sound CPU drives the memory-mapped Irem GA20 window", "[m92][board][audio]") {
    auto image = synthetic_m92_image();
    image.regions["soundcpu"] = synthetic_m92_sound_ga20_program();

    auto system = m92::assemble_m92(std::move(image), m92::board_params_for("bmaster"));
    REQUIRE(system != nullptr);
    CHECK(system->pcm.metadata().part_number == "GA20");
    CHECK(system->pcm.capture_divider() == m92::pcm_capture_divider);

    system->run_frame();

    CHECK(system->pcm.read_register(mnemos::chips::audio::irem_ga20::reg_status) ==
          mnemos::chips::audio::irem_ga20::status_active);
    CHECK(system->pcm.last_sample() < 0);
}

TEST_CASE("m92 V35 sound CPU fetches decrypted opcodes while data reads see encrypted ROM",
          "[m92][board][audio][opcode]") {
    auto image = synthetic_m92_image();
    image.regions["soundcpu"] = std::vector<std::uint8_t>(m92::sound_rom_size, 0x00U);
    image.regions[std::string{m92::sound_opcode_region}] = synthetic_m92_sound_ga20_program();

    auto system = m92::assemble_m92(std::move(image), m92::board_params_for("bmaster"));
    REQUIRE(system != nullptr);
    REQUIRE(system->sound_opcode_rom.size() == m92::sound_opcode_overlay_size);

    CHECK(system->sound_bus.read8(0xFFFF0U) == 0x00U);
    CHECK(system->sound_bus.fetch_opcode8(0xFFFF0U) == 0xEAU);
    CHECK(system->sound_bus.read8(0xE0200U) == 0x00U);
    CHECK(system->sound_bus.fetch_opcode8(0xE0200U) == 0xB8U);

    system->run_frame();

    CHECK(system->pcm.read_register(mnemos::chips::audio::irem_ga20::reg_status) ==
          mnemos::chips::audio::irem_ga20::status_active);
    CHECK(system->pcm.last_sample() < 0);
}

TEST_CASE("m92 sound command latch reaches the V35 and returns a reply", "[m92][board][audio]") {
    auto image = synthetic_m92_image();
    image.regions["maincpu"] = make_m92_program({0xB0U, 0x5AU, 0xE6U, 0x00U, 0xF4U});

    SECTION("unread command remains pending") {
        image.regions["soundcpu"] = synthetic_m92_sound_program();

        auto system = m92::assemble_m92(std::move(image), m92::board_params_for("bmaster"));
        REQUIRE(system != nullptr);

        system->run_frame();

        CHECK(system->sound_latch == 0x5AU);
        CHECK(system->sound_latch_pending);
        CHECK(system->sound_bus.read8(m92::sound_latch_addr) == 0x5AU);
        CHECK(system->sound_latch_pending);
        system->sound_bus.write8(m92::sound_latch_addr, 0x00U);
        CHECK_FALSE(system->sound_latch_pending);
        CHECK_FALSE(system->sound_reply_pending);
    }

    SECTION("V35 INTP1 handler acknowledges the command latch and replies") {
        image.regions["soundcpu"] = synthetic_m92_sound_command_program();

        auto system = m92::assemble_m92(std::move(image), m92::board_params_for("bmaster"));
        REQUIRE(system != nullptr);

        system->run_frame();

        CHECK(system->sound_latch == 0x5AU);
        CHECK_FALSE(system->sound_latch_pending);
        CHECK(system->sound_reply == 0x5AU);
        CHECK(system->sound_reply_pending);
        CHECK(system->sound_ram[0] == 0x5AU);
    }
}

TEST_CASE("m92 YM2151 IRQ reaches the V35 INTP0 vector", "[m92][board][audio]") {
    auto image = synthetic_m92_image();
    image.regions["soundcpu"] = synthetic_m92_sound_ym_irq_program();

    auto system = m92::assemble_m92(std::move(image), m92::board_params_for("bmaster"));
    REQUIRE(system != nullptr);

    system->run_frame();
    CHECK(system->sound_ram[1] == 0x00U);
    CHECK(system->sound_cpu.halted());

    system->fm.write_address(0x10U);
    system->fm.write_data(0xFFU);
    system->fm.write_address(0x11U);
    system->fm.write_data(0x02U); // CLKA = 1022, overflow after 128 OPM clocks.
    system->fm.write_address(0x14U);
    system->fm.write_data(0x05U); // run Timer A + enable IRQ.
    system->fm.tick(128U);
    REQUIRE(system->fm.irq_asserted());

    system->sound_cpu.tick(256U);
    CHECK(system->sound_ram[1] == 0xA5U);
}

TEST_CASE("m92 simultaneous sound IRQs prefer YM2151 INTP0 over command INTP1",
          "[m92][board][audio]") {
    auto image = synthetic_m92_image();
    image.regions["soundcpu"] = synthetic_m92_sound_irq_priority_program();

    auto system = m92::assemble_m92(std::move(image), m92::board_params_for("bmaster"));
    REQUIRE(system != nullptr);

    system->run_frame();
    CHECK(system->sound_ram[2] == 0x00U);
    REQUIRE(system->sound_cpu.halted());

    system->write_sound_latch(0x44U);
    REQUIRE(system->sound_latch_pending);

    system->fm.write_address(0x10U);
    system->fm.write_data(0xFFU);
    system->fm.write_address(0x11U);
    system->fm.write_data(0x02U);
    system->fm.write_address(0x14U);
    system->fm.write_data(0x05U);
    system->fm.tick(128U);
    REQUIRE(system->fm.irq_asserted());

    system->sound_cpu.tick(256U);
    CHECK(system->sound_ram[2] == 0xA0U);
    CHECK(system->sound_latch_pending);
}

TEST_CASE("m92 pending command IRQ is serviced after YM2151 INTP0 clears", "[m92][board][audio]") {
    auto image = synthetic_m92_image();
    image.regions["soundcpu"] = synthetic_m92_sound_irq_priority_chain_program();

    auto system = m92::assemble_m92(std::move(image), m92::board_params_for("bmaster"));
    REQUIRE(system != nullptr);

    system->run_frame();
    CHECK(system->sound_ram[2] == 0x00U);
    CHECK(system->sound_ram[3] == 0x00U);
    REQUIRE(system->sound_cpu.halted());

    system->write_sound_latch(0x44U);
    REQUIRE(system->sound_latch_pending);

    system->fm.write_address(0x10U);
    system->fm.write_data(0xFFU);
    system->fm.write_address(0x11U);
    system->fm.write_data(0x02U);
    system->fm.write_address(0x14U);
    system->fm.write_data(0x05U);
    system->fm.tick(128U);
    REQUIRE(system->fm.irq_asserted());

    system->sound_cpu.tick(4096U);
    CHECK(system->sound_ram[2] == 0xA0U);
    CHECK(system->sound_ram[3] == 0x44U);
    CHECK_FALSE(system->fm.irq_asserted());
    CHECK_FALSE(system->sound_latch_pending);
    CHECK(system->sound_cpu.halted());
}

TEST_CASE("m92 save state preserves board identity and runtime state", "[m92][board]") {
    auto source = m92::assemble_m92(synthetic_m92_image(), m92::board_params_for("bmaster"));
    source->set_inputs(0xFEU, 0xFDU, 0xFBU);
    source->write_sound_latch(0x76U);
    source->write_sound_reply(0x34U);
    source->run_frame();
    source->work_ram[7] = 0x6CU;

    std::vector<std::uint8_t> state;
    mnemos::chips::state_writer writer(state);
    source->save_state(writer);
    REQUIRE_FALSE(state.empty());

    mnemos::chips::state_reader header_reader(state);
    CHECK(header_reader.u32() == m92::m92_system_state_version);

    std::vector<std::uint8_t> old_version_state = state;
    REQUIRE(old_version_state.size() >= sizeof(std::uint32_t));
    old_version_state[0] = static_cast<std::uint8_t>(m92::m92_system_state_version - 1U);
    auto old_version = m92::assemble_m92(synthetic_m92_image(), m92::board_params_for("bmaster"));
    mnemos::chips::state_reader old_reader(old_version_state);
    old_version->load_state(old_reader);
    CHECK_FALSE(old_reader.ok());

    auto restored = m92::assemble_m92(synthetic_m92_image(), m92::board_params_for("bmaster"));
    mnemos::chips::state_reader reader(state);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored->work_ram[0] == 0x42U);
    CHECK(restored->work_ram[7] == 0x6CU);
    CHECK(restored->input_p1 == 0xFEU);
    CHECK(restored->sound_latch == 0x76U);
    CHECK(restored->sound_latch_pending);
    CHECK(restored->sound_reply == 0x34U);
    CHECK(restored->sound_reply_pending);

    auto wrong_layout = m92::assemble_m92(synthetic_m92_image(), m92::board_params_for("hook"));
    mnemos::chips::state_reader wrong_reader(state);
    wrong_layout->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
