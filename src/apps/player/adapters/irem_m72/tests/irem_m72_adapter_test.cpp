#include "irem_m72_adapter.hpp"

#include "adapter_registry.hpp"
#include "file.hpp"
#include "m72_game_manifests.hpp"
#include "rom_set_toml.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
            0xB8U, 0x00U, 0xA0U, // MOV AX,A000 (the base map's work RAM)
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

    [[nodiscard]] const char* opt_env(const char* name) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in test data path
#endif
        const char* value = std::getenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        return (value != nullptr && *value != '\0') ? value : nullptr;
    }

    [[nodiscard]] mnemos::manifests::common::rom_set_decl
    require_embedded_decl(std::string_view set_name) {
        const std::string_view toml = mnemos::manifests::irem_m72::game_manifest_toml(set_name);
        REQUIRE_FALSE(toml.empty());
        const auto parsed = mnemos::manifests::common::parse_rom_set_decl(toml, "embedded");
        for (const auto& error : parsed.errors) {
            INFO(error.source << ":" << error.line << ":" << error.column << ": "
                              << error.message);
        }
        REQUIRE(parsed.ok());
        REQUIRE(parsed.value.has_value());
        return *parsed.value;
    }

    [[nodiscard]] std::vector<std::pair<std::string, std::vector<std::uint8_t>>>
    placeholder_entries_for(const mnemos::manifests::common::rom_set_decl& decl,
                            std::uint8_t fill) {
        std::map<std::string, std::size_t, std::less<>> sizes;
        for (const auto& region : decl.regions) {
            for (const auto& file : region.files) {
                const std::size_t size = file.size == 0U ? 1U : file.size;
                auto [it, inserted] = sizes.emplace(file.name, size);
                if (!inserted) {
                    it->second = std::max(it->second, size);
                }
            }
        }

        std::vector<std::pair<std::string, std::vector<std::uint8_t>>> entries;
        entries.reserve(sizes.size());
        for (const auto& [name, size] : sizes) {
            entries.emplace_back(name, std::vector<std::uint8_t>(size, fill));
        }
        return entries;
    }

    [[nodiscard]] bool has_only_crc_issues(
        const std::vector<mnemos::manifests::common::rom_load_issue>& issues) {
        return !issues.empty() && std::all_of(issues.begin(), issues.end(), [](const auto& issue) {
                   return issue.message.find("crc32 mismatch") != std::string::npos;
               });
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
    const auto chips = adapter.chips();
    REQUIRE(chips.size() == 6U);
    CHECK(chips[0]->metadata().part_number == "m72_video");
    CHECK(chips[1]->metadata().part_number == "v30");
    CHECK(chips[2]->metadata().part_number == "Z80");
    CHECK(chips[3]->metadata().part_number == "8259A");
    CHECK(chips[4]->metadata().part_number == "ym2151");
    CHECK(chips[5]->metadata().part_number == "dac8");
    CHECK(adapter.system_spec().size() == 3U);
    CHECK(adapter.system_spec()[1].value == "Irem M72");
    CHECK(adapter.system_spec()[2].value == "smoke");
    CHECK(adapter.set_name().empty());

    const auto& session = adapter.session_capabilities();
    REQUIRE(session.input_ports.size() == 2U);
    CHECK(session.input_ports[0].format == mnemos::frontend_sdk::input_device_format::arcade_panel);
    CHECK(session.input_ports[1].device_id == "irem_m72.panel.p2");
    CHECK(session.deterministic_frame_input);
    CHECK(session.save_state_supported);
    CHECK(session.frame_exact_save_state);

    const auto& media = adapter.media_capabilities();
    REQUIRE(media.media.size() == 1U);
    CHECK(media.media[0].id == "rom_set");
    CHECK(media.media[0].label == "smoke");
    CHECK(media.media[0].byte_count == mnemos::manifests::irem_m72::main_rom_size);
    CHECK(media.media[0].hash_algorithm == mnemos::frontend_sdk::media_hash_algorithm::crc32);
    CHECK(media.media[0].full_hash.size() == 8U);
    CHECK(media.media[0].provider_id == "irem_m72.adapter");
}

TEST_CASE("irem_m72_adapter publishes board RAM as system memory views", "[irem_m72][adapter]") {
    namespace m72 = mnemos::manifests::irem_m72;

    irem_m72_adapter adapter(make_program());

    const auto find_view =
        [&](std::string_view name) -> const mnemos::instrumentation::memory_view* {
        for (const auto* view : adapter.memory_views()) {
            if (view != nullptr && view->name() == name) {
                return view;
            }
        }
        return nullptr;
    };
    const auto expect_view = [&](std::string_view name, std::size_t size) {
        const auto* view = find_view(name);
        REQUIRE(view != nullptr);
        CHECK(view->bytes().size() == size);
    };

    CHECK(adapter.memory_views().size() == 8U);
    expect_view("work_ram", m72::work_ram_size);
    expect_view("sound_ram", m72::sound_ram_size);
    expect_view("sprite_ram", m72::sprite_ram_size);
    expect_view("palette_a", m72::palette_size);
    expect_view("palette_b", m72::palette_size);
    expect_view("vram_a", m72::vram_size);
    expect_view("vram_b", m72::vram_size);
    expect_view("mcu_shared_ram", m72::mcu_shared_ram_size);
}

TEST_CASE("irem_m72_adapter maps pads onto the board's input bytes", "[irem_m72][adapter]") {
    irem_m72_adapter adapter(make_program());

    mnemos::frontend_sdk::controller_state p1{};
    p1.up = true;
    p1.a = true;
    p1.start = true;
    p1.select = true; // coin 1
    p1.mode = true;   // service 1
    adapter.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.right = true;
    p2.start = true;
    p2.mode = true; // service 2
    adapter.apply_input(1, p2);

    auto& machine = adapter.machine();
    // Hardware layout: up = bit 3, button 1 = bit 7; right = bit 0.
    CHECK(machine.input_p1 == static_cast<std::uint8_t>(0xFFU & ~0x08U & ~0x80U));
    CHECK(machine.input_p2 == static_cast<std::uint8_t>(0xFFU & ~0x01U));
    // start1/start2 (bits 0/1), coin1 (bit 2), and service1/2 (bits 4/5) held low.
    CHECK(machine.input_system ==
          static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x02U & ~0x04U & ~0x10U & ~0x20U));

    adapter.apply_input(2, p1); // out-of-range port ignored
    CHECK(machine.input_p2 == static_cast<std::uint8_t>(0xFFU & ~0x01U));
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

TEST_CASE("irem_m72_adapter mixes DAC writes at sound sample boundaries",
          "[irem_m72][adapter]") {
    irem_m72_adapter adapter(make_program());
    auto& machine = adapter.machine();

    machine.record_dac_write(0xC0U); // sound clock 0: affects the first sample.
    machine.fm.tick(64U);
    machine.record_dac_write(0x80U); // sound clock 64: next sample boundary.

    const auto first = adapter.drain_audio();
    REQUIRE(first.frame_count == 1U);
    REQUIRE(first.samples != nullptr);
    CHECK(first.samples[0] == (0xC0 - 0x80) * 64);
    CHECK(first.samples[1] == (0xC0 - 0x80) * 64);
    REQUIRE(machine.dac_write_events.size() == 1U);
    CHECK(machine.dac_write_events[0].sound_clock == 64U);

    machine.fm.tick(64U);
    const auto second = adapter.drain_audio();
    REQUIRE(second.frame_count == 1U);
    REQUIRE(second.samples != nullptr);
    CHECK(second.samples[0] == 0);
    CHECK(second.samples[1] == 0);
    CHECK(machine.dac_write_events.empty());
}

TEST_CASE("irem_m72_adapter applies the DIP override and reports orientation",
          "[irem_m72][adapter]") {
    mnemos::frontend_sdk::adapter_options options{};
    options.rom = make_program();
    options.dip_override = std::uint16_t{0xA5C3U};
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);
    CHECK(adapter.machine().dip_switches == 0xA5C3U);

    // Horizontal by default (R-Type); vertical games flip it from driver
    // metadata and the frontend rotates presentation.
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    adapter.set_orientation(mnemos::frontend_sdk::display_orientation::vertical);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
}

namespace {

    void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8U));
    }

    void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
        put16(out, static_cast<std::uint16_t>(v));
        put16(out, static_cast<std::uint16_t>(v >> 16U));
    }

    // Minimal STORED-method zip over the given entries (CRC fields zeroed;
    // the reader does not verify them).
    [[nodiscard]] std::vector<std::uint8_t>
    make_stored_zip(const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& entries) {
        std::vector<std::uint8_t> out;
        struct central final {
            std::string name;
            std::uint32_t size;
            std::uint32_t local_offset;
        };
        std::vector<central> directory;
        for (const auto& [name, data] : entries) {
            const auto local_offset = static_cast<std::uint32_t>(out.size());
            const auto size = static_cast<std::uint32_t>(data.size());
            put32(out, 0x04034B50U);
            put16(out, 20U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, 0U); // crc (unchecked by the reader)
            put32(out, size);
            put32(out, size);
            put16(out, static_cast<std::uint16_t>(name.size()));
            put16(out, 0U);
            out.insert(out.end(), name.begin(), name.end());
            out.insert(out.end(), data.begin(), data.end());
            directory.push_back({name, size, local_offset});
        }
        const auto cd_offset = static_cast<std::uint32_t>(out.size());
        for (const central& c : directory) {
            put32(out, 0x02014B50U);
            put16(out, 20U);
            put16(out, 20U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, 0U);
            put32(out, c.size);
            put32(out, c.size);
            put16(out, static_cast<std::uint16_t>(c.name.size()));
            put16(out, 0U);
            put16(out, 0U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, c.local_offset);
            out.insert(out.end(), c.name.begin(), c.name.end());
        }
        const auto cd_size = static_cast<std::uint32_t>(out.size()) - cd_offset;
        put32(out, 0x06054B50U);
        put16(out, 0U);
        put16(out, 0U);
        put16(out, static_cast<std::uint16_t>(directory.size()));
        put16(out, static_cast<std::uint16_t>(directory.size()));
        put32(out, cd_size);
        put32(out, cd_offset);
        put16(out, 0U);
        return out;
    }

} // namespace

TEST_CASE("irem_m72_adapter maps soundcpu.bin from a development zip",
          "[irem_m72][adapter]") {
    namespace m72 = mnemos::manifests::irem_m72;

    std::vector<std::uint8_t> sound_rom(m72::sound_rom_size, 0x00U);
    // LD A,66; LD (F010),A; HALT
    const std::vector<std::uint8_t> program{0x3EU, 0x66U, 0x32U, 0x10U, 0xF0U, 0x76U};
    for (std::size_t i = 0; i < program.size(); ++i) {
        sound_rom[i] = program[i];
    }
    const auto zip = make_stored_zip({
        {"maincpu.bin", make_program()},
        {"soundcpu.bin", sound_rom},
    });

    irem_m72_adapter adapter(zip, "dev-sound-rom");
    REQUIRE(adapter.machine().sound_rom_present);
    CHECK_FALSE(adapter.machine().sound_cpu.reset_line_held());

    adapter.step_one_frame();
    CHECK(adapter.machine().sound_ram[0xF010U] == 0x66U);
}

TEST_CASE("irem_m72_adapter maps optional samples and mcu bins from a development zip",
          "[irem_m72][adapter]") {
    namespace m72 = mnemos::manifests::irem_m72;

    std::vector<std::uint8_t> sound_rom(m72::sound_rom_size, 0x00U);
    // LD A,02; OUT (80),A; LD A,00; OUT (81),A; IN A,(84); LD (F010),A; HALT
    const std::vector<std::uint8_t> sound_program{
        0x3EU, 0x02U, 0xD3U, 0x80U, 0x3EU, 0x00U, 0xD3U,
        0x81U, 0xDBU, 0x84U, 0x32U, 0x10U, 0xF0U, 0x76U,
    };
    for (std::size_t i = 0; i < sound_program.size(); ++i) {
        sound_rom[i] = sound_program[i];
    }

    std::vector<std::uint8_t> samples{0x11U, 0x22U, 0x99U, 0x44U};
    const std::vector<std::uint8_t> mcu_program{
        0x90U, 0x00U, 0x02U, // MOV DPTR,#0002
        0xE0U,               // MOVX A,@DPTR
        0x24U, 0x01U,        // ADD A,#1
        0xF0U,               // MOVX @DPTR,A
        0x80U, 0xFEU,        // SJMP $
    };
    const auto zip = make_stored_zip({
        {"maincpu.bin", make_program()},
        {"soundcpu.bin", sound_rom},
        {"samples.bin", samples},
        {"mcu.bin", mcu_program},
    });

    irem_m72_adapter adapter(zip, "dev-optional-regions");
    REQUIRE(adapter.machine().sound_rom_present);
    REQUIRE(adapter.machine().mcu_present);
    REQUIRE(adapter.chips().size() == 7U);
    CHECK(adapter.chips().back()->metadata().part_number == "mcs51");

    adapter.machine().main_to_mcu = 0x20U;
    adapter.step_one_frame();
    CHECK(adapter.machine().sound_ram[0xF010U] == 0x99U);
    CHECK(adapter.machine().mcu_to_main == 0x21U);
}

TEST_CASE("irem_m72_adapter boots the real first-game set", "[irem_m72][adapter][data]") {
    // Data-gated (never committed): MNEMOS_M72_RTYPE_SET points at the
    // authentic rtype.zip dump set. The adapter uses the checked-in rtype.toml
    // when the zip does not carry game.toml. Asserts the hardware boot path:
    // the V30 uploads the sound program and releases the Z80, and the frame
    // goes non-blank.
    const char* set_env = opt_env("MNEMOS_M72_RTYPE_SET");
    if (set_env == nullptr || *set_env == '\0') {
        SKIP("set MNEMOS_M72_RTYPE_SET to the first-game rtype.zip set");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    irem_m72_adapter adapter(std::move(*bytes), "rtype", nullptr, {}, set_env);
    REQUIRE(adapter.machine().roms.issues.empty()); // CRC-verified load
    CHECK(adapter.machine().params.work_ram_base == 0x40000U);
    CHECK(adapter.machine().dip_switches == 0xFDFBU);
    CHECK(adapter.machine().sound_cpu.reset_line_held()); // parked at power-on
    // The boot-chunk reload must put a far jump at the V30 reset vector --
    // in the region, through the bus, and through the first executed
    // instruction.
    const auto& main_region = adapter.machine().roms.regions.at("maincpu");
    REQUIRE(main_region.size() == 0x100000U);
    REQUIRE(main_region[0xFFFF0U] == 0xEAU);
    REQUIRE(adapter.machine().main_bus.read8(0xFFFF0U) == 0xEAU);
    adapter.machine().main_cpu.step_instruction();
    const auto boot_regs = adapter.machine().main_cpu.cpu_registers();
    INFO("after reset-vector jump: cs=" << boot_regs.cs << " ip=" << boot_regs.ip);
    REQUIRE((static_cast<std::uint32_t>(boot_regs.cs) * 16U + boot_regs.ip) < 0x40000U);

    bool sound_released = false;
    bool frame_lit = false;
    for (int frame = 0; frame < 600 && !(sound_released && frame_lit); ++frame) {
        adapter.step_one_frame();
        sound_released = sound_released || !adapter.machine().sound_cpu.reset_line_held();
        const auto view = adapter.current_frame();
        for (std::uint32_t i = 1; i < view.width * view.height; ++i) {
            if (view.pixels[i] != view.pixels[0]) {
                frame_lit = true;
                break;
            }
        }
    }
    CHECK(sound_released);
    CHECK(frame_lit);
}

TEST_CASE("irem_m72_adapter boots a real protected M72 set",
          "[irem_m72][adapter][data]") {
    namespace m72 = mnemos::manifests::irem_m72;

    // Data-gated (never committed), game-agnostic protected-board check:
    // MNEMOS_M72_PROTECTED_SET points at a zip of a protected true-M72 set
    // (for example bchopper, mrheli, imgfight, airduelm72, dbreedm72, or
    // dkgensanm72). The adapter uses the checked-in game TOML when the zip does
    // not carry game.toml. Clone sets resolve their parent zip from the same
    // directory via adapter_options.rom_path. The board must CRC-load, expose
    // either a real MCU or the manifest-declared no-dump HLE profile, make the
    // sound CPU runnable, and light a frame within the warm-up.
    // MNEMOS_M72_PROTECTED_FRAMES overrides the warm-up count.
    const char* set_env = opt_env("MNEMOS_M72_PROTECTED_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_M72_PROTECTED_SET to a protected M72 set zip");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*bytes);
    options.display_name = "protected-m72";
    options.rom_path = set_env;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);
    auto& machine = adapter.machine();

    REQUIRE(machine.roms.issues.empty()); // every declared dump present and CRC-clean
    REQUIRE_FALSE(adapter.set_name().empty());
    const auto expected_params = m72::board_params_for(adapter.set_name());
    CHECK(machine.params.work_ram_base == expected_params.work_ram_base);
    CHECK(machine.dip_switches == expected_params.dip_default);
    REQUIRE((machine.mcu_present || machine.protection_hle_present));
    if (machine.mcu_present) {
        REQUIRE(adapter.chips().size() == 7U);
        CHECK(adapter.chips().back()->metadata().part_number == "mcs51");
    } else {
        REQUIRE(machine.protection_hle_present);
        REQUIRE(adapter.chips().size() == 6U);
        REQUIRE(machine.params.protection_hle_profile.has_value());
        CHECK((*machine.params.protection_hle_profile == "irem_m72.dbreedm72_no_dump_mcu" ||
               *machine.params.protection_hle_profile == "irem_m72.dkgensanm72_no_dump_mcu"));
        machine.main_bus.write8(m72::mcu_shared_main_base, 0x3CU);
        CHECK(machine.main_bus.read8(m72::mcu_shared_main_base) == 0xC3U);
    }

    const auto& main_region = machine.roms.regions.at("maincpu");
    REQUIRE(main_region.size() == mnemos::manifests::irem_m72::main_rom_size);
    REQUIRE(machine.main_bus.read8(0xFFFF0U) != 0xFFU);
    if (machine.sound_rom_present) {
        CHECK_FALSE(machine.sound_cpu.reset_line_held());
    } else {
        CHECK(machine.sound_cpu.reset_line_held()); // RAM-upload sound boards start parked
    }

    int warmup_frames = 600;
    if (const char* frames_env = opt_env("MNEMOS_M72_PROTECTED_FRAMES")) {
        const int parsed = std::atoi(frames_env);
        if (parsed > 0) {
            warmup_frames = parsed;
        }
    }

    bool sound_released = !machine.sound_cpu.reset_line_held();
    bool frame_lit = false;
    for (int frame = 0; frame < warmup_frames && !(sound_released && frame_lit); ++frame) {
        adapter.step_one_frame();
        sound_released = sound_released || !machine.sound_cpu.reset_line_held();
        if (!frame_lit) {
            const auto view = adapter.current_frame();
            for (std::uint32_t i = 1; i < view.width * view.height; ++i) {
                if (view.pixels[i] != view.pixels[0]) {
                    frame_lit = true;
                    break;
                }
            }
        }
    }
    CHECK(sound_released);
    CHECK(frame_lit);
}

TEST_CASE("irem_m72_adapter validates a real vertical M72 set orientation",
          "[irem_m72][adapter][data]") {
    // Data-gated (never committed): MNEMOS_M72_VERTICAL_SET points at a vertical
    // true-M72 set zip (for example imgfight or airduelm72). The adapter uses
    // the checked-in game TOML when the zip does not carry game.toml. Clone sets
    // resolve their parent zip beside it via adapter_options.rom_path.
    const char* set_env = opt_env("MNEMOS_M72_VERTICAL_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_M72_VERTICAL_SET to a vertical M72 set zip");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*bytes);
    options.display_name = "vertical-m72";
    options.rom_path = set_env;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    REQUIRE(adapter.machine().roms.issues.empty());
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    CHECK(adapter.current_frame().width == 384U);
    CHECK(adapter.current_frame().height == 256U);

    bool frame_lit = false;
    for (int frame = 0; frame < 600 && !frame_lit; ++frame) {
        adapter.step_one_frame();
        const auto view = adapter.current_frame();
        for (std::uint32_t i = 1; i < view.width * view.height; ++i) {
            if (view.pixels[i] != view.pixels[0]) {
                frame_lit = true;
                break;
            }
        }
    }
    CHECK(frame_lit);
}

TEST_CASE("irem_m72_adapter loads a declarative game.toml set from a zip", "[irem_m72][adapter]") {
    // Split the working program image into even/odd halves and let the
    // declaration's stride-2 placement reassemble it.
    const auto whole = make_program();
    std::vector<std::uint8_t> low(whole.size() / 2U);
    std::vector<std::uint8_t> high(whole.size() / 2U);
    for (std::size_t i = 0; i < whole.size() / 2U; ++i) {
        low[i] = whole[i * 2U];
        high[i] = whole[i * 2U + 1U];
    }
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "synthetic"
board = "irem_m72"
orientation = "vertical"

[[dip]]
bank = "SW1"
name = "Lives"
mask = 0x0003
default = 0x0003

[[dip.option]]
label = "2"
value = 0x0002

[[dip.option]]
label = "3"
value = 0x0003

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog.lo"
offset = 0
stride = 2

[[region.file]]
name = "prog.hi"
offset = 1
stride = 2
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog.lo", low},
        {"prog.hi", high},
    });

    irem_m72_adapter adapter(zip, "synthetic");
    CHECK(adapter.set_name() == "synthetic");
    CHECK(adapter.system_spec()[2].value == "synthetic");
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::vertical);
    REQUIRE(adapter.dip_switches().size() == 1U);
    CHECK(adapter.dip_switches()[0].name == "Lives");
    CHECK(adapter.dip_switches()[0].default_value == 0x0003U);
    CHECK(adapter.system_spec().back().label == "DIP switches");
    CHECK(adapter.system_spec().back().value == "1");
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U); // the program ran
}

TEST_CASE("irem_m72_adapter passes declarative MCU HLE profiles to the board",
          "[irem_m72][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "dbreedm72"
board = "irem_m72"

[[hle]]
chip = "mcu"
profile = "irem_m72.dbreedm72_no_dump_mcu"
rationale = "The set's i8751 dump is unavailable; use the declared interim profile."

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    irem_m72_adapter adapter(zip, "dbreedm72-hle");
    CHECK(adapter.set_name() == "dbreedm72");
    CHECK_FALSE(adapter.machine().mcu_present);
    REQUIRE(adapter.machine().protection_hle_present);
    CHECK(adapter.machine().params.protection_hle_profile ==
          std::optional<std::string>{"irem_m72.dbreedm72_no_dump_mcu"});

    adapter.machine().main_bus.write8(mnemos::manifests::irem_m72::mcu_shared_main_base, 0x34U);
    CHECK(adapter.machine().main_bus.read8(mnemos::manifests::irem_m72::mcu_shared_main_base) ==
          0xCBU);
}

TEST_CASE("irem_m72_adapter reports unsupported declarative MCU HLE profiles",
          "[irem_m72][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "dbreedm72"
board = "irem_m72"

[[hle]]
chip = "mcu"
profile = "irem_m72.unknown_no_dump_mcu"
rationale = "Synthetic invalid profile for loader diagnostics."

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    irem_m72_adapter adapter(zip, "dbreedm72-invalid-hle");
    CHECK(adapter.set_name() == "dbreedm72");
    CHECK_FALSE(adapter.machine().mcu_present);
    CHECK_FALSE(adapter.machine().protection_hle_present);
    CHECK_FALSE(adapter.machine().params.protection_hle_profile.has_value());
    REQUIRE(adapter.machine().roms.issues.size() == 1U);
    CHECK(adapter.machine().roms.issues[0].file == "mcu");
    CHECK(adapter.machine().roms.issues[0].message.find("unsupported M72 MCU HLE profile") !=
          std::string::npos);
}

TEST_CASE("irem_m72_adapter whole-player save-state round-trips through runtime",
          "[irem_m72][adapter]") {
    namespace irem = mnemos::apps::player::adapters::irem_m72;

    irem::irem_m72_adapter source(make_program(), "save-source");
    source.step_one_frame();
    const auto warm_audio = source.drain_audio();
    REQUIRE(warm_audio.frame_count > 0U);

    mnemos::frontend_sdk::controller_state p1{};
    p1.up = true;
    p1.a = true;
    p1.start = true;
    p1.select = true;
    p1.mode = true;
    p1.aim_x = 123;
    p1.aim_y = 45;
    p1.trigger = true;
    source.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.right = true;
    p2.b = true;
    p2.start = true;
    p2.mode = true;
    source.apply_input(1, p2);

    source.machine().work_ram[0x22U] = 0xABU;
    source.machine().sample_address = 0x3456U;
    source.machine().main_to_mcu = 0x40U;
    source.machine().mcu_to_main = 0x41U;
    source.machine().record_dac_write(0xC0U);
    source.machine().fm.tick(mnemos::chips::audio::ym2151::clocks_per_sample);
    const auto first_dac_audio = source.drain_audio();
    REQUIRE(first_dac_audio.frame_count == 1U);
    REQUIRE(first_dac_audio.samples != nullptr);
    CHECK(first_dac_audio.samples[0] == (0xC0 - 0x80) * 64);
    CHECK(first_dac_audio.samples[1] == (0xC0 - 0x80) * 64);
    source.machine().fm.tick(mnemos::chips::audio::ym2151::clocks_per_sample);

    const mnemos::runtime::save_target board_target =
        irem::build_save_target(source.machine());
    CHECK(board_target.manifest_id == "irem_m72");
    CHECK(board_target.manifest_rev == 6U);
    const mnemos::runtime::save_target source_target = irem::build_save_target(source);
    CHECK(source_target.manifest_id == "irem_m72.adapter");
    CHECK(source_target.manifest_rev == 1U);
    REQUIRE(source_target.components.size() == 2U);
    const std::vector<std::uint8_t> blob = mnemos::runtime::write_save_state(source_target);
    REQUIRE_FALSE(blob.empty());

    irem::irem_m72_adapter restored(make_program(), "save-restored");
    CHECK(restored.machine().work_ram[0x22U] != 0xABU);
    mnemos::runtime::save_target restored_target = irem::build_save_target(restored);
    mnemos::runtime::save_target stale_target = restored_target;
    stale_target.manifest_rev = 0U;
    CHECK(mnemos::runtime::read_save_state(blob, stale_target).status ==
          mnemos::runtime::load_status::manifest_mismatch);
    auto mismatched_rom = make_program();
    mismatched_rom[0x200U] ^= 0x01U;
    irem::irem_m72_adapter mismatched(std::move(mismatched_rom), "save-wrong-rom");
    CHECK(mnemos::runtime::read_save_state(blob, irem::build_save_target(mismatched))
              .status == mnemos::runtime::load_status::chunk_rejected);

    const mnemos::runtime::load_result result =
        mnemos::runtime::read_save_state(blob, restored_target);
    REQUIRE(result.ok());

    CHECK(restored.frames_stepped() == source.frames_stepped());
    CHECK(restored.machine().work_ram[0x22U] == 0xABU);
    CHECK(restored.machine().sample_address == 0x3456U);
    CHECK(restored.machine().main_to_mcu == 0x40U);
    CHECK(restored.machine().mcu_to_main == 0x41U);
    CHECK(restored.machine().input_p1 == static_cast<std::uint8_t>(0xFFU & ~0x08U & ~0x80U));
    CHECK(restored.machine().input_p2 == static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x40U));
    CHECK(restored.machine().input_system ==
          static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x02U & ~0x04U & ~0x10U & ~0x20U));
    CHECK(restored.machine().dac.level() == 0xC0U);

    const auto restored_dac_audio = restored.drain_audio();
    REQUIRE(restored_dac_audio.frame_count == 1U);
    REQUIRE(restored_dac_audio.samples != nullptr);
    CHECK(restored_dac_audio.samples[0] == (0xC0 - 0x80) * 64);
    CHECK(restored_dac_audio.samples[1] == (0xC0 - 0x80) * 64);

    restored.apply_input(0, {});
    CHECK(restored.machine().input_p1 == 0xFFU);
    CHECK(restored.machine().input_p2 == static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x40U));
    CHECK(restored.machine().input_system == static_cast<std::uint8_t>(0xFFU & ~0x02U & ~0x20U));
}

TEST_CASE("irem_m72_adapter resolves a declarative clone parent zip",
          "[irem_m72][adapter]") {
    const auto whole = make_program();
    std::vector<std::uint8_t> low(whole.size() / 2U);
    std::vector<std::uint8_t> high(whole.size() / 2U);
    for (std::size_t i = 0; i < whole.size() / 2U; ++i) {
        low[i] = whole[i * 2U];
        high[i] = whole[i * 2U + 1U];
    }

    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "mrheli"
parent = "bchopper"
board = "irem_m72"
orientation = "horizontal"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "clone.lo"
offset = 0
stride = 2

[[region.file]]
name = "parent.hi"
offset = 1
stride = 2
)";
    const std::string parent_manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "bchopper"
board = "irem_m72"

[[dip]]
bank = "SW1"
name = "Parent Lives"
mask = 0x0003
default = 0x0003

[[dip.option]]
label = "3"
value = 0x0003

[[region]]
name = "tiles_a"
size = 4

[[region.file]]
name = "parent.gfx"
offset = 0
size = 4
)";

    const auto root = std::filesystem::temp_directory_path() / "mnemos_irem_m72_parent_fallback";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));

    const std::vector<std::uint8_t> parent_gfx{0x10U, 0x11U, 0x12U, 0x13U};
    const auto parent_zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(parent_manifest.begin(), parent_manifest.end())},
        {"parent.hi", high},
        {"parent.gfx", parent_gfx},
    });
    const auto clone_zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"clone.lo", low},
    });
    const auto parent_path = (root / "bchopper.zip").string();
    const auto clone_path = (root / "mrheli.zip").string();
    REQUIRE(mnemos::io::write_file(parent_path, parent_zip));
    REQUIRE(mnemos::io::write_file(clone_path, clone_zip));

    auto clone_bytes = mnemos::io::read_file(clone_path);
    REQUIRE(clone_bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*clone_bytes);
    options.display_name = "mrheli";
    options.rom_path = clone_path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    CHECK(adapter.machine().roms.issues.empty());
    CHECK(adapter.machine().params.work_ram_base == 0xA0000U);
    REQUIRE(adapter.dip_switches().size() == 1U);
    CHECK(adapter.dip_switches()[0].name == "Parent Lives");
    REQUIRE(adapter.machine().roms.region("tiles_a") != nullptr);
    CHECK(*adapter.machine().roms.region("tiles_a") == parent_gfx);
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U);
}

TEST_CASE("irem_m72_adapter resolves checked-in manifests for standard set zips",
          "[irem_m72][adapter]") {
    const auto rtype_decl = require_embedded_decl("rtype");
    const auto rtype_zip = make_stored_zip(placeholder_entries_for(rtype_decl, 0x11U));

    const auto root = std::filesystem::temp_directory_path() / "mnemos_irem_m72_embedded_manifest";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    const auto set_path = (root / "rtype.zip").string();
    REQUIRE(mnemos::io::write_file(set_path, rtype_zip));
    auto bytes = mnemos::io::read_file(set_path);
    REQUIRE(bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*bytes);
    options.display_name = "rtype";
    options.rom_path = set_path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    CHECK(adapter.set_name() == "rtype");
    CHECK(adapter.system_spec()[2].value == "rtype");
    CHECK(adapter.machine().params.work_ram_base == 0x40000U);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    REQUIRE(adapter.dip_switches().size() == 13U);
    CHECK(adapter.dip_switches()[0].name == "Lives");
    CHECK(adapter.machine().roms.region("maincpu") != nullptr);
    CHECK(adapter.machine().roms.region("sprites") != nullptr);
    CHECK(has_only_crc_issues(adapter.machine().roms.issues));
}

TEST_CASE("irem_m72_adapter resolves checked-in clone and parent manifests",
          "[irem_m72][adapter]") {
    const auto parent_decl = require_embedded_decl("rtype");
    const auto clone_decl = require_embedded_decl("rtypej");
    const auto parent_zip = make_stored_zip(placeholder_entries_for(parent_decl, 0x22U));
    const auto clone_zip = make_stored_zip(placeholder_entries_for(clone_decl, 0x33U));

    const auto root =
        std::filesystem::temp_directory_path() / "mnemos_irem_m72_embedded_clone_manifest";
    REQUIRE((std::filesystem::create_directories(root) || std::filesystem::exists(root)));
    const auto parent_path = (root / "rtype.zip").string();
    const auto clone_path = (root / "rtypej.zip").string();
    REQUIRE(mnemos::io::write_file(parent_path, parent_zip));
    REQUIRE(mnemos::io::write_file(clone_path, clone_zip));
    auto bytes = mnemos::io::read_file(clone_path);
    REQUIRE(bytes.has_value());

    mnemos::frontend_sdk::adapter_options options{};
    options.rom = std::move(*bytes);
    options.display_name = "rtypej";
    options.rom_path = clone_path;
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("irem_m72", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<irem_m72_adapter&>(*system);

    CHECK(adapter.set_name() == "rtypej");
    CHECK(adapter.system_spec()[2].value == "rtypej");
    CHECK(adapter.machine().params.work_ram_base == 0x40000U);
    REQUIRE(adapter.dip_switches().size() == 13U);
    CHECK(adapter.dip_switches()[0].name == "Lives");
    REQUIRE(adapter.machine().roms.region("tiles_a") != nullptr);
    CHECK(adapter.machine().roms.region("tiles_a")->front() == 0x22U);
    REQUIRE(adapter.machine().roms.region("maincpu") != nullptr);
    CHECK(adapter.machine().roms.region("maincpu")->front() == 0x33U);
    CHECK(has_only_crc_issues(adapter.machine().roms.issues));
}

TEST_CASE("irem_m72_adapter rejects a game.toml for another board", "[irem_m72][adapter]") {
    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "wrong_board"
board = "capcom_cps1"

[[region]]
name = "maincpu"
size = 0x100000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    irem_m72_adapter adapter(zip, "wrong_board");

    const auto& main_region = adapter.machine().roms.regions.at("maincpu");
    REQUIRE(main_region.size() == mnemos::manifests::irem_m72::main_rom_size);
    CHECK(main_region[0xFFFF0U] == 0xFFU);
    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x00U);
}
