#include "amiga500_adapter.hpp"

#include "adapter_registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    using mnemos::apps::player::adapters::amiga500::amiga500_adapter;
    using mnemos::manifests::amiga500::amiga500_config;
    using mnemos::manifests::amiga500::amiga500_keyboard_layout;
    using mnemos::manifests::amiga500::amiga500_model;
    using mnemos::manifests::amiga500::amiga500_system;
    using agnus = mnemos::chips::video::agnus;

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

    struct frame_color_stats final {
        std::size_t non_black{};
        std::size_t non_background{};
        std::size_t center_non_background{};
        std::size_t distinct_colors{};
        std::uint32_t background{};
    };

    [[nodiscard]] frame_color_stats analyze_frame_colors(
        const mnemos::chips::frame_buffer_view& frame) {
        if (frame.pixels == nullptr || frame.width == 0U || frame.height == 0U) {
            return {};
        }

        frame_color_stats stats{.background = frame.pixels[0] & 0x00FFFFFFU};
        std::vector<std::uint32_t> colors;
        colors.reserve(8U);

        const std::uint32_t stride = frame.effective_stride();
        const std::uint32_t center_left = frame.width / 5U;
        const std::uint32_t center_right = (frame.width * 4U) / 5U;
        const std::uint32_t center_top = frame.height / 6U;
        const std::uint32_t center_bottom = (frame.height * 5U) / 6U;
        for (std::uint32_t y = 0; y < frame.height; ++y) {
            const std::uint32_t* row = frame.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < frame.width; ++x) {
                const std::uint32_t rgb = row[x] & 0x00FFFFFFU;
                if (rgb != 0U) {
                    ++stats.non_black;
                }
                if (rgb != stats.background) {
                    ++stats.non_background;
                    if (x >= center_left && x < center_right && y >= center_top &&
                        y < center_bottom) {
                        ++stats.center_non_background;
                    }
                }
                if (std::find(colors.begin(), colors.end(), rgb) == colors.end()) {
                    colors.push_back(rgb);
                }
            }
        }
        stats.distinct_colors = colors.size();
        return stats;
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

    [[nodiscard]] std::vector<std::uint8_t> kickstart_signature_rom() {
        auto rom = tiny_kickstart();
        const auto w32 = [&](std::size_t off, std::uint32_t v) {
            rom[off + 0U] = static_cast<std::uint8_t>(v >> 24U);
            rom[off + 1U] = static_cast<std::uint8_t>(v >> 16U);
            rom[off + 2U] = static_cast<std::uint8_t>(v >> 8U);
            rom[off + 3U] = static_cast<std::uint8_t>(v);
        };
        w32(0x0004U, 0x00FC00D2U);
        constexpr std::string_view exec = "exec.library";
        constexpr std::string_view title = "AMIGA ROM";
        std::copy(exec.begin(), exec.end(), rom.begin() + 0x00A8U);
        std::copy(title.begin(), title.end(), rom.begin() + 0x0038U);
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> tiny_adf(std::uint8_t fill) {
        std::vector<std::uint8_t> adf(amiga500_system::floppy_dd_size, fill);
        adf[0] = 0x44U;
        adf[1] = 0x89U;
        return adf;
    }

    void write_chip_word(amiga500_system& sys, std::uint32_t address, std::uint16_t value) {
        sys.bus.write8(address, static_cast<std::uint8_t>(value >> 8U));
        sys.bus.write8(address + 1U, static_cast<std::uint8_t>(value));
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

    void expect_keyboard_edge(amiga500_adapter& adapter,
                              mnemos::frontend_sdk::controller_state& keyboard, std::uint16_t usage,
                              std::uint8_t raw_key) {
        keyboard.set_key(usage, true);
        adapter.apply_input(2, keyboard);
        CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(raw_key));

        acknowledge_keyboard(adapter.system());
        keyboard.set_key(usage, false);
        adapter.apply_input(2, keyboard);
        CHECK(adapter.system().cia_a.read(0x0CU) ==
              keyboard_sdr(static_cast<std::uint8_t>(raw_key | 0x80U)));

        acknowledge_keyboard(adapter.system());
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
    REQUIRE(adapter.chips().size() == 7U);
    CHECK(adapter.chips()[6]->metadata().part_number == "amiga500_board");
    REQUIRE(adapter.chips()[6]->introspection().registers() != nullptr);
    const auto board_regs = adapter.chips()[6]->introspection().registers()->registers();
    REQUIRE(board_regs.size() == 23U);
    CHECK(board_regs[0].name == "INTENA");
    CHECK(board_regs[1].name == "INTREQ");
    CHECK(board_regs[3].name == "IRQ");
    CHECK(board_regs[6].name == "DMACON");
    CHECK(board_regs[7].name == "DMACONR");
    CHECK(board_regs[8].name == "BBUSY");
    CHECK(board_regs[9].name == "BLTCYC");
    CHECK(board_regs[12].name == "COPPC");
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
    CHECK(session.save_state_supported);
    CHECK(session.frame_exact_save_state);

    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 1U);
    CHECK(media.media[0].id == "kickstart");
    CHECK(media.media[0].provider_id == "amiga500.kickstart");
}

TEST_CASE("amiga500 adapter configures Amiga 500+ metadata and chip RAM",
          "[apps][player][amiga500plus]") {
    const amiga500_config config{.video_region = mnemos::video_region::pal,
                                 .keyboard_layout = amiga500_keyboard_layout::us,
                                 .model = amiga500_model::amiga500_plus};
    std::vector<std::vector<std::uint8_t>> disks;
    disks.push_back(tiny_adf(0x33U));
    amiga500_adapter adapter(tiny_kickstart(), config, "Kickstart 2.0", std::move(disks));
    auto& sys = adapter.system();

    REQUIRE(sys.chip_ram.size() == amiga500_system::chip_ram_size_1m);
    REQUIRE(sys.paula.chipram().size() == amiga500_system::chip_ram_size_1m);
    REQUIRE(adapter.memory_views().size() == 1U);
    CHECK(adapter.memory_views()[0]->bytes().size() == amiga500_system::chip_ram_size_1m);

    const auto& spec = adapter.system_spec();
    REQUIRE(spec.size() >= 2U);
    CHECK(spec[0].label == "System");
    CHECK(spec[0].value == "Amiga 500+");
    CHECK(spec[1].label == "Chip RAM");
    CHECK(spec[1].value == "1 MiB");

    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 2U);
    CHECK(media.media[0].provider_id == "amiga500plus.kickstart");
    CHECK(media.media[1].provider_id == "amiga500plus.df0");

    constexpr std::uint32_t upper_chip_ram = 0x080000U;
    sys.bus.write8(upper_chip_ram, 0x5AU);
    CHECK(sys.chip_ram[upper_chip_ram] == 0x5AU);
    CHECK(sys.paula.chipram()[upper_chip_ram] == 0x5AU);
}

TEST_CASE("amiga500 adapter seeds Kickstart keyboard power-up stream",
          "[apps][player][amiga500][input]") {
    amiga500_adapter adapter(kickstart_signature_rom(), {}, "Kickstart signature");

    CHECK(adapter.system().cia_a.read(0x0CU) ==
          keyboard_sdr(amiga500_system::keyboard_powerup_stream_start_code));
    CHECK(adapter.system().keyboard_pending_count() == 1U);

    acknowledge_keyboard(adapter.system());

    CHECK(adapter.system().cia_a.read(0x0CU) ==
          keyboard_sdr(amiga500_system::keyboard_powerup_stream_end_code));
    CHECK(adapter.system().keyboard_pending_count() == 0U);
}

TEST_CASE("amiga500 adapter CPU RESET pulse resets board devices and preserves memory",
          "[apps][player][amiga500]") {
    auto rom = tiny_kickstart();
    rom[0x0008U] = 0x4EU;
    rom[0x0009U] = 0x70U; // RESET.
    rom[0x000AU] = 0x4EU;
    rom[0x000BU] = 0x71U; // NOP after RESET.

    std::vector<std::vector<std::uint8_t>> disks;
    disks.push_back(tiny_adf(0x44U));
    amiga500_adapter adapter(std::move(rom), {}, "Workbench", std::move(disks));
    auto& sys = adapter.system();
    REQUIRE(sys.floppy_loaded());

    sys.bus.write8(0x00BFE201U, 0x03U);
    sys.bus.write8(0x00BFE001U, 0x02U);
    REQUIRE_FALSE(sys.kickstart_overlay_active());
    sys.bus.write8(0x000123U, 0x5AU);
    REQUIRE(sys.chip_ram[0x0123U] == 0x5AU);

    sys.write_custom_word(0x09AU, static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                             amiga500_system::int_master |
                                                             amiga500_system::int_vertb));
    sys.write_custom_word(0x080U, 0x0001U);
    sys.write_custom_word(0x082U, 0x2340U);
    sys.write_custom_word(0x084U, 0x0002U);
    sys.write_custom_word(0x086U, 0x4680U);
    sys.write_custom_word(0x096U,
                          static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                     agnus::dmacon_dmaen | agnus::dmacon_copen |
                                                     agnus::dmacon_dsken));
    sys.write_custom_word(0x024U, 0x8004U);
    sys.frame_index = 5U;
    select_df0(sys);
    REQUIRE(sys.selected_floppy_drive() == 0U);

    const int cycles = sys.cpu.step_instruction();
    const auto regs = sys.cpu.cpu_registers();
    CHECK(cycles == 132);
    CHECK(regs.pc == amiga500_system::kickstart_base + 0x000AU);
    CHECK(sys.kickstart_overlay_active());
    CHECK(sys.read_custom_word(0x01CU) == 0U);
    CHECK(sys.read_custom_word(0x002U) == 0U);
    CHECK(sys.read_custom_word(0x024U) == 0U);
    CHECK(sys.cop1lc == 0U);
    CHECK(sys.cop2lc == 0U);
    CHECK(sys.frame_index == 0U);
    CHECK(sys.floppy_loaded());
    CHECK(sys.selected_floppy_drive() == amiga500_system::no_floppy_drive);
    CHECK_FALSE(sys.floppy_motor_on);
    CHECK(sys.chip_ram[0x0123U] == 0x5AU);
    CHECK(sys.paula.chipram()[0x0123U] == 0x5AU);
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
    select_df0(adapter.system());
    CHECK((adapter.system().cia_a.read(0x00U) & 0x04U) != 0U);

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
    select_df0(adapter.system());
    CHECK((adapter.system().cia_a.read(0x00U) & 0x04U) == 0U);
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

TEST_CASE("amiga500 adapter keeps raw keys held across duplicate frontend sources",
          "[apps][player][amiga500][input]") {
    amiga500_adapter adapter(tiny_kickstart());

    mnemos::frontend_sdk::controller_state keyboard{};
    keyboard.select = true;        // Frontend shortcut for Amiga Space.
    keyboard.set_key(0x2CU, true); // HID Space, same Amiga raw key.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x40U));

    acknowledge_keyboard(adapter.system());
    keyboard.select = false;
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().keyboard_pending_count() == 0U);

    keyboard.set_key(0x2CU, false);
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0xC0U));
}

TEST_CASE("amiga500 adapter keeps raw keys held across keyboard-producing ports",
          "[apps][player][amiga500][input]") {
    amiga500_adapter adapter(tiny_kickstart());

    mnemos::frontend_sdk::controller_state pad{};
    pad.start = true; // Frontend shortcut for Amiga Return.
    adapter.apply_input(0, pad);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x44U));

    acknowledge_keyboard(adapter.system());
    mnemos::frontend_sdk::controller_state keyboard{};
    keyboard.set_key(0x28U, true); // HID Return, same Amiga raw key.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().keyboard_pending_count() == 0U);

    pad.start = false;
    adapter.apply_input(0, pad);
    CHECK(adapter.system().keyboard_pending_count() == 0U);

    keyboard.set_key(0x28U, false);
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0xC4U));
}

TEST_CASE("amiga500 adapter blocks new keys that would ghost in the keyboard matrix",
          "[apps][player][amiga500][input]") {
    amiga500_adapter adapter(tiny_kickstart());

    mnemos::frontend_sdk::controller_state keyboard{};
    keyboard.set_key(0x04U, true); // HID A -> raw 0x20.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x20U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x16U, true); // HID S -> raw 0x21.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x21U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x14U, true); // HID Q -> raw 0x10; would phantom W.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().keyboard_pending_count() == 0U);

    keyboard.set_key(0x1AU, true); // HID W -> raw 0x11; opposite phantom corner.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().keyboard_pending_count() == 0U);

    keyboard.set_key(0x16U, false); // Releasing S clears the ambiguity.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0xA1U));
    CHECK(adapter.system().keyboard_pending_count() == 1U);

    acknowledge_keyboard(adapter.system());
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x10U));
    CHECK(adapter.system().keyboard_pending_count() == 0U);
}

TEST_CASE("amiga500 adapter selects German physical keyboard layout",
          "[apps][player][amiga500][input]") {
    const amiga500_config config{.video_region = mnemos::video_region::pal,
                                 .keyboard_layout = amiga500_keyboard_layout::german};
    amiga500_adapter adapter(tiny_kickstart(), config);

    bool spec_reports_layout = false;
    for (const auto& field : adapter.system_spec()) {
        spec_reports_layout =
            spec_reports_layout || (field.label == "Keyboard" && field.value == "German");
    }
    CHECK(spec_reports_layout);

    mnemos::frontend_sdk::controller_state keyboard{};
    keyboard.set_key(0x1CU, true); // HID Y position -> Amiga Z on German QWERTZ.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x31U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x1CU, false);
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0xB1U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x1DU, true); // HID Z position -> Amiga Y on German QWERTZ.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x15U));
}

TEST_CASE("amiga500 adapter selects AZERTY physical keyboard layout",
          "[apps][player][amiga500][input]") {
    const amiga500_config config{.video_region = mnemos::video_region::pal,
                                 .keyboard_layout = amiga500_keyboard_layout::azerty};
    amiga500_adapter adapter(tiny_kickstart(), config);

    bool spec_reports_layout = false;
    for (const auto& field : adapter.system_spec()) {
        spec_reports_layout =
            spec_reports_layout || (field.label == "Keyboard" && field.value == "AZERTY");
    }
    CHECK(spec_reports_layout);

    mnemos::frontend_sdk::controller_state keyboard{};
    keyboard.set_key(0x14U, true); // HID Q position -> Amiga A on AZERTY.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x20U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x14U, false);
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0xA0U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x04U, true); // HID A position -> Amiga Q on AZERTY.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x10U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x04U, false);
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x90U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x1AU, true); // HID W position -> Amiga Z on AZERTY.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x31U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x33U, true); // HID semicolon position -> Amiga M on AZERTY.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x37U));
}

TEST_CASE("amiga500 adapter selects international QWERTY physical keyboard layout",
          "[apps][player][amiga500][input]") {
    const amiga500_config config{.video_region = mnemos::video_region::pal,
                                 .keyboard_layout = amiga500_keyboard_layout::qwerty_international};
    amiga500_adapter adapter(tiny_kickstart(), config);

    bool spec_reports_layout = false;
    for (const auto& field : adapter.system_spec()) {
        spec_reports_layout = spec_reports_layout ||
                              (field.label == "Keyboard" && field.value == "International QWERTY");
    }
    CHECK(spec_reports_layout);

    mnemos::frontend_sdk::controller_state keyboard{};
    keyboard.set_key(0x1CU, true); // HID Y stays Amiga Y on QWERTY layouts.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x15U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x1CU, false);
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x95U));

    acknowledge_keyboard(adapter.system());
    keyboard.set_key(0x14U, true); // HID Q stays Amiga Q, unlike AZERTY.
    adapter.apply_input(2, keyboard);
    CHECK(adapter.system().cia_a.read(0x0CU) == keyboard_sdr(0x10U));
}

TEST_CASE("amiga500 adapter maps international reserved symbol usages",
          "[apps][player][amiga500][input]") {
    const amiga500_config config{.video_region = mnemos::video_region::pal,
                                 .keyboard_layout = amiga500_keyboard_layout::qwerty_international};
    amiga500_adapter adapter(tiny_kickstart(), config);

    mnemos::frontend_sdk::controller_state keyboard{};
    expect_keyboard_edge(adapter, keyboard, 0x32U, 0x2BU);
    expect_keyboard_edge(adapter, keyboard, 0x64U, 0x30U);
    expect_keyboard_edge(adapter, keyboard, 0x87U, 0x3BU);
    expect_keyboard_edge(adapter, keyboard, 0x89U, 0x0EU);
}

TEST_CASE("amiga500 adapter registry accepts keyboard layout override",
          "[apps][player][amiga500][input]") {
    for (const std::string layout_token : {"de_DE", "de-AT", "de CH", "qwertz"}) {
        mnemos::frontend_sdk::adapter_options options{};
        options.rom = tiny_kickstart();
        options.video_region = mnemos::video_region::pal;
        options.keyboard_layout_override = layout_token;

        auto system = mnemos::frontend_sdk::adapter_registry::instance().create("amiga500",
                                                                                std::move(options));
        REQUIRE(system != nullptr);

        bool spec_reports_layout = false;
        for (const auto& field : system->system_spec()) {
            spec_reports_layout =
                spec_reports_layout || (field.label == "Keyboard" && field.value == "German");
        }
        CHECK(spec_reports_layout);

        mnemos::frontend_sdk::controller_state keyboard{};
        keyboard.set_key(0x1CU, true); // HID Y position -> Amiga Z on German QWERTZ.
        system->apply_input(2, keyboard);
        CHECK(static_cast<amiga500_adapter*>(system.get())->system().cia_a.read(0x0CU) ==
              keyboard_sdr(0x31U));
    }
}

TEST_CASE("amiga500 adapter registry accepts AZERTY keyboard layout override",
          "[apps][player][amiga500][input]") {
    for (const std::string layout_token : {"fr_FR", "fr-BE", "be fr", "azerty"}) {
        mnemos::frontend_sdk::adapter_options options{};
        options.rom = tiny_kickstart();
        options.video_region = mnemos::video_region::pal;
        options.keyboard_layout_override = layout_token;

        auto system = mnemos::frontend_sdk::adapter_registry::instance().create("amiga500",
                                                                                std::move(options));
        REQUIRE(system != nullptr);

        bool spec_reports_layout = false;
        for (const auto& field : system->system_spec()) {
            spec_reports_layout =
                spec_reports_layout || (field.label == "Keyboard" && field.value == "AZERTY");
        }
        CHECK(spec_reports_layout);

        mnemos::frontend_sdk::controller_state keyboard{};
        keyboard.set_key(0x1DU, true); // HID Z position -> Amiga W on AZERTY.
        system->apply_input(2, keyboard);
        CHECK(static_cast<amiga500_adapter*>(system.get())->system().cia_a.read(0x0CU) ==
              keyboard_sdr(0x11U));
    }
}

TEST_CASE("amiga500 adapter registry accepts international QWERTY keyboard layout override",
          "[apps][player][amiga500][input]") {
    for (const std::string layout_token : {"en_GB", "uk", "us intl", "es-ES", "it_IT", "pt-BR",
                                           "br", "sv-SE", "fi", "dk", "nb-NO"}) {
        mnemos::frontend_sdk::adapter_options options{};
        options.rom = tiny_kickstart();
        options.video_region = mnemos::video_region::pal;
        options.keyboard_layout_override = layout_token;

        auto system = mnemos::frontend_sdk::adapter_registry::instance().create("amiga500",
                                                                                std::move(options));
        REQUIRE(system != nullptr);

        bool spec_reports_layout = false;
        for (const auto& field : system->system_spec()) {
            spec_reports_layout = spec_reports_layout || (field.label == "Keyboard" &&
                                                          field.value == "International QWERTY");
        }
        CHECK(spec_reports_layout);

        mnemos::frontend_sdk::controller_state keyboard{};
        keyboard.set_key(0x1DU, true); // HID Z stays Amiga Z, unlike German QWERTZ.
        system->apply_input(2, keyboard);
        CHECK(static_cast<amiga500_adapter*>(system.get())->system().cia_a.read(0x0CU) ==
              keyboard_sdr(0x31U));
    }
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
    a.system().cia_b.write(0x01U, 0x76U); // Step inward to make drive state non-default.
    a.system().set_joystick(1U, static_cast<std::uint8_t>(amiga500_system::joy_up |
                                                          amiga500_system::joy_right |
                                                          amiga500_system::joy_fire));
    REQUIRE(a.system().enqueue_keyboard_key(0x4CU, true));
    REQUIRE(a.system().enqueue_keyboard_key(0x4CU, false));
    a.system().bus.write8(0x000123U, 0x5AU);
    a.system().write_custom_word(0x180U, 0x00A5U);
    write_chip_word(a.system(), 0x0000U, 0x8000U);
    a.system().write_custom_word(0x182U, 0x0F00U);
    a.system().write_custom_word(0x100U, 0x1000U);
    a.system().write_custom_word(0x08EU, 0x2C00U);
    a.system().write_custom_word(0x090U, 0xF400U);
    a.system().write_custom_word(0x092U, 0x0038U);
    a.system().write_custom_word(0x094U, 0x00D0U);
    a.system().write_custom_word(0x0E0U, 0x0000U);
    a.system().write_custom_word(0x0E2U, 0x0000U);
    a.system().write_custom_word(0x096U, static_cast<std::uint16_t>(amiga500_system::setclr_bit |
                                                                    agnus::dmacon_dmaen |
                                                                    agnus::dmacon_bplen));
    a.system().agnus.tick(static_cast<std::uint64_t>(agnus::color_clocks_per_line) *
                          agnus::scanlines_pal);
    CHECK(a.system().agnus.framebuffer().pixels[0] == 0x00FF0000U);
    const std::uint32_t saved_bitplane_pointer = a.system().agnus.bitplane_pointer(0U);
    CHECK(saved_bitplane_pointer != 0U);
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
    CHECK(b.system().agnus.bitplane_pointer(0U) == saved_bitplane_pointer);
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

TEST_CASE("amiga500 adapter player save-state preserves frontend input cursors",
          "[apps][player][amiga500][save][input]") {
    std::vector<std::vector<std::uint8_t>> disks;
    disks.push_back(tiny_adf(0x33U));
    disks.push_back(tiny_adf(0x77U));
    amiga500_adapter live(tiny_kickstart(), {}, "Workbench", disks);
    constexpr std::size_t disk_payload_probe = 2U;

    REQUIRE(live.insert_media(1U));
    CHECK(live.current_media_index() == 1U);
    REQUIRE(live.system().floppy_loaded());
    REQUIRE(live.system().floppy_drives[0].image.size() > disk_payload_probe);
    CHECK(live.system().floppy_drives[0].image[disk_payload_probe] == 0x77U);
    select_df0(live.system());
    live.system().cia_b.write(0x01U,
                              0x76U); // Step inward to prove load does not remount/reset DF0.
    CHECK(live.system().floppy_cylinder() == 1U);
    live.system().floppy_drives[0].image[disk_payload_probe] = 0x99U;

    mnemos::frontend_sdk::controller_state keyboard{};
    keyboard.set_key(0x04U, true); // HID A -> raw 0x20.
    live.apply_input(2, keyboard);
    CHECK(live.system().cia_a.read(0x0CU) == keyboard_sdr(0x20U));
    acknowledge_keyboard(live.system());

    mnemos::frontend_sdk::controller_state mouse{};
    mouse.aim_x = 10;
    mouse.aim_y = 20;
    live.apply_input(3, mouse);
    CHECK(live.system().read_custom_word(0x00AU) == 0x0000U);

    const std::vector<std::uint8_t> blob = live.save_state();
    REQUIRE(!blob.empty());

    amiga500_adapter restored(tiny_kickstart(), {}, "Workbench", disks);
    CHECK(restored.current_media_index() == 0U);
    const mnemos::runtime::load_result result = restored.load_state(blob);
    REQUIRE(result.ok());
    CHECK(restored.current_media_index() == 1U);
    REQUIRE(restored.system().floppy_loaded());
    REQUIRE(restored.system().floppy_drives[0].image.size() > disk_payload_probe);
    CHECK(restored.system().floppy_drives[0].image[disk_payload_probe] == 0x99U);
    CHECK(restored.system().floppy_cylinder() == 1U);

    keyboard.set_key(0x04U, false);
    restored.apply_input(2, keyboard);
    CHECK(restored.system().cia_a.read(0x0CU) == keyboard_sdr(0xA0U));

    mouse.aim_x = 13;
    mouse.aim_y = 18;
    restored.apply_input(3, mouse);
    CHECK(restored.system().read_custom_word(0x00AU) == 0xFE03U);
}

TEST_CASE("amiga500 adapter renders the real Kickstart insert-disk prompt",
          "[apps][player][amiga500][data][video]") {
    const auto kickstart_path = get_env("MNEMOS_AMIGA500_KICKSTART");
    if (!kickstart_path) {
        SKIP("set MNEMOS_AMIGA500_KICKSTART to run the copyrighted-data Kickstart "
             "insert-disk prompt gate");
    }

    auto kickstart = read_file(fs::path(*kickstart_path));
    REQUIRE(kickstart.has_value());
    REQUIRE((kickstart->size() == amiga500_system::kickstart_window_size ||
             kickstart->size() == amiga500_system::kickstart_window_size / 2U));

    amiga500_adapter adapter(std::move(*kickstart), {}, fs::path(*kickstart_path).filename().string());
    const std::uint64_t frames = get_env_u64("MNEMOS_AMIGA500_PROMPT_FRAMES", 900U);
    for (std::uint64_t frame = 0U; frame < frames; ++frame) {
        adapter.step_one_frame();
    }

    const auto view = adapter.current_frame();
    const auto color_stats = analyze_frame_colors(view);
    const auto regs = adapter.system().cpu.cpu_registers();
    INFO("frames: " << frames);
    INFO("pc: " << regs.pc);
    INFO("sr: " << regs.sr);
    INFO("frame index: " << adapter.system().frame_index);
    INFO("dmacon: " << adapter.system().agnus.dmacon());
    INFO("dmaconr: " << adapter.system().agnus.read_dmaconr());
    INFO("cop1lc: " << adapter.system().agnus.cop1lc());
    INFO("cop2lc: " << adapter.system().agnus.cop2lc());
    INFO("copper pc: " << adapter.system().agnus.copper_pc());
    INFO("bplcon0: " << adapter.system().read_custom_word(0x100U));
    INFO("color00: " << adapter.system().read_custom_word(0x180U));
    INFO("color01: " << adapter.system().read_custom_word(0x182U));
    INFO("background color: " << color_stats.background);
    INFO("distinct colors: " << color_stats.distinct_colors);
    INFO("non-background pixels: " << color_stats.non_background);
    INFO("center non-background pixels: " << color_stats.center_non_background);
    INFO("non-black pixels: " << color_stats.non_black << " of "
                              << static_cast<std::size_t>(view.width) * view.height);
    CHECK(view.width == agnus::visible_width);
    CHECK(view.height == agnus::visible_height_pal);
    CHECK(color_stats.non_black > (static_cast<std::size_t>(view.width) * view.height) / 2U);
    CHECK(color_stats.distinct_colors >= 4U);
    CHECK(color_stats.non_background > 4096U);
    CHECK(color_stats.non_background < 10000U);
    CHECK(color_stats.center_non_background > 4096U);
    CHECK(color_stats.center_non_background < 10000U);
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
    INFO("vblank frames: " << adapter.system().frame_index);

    CHECK(pc_moved);
    CHECK(adapter.system().frame_index > 0U);
    CHECK(adapter.system().frame_index <= frames);
    CHECK(adapter.system().frame_index + 64U >= frames);
    CHECK(adapter.scheduler().master_cycle() > 0U);
    CHECK_FALSE(adapter.system().kickstart_overlay_active());
}
