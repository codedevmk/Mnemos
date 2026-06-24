#include "amiga500_adapter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    using mnemos::apps::player::adapters::amiga500::amiga500_adapter;
    using mnemos::manifests::amiga500::amiga500_system;

    [[nodiscard]] std::optional<std::string> get_env(const char* name) {
#ifdef _WIN32
        char* buf = nullptr;
        std::size_t len = 0U;
        if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) {
            return std::nullopt;
        }
        std::string value(buf);
        std::free(buf);
        if (value.empty()) {
            return std::nullopt;
        }
        return value;
#else
        const char* value = std::getenv(name);
        if (value == nullptr || *value == '\0') {
            return std::nullopt;
        }
        return std::string(value);
#endif
    }

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> read_file(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
    }

    [[nodiscard]] std::uint64_t get_env_u64(const char* name, std::uint64_t fallback) {
        const auto value = get_env(name);
        if (!value) {
            return fallback;
        }
        const auto parsed = std::strtoull(value->c_str(), nullptr, 10);
        return parsed == 0ULL ? fallback : parsed;
    }

    [[nodiscard]] std::vector<std::uint8_t> tiny_kickstart() {
        std::vector<std::uint8_t> rom(amiga500_system::kickstart_window_size, 0x00U);
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
        w32(0x0000U, 0x0007F000U);
        w32(0x0004U, amiga500_system::kickstart_base + 0x0008U);
        w16(0x0008U, 0x46FCU);
        w16(0x000AU, 0x2700U);
        w16(0x000CU, 0x60FEU);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> tiny_adf(std::uint8_t fill) {
        std::vector<std::uint8_t> adf(amiga500_system::floppy_dd_size, fill);
        adf[0] = 0x44U;
        adf[1] = 0x89U;
        return adf;
    }

    [[nodiscard]] bool joy_up(std::uint16_t joy) {
        return (((joy >> 9U) ^ (joy >> 8U)) & 0x01U) != 0U;
    }

    [[nodiscard]] bool joy_down(std::uint16_t joy) { return (((joy >> 1U) ^ joy) & 0x01U) != 0U; }

    [[nodiscard]] bool joy_left(std::uint16_t joy) { return (joy & 0x0200U) != 0U; }

    [[nodiscard]] bool joy_right(std::uint16_t joy) { return (joy & 0x0002U) != 0U; }

    [[nodiscard]] std::uint8_t keyboard_sdr(std::uint8_t raw_code) noexcept {
        const auto inverted = static_cast<std::uint8_t>(~raw_code);
        return static_cast<std::uint8_t>((inverted << 1U) | (inverted >> 7U));
    }

    void acknowledge_keyboard(amiga500_system& sys) {
        sys.cia_a.write(0x04U, 0x01U); // Timer A latch = 1 for a short SP pulse.
        sys.cia_a.write(0x05U, 0x00U);
        sys.cia_a.write(0x0EU, 0x41U); // CRA: START | SPMODE output.
        sys.cia_a.write(0x0CU, 0x00U); // Drive KDAT/SP low.
        sys.cia_a.tick(12U);
        sys.cia_a.write(0x0EU, 0x00U); // Release KDAT/SP high.
        sys.service_keyboard_queue();
    }

    void select_df0(amiga500_system& sys) {
        sys.cia_b.write(0x03U, 0xFFU);
        sys.cia_b.write(0x01U, 0x75U);
    }

    void select_df1(amiga500_system& sys) {
        sys.cia_b.write(0x03U, 0xFFU);
        sys.cia_b.write(0x01U, 0x6DU);
    }
} // namespace

TEST_CASE("amiga500 adapter constructs and steps frames", "[apps][player][amiga500]") {
    amiga500_adapter adapter(tiny_kickstart());

    const auto fb0 = adapter.current_frame();
    CHECK(fb0.width == 320U);
    CHECK(fb0.height == 256U);
    CHECK(adapter.chips().size() == 6U);
    CHECK(adapter.memory_views().size() == 1U);

    adapter.step_one_frame();
    CHECK(adapter.current_frame().pixels != nullptr);
}

TEST_CASE("amiga500 adapter publishes session and media metadata", "[apps][player][amiga500]") {
    amiga500_adapter adapter(tiny_kickstart(), {}, "Kickstart 1.3");

    const auto& session = adapter.session_capabilities();
    REQUIRE(session.input_ports.size() == 6U);
    CHECK(session.input_ports[0].device_id == "amiga.joystick.port.2");
    CHECK(session.input_ports[2].device_id == "amiga.keyboard");
    CHECK(session.input_ports[2].format == mnemos::frontend_sdk::input_device_format::keyboard);
    CHECK(session.input_ports[3].device_id == "amiga.mouse.port.1");
    CHECK(session.input_ports[3].format == mnemos::frontend_sdk::input_device_format::mouse);
    CHECK(session.input_ports[4].device_id == "amiga.pot.port.1");
    CHECK(session.input_ports[4].format == mnemos::frontend_sdk::input_device_format::analog);
    CHECK(session.input_ports[5].device_id == "amiga.pot.port.2");
    CHECK(session.input_ports[5].format == mnemos::frontend_sdk::input_device_format::analog);
    CHECK(session.deterministic_frame_input);

    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 1U);
    CHECK(media.media[0].id == "kickstart");
    CHECK(media.media[0].provider_id == "amiga500.kickstart");
}

TEST_CASE("amiga500 adapter mounts and swaps ADF disk media", "[apps][player][amiga500]") {
    std::vector<std::vector<std::uint8_t>> disks;
    disks.push_back(tiny_adf(0x11U));
    disks.push_back(tiny_adf(0x22U));
    amiga500_adapter adapter(tiny_kickstart(), {}, "Workbench", std::move(disks));

    REQUIRE(adapter.media_count() == 2U);
    CHECK(adapter.current_media_index() == 0U);
    CHECK(adapter.system().floppy_loaded());
    CHECK(adapter.system().floppy_loaded(1U));
    CHECK(adapter.system().floppy_size() == amiga500_system::floppy_dd_size);

    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 3U);
    CHECK(media.media[1].id == "disk.0");
    CHECK(media.media[2].id == "disk.1");
    CHECK(media.media[1].provider_id == "amiga500.df0");
    CHECK(media.media[2].provider_id == "amiga500.df1");

    select_df1(adapter.system());
    CHECK(adapter.system().selected_floppy_drive() == 1U);
    CHECK(adapter.system().floppy_size(1U) == amiga500_system::floppy_dd_size);

    REQUIRE(adapter.insert_media(1U));
    CHECK(adapter.current_media_index() == 1U);
    CHECK(adapter.system().floppy_loaded(0U));
    CHECK_FALSE(adapter.insert_media(2U));
}

TEST_CASE("amiga500 adapter routes controller state to the Amiga game ports",
          "[apps][player][amiga500][input]") {
    amiga500_adapter adapter(tiny_kickstart());

    mnemos::frontend_sdk::controller_state p1{};
    p1.up = true;
    p1.right = true;
    p1.a = true;
    p1.b = true;
    adapter.apply_input(0, p1);

    const std::uint16_t joy1 = adapter.system().read_custom_word(0x00CU);
    CHECK(joy_up(joy1));
    CHECK_FALSE(joy_down(joy1));
    CHECK_FALSE(joy_left(joy1));
    CHECK(joy_right(joy1));
    CHECK((adapter.system().cia_a.read(0x00U) & 0x80U) == 0U);
    CHECK((adapter.system().read_custom_word(0x016U) & 0x4000U) == 0U);

    mnemos::frontend_sdk::controller_state p2{};
    p2.down = true;
    p2.left = true;
    p2.a = true;
    adapter.apply_input(1, p2);

    const std::uint16_t joy0 = adapter.system().read_custom_word(0x00AU);
    CHECK_FALSE(joy_up(joy0));
    CHECK(joy_down(joy0));
    CHECK(joy_left(joy0));
    CHECK_FALSE(joy_right(joy0));
    CHECK((adapter.system().cia_a.read(0x00U) & 0x40U) == 0U);
}

TEST_CASE("amiga500 adapter maps frontend input edges to Amiga keyboard serial",
          "[apps][player][amiga500][input]") {
    amiga500_adapter adapter(tiny_kickstart());
    adapter.system().cia_a.write(0x0DU, 0x88U); // Enable serial interrupt mask.

    mnemos::frontend_sdk::controller_state p1{};
    p1.start = true;
    adapter.apply_input(0, p1);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x44U)); // Return down.
    adapter.system().cia_a.tick(1U);
    CHECK(adapter.system().cia_a.irq_asserted());

    CHECK((adapter.system().cia_a.read(0x0DU) & 0x08U) != 0U);
    p1.start = false;
    adapter.apply_input(0, p1);
    acknowledge_keyboard(adapter.system());
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0xC4U)); // Return up.
}

TEST_CASE("amiga500 adapter accepts direct keyboard-port input",
          "[apps][player][amiga500][input]") {
    amiga500_adapter adapter(tiny_kickstart());

    mnemos::frontend_sdk::controller_state keyboard{};
    keyboard.select = true;
    adapter.apply_input(2, keyboard);

    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x40U)); // Space down.
}

TEST_CASE("amiga500 adapter maps direct keyboard-port controls beyond the joystick port",
          "[apps][player][amiga500][input]") {
    amiga500_adapter adapter(tiny_kickstart());

    mnemos::frontend_sdk::controller_state pad{};
    pad.y = true;
    adapter.apply_input(0, pad);
    CHECK_FALSE(adapter.system().keyboard_caps_lock_led_on());

    mnemos::frontend_sdk::controller_state keyboard{};
    keyboard.y = true;
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().keyboard_caps_lock_led_on());
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x62U));

    acknowledge_keyboard(adapter.system());
    keyboard.y = false;
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().keyboard_pending_count() == 0U);

    keyboard.x = true;
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x45U)); // Escape down.
}

TEST_CASE("amiga500 adapter maps physical keyboard usages to Amiga raw serial",
          "[apps][player][amiga500][input]") {
    amiga500_adapter adapter(tiny_kickstart());

    mnemos::frontend_sdk::controller_state keyboard{};
    keyboard.set_key(0x04U, true); // HID A -> Amiga A.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x20U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x04U, false);
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0xA0U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x3AU, true); // HID F1 -> Amiga F1.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x50U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x3AU, false);
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0xD0U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0xE3U, true); // HID Left GUI -> left Amiga key.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x66U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0xE3U, false);
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0xE6U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x39U, true); // HID Caps Lock toggles the Amiga LED state.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().keyboard_caps_lock_led_on());
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x62U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x39U, false);
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().keyboard_pending_count() == 0U);
}

TEST_CASE("amiga500 adapter converts frontend mouse movement to Amiga counters",
          "[apps][player][amiga500][input]") {
    amiga500_adapter adapter(tiny_kickstart());

    mnemos::frontend_sdk::controller_state mouse{};
    mouse.aim_x = 10;
    mouse.aim_y = 20;
    mouse.trigger = true;
    mouse.a = true;
    mouse.b = true;
    adapter.apply_input(3, mouse);

    CHECK(adapter.system().read_custom_word(0x00AU) == 0x0000U);
    CHECK((adapter.system().cia_a.read(0x00U) & 0x40U) == 0U);
    CHECK((adapter.system().read_custom_word(0x016U) & 0x0400U) == 0U);
    CHECK((adapter.system().read_custom_word(0x016U) & 0x0100U) == 0U);

    mouse.aim_x = 13;
    mouse.aim_y = 18;
    mouse.b = false;
    adapter.apply_input(3, mouse);

    CHECK(adapter.system().read_custom_word(0x00AU) == 0xFE03U);
    CHECK((adapter.system().cia_a.read(0x00U) & 0x40U) == 0U);
    CHECK((adapter.system().read_custom_word(0x016U) & 0x0400U) == 0U);
    CHECK((adapter.system().read_custom_word(0x016U) & 0x0100U) != 0U);

    mouse.aim_x = -1;
    mouse.aim_y = -1;
    mouse.trigger = false;
    mouse.a = false;
    adapter.apply_input(3, mouse);

    CHECK(adapter.system().read_custom_word(0x00AU) == 0xFE03U);
    CHECK((adapter.system().cia_a.read(0x00U) & 0x40U) != 0U);
    CHECK((adapter.system().read_custom_word(0x016U) & 0x0400U) != 0U);
}

TEST_CASE("amiga500 adapter routes frontend analog axes to POT counters",
          "[apps][player][amiga500][input]") {
    amiga500_adapter adapter(tiny_kickstart());

    mnemos::frontend_sdk::controller_state pot0{};
    pot0.aim_x = 4;
    pot0.aim_y = 7;
    adapter.apply_input(4, pot0);

    mnemos::frontend_sdk::controller_state pot1{};
    pot1.aim_x = 12;
    pot1.aim_y = 2;
    adapter.apply_input(5, pot1);

    adapter.system().write_custom_word(0x034U, 0x0001U);
    adapter.system().agnus.tick(
        static_cast<std::uint64_t>(mnemos::chips::video::agnus::color_clocks_per_line) * 12U);

    CHECK(adapter.system().read_custom_word(0x012U) == 0x0504U);
    CHECK(adapter.system().read_custom_word(0x014U) == 0x0205U);

    mnemos::frontend_sdk::controller_state disconnected{};
    adapter.apply_input(4, disconnected);
    adapter.system().write_custom_word(0x034U, 0x0001U);
    adapter.system().agnus.tick(
        static_cast<std::uint64_t>(mnemos::chips::video::agnus::color_clocks_per_line) * 300U);
    CHECK(adapter.system().read_custom_word(0x012U) == 0xFFFFU);
}

TEST_CASE("amiga500 adapter whole-machine save-state round-trips",
          "[apps][player][amiga500][save]") {
    namespace amiga500 = mnemos::apps::player::adapters::amiga500;

    std::vector<std::vector<std::uint8_t>> disks;
    disks.push_back(tiny_adf(0x33U));
    amiga500_adapter a(tiny_kickstart(), {}, "Workbench", disks);

    select_df0(a.system());
    a.system().cia_b.write(0x01U, 0x74U); // Step inward to make drive state non-default.
    a.system().set_joystick(1U, static_cast<std::uint8_t>(amiga500_system::joy_up |
                                                          amiga500_system::joy_right |
                                                          amiga500_system::joy_fire));
    REQUIRE(a.system().enqueue_keyboard_key(0x4CU, true));
    REQUIRE(a.system().enqueue_keyboard_key(0x4CU, false));
    a.system().bus.write8(0x000123U, 0x5AU);
    a.system().write_custom_word(0x180U, 0x00A5U);
    a.scheduler().run_master_cycles(37U);

    const std::uint64_t saved_cycle = a.scheduler().master_cycle();
    const auto saved_regs = a.system().cpu.cpu_registers();

    const mnemos::runtime::save_target ta = amiga500::build_save_target(a);
    const std::vector<std::uint8_t> blob = mnemos::runtime::write_save_state(ta);
    REQUIRE(!blob.empty());

    amiga500_adapter b(tiny_kickstart(), {}, "Workbench", disks);
    CHECK(b.system().chip_ram[0x0123U] != 0x5AU);
    CHECK(b.scheduler().master_cycle() != saved_cycle);

    mnemos::runtime::save_target tb = amiga500::build_save_target(b);
    const mnemos::runtime::load_result result = mnemos::runtime::read_save_state(blob, tb);
    REQUIRE(result.ok());

    CHECK(result.master_cycle == saved_cycle);
    CHECK(b.scheduler().master_cycle() == saved_cycle);
    CHECK(b.system().chip_ram[0x0123U] == 0x5AU);
    CHECK(b.system().paula.chipram()[0x0123U] == 0x5AU);
    CHECK(b.system().read_custom_word(0x180U) == 0x00A5U);
    CHECK(b.system().floppy_loaded());
    CHECK(b.system().floppy_cylinder() == 1U);
    CHECK(b.system().keyboard_pending_count() == 1U);
    CHECK(b.system().cia_a.read(0x0CU) == keyboard_sdr(0x4CU));

    const auto restored_regs = b.system().cpu.cpu_registers();
    CHECK(restored_regs.pc == saved_regs.pc);
    CHECK(restored_regs.sr == saved_regs.sr);

    const std::uint16_t joy1 = b.system().read_custom_word(0x00CU);
    CHECK(joy_up(joy1));
    CHECK_FALSE(joy_down(joy1));
    CHECK_FALSE(joy_left(joy1));
    CHECK(joy_right(joy1));
    CHECK((b.system().cia_a.read(0x00U) & 0x80U) == 0U);

    CHECK((b.system().cia_a.read(0x0DU) & 0x08U) != 0U);
    acknowledge_keyboard(b.system());
    CHECK(b.system().keyboard_pending_count() == 0U);
    CHECK(b.system().cia_a.read(0x0CU) == keyboard_sdr(0xCCU));
}

TEST_CASE("amiga500 adapter boots real Kickstart with an ADF disk when data is supplied",
          "[apps][player][amiga500][data]") {
    const auto kickstart_path = get_env("MNEMOS_AMIGA500_KICKSTART");
    const auto adf_path = get_env("MNEMOS_AMIGA500_ADF");
    if (!kickstart_path || !adf_path) {
        SKIP("set MNEMOS_AMIGA500_KICKSTART and MNEMOS_AMIGA500_ADF to run the "
             "copyrighted-data Amiga500 boot gate");
    }

    auto kickstart = read_file(fs::path(*kickstart_path));
    REQUIRE(kickstart.has_value());
    REQUIRE((kickstart->size() == amiga500_system::kickstart_window_size ||
             kickstart->size() == amiga500_system::kickstart_window_size / 2U));

    auto adf = read_file(fs::path(*adf_path));
    REQUIRE(adf.has_value());
    REQUIRE(adf->size() == amiga500_system::floppy_dd_size);

    std::vector<std::vector<std::uint8_t>> disks;
    disks.push_back(std::move(*adf));
    amiga500_adapter adapter(std::move(*kickstart), {}, fs::path(*adf_path).filename().string(),
                             std::move(disks));

    REQUIRE(adapter.system().floppy_loaded());
    CHECK(adapter.system().floppy_size() == amiga500_system::floppy_dd_size);
    CHECK(adapter.system().kickstart_overlay_active());

    const auto initial_regs = adapter.system().cpu.cpu_registers();
    CHECK(initial_regs.pc >= amiga500_system::kickstart_base);

    const std::uint64_t frames = get_env_u64("MNEMOS_AMIGA500_BOOT_FRAMES", 300U);
    bool pc_moved = false;
    for (std::uint64_t frame = 0U; frame < frames; ++frame) {
        adapter.step_one_frame();
        if (adapter.system().cpu.cpu_registers().pc != initial_regs.pc) {
            pc_moved = true;
        }
    }

    const auto final_regs = adapter.system().cpu.cpu_registers();
    INFO("frames: " << frames);
    INFO("initial pc: " << initial_regs.pc);
    INFO("final pc: " << final_regs.pc);
    INFO("final sr: " << final_regs.sr);

    CHECK(pc_moved);
    CHECK(adapter.system().frame_index >= frames);
    CHECK(adapter.scheduler().master_cycle() > 0U);
    CHECK_FALSE(adapter.system().kickstart_overlay_active());
}
