// Integration parity for build_sms_runtime (the manifest-path wrapper the
// player adapter consumes) against assemble_sms (the hand-written oracle).
//
// The lower-level manifest parity tests already compare a bare build_system
// against assemble_sms. This test exercises the whole wrapper as one call --
// mapper auto-selection, cartridge attach, the post-BIOS SP fixup, and the
// default MK-3020 pads -- and asserts the resulting machine renders identically
// to assemble_sms over N frames, for both mappers.
//
//   - Synthetic carts (always run): Sega and Codemasters.
//   - Real BIOS (data-gated on MNEMOS_SMS_BIOS): full A/B.

#include "sms_runtime.hpp"
#include "sms_system.hpp"

#include "scheduler.hpp"
#include "sms_vdp.hpp"
#include "z80.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {

    using mnemos::manifests::sms::assemble_sms;
    using mnemos::manifests::sms::build_sms_runtime;
    using mnemos::manifests::sms::sms_config;

    // 32 KiB blank cart (no Codemasters header) -> resolves to the Sega mapper.
    std::vector<std::uint8_t> sega_rom() { return std::vector<std::uint8_t>(0x8000U, 0x00U); }

    // 32 KiB cart with a valid Codemasters checksum header -> Codemasters mapper.
    std::vector<std::uint8_t> codies_rom() {
        std::vector<std::uint8_t> rom(0x8000U, 0U);
        for (std::size_t i = 0x4000U; i < 0x8000U; ++i) {
            rom[i] = 1U;
        }
        rom[0x7FE6U] = 0x34U;
        rom[0x7FE7U] = 0x12U;
        rom[0x7FE8U] = 0xCCU;
        rom[0x7FE9U] = 0xEDU;
        return rom;
    }

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

    [[nodiscard]] frame_pixels run_assemble(const std::vector<std::uint8_t>& rom,
                                            const sms_config& config, int frames) {
        auto sys = assemble_sms(rom, config);
        std::vector<mnemos::runtime::scheduled_chip> chips = {
            {&sys->vdp, 1U}, {&sys->cpu, 1U}, {&sys->psg, 1U}};
        mnemos::runtime::scheduler sched(std::move(chips), &sys->vdp);
        for (int i = 0; i < frames; ++i) {
            sched.run_frame();
        }
        return snapshot(sys->vdp.framebuffer());
    }

    [[nodiscard]] frame_pixels run_runtime(const std::vector<std::uint8_t>& rom,
                                           const sms_config& config, int frames) {
        auto rt = build_sms_runtime(rom, config);
        std::vector<mnemos::runtime::scheduled_chip> chips = {
            {rt->vdp(), 1U}, {rt->cpu(), 1U}, {rt->psg(), 1U}};
        mnemos::runtime::scheduler sched(std::move(chips), rt->vdp());
        for (int i = 0; i < frames; ++i) {
            sched.run_frame();
        }
        return snapshot(rt->vdp()->framebuffer());
    }

    void require_parity(const frame_pixels& a, const frame_pixels& b) {
        REQUIRE(a.width == b.width);
        REQUIRE(a.height == b.height);
        REQUIRE(a.pixels.size() == b.pixels.size());
        std::size_t diff = 0;
        for (std::size_t i = 0; i < a.pixels.size(); ++i) {
            if (a.pixels[i] != b.pixels[i]) {
                ++diff;
            }
        }
        INFO("pixels differing: " << diff << " / " << a.pixels.size());
        CHECK(diff == 0);
    }

} // namespace

TEST_CASE("build_sms_runtime matches assemble_sms (Sega mapper)", "[sms][runtime][parity]") {
    require_parity(run_assemble(sega_rom(), {}, 5), run_runtime(sega_rom(), {}, 5));
}

TEST_CASE("build_sms_runtime matches assemble_sms (Codemasters mapper)", "[sms][runtime][parity]") {
    const sms_config codies{.cartridge_mapper = sms_config::mapper::codemasters};
    require_parity(run_assemble(codies_rom(), codies, 5), run_runtime(codies_rom(), codies, 5));
}

TEST_CASE("build_sms_runtime default-plugs both controller ports", "[sms][runtime]") {
    auto rt = build_sms_runtime(sega_rom(), {});
    REQUIRE(rt->port_device(0) != nullptr);
    REQUIRE(rt->port_device(1) != nullptr);
    CHECK(rt->port_device(0)->read_data() == 0xFFU); // idle pad, active-low
    CHECK(rt->port_device(2) == nullptr);            // out of range
}

TEST_CASE("build_sms_runtime matches assemble_sms on a real BIOS", "[sms][runtime][bios][parity]") {
    auto rom = read_bios_or_skip();
    if (!rom) {
        SKIP("MNEMOS_SMS_BIOS env var unset; data-gated parity check skipped");
    }
    require_parity(run_assemble(*rom, {}, 5), run_runtime(*rom, {}, 5));
}
