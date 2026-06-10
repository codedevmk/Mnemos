#include "sega32x_adapter.hpp"

#include "adapter_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

    using mnemos::apps::player::adapters::sega32x::sega32x_adapter;
    using mnemos::manifests::sega32x::sega32x_bios;

    // A minimal Genesis cartridge: valid 68000 reset vectors and a self-branch
    // idle loop, so frames advance deterministically without a BIOS.
    std::vector<std::uint8_t> make_cart() {
        std::vector<std::uint8_t> cart(0x10000, 0);
        cart[0] = 0x00;
        cart[1] = 0xFF; // SSP = $00FF0000
        cart[6] = 0x02; // PC  = $00000200
        cart[0x200] = 0x60;
        cart[0x201] = 0xFE; // BRA.B * idle loop
        return cart;
    }

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in test data paths
#endif
    std::optional<std::vector<std::uint8_t>> read_file(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>{});
    }

    std::optional<const char*> env(const char* name) {
        const char* v = std::getenv(name);
        if (v == nullptr || v[0] == '\0') {
            return std::nullopt;
        }
        return v;
    }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace

TEST_CASE("sega32x_adapter steps frames and passes the Genesis picture through",
          "[sega32x][adapter]") {
    sega32x_adapter adapter{make_cart()};

    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 2U);

    // With the 32X bitmap mode off, the composed frame mirrors the Genesis
    // frame (same geometry, same pixels).
    const auto composed = adapter.current_frame();
    const auto gen = adapter.machine().genesis->vdp.framebuffer();
    REQUIRE(composed.width == gen.width);
    REQUIRE(composed.height == gen.height);
    bool identical = true;
    for (std::uint32_t y = 0; y < gen.height && identical; ++y) {
        const auto* a = composed.pixels + static_cast<std::size_t>(y) * composed.effective_stride();
        const auto* b = gen.pixels + static_cast<std::size_t>(y) * gen.effective_stride();
        for (std::uint32_t x = 0; x < gen.width; ++x) {
            if (a[x] != b[x]) {
                identical = false;
                break;
            }
        }
    }
    CHECK(identical);
}

TEST_CASE("sega32x_adapter overlays 32X pixels onto the composed frame", "[sega32x][adapter]") {
    sega32x_adapter adapter{make_cart()};
    auto& tx = *adapter.machine().sega32x;

    // Packed mode, palette index 1 = priority white, one pixel at row 0 col 0.
    // FS = 0 displays bank 1; the line table at the bank start points row 0 at
    // word $0100 (byte $200) within the bank.
    using vdp_chip = mnemos::chips::video::sega32x_vdp;
    tx.vdp.write16(vdp_chip::reg_bitmap_mode, vdp_chip::mode_packed);
    tx.vdp.palette_write16(1U * 2U, 0xFFFFU); // priority + white (5:5:5 all on)
    tx.framebuffer[0x20000] = 0x01U;
    tx.framebuffer[0x20001] = 0x00U;
    tx.framebuffer[0x20200] = 1U;

    adapter.step_one_frame();
    const auto composed = adapter.current_frame();
    CHECK(composed.pixels[0] == 0x00FFFFFFU);
    CHECK(composed.pixels[1] == 0x00000000U); // index 0 stays transparent
}

TEST_CASE("sega32x_adapter mixes PWM audio into the output", "[sega32x][adapter]") {
    sega32x_adapter adapter{make_cart()};
    auto& tx = *adapter.machine().sega32x;

    // Release the SH-2s and program a PWM carrier so step_pwm queues samples.
    adapter.machine().genesis->bus.write8(0xA15101U, 0x03U);
    tx.master_bus.write8(0x00004033U, 0x20U); // CYCLE = 32
    tx.master_bus.write8(0x00004038U, 0x00U); // MONO duty 24 (above half)
    tx.master_bus.write8(0x00004039U, 0x18U);

    adapter.step_one_frame();
    const auto chunk = adapter.drain_audio();
    CHECK(chunk.frame_count > 0U);
}

TEST_CASE("sega32x_adapter boots a real cartridge through the 32X BIOS handshake",
          "[sega32x][adapter][rom]") {
    const auto bios_dir = env("MNEMOS_32X_BIOS_DIR");
    const auto rom_path = env("MNEMOS_32X_ROM");
    if (!bios_dir || !rom_path) {
        SKIP("MNEMOS_32X_BIOS_DIR / MNEMOS_32X_ROM unset; data-gated boot check skipped");
    }
    const std::string dir{*bios_dir};
    auto m_bios = read_file(dir + "/32X_M_BIOS.bin");
    auto s_bios = read_file(dir + "/32X_S_BIOS.bin");
    auto g_bios = read_file(dir + "/32X_G_BIOS.bin");
    auto cart = read_file(*rom_path);
    REQUIRE(m_bios.has_value());
    REQUIRE(s_bios.has_value());
    REQUIRE(g_bios.has_value());
    REQUIRE((cart.has_value() && !cart->empty()));

    // Construct through the adapter registry -- the same path the player takes.
    std::vector<std::vector<std::uint8_t>> bios_images;
    bios_images.push_back(std::move(*m_bios));
    bios_images.push_back(std::move(*s_bios));
    bios_images.push_back(std::move(*g_bios));
    auto system = mnemos::frontend_sdk::adapter_registry::instance().create(
        "sega32x", {.rom = std::move(*cart), .bios_images = std::move(bios_images)});
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<sega32x_adapter&>(*system);

    for (int i = 0; i < 120; ++i) {
        adapter.step_one_frame();
    }

    auto& tx = *adapter.machine().sega32x;
    INFO("master pc = " << std::hex << tx.master_cpu.cpu_registers().pc);
    INFO("slave  pc = " << std::hex << tx.slave_cpu.cpu_registers().pc);
    // The 68000 boot code must have enabled the adapter and released the SH-2s.
    CHECK_FALSE(tx.sh2_reset_asserted);
    CHECK(tx.master_cpu.elapsed_cycles() > 0U);
    CHECK(tx.slave_cpu.elapsed_cycles() > 0U);
    // After the boot handshake both SH-2s leave their 2 KiB boot ROMs (game
    // code runs from SDRAM or the cart windows).
    CHECK(tx.master_cpu.cpu_registers().pc >= 0x00004000U);
    // The handshake completed: game code enabled its 32X interrupts and the
    // master loaded data into SDRAM.
    CHECK(tx.master_irq_mask != 0U);
    bool sdram_touched = false;
    for (const std::uint8_t b : tx.sdram) {
        if (b != 0U) {
            sdram_touched = true;
            break;
        }
    }
    CHECK(sdram_touched);
}
