#include "m72_system.hpp"

#include "scheduler.hpp"

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
    // MOV AX,A000; MOV DS,AX; MOV AL,77; MOV [0010],AL; HLT
    auto system = assemble_m72(
        make_image({0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U, 0x77U, 0xA2U, 0x10U, 0x00U, 0xF4U}));

    run_until_halt(system->main_cpu, 16);
    CHECK(system->main_cpu.halted());
    // DS=A000 offset 0x10 = physical 0xA0010, the base map's work-RAM overlay.
    CHECK(system->main_bus.read8(0xA0010U) == 0x77U);
    CHECK(system->work_ram[0x10U] == 0x77U);
}

TEST_CASE("m72 board params place the work RAM per declared set", "[m72]") {
    using mnemos::manifests::irem_m72::board_params_for;
    CHECK(board_params_for("rtype").work_ram_base == 0x40000U);
    CHECK(board_params_for("rtype").dip_default == 0xFDFBU);
    CHECK(board_params_for("unknown").work_ram_base == 0xA0000U);

    // MOV AX,4000; MOV DS,AX; MOV AL,55; MOV [0010],AL; HLT
    auto system = assemble_m72(
        make_image({0xB8U, 0x00U, 0x40U, 0x8EU, 0xD8U, 0xB0U, 0x55U, 0xA2U, 0x10U, 0x00U, 0xF4U}),
        board_params_for("rtype"));
    run_until_halt(system->main_cpu, 16);
    CHECK(system->work_ram[0x10U] == 0x55U);
    CHECK(system->dip_switches == 0xFDFBU);
}

TEST_CASE("m72 sound latch crosses from the V30 to the Z80 and acks via port 6", "[m72]") {
    // Main: MOV AL,5A; OUT 00,AL; HLT
    auto system = assemble_m72(make_image({0xB0U, 0x5AU, 0xE6U, 0x00U, 0xF4U}));

    // Z80 program: IN A,(02); LD (F800),A; OUT (06),A; HALT
    const std::vector<std::uint8_t> z80_program{0xDBU, 0x02U, 0x32U, 0x00U,
                                                0xF8U, 0xD3U, 0x06U, 0x76U};
    for (std::size_t i = 0; i < z80_program.size(); ++i) {
        system->sound_ram[i] = z80_program[i];
    }
    system->sound_cpu.set_reset_line(false); // upload done, run

    run_until_halt(system->main_cpu, 16);
    CHECK(system->sound_latch == 0x5AU);
    CHECK(system->sound_latch_irq); // pending until the explicit acknowledge

    system->sound_cpu.step_instruction(); // IN A,(02) -- read does NOT ack
    CHECK(system->sound_latch_irq);
    system->sound_cpu.step_instruction(); // LD (F800),A
    system->sound_cpu.step_instruction(); // OUT (06),A -- the acknowledge
    CHECK_FALSE(system->sound_latch_irq);
    system->sound_cpu.step_instruction(); // HALT
    CHECK(system->sound_bus.read8(0xF800U) == 0x5AU);
    CHECK(system->sound_ram[0xF800U] == 0x5AU);
}

TEST_CASE("m72 V30 uploads the sound program through the shared-RAM window and releases the Z80",
          "[m72]") {
    // The hardware boot path: at power-on the Z80 is parked in reset; the V30
    // writes its program through 0xE0000 and raises control bit 4.
    // Main: MOV AX,E000; MOV DS,AX; MOV AL,3E (LD A,n); MOV [0000],AL;
    //       MOV AL,77; MOV [0001],AL; MOV AL,76 (HALT); MOV [0002],AL;
    //       MOV AL,10; OUT 02,AL; HLT
    auto system = assemble_m72(make_image({
        0xB8U, 0x00U, 0xE0U, // MOV AX,E000
        0x8EU, 0xD8U,        // MOV DS,AX
        0xB0U, 0x3EU,        // MOV AL,3E
        0xA2U, 0x00U, 0x00U, // MOV [0000],AL
        0xB0U, 0x77U,        // MOV AL,77
        0xA2U, 0x01U, 0x00U, // MOV [0001],AL
        0xB0U, 0x76U,        // MOV AL,76
        0xA2U, 0x02U, 0x00U, // MOV [0002],AL
        0xB0U, 0x10U,        // MOV AL,10 (bit 4: release the sound CPU)
        0xE6U, 0x02U,        // OUT 02,AL
        0xF4U                // HLT
    }));

    CHECK(system->sound_cpu.reset_line_held()); // parked at power-on

    // Held: stepping the Z80 does nothing.
    system->sound_cpu.tick(16U);
    CHECK(system->sound_cpu.cpu_registers().pc == 0x0000U);

    run_until_halt(system->main_cpu, 32);
    CHECK(system->sound_ram[0x0000U] == 0x3EU); // upload landed in Z80 space
    CHECK_FALSE(system->sound_cpu.reset_line_held());

    system->sound_cpu.step_instruction(); // LD A,77
    CHECK((system->sound_cpu.cpu_registers().af >> 8U) == 0x77U);
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

TEST_CASE("m72 ROM skeleton declares the program region only", "[m72]") {
    // No sound region: the Z80 runs from the RAM the V30 fills at boot.
    const auto decl = m72_rom_skeleton("rtype");
    CHECK(decl.name == "rtype");
    REQUIRE(decl.regions.size() == 1U);
    CHECK(decl.regions[0].name == "maincpu");
    CHECK(decl.regions[0].size == mnemos::manifests::irem_m72::main_rom_size);
}

TEST_CASE("m72 pads a missing program region to open bus; sound RAM reads zero", "[m72]") {
    auto system = assemble_m72(rom_set_image{});
    CHECK(system->main_bus.read8(0x00000U) == 0xFFU);
    CHECK(system->main_bus.read8(0xFFFF0U) == 0xFFU);
    CHECK(system->sound_bus.read8(0x0000U) == 0x00U); // RAM, not ROM
    // The V30's window and the Z80's address space are the same memory.
    system->main_bus.write8(mnemos::manifests::irem_m72::sound_ram_window + 0x1234U, 0xABU);
    CHECK(system->sound_bus.read8(0x1234U) == 0xABU);
}

TEST_CASE("m72 vblank interrupts the V30 through the interrupt controller under the scheduler",
          "[m72]") {
    // Boot: program the uPD71059 (edge, single, vector base 0x20, ICW4,
    // unmasked), set up a stack, enable interrupts, halt. The one-line
    // vblank pulse on IR0 then vectors through IVT[0x20] to the handler,
    // which writes a marker into work RAM and halts again.
    auto image = make_image({
        0xB0U, 0x13U,        // MOV AL,13    (ICW1: edge, SNGL, IC4)
        0xE6U, 0x40U,        // OUT 40,AL
        0xB0U, 0x20U,        // MOV AL,20    (ICW2: vector base)
        0xE6U, 0x42U,        // OUT 42,AL
        0xB0U, 0x01U,        // MOV AL,01    (ICW4: 8086 mode)
        0xE6U, 0x42U,        // OUT 42,AL
        0xB0U, 0x00U,        // MOV AL,00    (OCW1: unmask all)
        0xE6U, 0x42U,        // OUT 42,AL
        0xB8U, 0x00U, 0xA0U, // MOV AX,A000
        0x8EU, 0xD0U,        // MOV SS,AX
        0xBCU, 0x00U, 0x10U, // MOV SP,1000
        0xFBU,               // STI
        0xF4U                // HLT
    });
    auto& main = image.regions["maincpu"];
    // IVT[0x20] -> 0040:0008 (physical 0x408).
    main[0x80U] = 0x08U;
    main[0x81U] = 0x00U;
    main[0x82U] = 0x40U;
    main[0x83U] = 0x00U;
    // Handler: MOV AX,A000; MOV DS,AX; MOV AL,99; MOV [0020],AL; HLT
    const std::vector<std::uint8_t> handler{0xB8U, 0x00U, 0xA0U, 0x8EU, 0xD8U, 0xB0U,
                                            0x99U, 0xA2U, 0x20U, 0x00U, 0xF4U};
    for (std::size_t i = 0; i < handler.size(); ++i) {
        main[0x408U + i] = handler[i];
    }

    auto system = assemble_m72(std::move(image));
    // 32 MHz master: pixel clock /4, V30 /4 (8 MHz). Video first so the
    // CPUs observe the freshly advanced beam.
    mnemos::runtime::scheduler scheduler(
        {{.chip = &system->video, .divider = 4U}, {.chip = &system->main_cpu, .divider = 4U}},
        &system->video);

    scheduler.run_frame(); // stops the master cycle the frame completes
    CHECK(system->video.frame_index() == 1U);
    scheduler.run_master_cycles(8192U); // let the handler run
    CHECK(system->work_ram[0x20U] == 0x99U);
    CHECK(system->pic.vector_base() == 0x20U);
    CHECK(system->pic.isr() == 0x01U); // IR0 in service, never EOI'd
}

TEST_CASE("m72 raster port arms the compare line and withdraws a pending request", "[m72]") {
    // OUT 06/07 byte lanes: line = (0x150 & 0x1ff) - 128 = 208.
    auto system = assemble_m72(make_image({
        0xB0U, 0x50U, // MOV AL,50
        0xE6U, 0x06U, // OUT 06,AL
        0xB0U, 0x01U, // MOV AL,01
        0xE6U, 0x07U, // OUT 07,AL
        0xF4U         // HLT
    }));
    run_until_halt(system->main_cpu, 16);
    CHECK(system->video.raster_compare_matches(208U));
    CHECK_FALSE(system->video.raster_compare_matches(207U));
}

TEST_CASE("m72 sprite DMA latches a stable copy for the renderer", "[m72]") {
    // A 16x16 sprite of solid pen 15 at the visible origin; sprite palette
    // color 0 pen 15 = full red. OUT 04 copies live sprite RAM into the
    // video unit's holding buffer -- so clearing sprite RAM AFTER the DMA
    // must not blank the rendered sprite.
    auto image = make_image({0xE6U, 0x04U, 0xF4U});   // OUT 04,AL ; HLT
    image.regions["sprites"].assign(4U * 32U, 0xFFU); // one solid cell, 4 planes
    auto system = assemble_m72(std::move(image));

    system->palette_a[15U * 2U] = 0x1FU; // pen 15: red gun full
    // Entry: y = 0x170 (384-368-16 = row 0), code 0, color 0, x = 0x140
    // (320 - 256 - 64 = column 0).
    system->sprite_ram[0] = 0x70U;
    system->sprite_ram[1] = 0x01U;
    system->sprite_ram[6] = 0x40U;
    system->sprite_ram[7] = 0x01U;

    run_until_halt(system->main_cpu, 8); // OUT 04 performs the DMA
    for (auto& byte : system->sprite_ram) {
        byte = 0U; // post-DMA clobber must not show
    }

    // One full beam pass reaches the vblank render point.
    system->video.tick(static_cast<std::uint64_t>(512U) * 284U);
    const auto view = system->video.framebuffer();
    CHECK(view.pixels[0] != 0U); // the latched sprite still renders
}

TEST_CASE("m72 IM0 jam vector ANDs the pending RST sources", "[m72]") {
    auto system = assemble_m72(rom_set_image{});

    // Idle: floating bus.
    system->sound_latch_irq = false;
    CHECK_FALSE(system->fm.irq_asserted());

    system->sound_latch_irq = true;
    system->update_sound_irq();
    // Latch only -> RST 18h.
    // (Query through the Z80's own IACK: IM 0 + EI + a pending line.)
    auto regs = system->sound_cpu.cpu_registers();
    regs.im = 0U;
    regs.iff1 = regs.iff2 = true;
    regs.sp = 0xF000U;
    regs.pc = 0x0100U;
    system->sound_cpu.set_registers(regs);
    system->sound_cpu.set_reset_line(false);
    system->sound_cpu.step_instruction(); // services the IRQ
    CHECK(system->sound_cpu.cpu_registers().pc == 0x0018U);
}

TEST_CASE("m72 Z80 programs the YM2151 through its ports and takes the timer IRQ", "[m72]") {
    auto system = assemble_m72(rom_set_image{});
    auto& sound = system->sound_ram;
    system->sound_cpu.set_reset_line(false); // as if the upload completed

    // Program CLKA = 1023 (overflow every 64 chip clocks), run + IRQ enable,
    // then IM 1; EI; HALT. The IM 1 handler at 0x38 stores a marker.
    const std::vector<std::uint8_t> program{
        0x3EU, 0x10U, 0xD3U, 0x00U, // LD A,10; OUT (0),A   (address CLKA hi)
        0x3EU, 0xFFU, 0xD3U, 0x01U, // LD A,FF; OUT (1),A
        0x3EU, 0x11U, 0xD3U, 0x00U, // LD A,11; OUT (0),A   (address CLKA lo)
        0x3EU, 0x03U, 0xD3U, 0x01U, // LD A,03; OUT (1),A   (CLKA = 1023)
        0x3EU, 0x14U, 0xD3U, 0x00U, // LD A,14; OUT (0),A   (timer control)
        0x3EU, 0x05U, 0xD3U, 0x01U, // LD A,05; OUT (1),A   (run A + IRQ enable)
        0xEDU, 0x56U,               // IM 1
        0x31U, 0x00U, 0xF8U,        // LD SP,F800 (stack in sound RAM)
        0xFBU,                      // EI
        0x76U,                      // HALT
    };
    for (std::size_t i = 0; i < program.size(); ++i) {
        sound[i] = program[i];
    }
    // IM 1 vector: LD A,99; LD (F900),A; HALT
    const std::vector<std::uint8_t> handler{0x3EU, 0x99U, 0x32U, 0x00U, 0xF9U, 0x76U};
    for (std::size_t i = 0; i < handler.size(); ++i) {
        sound[0x38U + i] = handler[i];
    }

    for (int i = 0; i < 16; ++i) {
        system->sound_cpu.step_instruction(); // runs to the HALT
    }
    CHECK(system->fm.register_value(0x14U) == 0x05U);
    CHECK_FALSE(system->sound_cpu.cpu_registers().halted == false);

    system->fm.tick(64U); // timer A overflows -> IRQ -> Z80 INT line
    CHECK(system->fm.irq_asserted());
    for (int i = 0; i < 4; ++i) {
        system->sound_cpu.step_instruction(); // INT accept + handler
    }
    CHECK(system->sound_bus.read8(0xF900U) == 0x99U);
}

TEST_CASE("m72 sound latch and YM2151 IRQs OR onto the Z80 INT line", "[m72]") {
    auto system = assemble_m72(rom_set_image{});

    // Assert both sources, then clear them one at a time: the line drops only
    // after BOTH are acknowledged.
    system->sound_latch_irq = true;
    system->update_sound_irq();
    system->fm.write_address(0x10U);
    system->fm.write_data(0xFFU);
    system->fm.write_address(0x11U);
    system->fm.write_data(0x03U);
    system->fm.write_address(0x14U);
    system->fm.write_data(0x05U);
    system->fm.tick(64U);
    REQUIRE(system->fm.irq_asserted());

    system->sound_latch_irq = false; // latch acknowledged
    system->update_sound_irq();
    // The YM2151 still holds the line: clearing its flag drops it.
    system->fm.write_address(0x14U);
    system->fm.write_data(0x15U); // reset flag A
    CHECK_FALSE(system->fm.irq_asserted());
}

TEST_CASE("m72 Z80 streams sample bytes from the sample ROM into the DAC", "[m72]") {
    rom_set_image image;
    image.regions["samples"] = {0x10U, 0x20U, 0x30U, 0x40U, 0x90U, 0xA0U};
    auto system = assemble_m72(std::move(image));
    auto& sound = system->sound_ram;
    system->sound_cpu.set_reset_line(false); // as if the upload completed

    // Set the sample cursor to 4, then stream two bytes to the DAC.
    const std::vector<std::uint8_t> program{
        0x3EU, 0x04U, 0xD3U, 0x80U, // LD A,04; OUT (80),A  (address low)
        0x3EU, 0x00U, 0xD3U, 0x81U, // LD A,00; OUT (81),A  (address high)
        0xDBU, 0x84U, 0xD3U, 0x82U, // IN A,(84); OUT (82),A
        0xDBU, 0x84U, 0xD3U, 0x82U, // IN A,(84); OUT (82),A
        0x76U,                      // HALT
    };
    for (std::size_t i = 0; i < program.size(); ++i) {
        sound[i] = program[i];
    }

    for (int i = 0; i < 6; ++i) {
        system->sound_cpu.step_instruction(); // through the first IN/OUT pair
    }
    CHECK(system->dac.level() == 0x90U); // first streamed byte
    for (int i = 0; i < 2; ++i) {
        system->sound_cpu.step_instruction(); // second IN/OUT pair
    }
    CHECK(system->dac.level() == 0xA0U); // cursor auto-incremented
    CHECK(system->sample_address == 6U);
    CHECK(system->dac.output() == (0xA0 - 0x80) * 64);
}

TEST_CASE("m72 protection MCU answers the V30 through the latch pair", "[m72]") {
    // MCU program: poll the main->MCU latch, reply with value+1.
    //   loop: MOV DPTR,#E000; MOVX A,@DPTR; ADD A,#1;
    //         MOV DPTR,#E001; MOVX @DPTR,A; SJMP loop
    rom_set_image image;
    image.regions["mcu"] = {
        0x90U, 0xE0U, 0x00U, // MOV DPTR,#E000
        0xE0U,               // MOVX A,@DPTR
        0x24U, 0x01U,        // ADD A,#1
        0x90U, 0xE0U, 0x01U, // MOV DPTR,#E001
        0xF0U,               // MOVX @DPTR,A
        0x80U, 0xF4U,        // SJMP loop
    };
    auto& main = image.regions["maincpu"];
    main.assign(mnemos::manifests::irem_m72::main_rom_size, 0xFFU);
    main[0xFFFF0U] = 0xEAU; // JMP 0000:0200
    main[0xFFFF1U] = 0x00U;
    main[0xFFFF2U] = 0x02U;
    main[0xFFFF3U] = 0x00U;
    main[0xFFFF4U] = 0x00U;
    // MOV AL,41; OUT C0,AL (latch + INT1 knock); HLT
    const std::vector<std::uint8_t> program{0xB0U, 0x41U, 0xE6U, 0xC0U, 0xF4U};
    for (std::size_t i = 0; i < program.size(); ++i) {
        main[0x200U + i] = program[i];
    }

    auto system = assemble_m72(std::move(image));
    REQUIRE(system->mcu_present);

    run_until_halt(system->main_cpu, 8);
    CHECK(system->main_to_mcu == 0x41U);

    system->mcu.tick(64U); // a few echo-loop iterations
    CHECK(system->mcu_to_main == 0x42U);

    // The MCU's polling loop keeps tracking new latch values.
    system->main_to_mcu = 0x10U;
    system->mcu.tick(64U);
    CHECK(system->mcu_to_main == 0x11U);
}

TEST_CASE("m72 without an mcu region schedules no MCU", "[m72]") {
    auto system = assemble_m72(rom_set_image{});
    CHECK_FALSE(system->mcu_present);
}
