#include "capcom_cps1_adapter.hpp"

#include "adapter_registry.hpp"
#include "cps_b_profiles.hpp"
#include "file.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace {

    namespace cps1 = mnemos::manifests::capcom_cps1;
    using mnemos::apps::player::adapters::capcom_cps1::capcom_cps1_adapter;

    void poke32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 24U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value >> 16U);
        bytes[at + 2U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 3U] = static_cast<std::uint8_t>(value);
    }

    void poke16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value);
    }

    // A bare 68000 program image: reset vectors ($0 = SSP, $4 = PC) point at a
    // handler at $000400 that writes a marker word into work RAM ($FF0000) then
    // branches to self, so the board keeps running across frames.
    [[nodiscard]] std::vector<std::uint8_t> make_program() {
        std::vector<std::uint8_t> rom(cps1::main_rom_size, 0xFFU);
        poke32(rom, 0x0U, 0x00FF0000U); // initial SSP -> top of work RAM
        poke32(rom, 0x4U, 0x00000400U); // initial PC -> the program below
        // MOVE.W #$4242, ($00FF0000).L  -- stamp a marker into work RAM word 0.
        poke16(rom, 0x400U, 0x33FCU);
        poke16(rom, 0x402U, 0x4242U);     // immediate
        poke32(rom, 0x404U, 0x00FF0000U); // absolute long destination
        poke16(rom, 0x408U, 0x60FEU);     // BRA * (branch to self)
        return rom;
    }

    void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8U));
    }

    void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
        put16(out, static_cast<std::uint16_t>(v));
        put16(out, static_cast<std::uint16_t>(v >> 16U));
    }

    // Minimal STORED-method zip over the given entries (CRC fields zeroed; the
    // reader does not verify them). Mirrors the M72 adapter test helper.
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

TEST_CASE("capcom_cps1_adapter boots a bare program through the registry",
          "[capcom_cps1][adapter]") {
    mnemos::frontend_sdk::adapter_options options{};
    options.rom = make_program();
    options.display_name = "smoke";
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("cps1", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<capcom_cps1_adapter&>(*system);

    REQUIRE(adapter.machine().video.frame_index() == 0U);
    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 2U);
    CHECK(adapter.machine().video.frame_index() == 2U);
    // The program ran: the marker landed in work RAM (big-endian word $4242).
    CHECK(adapter.machine().work_ram[0] == 0x42U);
    CHECK(adapter.machine().work_ram[1] == 0x42U);

    const auto frame = adapter.current_frame();
    CHECK(frame.width == 384U);
    CHECK(frame.height == 224U);
    CHECK(adapter.region().frames_per_second_x1000 == 59600U);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    CHECK(adapter.chips().size() == 5U);
    CHECK(adapter.system_spec().size() == 3U);
    CHECK(adapter.system_spec()[1].value == "Capcom CPS1");
}

TEST_CASE("capcom_cps1_adapter threads cps_b_profile from the game.toml into the board",
          "[capcom_cps1][adapter][profile]") {
    // Profile 24 carries an ID protection port + the 16x16 multiplier, so the
    // selection is observable through a CPS-B protection read-back.
    constexpr std::uint16_t profile_id = 24U;
    const auto profile = cps1::profile_for_id(profile_id);
    REQUIRE(profile.has_value());

    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "cps1_synth"
board = "capcom_cps1"
cps_b_profile = 24

[[region]]
name = "maincpu"
size = 0x800000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    capcom_cps1_adapter adapter(zip, "cps1_synth");
    // The declared profile reached the board.
    CHECK(adapter.machine().profile.id == profile_id);

    // The CPS-B ID port returns the profile's identity value (the protection
    // read-back the game's 68000 check reads).
    const auto cps_b_addr = [](std::uint8_t off) { return cps1::cps_b_reg_base + off; };
    CHECK(adapter.machine().main_bus.read16_be(cps_b_addr(profile->id_offset)) ==
          profile->id_value);

    // An absent cps_b_profile keeps the chip's legacy default (id 0).
    const std::string no_profile = R"(
[set]
schema = "mnemos-romset/1"
name = "cps1_synth"
board = "capcom_cps1"

[[region]]
name = "maincpu"
size = 0x800000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip2 = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(no_profile.begin(), no_profile.end())},
        {"prog", make_program()},
    });
    capcom_cps1_adapter adapter2(zip2, "cps1_synth");
    CHECK(adapter2.machine().profile.id == 0U);
}

TEST_CASE("capcom_cps1_adapter maps pads onto the board's input words",
          "[capcom_cps1][adapter][input]") {
    capcom_cps1_adapter adapter(make_program());

    mnemos::frontend_sdk::controller_state p1{};
    p1.up = true;
    p1.a = true;      // button 1
    p1.start = true;  // start 1
    p1.select = true; // coin 1
    adapter.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.right = true;
    adapter.apply_input(1, p2);

    auto& machine = adapter.machine();
    // P1 (low byte): up = bit3, button1 = bit4. P2 (high byte): right = bit0.
    const std::uint8_t p1_byte = static_cast<std::uint8_t>(0xFFU & ~0x08U & ~0x10U);
    const std::uint8_t p2_byte = static_cast<std::uint8_t>(0xFFU & ~0x01U);
    CHECK(machine.input_p == static_cast<std::uint16_t>((p2_byte << 8U) | p1_byte));
    // The board surfaces the player word on the read window (P2 high, P1 low).
    CHECK(machine.main_bus.read16_be(cps1::player_input_base) ==
          static_cast<std::uint16_t>((p2_byte << 8U) | p1_byte));

    // System byte: start1 = bit0, coin1 = bit4 held low.
    const std::uint8_t sys_byte = static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x10U);
    CHECK(static_cast<std::uint8_t>(machine.input_sys & 0xFFU) == sys_byte);

    adapter.apply_input(2, p1); // out-of-range port ignored
    CHECK(machine.input_p == static_cast<std::uint16_t>((p2_byte << 8U) | p1_byte));
}

namespace {

    // Minimal binary PPM (P6) writer for the optional visual-A/B dump. Packs the
    // visible region of an XRGB framebuffer row-by-row (storage may be strided).
    [[nodiscard]] bool write_ppm(const mnemos::chips::frame_buffer_view& fb,
                                 const std::string& path) {
        if (fb.pixels == nullptr || fb.width == 0U || fb.height == 0U) {
            return false;
        }
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::fopen: opt-in test artifact path
#endif
        std::FILE* out = std::fopen(path.c_str(), "wb");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (out == nullptr) {
            return false;
        }
        std::fprintf(out, "P6\n%u %u\n255\n", fb.width, fb.height);
        const std::uint32_t stride = fb.effective_stride();
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                const std::uint32_t p = row[x];
                const unsigned char rgb[3] = {static_cast<unsigned char>((p >> 16U) & 0xFFU),
                                              static_cast<unsigned char>((p >> 8U) & 0xFFU),
                                              static_cast<unsigned char>(p & 0xFFU)};
                std::fwrite(rgb, 1U, 3U, out);
            }
        }
        std::fclose(out);
        return true;
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

} // namespace

TEST_CASE("capcom_cps1_adapter boots a real CPS1 set", "[capcom_cps1][adapter][data]") {
    // Data-gated (never committed), game-agnostic: MNEMOS_CPS1_SET points at a zip
    // of the authentic dump files plus a "game.toml" copy of the matching
    // src/manifests/capcom_cps1/games/<name>.toml. Asserts the hardware boot path
    // on any CPS1 set the board supports -- a CRC-clean declarative load, a 68K
    // reset vector inside the program image, and a frame that goes non-blank with
    // the vblank IRQ raised and serviced. MNEMOS_CPS1_PPM dumps the rendered frame
    // for visual A/B; MNEMOS_CPS1_FRAMES overrides the warm-up frame count.
    const char* set_env = opt_env("MNEMOS_CPS1_SET");
    if (set_env == nullptr) {
        SKIP("set MNEMOS_CPS1_SET to a CPS1 set zip (game.toml inside)");
    }
    auto bytes = mnemos::io::read_file(set_env);
    REQUIRE(bytes.has_value());

    // Pass the set's own path so a clone set (game.toml with a `parent` key)
    // resolves its parent zip from the same directory and merges the shared ROMs.
    capcom_cps1_adapter adapter(std::move(*bytes), "cps1", nullptr, {}, set_env);
    auto& machine = adapter.machine();
    REQUIRE(machine.roms.issues.empty()); // every file present, sized, CRC-verified
    INFO("cps_b_profile id = " << machine.profile.id);

    // The program region is padded up to the 8 MiB 68K program window; the reset
    // PC ($000004) must land inside the program image (not the 0xFF padding).
    const auto& main_region = machine.roms.regions.at("maincpu");
    REQUIRE(main_region.size() == 0x800000U);
    const std::uint32_t reset_pc =
        (static_cast<std::uint32_t>(machine.main_bus.read16_be(0x4U)) << 16U) |
        machine.main_bus.read16_be(0x6U);
    INFO("reset PC = " << reset_pc);
    REQUIRE(reset_pc < 0x800000U);

    // Run a fixed warm-up (~10 s at 60 Hz) so the game boots past the hardware
    // self-test and reaches an attract/title screen before asserting. (An early
    // exit on the first non-blank frame would fire before the game lowers the
    // 68K interrupt mask, so no IRQ would have been serviced yet.)
    int warmup_frames = 600;
    if (const char* frames_env = opt_env("MNEMOS_CPS1_FRAMES")) {
        warmup_frames = std::atoi(frames_env);
        if (warmup_frames <= 0) {
            warmup_frames = 600;
        }
    }
    // Opt-in: MNEMOS_CPS1_HALT=<hex pc> captures the instruction trail INTO that PC
    // (the first time it executes) -- the path a boot takes into a fatal halt loop.
    std::array<std::uint32_t, 512> pc_ring{};
    std::size_t pc_ring_pos = 0U;
    std::array<std::uint32_t, 512> pc_trail{};
    bool trail_captured = false;
    if (const char* halt_env = opt_env("MNEMOS_CPS1_HALT")) {
        const auto halt_pc = static_cast<std::uint32_t>(std::strtoul(halt_env, nullptr, 16));
        machine.main_cpu.diagnostics().set_trace_callback([&, halt_pc](std::uint32_t pc) {
            pc_ring[pc_ring_pos % pc_ring.size()] = pc;
            ++pc_ring_pos;
            if (pc == halt_pc && !trail_captured) {
                trail_captured = true;
                for (std::size_t i = 0; i < pc_ring.size(); ++i) {
                    pc_trail[i] = pc_ring[(pc_ring_pos + i) % pc_ring.size()];
                }
            }
        });
    }

    bool frame_lit = false;
    for (int frame = 0; frame < warmup_frames; ++frame) {
        adapter.step_one_frame();
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
    if (trail_captured) {
        machine.main_cpu.diagnostics().set_trace_callback({});
        std::fprintf(stderr, "[trace] path into halt (consecutive repeats collapsed):\n ");
        std::uint32_t prev = 0xFFFFFFFFU;
        int col = 0;
        for (std::uint32_t pc : pc_trail) {
            if (pc == prev) {
                continue;
            }
            prev = pc;
            std::fprintf(stderr, " %06X", pc);
            if (++col % 12 == 0) {
                std::fprintf(stderr, "\n ");
            }
        }
        std::fprintf(stderr, "\n");
    }
    // Opt-in boot diagnostic: sample the 68K PC/SR over a few extra frames so a
    // hung set (a tight early-boot wait loop) shows its PC cluster + whether the
    // interrupt mask is still raised -- the lead for a bring-up that stalls.
    if (opt_env("MNEMOS_CPS1_TRACE") != nullptr) {
        for (int i = 0; i < 8; ++i) {
            adapter.step_one_frame();
            const auto r = machine.main_cpu.cpu_registers();
            std::fprintf(stderr, "[trace] frame +%d  pc=%06X  sr=%04X  ipm=%u\n", i, r.pc, r.sr,
                         static_cast<unsigned>((r.sr >> 8U) & 7U));
        }
        const auto r = machine.main_cpu.cpu_registers();
        // Memory window to dump: MNEMOS_CPS1_DUMP="start:len" (hex), else around pc.
        std::uint32_t dump_start = r.pc - 0x10U;
        std::uint32_t dump_len = 0x30U;
        if (const char* d = opt_env("MNEMOS_CPS1_DUMP")) {
            char* rest = nullptr;
            dump_start = static_cast<std::uint32_t>(std::strtoul(d, &rest, 16));
            if (rest != nullptr && *rest == ':') {
                dump_len = static_cast<std::uint32_t>(std::strtoul(rest + 1, nullptr, 16));
            }
        }
        std::fprintf(stderr, "[trace] mem @ %06X..%06X:", dump_start, dump_start + dump_len);
        for (std::uint32_t a = dump_start; a < dump_start + dump_len; ++a) {
            if (((a - dump_start) & 0xFU) == 0U) {
                std::fprintf(stderr, "\n  %06X:", a);
            }
            std::fprintf(stderr, " %02X", machine.main_bus.read8(a));
        }
        std::fprintf(stderr, "\n[trace] A-regs:");
        for (int i = 0; i < 8; ++i) {
            std::fprintf(stderr, " a%d=%06X", i, r.a[static_cast<std::size_t>(i)]);
        }
        std::fprintf(stderr, "\n[trace] D-regs:");
        for (int i = 0; i < 8; ++i) {
            std::fprintf(stderr, " d%d=%08X", i, r.d[static_cast<std::size_t>(i)]);
        }
        std::fprintf(stderr, "\n[trace] eeprom[0..16]:");
        {
            const auto eep = machine.eeprom.bytes();
            for (std::size_t i = 0; i < 16U && i < eep.size(); ++i) {
                std::fprintf(stderr, " %02X", eep[i]);
            }
        }
        std::fprintf(stderr, "\n[trace] cps_a_regs[0..18]:");
        for (std::size_t i = 0; i < 18U && i < machine.cps_a_regs.size(); ++i) {
            std::fprintf(stderr, " %04X", machine.cps_a_regs[i]);
        }
        std::fprintf(stderr, "\n[trace] profile id=%u layer_ctrl=%02X palette_ctrl=%02X\n",
                     static_cast<unsigned>(machine.profile.id),
                     static_cast<unsigned>(machine.profile.layer_control_offset),
                     static_cast<unsigned>(machine.profile.palette_control_offset));
        std::fprintf(stderr, "[trace] palette_ctrl_reg=%04X palette[0..16]:",
                     machine.video.cps_b_reg(
                         static_cast<std::uint8_t>(machine.profile.palette_control_offset >> 1U)));
        for (std::size_t i = 0; i < 32U && i < machine.palette.size(); ++i) {
            std::fprintf(stderr, " %02X", machine.palette[i]);
        }
        std::fprintf(stderr, "\n");
    }

    CHECK(frame_lit);
    CHECK(machine.vblank_irq_raised > 0U); // the beam raised the level-2 IRQ
    CHECK(machine.vblank_irq_acked > 0U);  // the 68K serviced it (IACK ran)

    if (const char* ppm = opt_env("MNEMOS_CPS1_PPM")) {
        CHECK(write_ppm(adapter.current_frame(), ppm));
    }
}

TEST_CASE("capcom_cps1_adapter advances frames and drains YM2151-clocked audio",
          "[capcom_cps1][adapter][audio]") {
    capcom_cps1_adapter adapter(make_program());
    adapter.step_one_frame();

    const auto chunk = adapter.drain_audio();
    // One stereo frame per 64 YM2151 clocks; a frame is ~3.58 MHz worth of Z80/
    // YM2151 time, so the first drain is a non-trivial run of frames.
    CHECK(chunk.frame_count > 0U);
    CHECK(chunk.sample_rate == 55930U);
    REQUIRE(chunk.samples != nullptr);

    // Nothing stepped since the last drain: nothing further is due.
    CHECK(adapter.drain_audio().frame_count == 0U);
}
