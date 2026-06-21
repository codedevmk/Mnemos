// NES player-adapter smoke test: construct from a synthetic NROM, step frames,
// and confirm the adapter advertises its chips and a 256x240 framebuffer. The
// real-game render is a manual / data-gated check (no .nes in the local corpus).

#include "nes_adapter.hpp"

#include "ppu2c02.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace {
    using mnemos::apps::player::adapters::nes::nes_adapter;
    using ppu = mnemos::chips::video::ppu2c02;

    // Minimal NROM (header + 16 KiB PRG + 8 KiB CHR); PRG spins, reset -> $8000.
    std::vector<std::uint8_t> tiny_nrom() {
        std::vector<std::uint8_t> rom(16U + 0x4000U + 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 1U;
        rom[5] = 1U;
        rom[16 + 0] = 0x4CU; // JMP $8000
        rom[16 + 1] = 0x00U;
        rom[16 + 2] = 0x80U;
        rom[16 + 0x3FFCU] = 0x00U; // reset vector -> $8000
        rom[16 + 0x3FFDU] = 0x80U;
        return rom;
    }
} // namespace

namespace {
    // An NROM whose 6502 boot programs the PPU to draw the top 16 character rows
    // (128 px) as solid white over a black backdrop: wait two vblanks, write the
    // palette ($3F00 = black, $3F01 = white), fill 512 nametable cells with the
    // solid tile 1, reset scroll, enable background. Exercises CPU -> $2000-$3FFF
    // MMIO -> PPU render end-to-end without a real ROM.
    std::vector<std::uint8_t> render_test_nrom() {
        std::vector<std::uint8_t> code = {
            0x78, 0xD8, 0xA2, 0xFF, 0x9A,                   // SEI / CLD / LDX #$FF / TXS
            0xA9, 0x00, 0x8D, 0x00, 0x20, 0x8D, 0x01, 0x20, // disable NMI + rendering
            0x2C, 0x02, 0x20, 0x10, 0xFB,                   // vbl1: BIT $2002 / BPL
            0x2C, 0x02, 0x20, 0x10, 0xFB,                   // vbl2: BIT $2002 / BPL
            0xA9, 0x3F, 0x8D, 0x06, 0x20, 0xA9, 0x00, 0x8D, 0x06, 0x20, // PPUADDR=$3F00
            0xA9, 0x0F, 0x8D, 0x07, 0x20,                               // palette[0] = $0F (black)
            0xA9, 0x30, 0x8D, 0x07, 0x20,                               // palette[1] = $30 (white)
            0xA9, 0x20, 0x8D, 0x06, 0x20, 0xA9, 0x00, 0x8D, 0x06, 0x20, // PPUADDR=$2000
            0xA9, 0x01,                                                 // LDA #$01 (solid tile)
            0xA2, 0x00, 0x8D, 0x07, 0x20, 0xCA, 0xD0, 0xFA,             // l1: 256x STA $2007
            0xA2, 0x00, 0x8D, 0x07, 0x20, 0xCA, 0xD0, 0xFA,             // l2: 256x STA $2007
            0xA9, 0x00, 0x8D, 0x05, 0x20, 0x8D, 0x05, 0x20, 0x8D, 0x00, 0x20, // scroll 0; ctrl 0
            0xA9, 0x0A, 0x8D, 0x01, 0x20, // PPUMASK = BG enable + left column
        };
        const std::uint16_t loop = static_cast<std::uint16_t>(0x8000U + code.size());
        code.push_back(0x4CU); // JMP loop (self)
        code.push_back(static_cast<std::uint8_t>(loop & 0xFFU));
        code.push_back(static_cast<std::uint8_t>(loop >> 8U));

        std::vector<std::uint8_t> prg(0x4000U, 0x00U);
        for (std::size_t i = 0; i < code.size(); ++i) {
            prg[i] = code[i];
        }
        prg[0x3FFCU] = 0x00U; // reset vector -> $8000
        prg[0x3FFDU] = 0x80U;

        std::vector<std::uint8_t> chr(0x2000U, 0x00U);
        for (int i = 0; i < 8; ++i) {
            chr[16 + i] = 0xFFU; // tile 1 plane 0 solid -> pixel value 1 everywhere
        }

        std::vector<std::uint8_t> rom = {'N', 'E', 'S', 0x1AU, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        rom.insert(rom.end(), prg.begin(), prg.end());
        rom.insert(rom.end(), chr.begin(), chr.end());
        return rom;
    }
} // namespace

TEST_CASE("nes adapter boots a program and renders the background", "[apps][player][nes]") {
    nes_adapter adapter(render_test_nrom());
    for (int i = 0; i < 16; ++i) {
        adapter.step_one_frame();
    }
    const auto fb = adapter.current_frame();
    constexpr std::uint32_t white = 0x00FFFFFFU;
    constexpr std::uint32_t black = 0x00000000U;
    const std::uint32_t w = fb.width;
    // Top rows are the solid-white band; the lower screen is the black backdrop.
    CHECK(fb.pixels[10U * w + 10U] == white);
    CHECK(fb.pixels[200U * w + 10U] == black);
}

TEST_CASE("nes adapter constructs and steps frames", "[apps][player][nes]") {
    nes_adapter adapter(tiny_nrom());

    const auto fb0 = adapter.current_frame();
    CHECK(fb0.width == ppu::visible_width);
    CHECK(fb0.height == ppu::visible_height);

    const std::uint64_t before = adapter.system().ppu.frame_index();
    adapter.step_one_frame();
    adapter.step_one_frame();

    // PPU (frame source), CPU, APU.
    CHECK(adapter.chips().size() == 3U);
    CHECK(adapter.current_frame().pixels != nullptr);
    CHECK(adapter.system().ppu.frame_index() > before); // the PPU advanced frames
}

TEST_CASE("nes adapter maps the pad to the controller port", "[apps][player][nes]") {
    nes_adapter adapter(tiny_nrom());
    mnemos::frontend_sdk::controller_state st{};
    st.a = true;
    st.start = true;
    adapter.apply_input(0, st);

    auto& sys = adapter.system();
    sys.bus.write8(0x4016U, 0x01U); // strobe
    sys.bus.write8(0x4016U, 0x00U);
    // Serial order A, B, Select, Start: A and Start pressed, B and Select not.
    CHECK((sys.bus.read8(0x4016U) & 0x01U) == 0x01U); // A
    CHECK((sys.bus.read8(0x4016U) & 0x01U) == 0x00U); // B
    CHECK((sys.bus.read8(0x4016U) & 0x01U) == 0x00U); // Select
    CHECK((sys.bus.read8(0x4016U) & 0x01U) == 0x01U); // Start
}

TEST_CASE("nes adapter Zapper reports light sense and trigger", "[apps][player][nes]") {
    // render_test_nrom draws a white band over the top 128 px, black below.
    nes_adapter adapter(render_test_nrom(), mnemos::manifests::nes::nes_config{.zapper = true});
    for (int i = 0; i < 16; ++i) {
        adapter.step_one_frame();
    }
    auto& sys = adapter.system();

    // Aim at the white band with the trigger pulled: light detected (bit 3 = 0),
    // trigger (bit 4 = 1).
    mnemos::frontend_sdk::controller_state gun{};
    gun.aim_x = 10;
    gun.aim_y = 10;
    gun.trigger = true;
    adapter.apply_input(1, gun);
    std::uint8_t v = sys.bus.read8(0x4017U);
    CHECK((v & 0x08U) == 0U); // light detected
    CHECK((v & 0x10U) != 0U); // trigger pulled

    // Aim at the black backdrop, trigger released: no light (bit 3 = 1).
    gun.aim_y = 200;
    gun.trigger = false;
    adapter.apply_input(1, gun);
    v = sys.bus.read8(0x4017U);
    CHECK((v & 0x08U) != 0U); // no light
    CHECK((v & 0x10U) == 0U); // no trigger

    // Off-screen aim sees no light.
    gun.aim_x = -1;
    gun.aim_y = -1;
    adapter.apply_input(1, gun);
    CHECK((sys.bus.read8(0x4017U) & 0x08U) != 0U);
}

TEST_CASE("nes adapter advertises a lightgun port only with the Zapper enabled",
          "[apps][player][nes]") {
    namespace fsdk = mnemos::frontend_sdk;

    // Zapper plugged in: port 1 is advertised as a light gun.
    nes_adapter gun(tiny_nrom(), mnemos::manifests::nes::nes_config{.zapper = true});
    const auto& gun_ports = gun.session_capabilities().input_ports;
    REQUIRE(gun_ports.size() >= 2U);
    CHECK(gun_ports[0].port_index == 0U);
    CHECK(gun_ports[0].format == fsdk::input_device_format::digital_pad);
    CHECK(gun_ports[1].port_index == 1U);
    CHECK(gun_ports[1].format == fsdk::input_device_format::lightgun);

    // No Zapper: that same port is a plain pad.
    nes_adapter pads(tiny_nrom());
    const auto& pad_ports = pads.session_capabilities().input_ports;
    REQUIRE(pad_ports.size() >= 2U);
    CHECK(pad_ports[1].port_index == 1U);
    CHECK(pad_ports[1].format == fsdk::input_device_format::digital_pad);
}

TEST_CASE("nes adapter runs PAL timing and reports 50 Hz", "[apps][player][nes]") {
    nes_adapter adapter(
        tiny_nrom(), mnemos::manifests::nes::nes_config{.video_region = mnemos::video_region::pal});
    CHECK(adapter.region().frames_per_second_x1000 == 50000U);
    // The PAL schedule uses a rational (16:5) CPU/APU rate -- stepping must work.
    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.system().ppu.frame_index() > 0U);
}

TEST_CASE("nes adapter exposes battery RAM only for battery carts", "[apps][player][nes]") {
    // A plain cart (no battery flag) persists nothing.
    nes_adapter plain(tiny_nrom());
    CHECK(plain.battery_ram().empty());

    // A battery cart (flags6 bit1) exposes its 8 KiB $6000-$7FFF RAM, and writes
    // through the bus land in the persisted span.
    auto rom = tiny_nrom();
    rom[6] |= 0x02U;
    nes_adapter battery(std::move(rom));
    REQUIRE(battery.battery_ram().size() == 0x2000U);
    battery.system().bus.write8(0x6000U, 0x99U);
    CHECK(battery.battery_ram()[0] == 0x99U);
}

TEST_CASE("nes adapter whole-machine save-state round-trips", "[apps][player][nes]") {
    namespace nes = mnemos::apps::player::adapters::nes;
    nes_adapter a(render_test_nrom());
    for (int i = 0; i < 8; ++i) {
        a.step_one_frame();
    }
    a.system().wram[0x100] = 0xABU; // mark work RAM after running

    const mnemos::runtime::save_target ta = nes::build_save_target(a.system());
    const std::vector<std::uint8_t> blob = mnemos::runtime::write_save_state(ta);
    REQUIRE(!blob.empty());

    // Restore into a fresh machine and confirm the captured state came back.
    nes_adapter b(render_test_nrom());
    CHECK(b.system().wram[0x100] != 0xABU);
    mnemos::runtime::save_target tb = nes::build_save_target(b.system());
    const mnemos::runtime::load_result result = mnemos::runtime::read_save_state(blob, tb);
    CHECK(result.ok());
    CHECK(b.system().wram[0x100] == 0xABU);
}

TEST_CASE("nes adapter drains APU audio after a frame", "[apps][player][nes]") {
    nes_adapter adapter(tiny_nrom());
    auto& sys = adapter.system();

    // Program pulse 1 through the bus, then run a frame so the scheduler ticks the
    // APU (capture is enabled in the adapter ctor) and drain the mixed output.
    sys.bus.write8(0x4015U, 0x01U);
    sys.bus.write8(0x4000U, 0xBFU);
    sys.bus.write8(0x4002U, 0xFFU);
    sys.bus.write8(0x4003U, 0x08U);

    adapter.step_one_frame();
    const auto chunk = adapter.drain_audio();
    REQUIRE(chunk.frame_count > 0U);
    CHECK(chunk.sample_rate > 0U);

    std::int16_t peak = 0;
    for (std::uint32_t i = 0; i < chunk.frame_count * 2U; ++i) {
        peak = std::max(peak, static_cast<std::int16_t>(std::abs(chunk.samples[i])));
    }
    CHECK(peak > 1000); // resampled APU output is audible
}
