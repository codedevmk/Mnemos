#include "msx_adapter.hpp"

#include "adapter_registry.hpp"
#include "msx_cassette.hpp"
#include "tms9918a.hpp"
#include "v9938.hpp"
#include "wd1793.hpp"

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("msx adapter maps player input to joystick port 1", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U));
    mnemos::frontend_sdk::controller_state state{};
    state.right = true;
    state.a = true;
    adapter.apply_input(0, state);

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
    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_b);
    sys.write_io(0xA1U, 0x40U);
    sys.write_io(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((sys.read_io(0xA2U) & 0x3FU) == 0x1BU);
}

TEST_CASE("msx adapter publishes session and media capabilities", "[apps][player][msx]") {
    msx_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                        std::vector<std::uint8_t>(0x4000U, 0xFFU), {}, "Tiny MSX");
    REQUIRE(adapter.session_capabilities().input_ports.size() == 3U);
    CHECK(adapter.session_capabilities().input_ports[2].device_id == "msx.joystick.port.2");
    CHECK(adapter.media_capabilities().media.size() == 2U);
    CHECK(adapter.media_capabilities().media[1].id == "cart");
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
            has_kanji_spec || (field.label == "Kanji ROM" && field.value == "JIS1/JIS2");
    }
    CHECK(has_kanji_spec);

    REQUIRE(adapter.media_capabilities().media.size() == 2U);
    CHECK(adapter.media_capabilities().media[1].id == "kanji");
    CHECK(adapter.media_capabilities().media[1].provider_id == "msx.kanji");

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

    REQUIRE(adapter.insert_media(1U));
    CHECK(adapter.current_media_index() == 1U);
    CHECK(adapter.system().fdc.disk_image()[0] == 0x22U);

    CHECK_FALSE(adapter.insert_media(2U));
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
