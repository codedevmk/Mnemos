#include "msx2_adapter.hpp"

#include "adapter_registry.hpp"
#include "msx_cassette.hpp"
#include "wd1793.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    using mnemos::apps::player::adapters::msx2::msx2_adapter;
    using mnemos::manifests::msx2::msx2_cartridge_mapper;

    std::vector<std::uint8_t> make_dsk() {
        std::vector<std::uint8_t> disk(static_cast<std::size_t>(80U) * 2U * 9U *
                                           mnemos::chips::storage::wd1793::sector_size,
                                       0xE5U);
        disk[0] = 0x41U;
        return disk;
    }

    std::vector<std::uint8_t> make_cas(std::initializer_list<std::uint8_t> bytes) {
        std::vector<std::uint8_t> cas(
            mnemos::chips::storage::msx_cassette::cas_header_magic.begin(),
            mnemos::chips::storage::msx_cassette::cas_header_magic.end());
        cas.insert(cas.end(), bytes.begin(), bytes.end());
        return cas;
    }

    const mnemos::frontend_sdk::media_image_info* find_media(const msx2_adapter& adapter,
                                                             std::string_view id) {
        const auto& media = adapter.media_capabilities().media;
        const auto it = std::find_if(media.begin(), media.end(),
                                     [&](const auto& image) { return image.id == id; });
        return it == media.end() ? nullptr : &*it;
    }

    std::vector<std::uint8_t> make_8k_paged_cart(std::size_t pages) {
        std::vector<std::uint8_t> cart(pages * 0x2000U, 0x00U);
        for (std::size_t page = 0; page < pages; ++page) {
            cart[page * 0x2000U] = static_cast<std::uint8_t>(0x80U + page);
        }
        return cart;
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

    void select_primary_slots(mnemos::manifests::msx2::msx2_system& sys, std::uint8_t value) {
        sys.io_write(0xABU, 0x82U);
        sys.io_write(0xA8U, value);
    }

    void configure_psg_gpio(mnemos::manifests::msx2::msx2_system& sys) {
        sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_mixer);
        sys.io_write(0xA1U, 0xBFU);
        sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
        sys.io_write(0xA1U, 0x0FU);
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

TEST_CASE("msx2 adapter constructs and steps frames", "[apps][player][msx2]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                         std::vector<std::uint8_t>(0x8000U, 0x00U));

    const auto fb0 = adapter.current_frame();
    CHECK(fb0.width == 256U);
    CHECK(fb0.height == 192U);

    adapter.step_one_frame();
    adapter.step_one_frame();

    REQUIRE(adapter.session_capabilities().input_ports.size() == 5U);
    CHECK(adapter.session_capabilities().input_ports[0].device_id == "msx2.joystick.port.1");
    CHECK(adapter.session_capabilities().input_ports[1].device_id == "msx2.keyboard.matrix");
    CHECK(adapter.session_capabilities().input_ports[2].device_id == "msx2.joystick.port.2");
    CHECK(adapter.session_capabilities().input_ports[3].device_id == "msx2.mouse.port.2");
    CHECK(adapter.session_capabilities().input_ports[3].format ==
          mnemos::frontend_sdk::input_device_format::mouse);
    CHECK(adapter.session_capabilities().input_ports[4].device_id == "msx2.mouse.port.1");
    CHECK(adapter.session_capabilities().save_state_supported);
    CHECK(adapter.session_capabilities().frame_exact_save_state);
    CHECK(adapter.chips().size() == 5U);
    CHECK(std::find(adapter.chips().begin(), adapter.chips().end(), &adapter.system().scc) ==
          adapter.chips().end());
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Audio" && field.value == "PSG";
                      }));
    CHECK(adapter.current_frame().pixels != nullptr);
    CHECK(adapter.system().vdp.frame_index() == 2U);
}

TEST_CASE("msx2 adapter generated BIOS renders deterministic frames",
          "[apps][player][msx2][boot]") {
    msx2_adapter adapter(make_generated_boot_bios(), {});
    adapter.step_one_frame();
    adapter.step_one_frame();

    const auto fb = adapter.current_frame();
    REQUIRE(fb.pixels != nullptr);
    CHECK_FALSE(framebuffer_is_uniform(fb));
    CHECK(fb.pixels[0] == 0x00FFFFFFU);
    CHECK(fb.pixels[1] == 0x00000000U);
    CHECK(framebuffer_digest(fb) == 0x94B67168FBE6DCC4ULL);

    msx2_adapter adapter2(make_generated_boot_bios(), {});
    adapter2.step_one_frame();
    adapter2.step_one_frame();
    CHECK(framebuffer_digest(adapter2.current_frame()) == framebuffer_digest(fb));
}

TEST_CASE("msx2 adapter save state restores board and scheduler state",
          "[apps][player][msx2][state]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                         std::vector<std::uint8_t>(0x8000U, 0x00U),
                         mnemos::manifests::msx2::msx2_config{.ram_size = 0x20000U});
    auto& sys = adapter.system();

    sys.ram[0x1234U] = 0x6BU;
    sys.primary_slot = 0x5AU;
    sys.ram_segment[2] = 3U;
    sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.io_write(0xA1U, 0x40U);
    adapter.scheduler().run_master_cycles(29U);
    const std::uint64_t saved_cycle = adapter.scheduler().master_cycle();

    const std::vector<std::uint8_t> saved = adapter.save_state();
    REQUIRE_FALSE(saved.empty());

    sys.ram[0x1234U] = 0x00U;
    sys.primary_slot = 0x00U;
    sys.ram_segment[2] = 0U;
    sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.io_write(0xA1U, 0x00U);
    adapter.scheduler().run_master_cycles(11U);

    const auto result = adapter.load_state(saved);
    REQUIRE(result.ok());
    CHECK(result.master_cycle == saved_cycle);
    CHECK(adapter.scheduler().master_cycle() == saved_cycle);
    CHECK(sys.ram[0x1234U] == 0x6BU);
    CHECK(sys.primary_slot == 0x5AU);
    CHECK(sys.ram_segment[2] == 3U);
    CHECK(sys.psg.read_reg(mnemos::chips::audio::ssg::reg_port_b) == 0x40U);
}

TEST_CASE("msx2 adapter maps player input to joystick port 1", "[apps][player][msx2]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                         std::vector<std::uint8_t>(0x8000U, 0x00U));

    mnemos::frontend_sdk::controller_state state{};
    state.right = true;
    state.a = true;
    adapter.apply_input(0, state);

    CHECK(adapter.system().keyboard_rows[8] == 0xFFU);
    configure_psg_gpio(adapter.system());
    adapter.system().io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((adapter.system().io_read(0xA2U) & 0x3FU) == 0x27U);

    adapter.apply_input(0, mnemos::frontend_sdk::controller_state{});
    CHECK(adapter.system().keyboard_rows[8] == 0xFFU);
    adapter.system().io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((adapter.system().io_read(0xA2U) & 0x3FU) == 0x3FU);
}

TEST_CASE("msx2 adapter maps player input to keyboard rows", "[apps][player][msx2]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                         std::vector<std::uint8_t>(0x8000U, 0x00U));

    mnemos::frontend_sdk::controller_state state{};
    state.right = true;
    state.a = true;
    adapter.apply_input(1, state);

    CHECK(adapter.system().keyboard_rows[8] == 0x7EU); // SPACE + RIGHT active-low
    configure_psg_gpio(adapter.system());
    adapter.system().io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((adapter.system().io_read(0xA2U) & 0x3FU) == 0x3FU);

    adapter.apply_input(1, mnemos::frontend_sdk::controller_state{});
    CHECK(adapter.system().keyboard_rows[8] == 0xFFU);
    adapter.system().io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((adapter.system().io_read(0xA2U) & 0x3FU) == 0x3FU);
}

TEST_CASE("msx2 adapter maps physical keyboard usages to the MSX matrix",
          "[apps][player][msx2][keyboard]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                         std::vector<std::uint8_t>(0x8000U, 0x00U));
    mnemos::frontend_sdk::controller_state state{};
    state.set_key(0x04U, true); // A
    state.set_key(0x3AU, true); // F1
    state.set_key(0x4CU, true); // Delete
    state.set_key(0x4FU, true); // Right arrow

    adapter.apply_input(1, state);

    CHECK(adapter.system().keyboard_rows[2] == 0xBFU);
    CHECK(adapter.system().keyboard_rows[6] == 0xDFU);
    CHECK(adapter.system().keyboard_rows[8] == 0x77U);

    adapter.apply_input(1, mnemos::frontend_sdk::controller_state{});
    CHECK(adapter.system().keyboard_rows[2] == 0xFFU);
    CHECK(adapter.system().keyboard_rows[6] == 0xFFU);
    CHECK(adapter.system().keyboard_rows[8] == 0xFFU);
}

TEST_CASE("msx2 adapter maps player input to joystick port 2", "[apps][player][msx2]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                         std::vector<std::uint8_t>(0x8000U, 0x00U));

    mnemos::frontend_sdk::controller_state state{};
    state.left = true;
    state.b = true;
    adapter.apply_input(2, state);

    configure_psg_gpio(adapter.system());
    adapter.system().io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    adapter.system().io_write(0xA1U, 0x4FU);
    adapter.system().io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((adapter.system().io_read(0xA2U) & 0x3FU) == 0x1BU);
}

TEST_CASE("msx2 adapter routes frontend mouse input to joystick port 2 by default",
          "[apps][player][msx2][mouse]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                         std::vector<std::uint8_t>(0x8000U, 0x00U));

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
    sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.io_write(0xA1U, 0x0FU);
    sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys.io_read(0xA2U) & 0x3FU) == 0x27U);

    sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.io_write(0xA1U, 0x4FU);
    sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys.io_read(0xA2U) & 0x3FU) == 0x21U);

    sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.io_write(0xA1U, 0x6FU);
    sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys.io_read(0xA2U) & 0x3FU) == 0x22U);
}

TEST_CASE("msx2 adapter mouse tracker state round-trips with the save state",
          "[apps][player][msx2][mouse][state]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                         std::vector<std::uint8_t>(0x8000U, 0x00U));

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
    sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.io_write(0xA1U, 0x4FU);
    sys.io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys.io_read(0xA2U) & 0x3FU) == 0x34U);
}

TEST_CASE("msx2 adapter registry maps Konami SCC override to the manifest",
          "[apps][player][msx2][mapper]") {
    mnemos::apps::player::adapters::msx2::force_link();

    mnemos::frontend_sdk::adapter_options opts{};
    opts.rom = std::vector<std::uint8_t>(0x10000U, 0x00U);
    opts.bios_images.push_back(std::vector<std::uint8_t>(0x8000U, 0x00U));
    opts.mapper_override = "konami-scc";

    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("msx2", std::move(opts));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<msx2_adapter&>(*system);

    CHECK(adapter.system().cart_mapper == msx2_cartridge_mapper::konami_scc);
}

TEST_CASE("msx2 adapter registry maps ASCII SRAM override and exposes battery RAM",
          "[apps][player][msx2][mapper][sram]") {
    mnemos::apps::player::adapters::msx2::force_link();

    mnemos::frontend_sdk::adapter_options opts{};
    opts.rom = make_8k_paged_cart(8U);
    opts.bios_images.push_back(std::vector<std::uint8_t>(0x8000U, 0x00U));
    opts.mapper_override = "ascii8-sram8";

    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("msx2", std::move(opts));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<msx2_adapter&>(*system);

    CHECK(adapter.system().cart_mapper == msx2_cartridge_mapper::ascii8_sram8);
    REQUIRE(adapter.battery_ram().size() == 0x2000U);
    CHECK(adapter.battery_ram_media_id() == "cart");
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Cart SRAM" && field.value == "8 KiB";
                      }));

    select_primary_slots(adapter.system(), 0x14U);
    adapter.system().bus.write8(0x7000U, 0x80U);
    adapter.system().bus.write8(0x8000U, 0xA6U);
    CHECK(adapter.battery_ram()[0] == 0xA6U);
}

TEST_CASE("msx2 adapter registry maps Korean MSX mapper override",
          "[apps][player][msx2][mapper][korean]") {
    mnemos::apps::player::adapters::msx2::force_link();

    mnemos::frontend_sdk::adapter_options opts{};
    opts.rom = make_8k_paged_cart(8U);
    opts.bios_images.push_back(std::vector<std::uint8_t>(0x8000U, 0x00U));
    opts.mapper_override = "korean-msx";

    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("msx2", std::move(opts));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<msx2_adapter&>(*system);

    CHECK(adapter.system().cart_mapper == msx2_cartridge_mapper::korean_msx);
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Mapper" && field.value == "Korean MSX";
                      }));
}

TEST_CASE("msx2 adapter registry auto-detects cartridge mapper", "[apps][player][msx2][mapper]") {
    mnemos::apps::player::adapters::msx2::force_link();

    std::vector<std::uint8_t> cart = make_8k_paged_cart(8U);
    add_mapper_signatures(cart, {0x6000U, 0x6800U, 0x7000U, 0x7800U});

    mnemos::frontend_sdk::adapter_options opts{};
    opts.rom = std::move(cart);
    opts.bios_images.push_back(std::vector<std::uint8_t>(0x8000U, 0x00U));

    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("msx2", std::move(opts));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<msx2_adapter&>(*system);
    auto& sys = adapter.system();
    REQUIRE(sys.cart_mapper == msx2_cartridge_mapper::ascii8);

    select_primary_slots(sys, 0x14U); // pages 1/2 -> cartridge slot
    CHECK(sys.bus.read8(0x4000U) == 0x80U);
    sys.bus.write8(0x6000U, 5U);
    CHECK(sys.bus.read8(0x4000U) == 0x85U);
    CHECK(sys.ascii8_bank[0] == 5U);
}

TEST_CASE("msx2 adapter registry maps second cartridge slot override",
          "[apps][player][msx2][mapper]") {
    mnemos::apps::player::adapters::msx2::force_link();

    mnemos::frontend_sdk::adapter_options opts{};
    opts.rom = make_8k_paged_cart(8U);
    opts.bios_images.push_back(std::vector<std::uint8_t>(0x8000U, 0x00U));
    std::vector<std::uint8_t> cart2 = make_8k_paged_cart(8U);
    for (std::size_t bank = 0; bank < 8U; ++bank) {
        cart2[bank * 0x2000U] = static_cast<std::uint8_t>(0x40U + bank);
    }
    opts.additional_media.push_back(std::move(cart2));
    opts.mapper2_override = "ascii8";

    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("msx2", std::move(opts));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<msx2_adapter&>(*system);

    CHECK(adapter.system().cart2_mapper == msx2_cartridge_mapper::ascii8);
    REQUIRE(adapter.system().cartridge2.size() == 0x10000U);
    const auto* cart2_media = find_media(adapter, "cart2");
    REQUIRE(cart2_media != nullptr);
    CHECK(cart2_media->byte_count == 0x10000U);
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Cart Slot 2" && field.value == "mounted";
                      }));
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Cart Slot 2 Mapper" &&
                                 field.value == "ASCII8";
                      }));

    select_primary_slots(adapter.system(), 0x28U);
    CHECK(adapter.system().bus.read8(0x4000U) == 0x40U);
    adapter.system().bus.write8(0x6000U, 5U);
    CHECK(adapter.system().bus.read8(0x4000U) == 0x45U);
}

TEST_CASE("msx2 adapter exposes FM-PAC SRAM as battery RAM", "[apps][player][msx2]") {
    mnemos::apps::player::adapters::msx2::force_link();

    mnemos::frontend_sdk::adapter_options opts{};
    opts.rom = std::vector<std::uint8_t>(0x8000U, 0xFFU);
    opts.bios_images.push_back(std::vector<std::uint8_t>(0x8000U, 0x00U));
    opts.fm_unit = true;

    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("msx2", std::move(opts));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<msx2_adapter&>(*system);
    auto& sys = adapter.system();

    REQUIRE(sys.msx_music_enabled());
    REQUIRE(adapter.battery_ram().size() == 0x2000U);
    CHECK(adapter.battery_ram_media_id() == "fmpac");
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "PAC SRAM" && field.value == "8 KiB";
                      }));

    select_primary_slots(sys, 0x14U);
    sys.bus.write8(0x5FFEU, 0x4DU);
    sys.bus.write8(0x5FFFU, 0x69U);
    sys.bus.write8(0x4000U, 0xA6U);
    CHECK(adapter.battery_ram()[0] == 0xA6U);
    CHECK(sys.bus.read8(0x4000U) == 0xA6U);
}

TEST_CASE("msx2 adapter exposes second cartridge SRAM as battery RAM",
          "[apps][player][msx2][mapper][sram]") {
    mnemos::apps::player::adapters::msx2::force_link();

    mnemos::frontend_sdk::adapter_options opts{};
    opts.rom = std::vector<std::uint8_t>(0x8000U, 0xFFU);
    opts.bios_images.push_back(std::vector<std::uint8_t>(0x8000U, 0x00U));
    opts.additional_media.push_back(make_8k_paged_cart(8U));
    opts.mapper2_override = "ascii8-sram8";

    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("msx2", std::move(opts));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<msx2_adapter&>(*system);
    auto& sys = adapter.system();

    REQUIRE(adapter.battery_ram().size() == 0x2000U);
    CHECK(adapter.battery_ram_media_id() == "cart2");
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Cart Slot 2 SRAM" && field.value == "8 KiB";
                      }));

    select_primary_slots(sys, 0x28U);
    sys.bus.write8(0x7000U, 0x80U);
    sys.bus.write8(0x8000U, 0xA9U);
    CHECK(adapter.battery_ram()[0] == 0xA9U);
    CHECK(sys.bus.read8(0x8000U) == 0xA9U);
}

TEST_CASE("msx2 adapter mixes SCC cartridge audio", "[apps][player][msx2][audio]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                         std::vector<std::uint8_t>(0x80000U, 0x00U),
                         mnemos::manifests::msx2::msx2_config{
                             .cartridge_mapper = msx2_cartridge_mapper::konami_scc});

    auto& sys = adapter.system();
    CHECK(std::find(adapter.chips().begin(), adapter.chips().end(), &sys.scc) !=
          adapter.chips().end());
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Audio" && field.value == "PSG+SCC";
                      }));
    select_primary_slots(sys, 0x14U); // pages 1 and 2 -> cartridge slot
    sys.bus.write8(0x9000U, 0x3FU);
    for (int i = 0; i < 32; ++i) {
        sys.bus.write8(static_cast<std::uint16_t>(0x9800U + i), i < 16 ? 0x60U : 0xA0U);
    }
    sys.bus.write8(0x9880U, 0x02U);
    sys.bus.write8(0x9881U, 0x00U);
    sys.bus.write8(0x988AU, 0x0FU);
    sys.bus.write8(0x988FU, 0x01U);
    sys.scc.set_clock_divider(1);
    sys.scc.tick(2048);

    const auto audio = adapter.drain_audio();
    REQUIRE(audio.samples != nullptr);
    REQUIRE(audio.frame_count > 0U);
    const auto* begin = audio.samples;
    const auto* end = begin + audio.frame_count * 2U;
    CHECK(std::any_of(begin, end, [](std::int16_t s) { return s != 0; }));
}

TEST_CASE("msx2 adapter exposes and mixes optional MSX-MUSIC", "[apps][player][msx2][audio]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                         std::vector<std::uint8_t>(0x8000U, 0x00U),
                         mnemos::manifests::msx2::msx2_config{.msx_music = true});

    auto& sys = adapter.system();
    REQUIRE(sys.msx_music_enabled());
    CHECK(adapter.chips().size() == 6U);
    CHECK(std::find(adapter.chips().begin(), adapter.chips().end(), &sys.scc) ==
          adapter.chips().end());
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Audio" && field.value == "PSG+MSX-MUSIC";
                      }));

    sys.music.set_clock_divider(1);
    sys.io_write(0x7CU, 0x30U);
    sys.io_write(0x7DU, 0x70U);
    sys.io_write(0x7CU, 0x10U);
    sys.io_write(0x7DU, 0xA0U);
    sys.io_write(0x7CU, 0x20U);
    sys.io_write(0x7DU, 0x18U);
    sys.music.tick(10000);

    const auto audio = adapter.drain_audio();
    REQUIRE(audio.samples != nullptr);
    REQUIRE(audio.frame_count > 0U);
    const auto* begin = audio.samples;
    const auto* end = begin + audio.frame_count * 2U;
    CHECK(std::any_of(begin, end, [](std::int16_t s) { return s != 0; }));
}

TEST_CASE("msx2 adapter registry passes optional sub-ROM as expanded slot 0-1",
          "[apps][player][msx2][slots]") {
    mnemos::apps::player::adapters::msx2::force_link();

    mnemos::frontend_sdk::adapter_options opts{};
    opts.rom = std::vector<std::uint8_t>(0x8000U, 0x00U);
    opts.bios_images.push_back(std::vector<std::uint8_t>(0x8000U, 0x00U));
    std::vector<std::uint8_t> sub_rom(0x4000U, 0x00U);
    sub_rom[0] = 0x66U;
    opts.bios_images.push_back(std::move(sub_rom));

    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("msx2", std::move(opts));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<msx2_adapter&>(*system);

    CHECK(adapter.system().expanded_slot[0]);
    adapter.system().bus.write8(0xFFFFU, 0x01U);
    CHECK(adapter.system().bus.read8(0x0000U) == 0x66U);
}

TEST_CASE("msx2 adapter registry passes disk media to the WD1793 controller",
          "[apps][player][msx2][fdc]") {
    mnemos::apps::player::adapters::msx2::force_link();

    mnemos::frontend_sdk::adapter_options opts{};
    opts.bios_images.push_back(std::vector<std::uint8_t>(0x8000U, 0x00U));
    opts.additional_media.push_back(make_dsk());
    auto disk2 = make_dsk();
    disk2[0] = 0x52U;
    opts.additional_media.push_back(std::move(disk2));
    opts.display_name = "boot.dsk";

    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("msx2", std::move(opts));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<msx2_adapter&>(*system);

    CHECK(adapter.system().disk_enabled());
    CHECK(adapter.system().fdc.mounted());
    CHECK(std::find(adapter.chips().begin(), adapter.chips().end(), &adapter.system().fdc) !=
          adapter.chips().end());
    const auto* bios = find_media(adapter, "bios");
    REQUIRE(bios != nullptr);
    CHECK(bios->provider_id == "msx2.adapter");
    const auto* disk = find_media(adapter, "disk.0");
    REQUIRE(disk != nullptr);
    CHECK(disk->provider_id == "msx2.wd1793");
    const auto* disk_extra = find_media(adapter, "disk.1");
    REQUIRE(disk_extra != nullptr);
    CHECK(disk_extra->label == "Disk 2");
    REQUIRE(adapter.media_count() == 2U);
    REQUIRE(adapter.insert_media(1U));
    CHECK(adapter.system().fdc.disk_image()[0] == 0x52U);
}

TEST_CASE("msx2 adapter publishes read-only disk media state", "[apps][player][msx2][fdc]") {
    const std::vector<std::uint8_t> disk = make_dsk();
    msx2_adapter adapter(
        std::vector<std::uint8_t>(0x8000U, 0x00U), std::vector<std::uint8_t>{},
        mnemos::manifests::msx2::msx2_config{.disk_image = disk, .disk_write_protected = true},
        "boot.dsk");

    REQUIRE(adapter.system().fdc.mounted());
    CHECK(adapter.system().fdc.write_protected());
    const auto* disk_media = find_media(adapter, "disk.0");
    REQUIRE(disk_media != nullptr);
    CHECK(disk_media->revision == "read-only");
    CHECK(disk_media->cache_hint == "resident_read_only");
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Disk Mode" && field.value == "Read-only";
                      }));
}

TEST_CASE("msx2 adapter mounts primary DSK media as a WD1793 disk", "[apps][player][msx2][fdc]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), make_dsk(), {}, "boot.dsk");

    REQUIRE(adapter.system().disk_enabled());
    REQUIRE(adapter.system().fdc.mounted());
    CHECK(find_media(adapter, "cart") == nullptr);
    const auto* disk = find_media(adapter, "disk.0");
    REQUIRE(disk != nullptr);
    CHECK(disk->provider_id == "msx2.wd1793");
    CHECK(disk->label == "boot.dsk");
}

TEST_CASE("msx2 adapter mounts resident DSK alongside primary cartridge",
          "[apps][player][msx2][fdc]") {
    auto cart = make_8k_paged_cart(4U);
    cart[0] = 0x11U;
    auto disk = make_dsk();
    disk[0] = 0x52U;

    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), cart, {}, "cart+disk", nullptr,
                         {disk});

    REQUIRE(adapter.system().disk_enabled());
    REQUIRE(adapter.system().fdc.mounted());
    CHECK(adapter.system().fdc.disk_image()[0] == 0x52U);
    CHECK(std::find(adapter.chips().begin(), adapter.chips().end(), &adapter.system().fdc) !=
          adapter.chips().end());
    CHECK(adapter.media_count() == 1U);

    const auto* cart_media = find_media(adapter, "cart");
    REQUIRE(cart_media != nullptr);
    CHECK(cart_media->byte_count == cart.size());
    const auto* disk_media = find_media(adapter, "disk.0");
    REQUIRE(disk_media != nullptr);
    CHECK(disk_media->provider_id == "msx2.wd1793");

    adapter.system().io_write(0xD2U, 1U);
    adapter.system().io_write(0xD0U, 0x80U);
    CHECK(adapter.system().io_read(0xD3U) == 0x52U);
}

TEST_CASE("msx2 adapter swaps resident disk media", "[apps][player][msx2][fdc]") {
    auto disk1 = make_dsk();
    disk1[0] = 0x11U;
    auto disk2 = make_dsk();
    disk2[0] = 0x22U;

    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), disk1, {}, "Disk Set", nullptr,
                         {disk2});

    REQUIRE(adapter.media_count() == 2U);
    CHECK(adapter.current_media_index() == 0U);
    REQUIRE(adapter.system().disk_enabled());
    REQUIRE(adapter.system().fdc.mounted());
    CHECK(adapter.system().fdc.disk_image()[0] == 0x11U);
    const auto* disk0 = find_media(adapter, "disk.0");
    REQUIRE(disk0 != nullptr);
    CHECK(disk0->label == "Disk Set");
    const auto* disk1_info = find_media(adapter, "disk.1");
    REQUIRE(disk1_info != nullptr);
    CHECK(disk1_info->label == "Disk 2");
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Disks" && field.value == "2";
                      }));

    REQUIRE(adapter.insert_media(1U));
    CHECK(adapter.current_media_index() == 1U);
    CHECK(adapter.system().fdc.disk_image()[0] == 0x22U);
    CHECK_FALSE(adapter.insert_media(2U));
    CHECK(adapter.current_media_index() == 1U);
}

TEST_CASE("msx2 adapter save state restores selected resident disk index",
          "[apps][player][msx2][state][fdc]") {
    auto disk1 = make_dsk();
    disk1[0] = 0x11U;
    auto disk2 = make_dsk();
    disk2[0] = 0x22U;

    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), disk1, {}, "Disk Set", nullptr,
                         {disk2});
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

TEST_CASE("msx2 adapter mounts primary CAS media as cassette tape",
          "[apps][player][msx2][cassette]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), make_cas({0x42U}), {},
                         "boot.cas");

    REQUIRE(adapter.system().cassette.loaded());
    CHECK(adapter.system().cassette.playing());
    CHECK(find_media(adapter, "cart") == nullptr);
    const auto* tape = find_media(adapter, "tape");
    REQUIRE(tape != nullptr);
    CHECK(tape->provider_id == "msx.cassette");
    CHECK(tape->label == "boot.cas");
}

TEST_CASE("msx2 adapter registry publishes CAS media and cassette chip state",
          "[apps][player][msx2][cassette]") {
    mnemos::frontend_sdk::adapter_options opts{};
    opts.rom = std::vector<std::uint8_t>(0x8000U, 0x00U);
    opts.bios_images.push_back(std::vector<std::uint8_t>(0x8000U, 0x00U));
    opts.additional_media.push_back(make_cas({0x42U}));
    opts.display_name = "Tiny CAS";

    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("msx2", std::move(opts));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<msx2_adapter&>(*system);

    REQUIRE(adapter.system().cassette.loaded());
    CHECK(adapter.system().cassette.playing());
    const auto* tape = find_media(adapter, "tape");
    REQUIRE(tape != nullptr);
    CHECK(tape->provider_id == "msx.cassette");
    CHECK(tape->byte_count == mnemos::chips::storage::msx_cassette::cas_header_magic.size() + 1U);
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Cassette" && field.value == "MSX CAS";
                      }));
}

TEST_CASE("msx2 adapter publishes resident firmware media", "[apps][player][msx2][media]") {
    std::vector<std::uint8_t> sub_bios(0x4000U, 0x11U);
    std::vector<std::uint8_t> logo_rom(0x4000U, 0x55U);
    std::vector<std::uint8_t> disk_rom(0x4000U, 0x22U);
    std::vector<std::uint8_t> kanji_rom(0x40000U, 0x33U);

    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U), std::vector<std::uint8_t>{},
                         mnemos::manifests::msx2::msx2_config{
                             .sub_bios = sub_bios,
                             .logo_rom = logo_rom,
                             .disk_rom = disk_rom,
                             .kanji_rom = kanji_rom});

    const auto* bios = find_media(adapter, "bios");
    REQUIRE(bios != nullptr);
    CHECK(bios->label == "MSX2 BIOS");
    CHECK(bios->byte_count == 0x8000U);

    const auto* sub = find_media(adapter, "sub_bios");
    REQUIRE(sub != nullptr);
    CHECK(sub->label == "MSX2 Sub-ROM");
    CHECK(sub->byte_count == 0x4000U);

    const auto* logo = find_media(adapter, "logo_rom");
    REQUIRE(logo != nullptr);
    CHECK(logo->label == "MSX2 Logo ROM");
    CHECK(logo->byte_count == 0x4000U);

    const auto* disk = find_media(adapter, "disk_rom");
    REQUIRE(disk != nullptr);
    CHECK(disk->provider_id == "msx2.wd1793");
    CHECK(disk->byte_count == 0x4000U);

    const auto* kanji = find_media(adapter, "kanji");
    REQUIRE(kanji != nullptr);
    CHECK(kanji->provider_id == "msx.kanji_rom");
    CHECK(kanji->byte_count == 0x40000U);
    CHECK(kanji->revision == "jis-level-1-2");
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Kanji ROM" && field.value == "JIS level 1+2";
                      }));
}
