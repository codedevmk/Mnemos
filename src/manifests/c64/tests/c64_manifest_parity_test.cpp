// Parity test for the C64 manifest path: drive the SAME ROMs through both the
// hand-written assemble_c64 path (the oracle) and the manifest-built
// build_c64_runtime path for N frames, then compare the VIC framebuffers
// byte-for-byte.
//
//   - Synthetic (always runs): zero-filled ROM images. The 6510 boots from a
//     null vector and runs identically on both paths -- a wiring smoke test.
//   - Real ROMs (data-gated on MNEMOS_C64_ROM_DIR): full boot A/B.

#include "c64_runtime.hpp"
#include "c64_system.hpp"

#include "scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    using mnemos::manifests::c64::assemble_c64;
    using mnemos::manifests::c64::build_c64_runtime;
    using mnemos::manifests::c64::c64_config;
    using reset_kind = mnemos::chips::reset_kind;

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

    [[nodiscard]] frame_pixels run_assemble(const std::vector<std::uint8_t>& basic,
                                            const std::vector<std::uint8_t>& kernal,
                                            const std::vector<std::uint8_t>& chargen, int frames) {
        auto sys = assemble_c64(basic, kernal, chargen);
        sys->cpu.reset(reset_kind::power_on);
        sys->cia1.reset(reset_kind::power_on);
        sys->cia2.reset(reset_kind::power_on);
        sys->sid.reset(reset_kind::power_on);
        sys->sid2.reset(reset_kind::power_on);
        sys->vic.reset(reset_kind::power_on);
        sys->drive8.reset(reset_kind::power_on);
        sys->tape.reset(reset_kind::power_on);
        std::vector<mnemos::runtime::scheduled_chip> chips = {
            {&sys->vic, 1U}, {&sys->cpu, 1U},    {&sys->cia1, 1U}, {&sys->cia2, 1U},
            {&sys->sid, 1U}, {&sys->drive8, 1U}, {&sys->tape, 1U}};
        mnemos::runtime::scheduler sched(std::move(chips), &sys->vic);
        for (int i = 0; i < frames; ++i) {
            sched.run_frame();
        }
        return snapshot(sys->vic.framebuffer());
    }

    [[nodiscard]] frame_pixels run_runtime(std::vector<std::uint8_t> basic,
                                           std::vector<std::uint8_t> kernal,
                                           std::vector<std::uint8_t> chargen, int frames) {
        auto rt = build_c64_runtime(std::move(basic), std::move(kernal), std::move(chargen));
        std::vector<mnemos::runtime::scheduled_chip> chips;
        for (const auto& e : rt->schedule()) {
            chips.push_back({e.chip, e.weight});
        }
        mnemos::runtime::scheduler sched(std::move(chips), rt->video());
        for (int i = 0; i < frames; ++i) {
            sched.run_frame();
        }
        return snapshot(rt->video()->framebuffer());
    }

    void require_parity(const frame_pixels& a, const frame_pixels& b) {
        REQUIRE(a.width == b.width);
        REQUIRE(a.height == b.height);
        REQUIRE(a.pixels.size() == b.pixels.size());
        std::size_t diff = 0;
        std::uint32_t first = 0;
        bool found = false;
        for (std::size_t i = 0; i < a.pixels.size(); ++i) {
            if (a.pixels[i] != b.pixels[i]) {
                if (!found) {
                    first = static_cast<std::uint32_t>(i);
                    found = true;
                }
                ++diff;
            }
        }
        INFO("pixels differing: " << diff << " / " << a.pixels.size() << "  first diff at index "
                                  << first);
        CHECK(diff == 0);
    }

    std::string to_lower(std::string str) {
        for (char& c : str) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return str;
    }

    std::optional<std::vector<std::uint8_t>> load_rom(const fs::path& dir, const char* role,
                                                      std::size_t expected_size) {
        const auto read = [](const fs::path& p) -> std::optional<std::vector<std::uint8_t>> {
            std::ifstream in(p, std::ios::binary);
            if (!in) {
                return std::nullopt;
            }
            return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                             std::istreambuf_iterator<char>{});
        };
        std::error_code ec;
        const std::string role_lower = to_lower(role);
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            if (to_lower(entry.path().filename().string()).find(role_lower) == std::string::npos) {
                continue;
            }
            auto bytes = read(entry.path());
            if (bytes && bytes->size() == expected_size) {
                return bytes;
            }
        }
        return std::nullopt;
    }

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    std::optional<std::string> rom_dir() {
        const char* env = std::getenv("MNEMOS_C64_ROM_DIR");
        return (env != nullptr && env[0] != '\0') ? std::optional<std::string>(env) : std::nullopt;
    }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace

TEST_CASE("build_c64_runtime matches assemble_c64 (synthetic zero ROMs)",
          "[c64][manifest][parity]") {
    const std::vector<std::uint8_t> basic(0x2000U, 0x00U);
    const std::vector<std::uint8_t> kernal(0x2000U, 0x00U);
    const std::vector<std::uint8_t> chargen(0x1000U, 0x00U);
    require_parity(run_assemble(basic, kernal, chargen, 3), run_runtime(basic, kernal, chargen, 3));
}

TEST_CASE("build_c64_runtime matches assemble_c64 on real ROMs", "[c64][manifest][rom][parity]") {
    const auto dir = rom_dir();
    if (!dir) {
        SKIP("MNEMOS_C64_ROM_DIR unset; data-gated parity check skipped");
    }
    auto basic = load_rom(fs::path(*dir), "basic", 0x2000U);
    auto kernal = load_rom(fs::path(*dir), "kernal", 0x2000U);
    auto chargen = load_rom(fs::path(*dir), "char", 0x1000U);
    if (!basic || !kernal || !chargen) {
        SKIP("MNEMOS_C64_ROM_DIR is missing a correctly-sized BASIC/KERNAL/CHARGEN image");
    }
    constexpr int frames = 30;
    require_parity(run_assemble(*basic, *kernal, *chargen, frames),
                   run_runtime(*basic, *kernal, *chargen, frames));
}
