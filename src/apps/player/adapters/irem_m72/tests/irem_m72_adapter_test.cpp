#include "irem_m72_adapter.hpp"

#include "adapter_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

    using mnemos::apps::player::adapters::irem_m72::irem_m72_adapter;

    // A bare V30 program image: far jump at the reset vector into a handler
    // that writes a marker into work RAM and halts.
    [[nodiscard]] std::vector<std::uint8_t> make_program() {
        std::vector<std::uint8_t> rom(mnemos::manifests::irem_m72::main_rom_size, 0xFFU);
        rom[0xFFFF0U] = 0xEAU; // JMP 0000:0200
        rom[0xFFFF1U] = 0x00U;
        rom[0xFFFF2U] = 0x02U;
        rom[0xFFFF3U] = 0x00U;
        rom[0xFFFF4U] = 0x00U;
        const std::vector<std::uint8_t> program{
            0xB8U, 0x00U, 0xE0U, // MOV AX,E000
            0x8EU, 0xD8U,        // MOV DS,AX
            0xB0U, 0x42U,        // MOV AL,42
            0xA2U, 0x00U, 0x00U, // MOV [0000],AL
            0xF4U,               // HLT
        };
        for (std::size_t i = 0; i < program.size(); ++i) {
            rom[0x200U + i] = program[i];
        }
        return rom;
    }

} // namespace

TEST_CASE("irem_m72_adapter boots a bare program through the registry", "[irem_m72][adapter]") {
    mnemos::frontend_sdk::adapter_options options{};
    options.rom = make_program();
    options.display_name = "smoke";
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 2U);
    CHECK(adapter.machine().video.frame_index() == 2U);
    CHECK(adapter.machine().work_ram[0] == 0x42U);

    const auto frame = adapter.current_frame();
    CHECK(frame.width == 384U);
    CHECK(frame.height == 256U);
    CHECK(adapter.region().frames_per_second_x1000 == 55018U);
    CHECK(adapter.chips().size() == 3U);
    CHECK(adapter.system_spec().size() == 3U);
    CHECK(adapter.system_spec()[1].value == "Irem M72");
}

TEST_CASE("irem_m72_adapter maps pads onto the board's input bytes", "[irem_m72][adapter]") {
    irem_m72_adapter adapter(make_program());

    mnemos::frontend_sdk::controller_state p1{};
    p1.up = true;
    p1.a = true;
    p1.start = true;
    p1.select = true; // coin 1
    adapter.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.right = true;
    p2.start = true;
    adapter.apply_input(1, p2);

    auto& machine = adapter.machine();
    CHECK(machine.input_p1 == static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x10U));
    CHECK(machine.input_p2 == static_cast<std::uint8_t>(0xFFU & ~0x08U));
    // coin1 (bit0) + start1 (bit2) + start2 (bit3) held low.
    CHECK(machine.input_system == static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x04U & ~0x08U));

    adapter.apply_input(2, p1); // out-of-range port ignored
    CHECK(machine.input_p2 == static_cast<std::uint8_t>(0xFFU & ~0x08U));
}

TEST_CASE("irem_m72_adapter drains YM2151-clocked audio frames", "[irem_m72][adapter]") {
    irem_m72_adapter adapter(make_program());
    adapter.step_one_frame();

    const auto chunk = adapter.drain_audio();
    // The power-on frame completes at the vblank line (256 of 284 lines in),
    // so the first drain is ~916 stereo frames; steady-state frames yield
    // ~1016 (581632 master cycles -> ~65062 YM2151 clocks, one frame per 64).
    CHECK(chunk.frame_count > 900U);
    CHECK(chunk.frame_count < 1050U);
    CHECK(chunk.sample_rate == 55930U);
    REQUIRE(chunk.samples != nullptr);
    for (std::uint32_t i = 0; i < chunk.frame_count * 2U; ++i) {
        if (chunk.samples[i] != 0) {
            FAIL("expected silence with no notes keyed on");
        }
    }

    // Nothing stepped since the last drain: nothing further is due.
    CHECK(adapter.drain_audio().frame_count == 0U);
}
