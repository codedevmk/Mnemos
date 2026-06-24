#include "msx2_adapter.hpp"

#include "adapter_registry.hpp"
#include "wd1793.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
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
} // namespace

TEST_CASE("msx2 adapter constructs and steps frames", "[apps][player][msx2]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                         std::vector<std::uint8_t>(0x8000U, 0x00U));

    const auto fb0 = adapter.current_frame();
    CHECK(fb0.width == 256U);
    CHECK(fb0.height == 192U);

    adapter.step_one_frame();
    adapter.step_one_frame();

    CHECK(adapter.chips().size() == 6U);
    CHECK(adapter.current_frame().pixels != nullptr);
    CHECK(adapter.system().vdp.frame_index() == 2U);
}

TEST_CASE("msx2 adapter maps controller state to keyboard rows", "[apps][player][msx2]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                         std::vector<std::uint8_t>(0x8000U, 0x00U));

    mnemos::frontend_sdk::controller_state state{};
    state.right = true;
    state.a = true;
    adapter.apply_input(0, state);

    CHECK(adapter.system().keyboard_rows[8] == 0x7EU); // SPACE + RIGHT active-low
    adapter.system().io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((adapter.system().io_read(0xA2U) & 0x3FU) == 0x27U);

    adapter.apply_input(0, mnemos::frontend_sdk::controller_state{});
    CHECK(adapter.system().keyboard_rows[8] == 0xFFU);
    adapter.system().io_write(0xA0U, mnemos::chips::audio::ssg::reg_port_a);
    CHECK((adapter.system().io_read(0xA2U) & 0x3FU) == 0x3FU);
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

TEST_CASE("msx2 adapter mixes SCC cartridge audio", "[apps][player][msx2][audio]") {
    msx2_adapter adapter(std::vector<std::uint8_t>(0x8000U, 0x00U),
                         std::vector<std::uint8_t>(0x80000U, 0x00U),
                         mnemos::manifests::msx2::msx2_config{
                             .cartridge_mapper = msx2_cartridge_mapper::konami_scc});

    auto& sys = adapter.system();
    sys.io_write(0xA8U, 0x14U); // pages 1 and 2 -> cartridge slot
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
    CHECK(adapter.chips().size() == 7U);

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
    opts.display_name = "boot.dsk";

    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("msx2", std::move(opts));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<msx2_adapter&>(*system);

    CHECK(adapter.system().fdc.mounted());
    REQUIRE(adapter.media_capabilities().media.size() == 1U);
    CHECK(adapter.media_capabilities().media[0].id == "disk.0");
    CHECK(adapter.media_capabilities().media[0].provider_id == "msx2.wd1793");
}

TEST_CASE("msx2 adapter publishes read-only disk media state", "[apps][player][msx2][fdc]") {
    const std::vector<std::uint8_t> disk = make_dsk();
    msx2_adapter adapter(
        std::vector<std::uint8_t>(0x8000U, 0x00U), std::vector<std::uint8_t>{},
        mnemos::manifests::msx2::msx2_config{.disk_image = disk, .disk_write_protected = true},
        "boot.dsk");

    REQUIRE(adapter.system().fdc.mounted());
    CHECK(adapter.system().fdc.write_protected());
    REQUIRE(adapter.media_capabilities().media.size() == 1U);
    CHECK(adapter.media_capabilities().media[0].revision == "read-only");
    CHECK(adapter.media_capabilities().media[0].cache_hint == "resident_read_only");
    CHECK(std::any_of(adapter.system_spec().begin(), adapter.system_spec().end(),
                      [](const mnemos::frontend_sdk::spec_field& field) {
                          return field.label == "Disk Mode" && field.value == "Read-only";
                      }));
}
