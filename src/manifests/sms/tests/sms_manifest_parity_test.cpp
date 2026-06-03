// Parity test: drive a real SMS BIOS through BOTH the hand-written
// `assemble_sms` path and the manifest-built `build_system` path for N
// frames each, then compare the resulting VDP framebuffers byte-for-byte.
//
// Catches any quiet behavioural divergence between the two paths the
// synthetic NOP smoke test in sms_manifest_path_test.cpp couldn't hit --
// real BIOS code exercises every IO port, VDP register, mapper bank
// write, and IRQ delivery the SMS architecture has.
//
// Data-gated by MNEMOS_SMS_BIOS: an absolute path to an SMS BIOS image.
// The test SKIPS cleanly if the env var is unset or the file is missing.

#include "builder.hpp"
#include "manifest.hpp"
#include "sms_callbacks.hpp"
#include "sms_manifests.hpp"
#include "sms_system.hpp"

#include "scheduler.hpp"
#include "sms_mapper.hpp"
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

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in test data path
#endif
    std::optional<std::vector<std::uint8_t>> read_bios_or_skip() {
        const char* env = std::getenv("MNEMOS_SMS_BIOS");
        if (env == nullptr || env[0] == '\0') {
            return std::nullopt;
        }
        std::ifstream in(env, std::ios::binary);
        if (!in) {
            std::fprintf(stderr,
                         "[sms-parity] MNEMOS_SMS_BIOS set to '%s' but file not "
                         "readable; skipping\n",
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

    [[nodiscard]] frame_pixels run_assemble_sms_path(std::vector<std::uint8_t> rom, int frames) {
        auto sys = mnemos::manifests::sms::assemble_sms(std::move(rom));
        std::vector<mnemos::runtime::scheduled_chip> chips = {
            {&sys->vdp, 1U}, {&sys->cpu, 1U}, {&sys->psg, 1U}};
        mnemos::runtime::scheduler sched(std::move(chips), &sys->vdp);
        for (int i = 0; i < frames; ++i) {
            sched.run_frame();
        }
        return snapshot(sys->vdp.framebuffer());
    }

    [[nodiscard]] frame_pixels run_manifest_path(const std::vector<std::uint8_t>& rom, int frames) {
        using namespace mnemos::manifests;

        const auto parsed = parse_manifest(
            sms::manifest_toml(mnemos::video_region::ntsc, sms::sms_config::mapper::sega));
        REQUIRE(parsed.ok());

        sms::sms_callbacks_state state;
        auto tables = sms::make_sms_host_tables(state);
        const auto no_roms = [](std::string_view) -> std::optional<std::vector<std::uint8_t>> {
            return std::nullopt;
        };
        auto built =
            build_system(*parsed.value, no_roms, tables.callbacks, {}, tables.mmio_factories);
        REQUIRE(built.ok());

        state.cpu = dynamic_cast<mnemos::chips::cpu::z80*>(built.value->chip("cpu"));
        state.vdp = dynamic_cast<mnemos::chips::video::sms_vdp*>(built.value->chip("video"));
        state.psg = dynamic_cast<mnemos::chips::audio::sn76489*>(built.value->chip("audio"));
        state.mapper =
            dynamic_cast<mnemos::chips::mapper::sms_mapper*>(built.value->chip("mapper"));
        REQUIRE(state.cpu != nullptr);
        REQUIRE(state.vdp != nullptr);
        REQUIRE(state.psg != nullptr);
        REQUIRE(state.mapper != nullptr);

        state.mapper->attach_rom(std::span<const std::uint8_t>(rom));

        // Mirror assemble_sms's post-BIOS SP fixup so the two paths start
        // in identical CPU state regardless of whether the ROM is a BIOS
        // or a cart. (The hand-written assembler unconditionally sets
        // SP=$DFF0; we match it here.)
        auto regs = state.cpu->cpu_registers();
        regs.sp = 0xDFF0U;
        state.cpu->set_registers(regs);

        std::vector<mnemos::runtime::scheduled_chip> chips = {
            {state.vdp, 1U}, {state.cpu, 1U}, {state.psg, 1U}};
        mnemos::runtime::scheduler sched(std::move(chips), state.vdp);
        for (int i = 0; i < frames; ++i) {
            sched.run_frame();
        }
        return snapshot(state.vdp->framebuffer());
    }

} // namespace

TEST_CASE("SMS manifest path matches assemble_sms framebuffer on a real BIOS",
          "[sms][manifest][bios][parity]") {
    auto rom = read_bios_or_skip();
    if (!rom) {
        SKIP("MNEMOS_SMS_BIOS env var unset; data-gated parity check skipped");
    }

    constexpr int frames = 5;

    const auto a = run_assemble_sms_path(*rom, frames);
    const auto b = run_manifest_path(*rom, frames);

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
