// Adapter smoke. The Genesis player_system adapter is a thin shell over
// assemble_genesis() + a scheduler; this test boots it on a minimal ROM
// that just spins in place, steps a few frames, and checks the
// player_system contract holds.

#include "genesis_adapter.hpp"

#include "battery_save.hpp"
#include "bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

    using mnemos::apps::player::adapters::genesis::genesis_adapter;

    // Minimal 68000 boot: SSP, PC at $0008, MOVE.W #$2000,SR then BRA.S *
    // (the system just powers on and spins; the VDP still raster-times frames).
    std::vector<std::uint8_t> tiny_rom() {
        std::vector<std::uint8_t> rom(0x100, 0x00);
        const auto w16 = [&](std::size_t off, std::uint16_t v) {
            rom[off] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 1U] = static_cast<std::uint8_t>(v);
        };
        const auto w32 = [&](std::size_t off, std::uint32_t v) {
            rom[off + 0U] = static_cast<std::uint8_t>(v >> 24U);
            rom[off + 1U] = static_cast<std::uint8_t>(v >> 16U);
            rom[off + 2U] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 3U] = static_cast<std::uint8_t>(v);
        };
        w32(0x00, 0x00FFF000U); // SSP
        w32(0x04, 0x00000008U); // PC
        w16(0x08, 0x46FC);
        w16(0x0A, 0x2000); // MOVE.W #$2000,SR
        w16(0x0C, 0x60FE); // BRA.S *
        return rom;
    }

    // tiny_rom() extended with an "RA" external-RAM header declaring odd-byte SRAM
    // at $200001-$203FFF -- above the (tiny) ROM, so it powers on mapped.
    std::vector<std::uint8_t> sram_rom() {
        auto rom = tiny_rom();
        rom.resize(0x200U, 0x00U); // room for the $1B0 header block
        rom[0x1B0U] = 'R';
        rom[0x1B1U] = 'A';
        rom[0x1B4U] = 0x00U;
        rom[0x1B5U] = 0x20U;
        rom[0x1B6U] = 0x00U;
        rom[0x1B7U] = 0x01U; // start $200001
        rom[0x1B8U] = 0x00U;
        rom[0x1B9U] = 0x20U;
        rom[0x1BAU] = 0x3FU;
        rom[0x1BBU] = 0xFFU; // end $203FFF
        return rom;
    }

} // namespace

TEST_CASE("genesis_adapter advances frames and reports region") {
    genesis_adapter adapter(tiny_rom(), {.video_region = mnemos::video_region::ntsc});

    CHECK(adapter.region().frames_per_second_x1000 == 60000U);
    CHECK(adapter.frames_stepped() == 0U);

    const auto fb_before = adapter.current_frame();
    REQUIRE(fb_before.width > 0U);
    REQUIRE(fb_before.height > 0U);

    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 2U);

    // current_frame() must remain accessible after stepping.
    const auto fb_after = adapter.current_frame();
    CHECK(fb_after.width == fb_before.width);
    CHECK(fb_after.height == fb_before.height);
}

TEST_CASE("genesis_adapter publishes work_ram and z80_ram as system memory views") {
    genesis_adapter adapter(tiny_rom());

    // Resolve by name so the check is order-independent and catches a manifest
    // region-id mismatch (an empty work_ram span) or a dropped/renamed entry --
    // failures the system-agnostic debug_dump test cannot see.
    std::size_t work_ram_bytes = 0U;
    std::size_t z80_ram_bytes = 0U;
    bool saw_work_ram = false;
    bool saw_z80_ram = false;
    for (const auto* mv : adapter.memory_views()) {
        REQUIRE(mv != nullptr);
        if (mv->name() == "work_ram") {
            saw_work_ram = true;
            work_ram_bytes = mv->bytes().size();
        } else if (mv->name() == "z80_ram") {
            saw_z80_ram = true;
            z80_ram_bytes = mv->bytes().size();
        }
    }
    REQUIRE(saw_work_ram);
    REQUIRE(saw_z80_ram);
    CHECK(work_ram_bytes == 0x10000U); // 64 KiB 68K work RAM
    CHECK(z80_ram_bytes == 0x2000U);   // 8 KiB Z80 RAM
}

TEST_CASE("genesis_adapter ignores out-of-range input ports") {
    genesis_adapter adapter(tiny_rom());
    mnemos::frontend_sdk::controller_state pad{};
    pad.start = true;
    pad.a = true;
    adapter.apply_input(0, pad);
    adapter.apply_input(7, pad);  // ignored
    adapter.apply_input(-1, pad); // ignored
    // No throw, no UB; in-range input was recorded (port 0).
    SUCCEED();
}

TEST_CASE("genesis_adapter drain_audio resamples to 48 kHz output") {
    genesis_adapter adapter(tiny_rom());
    // Before stepping, no samples queued but the adapter still reports the
    // fixed 48 kHz output rate so the player can open SDL_AudioStream upfront.
    auto audio = adapter.drain_audio();
    CHECK(audio.frame_count == 0U);
    CHECK(audio.sample_rate == 48000U);

    // Stepping one NTSC frame produces a frame's-worth of 48 kHz samples
    // (48000 / 60 = 800; allow +-2 for fractional accumulator rounding).
    adapter.step_one_frame();
    audio = adapter.drain_audio();
    CHECK(audio.sample_rate == 48000U);
    CHECK(audio.frame_count >= 798U);
    CHECK(audio.frame_count <= 802U);
    REQUIRE(audio.samples != nullptr);

    // Second drain of the same frame returns nothing (chip queues consumed).
    audio = adapter.drain_audio();
    CHECK(audio.frame_count == 0U);
}

TEST_CASE("genesis_adapter selects PAL pacing when configured") {
    genesis_adapter adapter(tiny_rom(), {.video_region = mnemos::video_region::pal});
    CHECK(adapter.region().frames_per_second_x1000 == 50000U);
}

// Per-pad protocol tests (3-button MK-1650 + 6-button MK-1653) now live with
// each device under src/peripheral_sdk/input/tests/. The adapter-level test below
// just verifies controller_state -> pad-bits routing end-to-end.

TEST_CASE("genesis_adapter routes controller_state into the attached pad") {
    genesis_adapter adapter(tiny_rom());

    mnemos::frontend_sdk::controller_state combo{};
    combo.right = true;
    combo.a = true;
    adapter.apply_input(0, combo);

    // Default attachment is a 6-button pad (MK-1653) at phase 0; the next
    // CPU write of TH=1 advances to phase 1, where the read byte exposes
    // CBRLDU. With Right + A pressed at TH=1, Right clears bit 3.
    auto* dev = adapter.system().port_device(0);
    REQUIRE(dev != nullptr);
    dev->write_data(0x40U); // TH = 1 -> phase 1
    // Bits: 0=U(1), 1=D(1), 2=L(1), 3=R(0 pressed), 4=B(1), 5=C(1), 6=TH=1.
    CHECK(dev->read_data() == 0x77U);
}

TEST_CASE("genesis_adapter exposes no battery_ram for a cart without a save chip") {
    genesis_adapter adapter(tiny_rom());
    CHECK(adapter.battery_ram().empty());
}

TEST_CASE("genesis_adapter battery_ram persists through a .srm round-trip") {
    using mnemos::apps::player::adapters::load_battery_ram;
    using mnemos::apps::player::adapters::save_battery_ram;

    const auto path =
        (std::filesystem::temp_directory_path() / "mnemos_genesis_adapter_test.srm").string();
    std::filesystem::remove(path);

    // Session 1: write a pattern into SRAM through the live bus, then save it.
    {
        genesis_adapter adapter(sram_rom());
        REQUIRE_FALSE(adapter.battery_ram().empty()); // cart declares SRAM
        auto* bus = adapter.system().state.main_bus;
        bus->write8(0x200001U, 0xCAU);
        bus->write8(0x200003U, 0xFEU);
        REQUIRE(save_battery_ram(path, adapter.battery_ram()));
    }
    // Session 2: a fresh boot powers SRAM to 0xFF; loading the .srm restores it.
    {
        genesis_adapter adapter(sram_rom());
        auto* bus = adapter.system().state.main_bus;
        CHECK(bus->read8(0x200001U) == 0xFFU); // nothing loaded yet
        REQUIRE(load_battery_ram(path, adapter.battery_ram()));
        CHECK(bus->read8(0x200001U) == 0xCAU); // restored from disk
        CHECK(bus->read8(0x200003U) == 0xFEU);
    }
    std::filesystem::remove(path);
}

// Cart-byte -> video_region resolution is now exercised end-to-end in the
// shared cross-family test in adapters/common/tests/region_test.cpp:
// parse_genesis_market() + default_video_for() compose into the same flow
// the player does; the adapter just passes the resolved video_region through.
