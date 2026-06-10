#include "m72_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::manifests::common::rom_set_image;
    using mnemos::manifests::irem_m72::assemble_m72;
    using mnemos::manifests::irem_m72::m72_rom_skeleton;

    // A maincpu region with a far jump at the V30 reset vector (FFFF:0000 ->
    // physical 0xFFFF0) into `program` placed at 0000:0200.
    [[nodiscard]] rom_set_image make_image(const std::vector<std::uint8_t>& program) {
        rom_set_image image;
        auto& main = image.regions["maincpu"];
        main.assign(mnemos::manifests::irem_m72::main_rom_size, 0xFFU);
        main[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        main[0xFFFF1U] = 0x00U;
        main[0xFFFF2U] = 0x02U;
        main[0xFFFF3U] = 0x00U;
        main[0xFFFF4U] = 0x00U;
        for (std::size_t i = 0; i < program.size(); ++i) {
            main[0x200U + i] = program[i];
        }
        return image;
    }

    // Run the main CPU until HLT (bounded).
    void run_until_halt(mnemos::chips::cpu::v30& cpu, int max_instructions) {
        for (int i = 0; i < max_instructions && !cpu.halted(); ++i) {
            cpu.step_instruction();
        }
    }

} // namespace

TEST_CASE("m72 boots a synthetic program from the reset vector", "[m72]") {
    // MOV AX,E000; MOV DS,AX; MOV AL,77; MOV [0010],AL; HLT
    auto system = assemble_m72(
        make_image({0xB8U, 0x00U, 0xE0U, 0x8EU, 0xD8U, 0xB0U, 0x77U, 0xA2U, 0x10U, 0x00U, 0xF4U}));

    run_until_halt(system->main_cpu, 16);
    CHECK(system->main_cpu.halted());
    // DS=E000 offset 0x10 = physical 0xE0010, inside the work-RAM overlay.
    CHECK(system->main_bus.read8(0xE0010U) == 0x77U);
    CHECK(system->work_ram[0x10U] == 0x77U);
}

TEST_CASE("m72 sound latch crosses from the V30 to the Z80 and acks the INT line", "[m72]") {
    // Main: MOV AL,5A; OUT 00,AL; HLT
    auto system = assemble_m72(make_image({0xB0U, 0x5AU, 0xE6U, 0x00U, 0xF4U}));

    // Z80 program: IN A,(02); LD (F800),A; HALT
    auto& sound = system->roms.regions["soundcpu"];
    sound[0x0000U] = 0xDBU;
    sound[0x0001U] = 0x02U;
    sound[0x0002U] = 0x32U;
    sound[0x0003U] = 0x00U;
    sound[0x0004U] = 0xF8U;
    sound[0x0005U] = 0x76U;

    run_until_halt(system->main_cpu, 16);
    CHECK(system->sound_latch == 0x5AU);

    system->sound_cpu.step_instruction(); // IN A,(02) -- also acks the INT line
    system->sound_cpu.step_instruction(); // LD (F800),A
    system->sound_cpu.step_instruction(); // HALT
    CHECK(system->sound_bus.read8(0xF800U) == 0x5AU);
    CHECK(system->sound_ram[0x800U] == 0x5AU);
}

TEST_CASE("m72 inputs and DIP switches read through the V30 I/O ports", "[m72]") {
    // IN AL,00; MOV BL,AL; IN AL,04; MOV BH,AL; HLT
    auto system =
        assemble_m72(make_image({0xE4U, 0x00U, 0x88U, 0xC3U, 0xE4U, 0x04U, 0x88U, 0xC7U, 0xF4U}));
    system->input_p1 = 0xFEU; // button held (active low)
    system->dip_switches = 0xABCDU;

    run_until_halt(system->main_cpu, 16);
    const auto regs = system->main_cpu.cpu_registers();
    CHECK((regs.bx & 0xFFU) == 0xFEU); // BL = P1 inputs
    CHECK((regs.bx >> 8U) == 0xCDU);   // BH = DSW low byte
}

TEST_CASE("m72 ROM skeleton declares the two program regions", "[m72]") {
    const auto decl = m72_rom_skeleton("rtype");
    CHECK(decl.name == "rtype");
    REQUIRE(decl.regions.size() == 2U);
    CHECK(decl.regions[0].name == "maincpu");
    CHECK(decl.regions[0].size == mnemos::manifests::irem_m72::main_rom_size);
    CHECK(decl.regions[1].name == "soundcpu");
    CHECK(decl.regions[1].size == mnemos::manifests::irem_m72::sound_rom_size);
}

TEST_CASE("m72 pads missing program regions to open bus", "[m72]") {
    auto system = assemble_m72(rom_set_image{});
    CHECK(system->main_bus.read8(0x00000U) == 0xFFU);
    CHECK(system->main_bus.read8(0xFFFF0U) == 0xFFU);
    CHECK(system->sound_bus.read8(0x0000U) == 0xFFU);
}
