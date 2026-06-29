#include "m119_system.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
    namespace m119 = mnemos::manifests::irem_m119;

    void poke16_be(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value);
    }

    void poke32_be(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 24U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value >> 16U);
        bytes[at + 2U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 3U] = static_cast<std::uint8_t>(value);
    }

    [[nodiscard]] std::vector<std::uint8_t> synthetic_program() {
        std::vector<std::uint8_t> rom(m119::main_rom_size, 0x00U);
        poke32_be(rom, 0x00U, 0x00000100U);
        poke32_be(rom, 0x04U, m119::work_ram_base + m119::work_ram_size - 4U);
        for (std::size_t at = 0x100U; at < 0x180U; at += 2U) {
            poke16_be(rom, at, 0x0009U); // NOP
        }
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> patterned(std::size_t size, std::uint8_t seed) {
        std::vector<std::uint8_t> bytes(size);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            bytes[i] = static_cast<std::uint8_t>(seed + (i * 13U));
        }
        return bytes;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_image synthetic_image() {
        mnemos::manifests::common::rom_set_image image;
        image.regions.emplace("maincpu", synthetic_program());
        image.regions.emplace("vdp", patterned(m119::vdp_rom_size, 0x21U));
        image.regions.emplace("ymz", patterned(m119::ymz_rom_size, 0x91U));
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

TEST_CASE("m119 executable board wires SH7708, uPD94244, YMZ280B, RAM, and MMIO",
          "[m119][board]") {
    auto system = m119::assemble_m119(synthetic_image(), m119::board_params_for("scumimon"));
    REQUIRE(system != nullptr);

    CHECK(system->main_cpu.metadata().part_number == "HD6417708S");
    CHECK(system->video.metadata().part_number == "uPD94244-210");
    CHECK(system->ymz.metadata().part_number == "YMZ280B");
    CHECK(system->main_bus.read8(0x100U) == 0x00U);

    system->main_bus.write8(m119::work_ram_base, 0x42U);
    system->main_bus.write8(m119::video_ram_base + 4U, 0x88U);
    system->main_bus.write8(m119::mmio_base + m119::mmio_control, 0x5AU);
    system->main_bus.write32_be(m119::mmio_base + m119::mmio_vdp_base + 4U, 0x11223344U);

    CHECK(system->work_ram[0] == 0x42U);
    CHECK(system->video.read_vram(4U) == 0x88U);
    CHECK(system->control_latch == 0x5AU);
    CHECK(system->video.read_register(1U) == 0x11223344U);
    CHECK(system->vdp_register_write_count == 4U);

    system->run_frame();
    CHECK(system->frames_run == 1U);
    CHECK(system->ymz_key_on_count > 0U);
    CHECK(system->ymz.last_sample() != 0);
    CHECK(system->ymz.pending_samples() > 0U);
    CHECK(frame_has_nonblack(system->video.framebuffer()));
}

TEST_CASE("m119 SH7708 program drives uPD94244 and YMZ280B MMIO", "[m119][board]") {
    auto image = synthetic_image();
    auto& rom = image.regions["maincpu"];
    poke16_be(rom, 0x100U, 0x2122U); // MOV.L R2,@R1
    poke16_be(rom, 0x102U, 0x6512U); // MOV.L @R1,R5
    poke16_be(rom, 0x104U, 0x2652U); // MOV.L R5,@R6
    poke16_be(rom, 0x106U, 0x2340U); // MOV.B R4,@R3
    poke16_be(rom, 0x108U, 0x0009U); // NOP

    auto system = m119::assemble_m119(std::move(image), m119::board_params_for("scumimon"));
    REQUIRE(system != nullptr);

    auto regs = system->main_cpu.cpu_registers();
    regs.core.pc = 0x100U;
    regs.core.r[1] = m119::mmio_base + m119::mmio_vdp_base + 4U;
    regs.core.r[2] = 0x12345678U;
    regs.core.r[3] = m119::mmio_base + m119::mmio_ymz_base +
                     mnemos::chips::audio::ymz280b::reg_volume;
    regs.core.r[4] = 0x00000040U;
    regs.core.r[6] = m119::work_ram_base;
    system->main_cpu.set_registers(regs);

    for (int i = 0; i < 4; ++i) {
        CHECK(system->main_cpu.step_instruction() > 0);
    }

    CHECK(system->video.read_register(1U) == 0x12345678U);
    CHECK(system->work_ram[0] == 0x12U);
    CHECK(system->work_ram[1] == 0x34U);
    CHECK(system->work_ram[2] == 0x56U);
    CHECK(system->work_ram[3] == 0x78U);
    CHECK(system->ymz.read_register(mnemos::chips::audio::ymz280b::reg_volume) == 0x40U);
    CHECK(system->vdp_register_write_count == 4U);
    CHECK(system->ymz_register_write_count == 1U);
}

TEST_CASE("m119 save state preserves board identity and runtime state", "[m119][board]") {
    auto source = m119::assemble_m119(synthetic_image(), m119::board_params_for("scumimon"));
    REQUIRE(source != nullptr);
    source->set_inputs(0xF0U);
    source->run_frame();
    source->main_bus.write8(m119::work_ram_base + 3U, 0x66U);
    source->main_bus.write8(m119::nvram_base + 5U, 0x77U);
    source->main_bus.write8(m119::mmio_base + m119::mmio_control, 0x99U);

    std::vector<std::uint8_t> state;
    mnemos::chips::state_writer writer(state);
    source->save_state(writer);
    REQUIRE_FALSE(state.empty());

    auto restored = m119::assemble_m119(synthetic_image(), m119::board_params_for("scumimon"));
    mnemos::chips::state_reader reader(state);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored->work_ram[3] == 0x66U);
    CHECK(restored->nvram[5] == 0x77U);
    CHECK(restored->input_latch == 0xF0U);
    CHECK(restored->control_latch == 0x99U);
    CHECK(restored->frames_run == 1U);

    auto wrong = m119::assemble_m119({});
    mnemos::chips::state_reader wrong_reader(state);
    wrong->load_state(wrong_reader);
    CHECK_FALSE(wrong_reader.ok());
}
