#include "msx_adapter.hpp"

#include "adapter_registry.hpp"
#include "msx_cassette.hpp"
#include "tms9918a.hpp"
#include "v9938.hpp"
#include "wd1793.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace {
    using mnemos::apps::player::adapters::msx::msx_adapter;
    using mnemos::chips::storage::msx_cassette;
    using wd1793 = mnemos::chips::storage::wd1793;
    using tms9918a = mnemos::chips::video::tms9918a;
    using v9938 = mnemos::chips::video::v9938;

    std::vector<std::uint8_t> make_cas(std::initializer_list<std::uint8_t> bytes) {
        std::vector<std::uint8_t> cas(msx_cassette::cas_header_magic.begin(),
                                      msx_cassette::cas_header_magic.end());
        cas.insert(cas.end(), bytes.begin(), bytes.end());
        return cas;
    }

    [[nodiscard]] std::vector<std::uint8_t> make_dsk() {
        std::vector<std::uint8_t> disk(
            40U * 2U * wd1793::standard_sectors_per_track * wd1793::sector_size, 0xE5U);
        disk[0] = 0x42U;
        return disk;
    }

    [[nodiscard]] std::vector<std::uint8_t> make_kanji_rom() {
        std::vector<std::uint8_t> rom(0x40000U, 0xFFU);
        rom[(0x0123U * 32U)] = 0x4BU;
        rom[0x20000U + (0x0045U * 32U)] = 0xD2U;
        return rom;
    }

    [[nodiscard]] std::vector<std::uint8_t> make_banked_8k_rom() {
        std::vector<std::uint8_t> rom(0x2000U * 8U, 0x00U);
        for (std::size_t i = 0; i < rom.size(); ++i) {
            rom[i] = static_cast<std::uint8_t>(i / 0x2000U);
        }
        return rom;
    }

    void write_mapper_signature(std::vector<std::uint8_t>& rom, std::size_t offset,
                                std::uint16_t address) {
        REQUIRE(offset + 3U <= rom.size());
        rom[offset + 0U] = 0x32U; // LD (nn),A
        rom[offset + 1U] = static_cast<std::uint8_t>(address & 0xFFU);
        rom[offset + 2U] = static_cast<std::uint8_t>(address >> 8U);
    }

    void add_mapper_signatures(std::vector<std::uint8_t>& rom,
                               std::initializer_list<std::uint16_t> addresses) {
        std::size_t offset = 0x0100U;
        for (const std::uint16_t address : addresses) {
            write_mapper_signature(rom, offset, address);
            offset += 0x20U;
        }
    }

    const mnemos::frontend_sdk::media_image_info* find_media(const msx_adapter& adapter,
                                                             std::string_view id) {
        const auto& media = adapter.media_capabilities().media;
        const auto it = std::find_if(media.begin(), media.end(),
                                     [&](const auto& image) { return image.id == id; });
        return it == media.end() ? nullptr : &*it;
    }

    void configure_psg_gpio(mnemos::manifests::msx::msx_system& sys) {
        sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_mixer);
        sys.write_io(0xA1U, 0xBFU);
        sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
        sys.write_io(0xA1U, 0x0FU);
    }

    void append_out_immediate(std::vector<std::uint8_t>& program, std::uint8_t port,
                              std::uint8_t value) {
        program.push_back(0x3EU); // LD A,n
        program.push_back(value);
        program.push_back(0xD3U); // OUT (n),A
        program.push_back(port);
    }

    void append_vdp_vram_write(std::vector<std::uint8_t>& program, std::uint16_t address,
                               std::uint8_t value) {
        append_out_immediate(program, 0x99U, static_cast<std::uint8_t>(address & 0xFFU));
        append_out_immediate(program, 0x99U,
                             static_cast<std::uint8_t>(0x40U | ((address >> 8U) & 0x3FU)));
        append_out_immediate(program, 0x98U, value);
    }

    void append_vdp_register_write(std::vector<std::uint8_t>& program, std::uint8_t reg,
                                   std::uint8_t value) {
        append_out_immediate(program, 0x99U, value);
        append_out_immediate(program, 0x99U, static_cast<std::uint8_t>(0x80U | (reg & 0x3FU)));
    }

    [[nodiscard]] std::vector<std::uint8_t> make_generated_boot_bios() {
        std::vector<std::uint8_t> bios(0x8000U, 0x76U);
        std::vector<std::uint8_t> program;
        program.reserve(96U);

        append_vdp_vram_write(program, 0x0008U, 0x80U);
        append_vdp_vram_write(program, 0x0800U, 0x01U);
        append_vdp_vram_write(program, 0x2000U, 0xF1U);
        append_vdp_register_write(program, 1U, 0x40U);
        append_vdp_register_write(program, 2U, 0x02U);
        append_vdp_register_write(program, 3U, 0x80U);
        append_vdp_register_write(program, 4U, 0x00U);
        program.push_back(0x76U);

        REQUIRE(program.size() <= bios.size());
        std::copy(program.begin(), program.end(), bios.begin());
        return bios;
    }

    [[nodiscard]] bool framebuffer_is_uniform(const mnemos::chips::frame_buffer_view& fb) {
        if (fb.pixels == nullptr || fb.width == 0U || fb.height == 0U) {
            return true;
        }
        const std::uint32_t first = fb.pixels[0];
        const std::uint32_t stride = fb.effective_stride();
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                if (row[x] != first) {
                    return false;
                }
            }
        }
        return true;
    }

    [[nodiscard]] std::uint64_t framebuffer_digest(const mnemos::chips::frame_buffer_view& fb) {
        std::uint64_t hash = 1469598103934665603ULL;
        const auto mix = [&hash](std::uint64_t value) {
            hash ^= value;
            hash *= 1099511628211ULL;
        };
        mix(fb.width);
        mix(fb.height);
        const std::uint32_t stride = fb.effective_stride();
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                mix(row[x]);
            }
        }
        return hash;
    }
} // namespace

TEST_CASE("msx adapter constructs and steps frames", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U));

    const auto fb0 = adapter.current_frame();
    CHECK(fb0.width == static_cast<std::uint32_t>(tms9918a::display_width));
    CHECK(fb0.height == static_cast<std::uint32_t>(tms9918a::display_height));

    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.system().vdp.frame_index() == 2U);
    CHECK(adapter.chips().size() == 4U);
    CHECK(adapter.current_frame().pixels != nullptr);
}

TEST_CASE("msx adapter generated BIOS renders deterministic frames", "[apps][player][msx][boot]") {
    msx_adapter adapter(make_generated_boot_bios());
    adapter.step_one_frame();
    adapter.step_one_frame();

    const auto fb = adapter.current_frame();
    REQUIRE(fb.pixels != nullptr);
    CHECK_FALSE(framebuffer_is_uniform(fb));
    CHECK(fb.pixels[0] == 0x00FFFFFFU);
    CHECK(fb.pixels[1] == 0x00000000U);
    CHECK(framebuffer_digest(fb) == 0x94B67168FBE6DCC4ULL);

    msx_adapter adapter2(make_generated_boot_bios());
    adapter2.step_one_frame();
    adapter2.step_one_frame();
    CHECK(framebuffer_digest(adapter2.current_frame()) == framebuffer_digest(fb));
}

TEST_CASE("msx adapter maps player input to joystick port 1", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U));
    mnemos::frontend_sdk::controller_state state{};
    state.right = true;
    state.a = true;
    adapter.apply_input(0, state);

    configure_psg_gpio(adapter.system());
    adapter.system().write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((adapter.system().read_io(0xA2U) & 0x3FU) == 0x27U);
}

TEST_CASE("msx adapter maps player input to joystick port 2", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U));
    mnemos::frontend_sdk::controller_state state{};
    state.left = true;
    state.x = true;
    adapter.apply_input(2, state);

    auto& sys = adapter.system();
    configure_psg_gpio(sys);
    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.write_io(0xA1U, 0x4FU);
    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys.read_io(0xA2U) & 0x3FU) == 0x1BU);
}

TEST_CASE("msx adapter routes frontend mouse input to joystick port 2 by default",
          "[apps][player][msx][mouse]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U));

    mnemos::frontend_sdk::controller_state joystick{};
    joystick.right = true;
    joystick.a = true;
    adapter.apply_input(0, joystick);

    mnemos::frontend_sdk::controller_state pointer{};
    pointer.aim_x = 0;
    pointer.aim_y = 0;
    adapter.apply_input(3, pointer);
    pointer.aim_x = 0x12;
    pointer.aim_y = 0x0C;
    pointer.trigger = true;
    adapter.apply_input(3, pointer);

    auto& sys = adapter.system();
    configure_psg_gpio(sys);
    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.write_io(0xA1U, 0x0FU);
    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys.read_io(0xA2U) & 0x3FU) == 0x27U);

    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.write_io(0xA1U, 0x4FU);
    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys.read_io(0xA2U) & 0x3FU) == 0x21U);

    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.write_io(0xA1U, 0x6FU);
    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys.read_io(0xA2U) & 0x3FU) == 0x22U);
}

TEST_CASE("msx adapter mouse tracker state round-trips with the save state",
          "[apps][player][msx][mouse][state]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U));

    mnemos::frontend_sdk::controller_state pointer{};
    pointer.aim_x = 0;
    pointer.aim_y = 0;
    adapter.apply_input(3, pointer);

    const std::vector<std::uint8_t> saved = adapter.save_state();
    REQUIRE_FALSE(saved.empty());

    pointer.aim_x = 0x45;
    pointer.aim_y = 0x67;
    adapter.apply_input(3, pointer);

    const auto result = adapter.load_state(saved);
    REQUIRE(result.ok());
    adapter.apply_input(3, pointer);

    auto& sys = adapter.system();
    configure_psg_gpio(sys);
    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.write_io(0xA1U, 0x4FU);
    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys.read_io(0xA2U) & 0x3FU) == 0x34U);
}

TEST_CASE("msx adapter maps physical keyboard usages to the MSX matrix",
          "[apps][player][msx][keyboard]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U));
    mnemos::frontend_sdk::controller_state state{};
    state.set_key(0x04U, true); // A
    state.set_key(0x1EU, true); // 1
    state.set_key(0x28U, true); // Return
    state.set_key(0x50U, true); // Left arrow

    adapter.apply_input(1, state);

    CHECK(adapter.system().keyboard_rows[0] == 0xFDU);
    CHECK(adapter.system().keyboard_rows[2] == 0xBFU);
    CHECK(adapter.system().keyboard_rows[7] == 0x7FU);
    CHECK(adapter.system().keyboard_rows[8] == 0xEFU);

    adapter.apply_input(1, mnemos::frontend_sdk::controller_state{});
    CHECK(adapter.system().keyboard_rows[0] == 0xFFU);
    CHECK(adapter.system().keyboard_rows[2] == 0xFFU);
    CHECK(adapter.system().keyboard_rows[7] == 0xFFU);
    CHECK(adapter.system().keyboard_rows[8] == 0xFFU);
}

TEST_CASE("msx adapter publishes session and media capabilities", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                        std::vector<std::uint8_t>(0x4000U, 0xFFU), {}, "Tiny MSX");
    REQUIRE(adapter.session_capabilities().input_ports.size() == 5U);
    CHECK(adapter.session_capabilities().input_ports[2].device_id == "msx.joystick.port.2");
    CHECK(adapter.session_capabilities().input_ports[3].device_id == "msx.mouse.port.2");
    CHECK(adapter.session_capabilities().input_ports[3].format ==
          mnemos::frontend_sdk::input_device_format::mouse);
    CHECK(adapter.session_capabilities().input_ports[4].device_id == "msx.mouse.port.1");
    CHECK(adapter.session_capabilities().save_state_supported);
    CHECK(adapter.session_capabilities().frame_exact_save_state);
    CHECK(adapter.media_capabilities().media.size() == 2U);
    CHECK(adapter.media_capabilities().media[1].id == "cart");
}

TEST_CASE("msx adapter publishes resident logo ROM", "[apps][player][msx][firmware]") {
    std::vector<std::uint8_t> logo(0x4000U, 0x55U);
    logo.front() = 0x5AU;
    logo.back() = 0xA5U;

    msx_adapter adapter(make_generated_boot_bios(), {}, {}, "MSX Logo", nullptr, {}, {}, {}, {},
                        std::move(logo));

    const auto* logo_media = find_media(adapter, "logo_rom");
    REQUIRE(logo_media != nullptr);
    CHECK(logo_media->label == "MSX Logo ROM");
    CHECK(logo_media->byte_count == 0x4000U);
    CHECK(logo_media->provider_id == "msx.adapter");

    CHECK(adapter.system().bus.read8(0x8000U) == 0x5AU);
    CHECK(adapter.system().bus.read8(0xBFFFU) == 0xA5U);

    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Logo ROM" &&
                                 field.value == "slot 0 $8000-$BFFF";
                      }));
}

TEST_CASE("msx adapter save state restores board and scheduler state", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                        std::vector<std::uint8_t>(0x4000U, 0xFFU),
                        {.ram_mapper_segments = 4U, .rtc_enabled = true});
    auto& sys = adapter.system();

    sys.mapped_ram[0x1234U] = 0x5AU;
    sys.primary_slot_select = 0xA5U;
    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.write_io(0xA1U, 0x40U);
    adapter.scheduler().run_master_cycles(23U);
    const std::uint64_t saved_cycle = adapter.scheduler().master_cycle();

    const std::vector<std::uint8_t> saved = adapter.save_state();
    REQUIRE_FALSE(saved.empty());

    sys.mapped_ram[0x1234U] = 0x00U;
    sys.primary_slot_select = 0x00U;
    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.write_io(0xA1U, 0x00U);
    adapter.scheduler().run_master_cycles(17U);

    const auto result = adapter.load_state(saved);
    REQUIRE(result.ok());
    CHECK(result.master_cycle == saved_cycle);
    CHECK(adapter.scheduler().master_cycle() == saved_cycle);
    CHECK(sys.mapped_ram[0x1234U] == 0x5AU);
    CHECK(sys.primary_slot_select == 0xA5U);
    CHECK(sys.psg.read_reg(mnemos::chips::audio::ssg::reg_port_b) == 0x40U);
}

TEST_CASE("msx adapter rejects save states from the alternate MSX video model",
          "[apps][player][msx][state]") {
    msx_adapter msx1(std::vector<std::uint8_t>(0x8000U, 0x00U));
    msx_adapter msx2_video(std::vector<std::uint8_t>(0x8000U, 0x00U), {},
                           {.video_model = mnemos::manifests::msx::msx_video_model::v9938});

    const std::vector<std::uint8_t> saved = msx2_video.save_state();
    const auto result = msx1.load_state(saved);
    CHECK(result.status == mnemos::runtime::load_status::manifest_mismatch);
}

TEST_CASE("msx adapter exposes a second cartridge slot", "[apps][player][msx]") {
    std::vector<std::uint8_t> cart1(0x8000U, 0xFFU);
    cart1[0] = 0x11U;
    std::vector<std::uint8_t> cart2(0x8000U, 0xFFU);
    cart2[0] = 0x22U;

    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), cart1, {}, "Two Cart", nullptr,
                        {}, {}, cart2);
    auto& sys = adapter.system();
    REQUIRE(sys.cartridge2.size() == 0x8000U);
    sys.primary_slot_select = 0x28U; // pages 1/2 -> slot 2
    CHECK(sys.bus.read8(0x4000U) == 0x22U);

    REQUIRE(adapter.media_capabilities().media.size() == 3U);
    CHECK(adapter.media_capabilities().media[1].id == "cart");
    CHECK(adapter.media_capabilities().media[2].id == "cart2");

    bool has_cart2_spec = false;
    for (const auto& field : adapter.system_spec()) {
        has_cart2_spec =
            has_cart2_spec || (field.label == "Cart Slot 2" && field.value == "mounted");
    }
    CHECK(has_cart2_spec);
}

TEST_CASE("msx adapter registry applies Korean MSX mapper override", "[apps][player][msx]") {
    auto system = mnemos::frontend_sdk::adapter_registry::instance().create(
        "msx", {.rom = std::vector<std::uint8_t>(0x8000U, 0x00U),
                .display_name = "Korean Mapper",
                .additional_media = {make_banked_8k_rom()},
                .mapper_override = "korean-msx"});
    REQUIRE(system != nullptr);

    auto* adapter = dynamic_cast<msx_adapter*>(system.get());
    REQUIRE(adapter != nullptr);
    auto& sys = adapter->system();
    sys.primary_slot_select = 0x15U; // pages 0/1/2 -> slot 1
    sys.bus.write8(0x0002U, 5U);

    CHECK(sys.bus.read8(0x4000U) == 5U);
    CHECK(std::any_of(adapter->system_spec().begin(), adapter->system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Mapper" && field.value == "Korean MSX";
                      }));
}

TEST_CASE("msx adapter registry auto-detects cartridge mapper", "[apps][player][msx][mapper]") {
    std::vector<std::uint8_t> cart = make_banked_8k_rom();
    add_mapper_signatures(cart, {0x6000U, 0x6800U, 0x7000U, 0x7800U});

    auto system = mnemos::frontend_sdk::adapter_registry::instance().create(
        "msx", {.rom = std::vector<std::uint8_t>(0x8000U, 0x00U),
                .display_name = "ASCII8 Auto",
                .additional_media = {cart}});
    REQUIRE(system != nullptr);

    auto* adapter = dynamic_cast<msx_adapter*>(system.get());
    REQUIRE(adapter != nullptr);
    auto& sys = adapter->system();
    REQUIRE(sys.mapper == mnemos::manifests::msx::msx_cartridge_mapper::ascii8);

    sys.primary_slot_select = 0x15U; // pages 0/1/2 -> cartridge slot
    CHECK(sys.bus.read8(0x4000U) == 0U);
    sys.bus.write8(0x6000U, 5U);
    CHECK(sys.bus.read8(0x4000U) == 5U);
    CHECK(sys.cart_8k_bank[0] == 5U);
    CHECK(std::any_of(adapter->system_spec().begin(), adapter->system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Mapper" && field.value == "ASCII8";
                      }));
}

TEST_CASE("msx adapter registry selects MSX2 V9938 video", "[apps][player][msx]") {
    auto system = mnemos::frontend_sdk::adapter_registry::instance().create(
        "msx", {.rom = std::vector<std::uint8_t>(0x8000U, 0x00U), .msx2 = true});
    REQUIRE(system != nullptr);

    auto* adapter = dynamic_cast<msx_adapter*>(system.get());
    REQUIRE(adapter != nullptr);
    CHECK(adapter->system().video_model == mnemos::manifests::msx::msx_video_model::v9938);
    REQUIRE_FALSE(adapter->chips().empty());
    CHECK(adapter->chips()[0]->metadata().part_number == std::string_view{"V9938"});

    bool has_msx2_spec = false;
    for (const auto& field : adapter->system_spec()) {
        has_msx2_spec = has_msx2_spec || (field.label == "System" && field.value == "MSX2");
    }
    CHECK(has_msx2_spec);

    auto fb = adapter->current_frame();
    CHECK(fb.width == static_cast<std::uint32_t>(v9938::display_width_256));
    CHECK(fb.height == static_cast<std::uint32_t>(v9938::display_height_192));
    CHECK(fb.stride == static_cast<std::uint32_t>(v9938::max_width));

    adapter->system().write_io(0x99U, 0x06U);
    adapter->system().write_io(0x99U, 0x80U);
    adapter->system().write_io(0x99U, 0x40U);
    adapter->system().write_io(0x99U, 0x81U);
    adapter->system().write_io(0x99U, 0x80U);
    adapter->system().write_io(0x99U, 0x89U);
    fb = adapter->current_frame();
    CHECK(fb.height == static_cast<std::uint32_t>(v9938::display_height_212));

    adapter->system().write_io(0x99U, 0x88U);
    adapter->system().write_io(0x99U, 0x89U);
    fb = adapter->current_frame();
    CHECK(fb.height == static_cast<std::uint32_t>(v9938::display_height_424));
}

TEST_CASE("msx adapter registry applies second cartridge mapper override", "[apps][player][msx]") {
    std::vector<std::uint8_t> cart1(0x8000U, 0xFFU);
    std::vector<std::uint8_t> cart2 = make_banked_8k_rom();
    for (std::size_t bank = 0; bank < 8U; ++bank) {
        cart2[bank * 0x2000U] = static_cast<std::uint8_t>(0x40U + bank);
    }

    auto system = mnemos::frontend_sdk::adapter_registry::instance().create(
        "msx", {.rom = std::vector<std::uint8_t>(0x8000U, 0x00U),
                .display_name = "Dual Cart",
                .additional_media = {cart1, cart2},
                .mapper2_override = "ascii8"});
    REQUIRE(system != nullptr);

    auto* adapter = dynamic_cast<msx_adapter*>(system.get());
    REQUIRE(adapter != nullptr);
    auto& sys = adapter->system();
    REQUIRE(sys.cartridge2_mapper == mnemos::manifests::msx::msx_cartridge_mapper::ascii8);
    sys.primary_slot_select = 0x28U; // pages 1/2 -> slot 2

    CHECK(sys.bus.read8(0x4000U) == 0x40U);
    sys.bus.write8(0x6000U, 5U);
    CHECK(sys.bus.read8(0x4000U) == 0x45U);
    CHECK(sys.cart2_8k_bank[0] == 5U);
}

TEST_CASE("msx adapter exposes mapper-backed RAM when configured", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), {}, {.ram_mapper_segments = 8U});

    const auto memories = adapter.memory_views();
    REQUIRE(memories.size() == 1U);
    CHECK(memories[0]->name() == "work_ram");
    CHECK(memories[0]->bytes().size() == 0x20000U);
}

TEST_CASE("msx adapter exposes optional Kanji ROM media", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), {}, {}, "Kanji Boot", nullptr,
                        {}, make_kanji_rom());

    bool has_kanji_spec = false;
    for (const auto& field : adapter.system_spec()) {
        has_kanji_spec =
            has_kanji_spec || (field.label == "Kanji ROM" && field.value == "JIS level 1+2");
    }
    CHECK(has_kanji_spec);

    REQUIRE(adapter.media_capabilities().media.size() == 2U);
    CHECK(adapter.media_capabilities().media[1].id == "kanji");
    CHECK(adapter.media_capabilities().media[1].provider_id == "msx.kanji_rom");
    CHECK(adapter.media_capabilities().media[1].revision == "jis-level-1-2");

    auto& sys = adapter.system();
    sys.write_io(0xD8U, 0x23U);
    sys.write_io(0xD9U, 0x04U);
    CHECK(sys.read_io(0xD9U) == 0x4BU);
    sys.write_io(0xDAU, 0x05U);
    sys.write_io(0xDBU, 0x01U);
    CHECK(sys.read_io(0xDBU) == 0xD2U);
}

TEST_CASE("msx adapter exposes optional RP-5C01 RTC hardware", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), {}, {.rtc_enabled = true});
    CHECK(adapter.chips().size() == 5U);
    CHECK(adapter.system().rtc_enabled);

    bool has_rtc_spec = false;
    for (const auto& field : adapter.system_spec()) {
        has_rtc_spec = has_rtc_spec || (field.label == "RTC" && field.value == "RP-5C01");
    }
    CHECK(has_rtc_spec);

    auto& sys = adapter.system();
    sys.rtc.set_cycles_per_second(2U);
    sys.rtc.set_time_24h(26U, 1U, 1U, 4U, 0U, 0U, 0U);
    sys.rtc.tick(2U);
    CHECK(sys.rtc.raw_block_register(0, 0) == 1U);
}

TEST_CASE("msx adapter mounts CAS media as cassette tape", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), make_cas({0xD3U, 0x00U}), {},
                        "Tape Game");
    CHECK(adapter.chips().size() == 4U);
    CHECK(adapter.system().cartridge.empty());
    CHECK(adapter.system().cassette.loaded());
    CHECK(adapter.system().cassette.playing());

    bool has_tape_spec = false;
    for (const auto& field : adapter.system_spec()) {
        has_tape_spec = has_tape_spec || (field.label == "Cassette" && field.value == "MSX CAS");
    }
    CHECK(has_tape_spec);
    REQUIRE(adapter.media_capabilities().media.size() == 2U);
    CHECK(adapter.media_capabilities().media[1].id == "tape");
    CHECK(adapter.media_capabilities().media[1].provider_id == "msx.cassette");
}

TEST_CASE("msx adapter mounts DSK media as a WD1793 floppy disk", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), make_dsk(), {}, "Disk Game",
                        nullptr, std::vector<std::uint8_t>(0x4000U, 0xFFU));
    CHECK(adapter.chips().size() == 5U);
    CHECK(adapter.media_count() == 1U);
    CHECK(adapter.system().cartridge.empty());
    CHECK(adapter.system().disk_enabled);
    CHECK(adapter.system().fdc.mounted());

    bool has_disk_spec = false;
    bool has_disk_rom_spec = false;
    for (const auto& field : adapter.system_spec()) {
        has_disk_spec = has_disk_spec || (field.label == "Disk" && field.value == "MSX DSK");
        has_disk_rom_spec =
            has_disk_rom_spec || (field.label == "Disk ROM" && field.value == "WD1793 interface");
    }
    CHECK(has_disk_spec);
    CHECK(has_disk_rom_spec);
    REQUIRE(adapter.media_capabilities().media.size() == 2U);
    CHECK(adapter.media_capabilities().media[1].id == "disk");
    CHECK(adapter.media_capabilities().media[1].provider_id == "msx.fdc");
}

TEST_CASE("msx adapter publishes and preserves read-only DSK media state",
          "[apps][player][msx][fdc]") {
    auto disk1 = make_dsk();
    disk1[0] = 0x31U;
    auto disk2 = make_dsk();
    disk2[0] = 0x42U;

    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), disk1,
                        mnemos::manifests::msx::msx_config{.disk_write_protected = true},
                        "Read Only Disk", nullptr, std::vector<std::uint8_t>{},
                        std::vector<std::uint8_t>{}, std::vector<std::uint8_t>{}, {disk2});

    REQUIRE(adapter.system().disk_enabled);
    CHECK(adapter.system().fdc.write_protected());
    REQUIRE(adapter.media_capabilities().media.size() >= 2U);
    CHECK(adapter.media_capabilities().media[1].id == "disk");
    CHECK(adapter.media_capabilities().media[1].revision == "read-only");
    CHECK(adapter.media_capabilities().media[1].cache_hint == "resident_read_only");
    const auto* disk2_media = find_media(adapter, "disk.1");
    REQUIRE(disk2_media != nullptr);
    CHECK(disk2_media->label == "Disk 2");
    CHECK(disk2_media->revision == "read-only");
    CHECK(disk2_media->cache_hint == "resident_read_only");

    bool has_write_spec = false;
    for (const auto& field : adapter.system_spec()) {
        has_write_spec =
            has_write_spec || (field.label == "Disk Write" && field.value == "Read-only");
    }
    CHECK(has_write_spec);

    REQUIRE(adapter.insert_media(1U));
    CHECK(adapter.system().fdc.write_protected());
    CHECK(adapter.system().fdc.disk_image()[0] == 0x42U);
}

TEST_CASE("msx adapter swaps additional DSK media", "[apps][player][msx]") {
    auto disk1 = make_dsk();
    disk1[0] = 0x11U;
    auto disk2 = make_dsk();
    disk2[0] = 0x22U;

    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), disk1, {}, "Disk Set", nullptr,
                        std::vector<std::uint8_t>(0x4000U, 0xFFU), {}, {}, {disk2});
    REQUIRE(adapter.media_count() == 2U);
    CHECK(adapter.current_media_index() == 0U);
    REQUIRE(adapter.system().fdc.mounted());
    CHECK(adapter.system().fdc.disk_image()[0] == 0x11U);

    bool has_disks_spec = false;
    for (const auto& field : adapter.system_spec()) {
        has_disks_spec = has_disks_spec || (field.label == "Disks" && field.value == "2");
    }
    CHECK(has_disks_spec);
    const auto* disk1_media = find_media(adapter, "disk");
    REQUIRE(disk1_media != nullptr);
    CHECK(disk1_media->label == "Disk Set");
    const auto* disk2_media = find_media(adapter, "disk.1");
    REQUIRE(disk2_media != nullptr);
    CHECK(disk2_media->label == "Disk 2");
    CHECK(disk2_media->provider_id == "msx.fdc");

    REQUIRE(adapter.insert_media(1U));
    CHECK(adapter.current_media_index() == 1U);
    CHECK(adapter.system().fdc.disk_image()[0] == 0x22U);

    CHECK_FALSE(adapter.insert_media(2U));
    CHECK(adapter.current_media_index() == 1U);
    CHECK(adapter.system().fdc.disk_image()[0] == 0x22U);
}

TEST_CASE("msx adapter save state restores selected resident disk index",
          "[apps][player][msx][state][fdc]") {
    auto disk1 = make_dsk();
    disk1[0] = 0x11U;
    auto disk2 = make_dsk();
    disk2[0] = 0x22U;

    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), disk1, {}, "Disk Set", nullptr,
                        std::vector<std::uint8_t>(0x4000U, 0xFFU), {}, {}, {disk2});
    REQUIRE(adapter.media_count() == 2U);

    const std::vector<std::uint8_t> disk0_state = adapter.save_state();
    REQUIRE_FALSE(disk0_state.empty());
    REQUIRE(adapter.insert_media(1U));
    REQUIRE(adapter.current_media_index() == 1U);

    auto load_disk0 = adapter.load_state(disk0_state);
    REQUIRE(load_disk0.ok());
    CHECK(adapter.current_media_index() == 0U);
    CHECK(adapter.system().fdc.disk_image()[0] == 0x11U);

    REQUIRE(adapter.insert_media(1U));
    const std::vector<std::uint8_t> disk1_state = adapter.save_state();
    REQUIRE_FALSE(disk1_state.empty());
    REQUIRE(adapter.insert_media(0U));
    REQUIRE(adapter.current_media_index() == 0U);

    auto load_disk1 = adapter.load_state(disk1_state);
    REQUIRE(load_disk1.ok());
    CHECK(adapter.current_media_index() == 1U);
    CHECK(adapter.system().fdc.disk_image()[0] == 0x22U);
}

TEST_CASE("msx adapter registry mounts a DSK beside cartridge media", "[apps][player][msx]") {
    auto cart = make_banked_8k_rom();
    auto disk = make_dsk();
    disk[0] = 0x5DU;

    auto system = mnemos::frontend_sdk::adapter_registry::instance().create(
        "msx", {.rom = std::vector<std::uint8_t>(0x8000U, 0x00U),
                .display_name = "Cart Plus Disk",
                .additional_media = {cart, disk},
                .bios_images = {std::vector<std::uint8_t>(0x4000U, 0xFFU)}});
    REQUIRE(system != nullptr);

    auto* adapter = dynamic_cast<msx_adapter*>(system.get());
    REQUIRE(adapter != nullptr);
    CHECK(adapter->system().cartridge.size() == cart.size());
    REQUIRE(adapter->media_count() == 1U);
    REQUIRE(adapter->system().fdc.mounted());
    CHECK(adapter->system().fdc.disk_image()[0] == 0x5DU);

    bool has_disk_spec = false;
    for (const auto& field : adapter->system_spec()) {
        has_disk_spec = has_disk_spec || (field.label == "Disk" && field.value == "MSX DSK");
    }
    CHECK(has_disk_spec);
    REQUIRE(adapter->media_capabilities().media.size() >= 3U);
    CHECK(adapter->media_capabilities().media[1].id == "cart");
    CHECK(adapter->media_capabilities().media[2].id == "disk");
}

TEST_CASE("msx adapter exposes and mixes MSX-MUSIC OPLL audio", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), {}, {.fm_music_enabled = true});
    CHECK(adapter.chips().size() == 5U);
    CHECK(adapter.system().fm_music_enabled);

    bool has_fm_spec = false;
    for (const auto& field : adapter.system_spec()) {
        has_fm_spec = has_fm_spec || (field.label == "Audio" && field.value == "MSX-MUSIC");
    }
    CHECK(has_fm_spec);

    auto& sys = adapter.system();
    sys.fm.set_clock_divider(1);
    sys.write_io(0x7CU, 0x30U);
    sys.write_io(0x7DU, 0x70U);
    sys.write_io(0x7CU, 0x10U);
    sys.write_io(0x7DU, 0xA0U);
    sys.write_io(0x7CU, 0x20U);
    sys.write_io(0x7DU, 0x18U);

    adapter.step_one_frame();
    const auto chunk = adapter.drain_audio();
    CHECK(chunk.sample_rate != 0U);
    CHECK(chunk.frame_count != 0U);
    REQUIRE(chunk.samples != nullptr);

    bool any_nonzero = false;
    for (std::uint32_t i = 0; i < chunk.frame_count * 2U; ++i) {
        any_nonzero = any_nonzero || chunk.samples[i] != 0;
    }
    CHECK(any_nonzero);
}

TEST_CASE("msx adapter exposes FM-PAC SRAM as battery RAM", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                        std::vector<std::uint8_t>(0x8000U, 0xFFU),
                        {.fm_music_enabled = true, .fmpac_sram_enabled = true});
    REQUIRE(adapter.battery_ram().size() == 0x2000U);
    CHECK(adapter.battery_ram_media_id() == "fmpac");

    bool has_pac_spec = false;
    for (const auto& field : adapter.system_spec()) {
        has_pac_spec = has_pac_spec || (field.label == "PAC SRAM" && field.value == "8 KiB");
    }
    CHECK(has_pac_spec);

    auto& sys = adapter.system();
    sys.primary_slot_select = 0x14U;
    sys.bus.write8(0x5FFEU, 0x4DU);
    sys.bus.write8(0x5FFFU, 0x69U);
    sys.bus.write8(0x4000U, 0xA6U);
    CHECK(adapter.battery_ram()[0] == 0xA6U);
    CHECK(sys.bus.read8(0x4000U) == 0xA6U);
}

TEST_CASE("msx adapter exposes ASCII cartridge SRAM as battery RAM", "[apps][player][msx]") {
    auto cart = make_banked_8k_rom();
    msx_adapter adapter(
        std::vector<std::uint8_t>(0x8000U, 0x00U), cart,
        {.cartridge_mapper = mnemos::manifests::msx::msx_cartridge_mapper::ascii8_sram8});
    REQUIRE(adapter.battery_ram().size() == 0x2000U);
    CHECK(adapter.battery_ram_media_id() == "cart");

    bool has_cart_sram_spec = false;
    for (const auto& field : adapter.system_spec()) {
        has_cart_sram_spec =
            has_cart_sram_spec || (field.label == "Cart SRAM" && field.value == "8 KiB");
    }
    CHECK(has_cart_sram_spec);

    auto& sys = adapter.system();
    sys.primary_slot_select = 0x14U;
    sys.bus.write8(0x7000U, 0x80U);
    sys.bus.write8(0x8000U, 0xC4U);
    CHECK(adapter.battery_ram()[0] == 0xC4U);
    CHECK(sys.bus.read8(0x8000U) == 0xC4U);
}

TEST_CASE("msx adapter exposes second cartridge SRAM as battery RAM", "[apps][player][msx]") {
    auto system = mnemos::frontend_sdk::adapter_registry::instance().create(
        "msx",
        {.rom = std::vector<std::uint8_t>(0x8000U, 0x00U),
         .display_name = "Slot 2 SRAM",
         .additional_media = {std::vector<std::uint8_t>(0x8000U, 0xFFU), make_banked_8k_rom()},
         .mapper2_override = "ascii8-sram8"});
    REQUIRE(system != nullptr);

    auto* adapter = dynamic_cast<msx_adapter*>(system.get());
    REQUIRE(adapter != nullptr);
    REQUIRE(adapter->battery_ram().size() == 0x2000U);
    CHECK(adapter->battery_ram_media_id() == "cart2");

    bool has_cart2_sram_spec = false;
    for (const auto& field : adapter->system_spec()) {
        has_cart2_sram_spec =
            has_cart2_sram_spec || (field.label == "Cart Slot 2 SRAM" && field.value == "8 KiB");
    }
    CHECK(has_cart2_sram_spec);

    auto& sys = adapter->system();
    sys.primary_slot_select = 0x28U; // pages 1/2 -> slot 2
    sys.bus.write8(0x7000U, 0x80U);
    sys.bus.write8(0x8000U, 0xA9U);
    CHECK(adapter->battery_ram()[0] == 0xA9U);
    CHECK(sys.bus.read8(0x8000U) == 0xA9U);
}

TEST_CASE("msx adapter exposes and mixes Konami SCC cartridge audio", "[apps][player][msx]") {
    msx_adapter adapter(
        std::vector<std::uint8_t>(0x8000U, 0x00U), std::vector<std::uint8_t>(0x2000U * 8U, 0xFFU),
        {.cartridge_mapper = mnemos::manifests::msx::msx_cartridge_mapper::konami_scc});
    CHECK(adapter.chips().size() == 5U);

    auto& sys = adapter.system();
    sys.primary_slot_select = 0x14U;
    sys.bus.write8(0x9000U, 0x3FU);
    sys.bus.write8(0x9800U, 0x7FU);
    sys.bus.write8(0x9880U, 0x10U); // non-zero period keeps the channel audible
    sys.bus.write8(0x9881U, 0x00U);
    sys.bus.write8(0x988AU, 0x0FU);
    sys.bus.write8(0x988FU, 0x01U);

    adapter.step_one_frame();
    const auto chunk = adapter.drain_audio();
    CHECK(chunk.sample_rate != 0U);
    CHECK(chunk.frame_count != 0U);
    REQUIRE(chunk.samples != nullptr);

    bool any_nonzero = false;
    for (std::uint32_t i = 0; i < chunk.frame_count * 2U; ++i) {
        any_nonzero = any_nonzero || chunk.samples[i] != 0;
    }
    CHECK(any_nonzero);
}

TEST_CASE("msx adapter exposes and mixes Konami SCC audio from cartridge slot 2",
          "[apps][player][msx]") {
    msx_adapter adapter(
        std::vector<std::uint8_t>(0x8000U, 0x00U), std::vector<std::uint8_t>(0x8000U, 0xFFU),
        {.cartridge2_mapper = mnemos::manifests::msx::msx_cartridge_mapper::konami_scc}, {},
        nullptr, {}, {}, std::vector<std::uint8_t>(0x2000U * 8U, 0xFFU));
    CHECK(adapter.chips().size() == 5U);

    bool has_scc_spec = false;
    for (const auto& field : adapter.system_spec()) {
        has_scc_spec = has_scc_spec || (field.label == "Cart Audio" && field.value == "Konami SCC");
    }
    CHECK(has_scc_spec);

    auto& sys = adapter.system();
    sys.primary_slot_select = 0x28U; // pages 1/2 -> slot 2
    sys.bus.write8(0x9000U, 0x3FU);
    sys.bus.write8(0x9800U, 0x7FU);
    sys.bus.write8(0x9880U, 0x10U); // non-zero period keeps the channel audible
    sys.bus.write8(0x9881U, 0x00U);
    sys.bus.write8(0x988AU, 0x0FU);
    sys.bus.write8(0x988FU, 0x01U);

    adapter.step_one_frame();
    const auto chunk = adapter.drain_audio();
    CHECK(chunk.sample_rate != 0U);
    CHECK(chunk.frame_count != 0U);
    REQUIRE(chunk.samples != nullptr);

    bool any_nonzero = false;
    for (std::uint32_t i = 0; i < chunk.frame_count * 2U; ++i) {
        any_nonzero = any_nonzero || chunk.samples[i] != 0;
    }
    CHECK(any_nonzero);
}
