#include "irem_m72_adapter.hpp"

#include "adapter_registry.hpp"
#include "file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <string>
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
    CHECK(adapter.chips().size() == 3U);
    CHECK(adapter.system_spec().size() == 3U);
    CHECK(adapter.system_spec()[1].value == "Irem M72");
}

TEST_CASE("irem_m72_adapter maps pads onto the board's input bytes", "[irem_m72][adapter]") {
    irem_m72_adapter adapter(make_program());

    mnemos::frontend_sdk::controller_state p1{};
    p1.up = true;
    p1.a = true;
    p1.start = true;
    p1.select = true; // coin 1
    adapter.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.right = true;
    p2.start = true;
    adapter.apply_input(1, p2);

    auto& machine = adapter.machine();
    // Hardware layout: up = bit 3, button 1 = bit 7; right = bit 0.
    CHECK(machine.input_p1 == static_cast<std::uint8_t>(0xFFU & ~0x08U & ~0x80U));
    CHECK(machine.input_p2 == static_cast<std::uint8_t>(0xFFU & ~0x01U));
    // start1 (bit0) + start2 (bit1) + coin1 (bit2) held low.
    CHECK(machine.input_system == static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x02U & ~0x04U));

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

TEST_CASE("irem_m72_adapter boots the real first-game set", "[irem_m72][adapter][data]") {
    // Data-gated (never committed): MNEMOS_M72_RTYPE_SET points at a zip of
    // the authentic dump files plus a "game.toml" copy of
    // src/manifests/irem_m72/games/rtype.toml. Asserts the hardware boot
    // path: the V30 uploads the sound program and releases the Z80, and the
    // frame goes non-blank.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in test data path
#endif
    const char* set_env = std::getenv("MNEMOS_M72_RTYPE_SET");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (set_env == nullptr || *set_env == '\0') {
        SKIP("set MNEMOS_M72_RTYPE_SET to the first-game set zip (game.toml inside)");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    irem_m72_adapter adapter(std::move(*bytes), "rtype");
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
    adapter.step_one_frame();
    CHECK(adapter.machine().work_ram[0] == 0x42U); // the program ran
}
