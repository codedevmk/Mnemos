// Parity test for the Codemasters-mapper SMS manifest
// (sms.ntsc.codemasters.toml): drive the SAME cartridge through both the
// hand-written `assemble_sms` path (forced to the Codemasters mapper) and the
// manifest-built `build_system` path, then compare results.
//
// Two layers of verification:
//   1. Synthetic cart (always runs): a 32 KiB image carrying a valid
//      Codemasters checksum header. Asserts (a) byte-for-byte VDP framebuffer
//      parity over N frames and (b) Codemasters-specific banking parity --
//      ROM-space page-register writes ($0000/$4000/$8000) resolve identically
//      on both buses. A real Codemasters game can't live in the repo, so this
//      synthetic cart proves the WIRING is equivalent.
//   2. Real game (data-gated on MNEMOS_SMS_CODIES_ROM): framebuffer A/B over N
//      frames against an actual Codemasters cartridge. SKIPs cleanly when the
//      env var is unset or the file is missing.

#include "builder.hpp"
#include "manifest.hpp"
#include "sms_callbacks.hpp"
#include "sms_manifests.hpp"
#include "sms_system.hpp"

#include "codemasters_mapper.hpp"
#include "scheduler.hpp"
#include "sms_vdp.hpp"
#include "sn76489.hpp"
#include "z80.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

    // 32 KiB cart with a valid Codemasters checksum header (word at $7FE6 plus
    // the complement at $7FE8 sum to $10000) so detection picks Codemasters.
    // Page 0 ($0000-$3FFF) reads 0, page 1 ($4000-$7FFF) reads 1 -- enough to
    // observe banking through the page registers.
    std::vector<std::uint8_t> codies_rom() {
        std::vector<std::uint8_t> rom(0x8000U, 0U);
        for (std::size_t i = 0x4000U; i < 0x8000U; ++i) {
            rom[i] = 1U;
        }
        rom[0x7FE6U] = 0x34U; // checksum $1234
        rom[0x7FE7U] = 0x12U;
        rom[0x7FE8U] = 0xCCU; // complement $EDCC -> $1234 + $EDCC == $10000
        rom[0x7FE9U] = 0xEDU;
        return rom;
    }

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in test data path
#endif
    std::optional<std::vector<std::uint8_t>> read_codies_rom_or_skip() {
        const char* env = std::getenv("MNEMOS_SMS_CODIES_ROM");
        if (env == nullptr || env[0] == '\0') {
            return std::nullopt;
        }
        std::ifstream in(env, std::ios::binary);
        if (!in) {
            std::fprintf(stderr,
                         "[sms-codies-parity] MNEMOS_SMS_CODIES_ROM set to '%s' "
                         "but file not readable; skipping\n",
                         env);
            return std::nullopt;
        }
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                         std::istreambuf_iterator<char>{});
    }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    struct frame_pixels final {
        std::uint32_t width{};
        std::uint32_t height{};
        std::vector<std::uint32_t> pixels;
    };

    [[nodiscard]] frame_pixels snapshot(const mnemos::chips::frame_buffer_view& fb) {
        frame_pixels out;
        out.width = fb.width;
        out.height = fb.height;
        const std::uint32_t stride = fb.effective_stride();
        out.pixels.reserve(static_cast<std::size_t>(fb.width) * fb.height);
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                out.pixels.push_back(row[x]);
            }
        }
        return out;
    }

    [[nodiscard]] frame_pixels run_assemble_codies_path(std::vector<std::uint8_t> rom, int frames) {
        using mnemos::manifests::sms::assemble_sms;
        using mnemos::manifests::sms::sms_config;
        auto sys =
            assemble_sms(std::move(rom), {.cartridge_mapper = sms_config::mapper::codemasters});
        REQUIRE(sys->codemasters_active);
        std::vector<mnemos::runtime::scheduled_chip> chips = {
            {&sys->vdp, 1U}, {&sys->cpu, 1U}, {&sys->psg, 1U}};
        mnemos::runtime::scheduler sched(std::move(chips), &sys->vdp);
        for (int i = 0; i < frames; ++i) {
            sched.run_frame();
        }
        return snapshot(sys->vdp.framebuffer());
    }

    // Build the Codemasters manifest system, wire chip pointers, attach the
    // ROM, and mirror assemble_sms's post-BIOS SP fixup. The caller-owned
    // `state` must outlive the returned graph (its callbacks capture &state).
    [[nodiscard]] mnemos::manifests::build_result
    build_codies_manifest(const std::vector<std::uint8_t>& rom,
                          mnemos::manifests::sms::sms_callbacks_state& state,
                          mnemos::manifests::sms::sms_host_tables& tables) {
        using namespace mnemos::manifests;
        const auto parsed = parse_manifest(sms::manifest_toml(mnemos::video_region::ntsc, true));
        REQUIRE(parsed.ok());

        const auto no_roms = [](std::string_view) -> std::optional<std::vector<std::uint8_t>> {
            return std::nullopt;
        };
        auto built =
            build_system(*parsed.value, no_roms, tables.callbacks, {}, tables.mmio_factories);
        REQUIRE(built.ok());

        state.cpu = dynamic_cast<mnemos::chips::cpu::z80*>(built.value->chip("cpu"));
        state.vdp = dynamic_cast<mnemos::chips::video::sms_vdp*>(built.value->chip("video"));
        state.psg = dynamic_cast<mnemos::chips::audio::sn76489*>(built.value->chip("audio"));
        auto* codies =
            dynamic_cast<mnemos::chips::mapper::codemasters_mapper*>(built.value->chip("mapper"));
        REQUIRE(state.cpu != nullptr);
        REQUIRE(state.vdp != nullptr);
        REQUIRE(state.psg != nullptr);
        REQUIRE(codies != nullptr);

        codies->attach_rom(std::span<const std::uint8_t>(rom));

        auto regs = state.cpu->cpu_registers();
        regs.sp = 0xDFF0U;
        state.cpu->set_registers(regs);
        return built;
    }

    [[nodiscard]] frame_pixels run_manifest_codies_path(const std::vector<std::uint8_t>& rom,
                                                        int frames) {
        mnemos::manifests::sms::sms_callbacks_state state;
        auto tables = mnemos::manifests::sms::make_sms_host_tables(state);
        auto built = build_codies_manifest(rom, state, tables);

        std::vector<mnemos::runtime::scheduled_chip> chips = {
            {state.vdp, 1U}, {state.cpu, 1U}, {state.psg, 1U}};
        mnemos::runtime::scheduler sched(std::move(chips), state.vdp);
        for (int i = 0; i < frames; ++i) {
            sched.run_frame();
        }
        return snapshot(state.vdp->framebuffer());
    }

    void require_frame_parity(const frame_pixels& a, const frame_pixels& b) {
        REQUIRE(a.width == b.width);
        REQUIRE(a.height == b.height);
        REQUIRE(a.pixels.size() == b.pixels.size());
        std::size_t diff = 0;
        std::uint32_t first_diff_index = 0;
        bool found_first = false;
        for (std::size_t i = 0; i < a.pixels.size(); ++i) {
            if (a.pixels[i] != b.pixels[i]) {
                if (!found_first) {
                    first_diff_index = static_cast<std::uint32_t>(i);
                    found_first = true;
                }
                ++diff;
            }
        }
        INFO("pixels differing: " << diff << " / " << a.pixels.size() << "  first diff at index "
                                  << first_diff_index);
        CHECK(diff == 0);
    }

} // namespace

TEST_CASE("SMS Codemasters manifest matches assemble_sms framebuffer (synthetic cart)",
          "[sms][manifest][codemasters][parity]") {
    constexpr int frames = 5;
    const auto rom = codies_rom();
    const auto a = run_assemble_codies_path(rom, frames);
    const auto b = run_manifest_codies_path(rom, frames);
    require_frame_parity(a, b);
}

TEST_CASE("SMS Codemasters manifest banks ROM slots identically to assemble_sms",
          "[sms][manifest][codemasters][mapper]") {
    using mnemos::manifests::sms::assemble_sms;
    using mnemos::manifests::sms::sms_config;

    const auto rom = codies_rom();

    // Hand-written path (forced Codemasters).
    auto sys = assemble_sms(rom, {.cartridge_mapper = sms_config::mapper::codemasters});
    REQUIRE(sys->codemasters_active);

    // Manifest path.
    mnemos::manifests::sms::sms_callbacks_state state;
    auto tables = mnemos::manifests::sms::make_sms_host_tables(state);
    auto built = build_codies_manifest(rom, state, tables);
    auto* bus = built.value->bus("main");
    REQUIRE(bus != nullptr);

    // Power-on banking: slot 0 = page 0 (reads 0), slot 1 = page 1 (reads 1),
    // slot 2 = page 0 (reads 0). Both paths must agree.
    CHECK(sys->bus.read8(0x0000U) == bus->read8(0x0000U));
    CHECK(sys->bus.read8(0x4000U) == bus->read8(0x4000U));
    CHECK(sys->bus.read8(0x8000U) == bus->read8(0x8000U));
    CHECK(bus->read8(0x0000U) == 0U);
    CHECK(bus->read8(0x4000U) == 1U);

    // Page slot 2 -> page 1 via a ROM-space write at $8000 (Codemasters has no
    // $FFFC register file; the write routes through the cartridge overlay).
    sys->bus.write8(0x8000U, 1U);
    bus->write8(0x8000U, 1U);
    CHECK(sys->bus.read8(0x8000U) == bus->read8(0x8000U));
    CHECK(bus->read8(0x8000U) == 1U);

    // Page slot 0 -> page 1 via a write at $0000 (no fixed first 1 KiB).
    sys->bus.write8(0x0000U, 1U);
    bus->write8(0x0000U, 1U);
    CHECK(sys->bus.read8(0x0000U) == bus->read8(0x0000U));
    CHECK(bus->read8(0x0000U) == 1U);
}

TEST_CASE("SMS Codemasters manifest matches assemble_sms on a real cart",
          "[sms][manifest][codemasters][bios][parity]") {
    auto rom = read_codies_rom_or_skip();
    if (!rom) {
        SKIP("MNEMOS_SMS_CODIES_ROM env var unset; data-gated parity check skipped");
    }
    constexpr int frames = 5;
    const auto a = run_assemble_codies_path(*rom, frames);
    const auto b = run_manifest_codies_path(*rom, frames);
    require_frame_parity(a, b);
}
