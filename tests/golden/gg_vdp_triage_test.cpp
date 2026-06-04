// VDP triage tool (data-gated, diagnostic): boots a GG cartridge and dumps the
// VDP register + VRAM state so we can tell a "graphics never loaded" boot stall
// from a "wrong name-table address" VDP bug. Gated on MNEMOS_GG_ROM; prints to
// stderr. Not a correctness assertion -- a probe.

#include "scheduler.hpp"
#include "sms_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    using mnemos::manifests::sms::assemble_sms;

    std::optional<std::string> get_env(const char* name) {
#ifdef _WIN32
        char* buf = nullptr;
        std::size_t len = 0;
        if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) {
            return std::nullopt;
        }
        std::string v(buf);
        std::free(buf);
        return v;
#else
        const char* v = std::getenv(name);
        return v ? std::optional<std::string>(v) : std::nullopt;
#endif
    }

    std::optional<std::vector<std::uint8_t>> read_file(const fs::path& p) {
        std::ifstream in(p, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
    }

    // Count name-table entries (32x28, 2 bytes each) whose tile word is nonzero,
    // at a candidate base address -- a coherent screen has many; a wrong base is
    // sparse or full of pattern garbage.
    int nonzero_nt_entries(std::span<const std::uint8_t> vram, std::uint16_t base) {
        int n = 0;
        for (int e = 0; e < 32 * 28; ++e) {
            const std::size_t a =
                (static_cast<std::size_t>(base) + static_cast<std::size_t>(e) * 2U) & 0x3FFFU;
            const std::uint16_t w =
                static_cast<std::uint16_t>(vram[a] | (vram[(a + 1U) & 0x3FFFU] << 8U));
            if (w != 0U) {
                ++n;
            }
        }
        return n;
    }
} // namespace

TEST_CASE("gg vdp triage", "[golden][gg][triage]") {
    const auto rom_path = get_env("MNEMOS_GG_ROM");
    if (!rom_path) {
        SKIP("set MNEMOS_GG_ROM to a .gg image for VDP triage");
    }
    auto cart = read_file(fs::path(*rom_path));
    if (!cart || cart->empty()) {
        SKIP("MNEMOS_GG_ROM could not be read");
    }
    std::uint64_t frames = 3000U;
    if (const auto f = get_env("MNEMOS_GG_BOOT_FRAMES")) {
        frames = std::strtoull(f->c_str(), nullptr, 10);
    }

    auto sys = assemble_sms(*cart, {.game_gear = true});
    std::vector<mnemos::runtime::scheduled_chip> chips = {
        {&sys->vdp, 1U}, {&sys->cpu, 1U}, {&sys->psg, 1U}};
    mnemos::runtime::scheduler sched(std::move(chips), &sys->vdp);
    sched.run_frames(frames);

    auto& v = sys->vdp;
    std::fprintf(stderr, "\n=== GG VDP triage after %llu frames ===\n",
                 static_cast<unsigned long long>(frames));
    std::fprintf(stderr, "regs: r0=%02X r1=%02X r2=%02X r5=%02X r6=%02X r8=%02X r9=%02X r10=%02X\n",
                 v.reg(0), v.reg(1), v.reg(2), v.reg(5), v.reg(6), v.reg(8), v.reg(9), v.reg(10));
    std::fprintf(stderr, "display_enable(r1.6)=%d  visible_h=%d  is_gg=%d\n",
                 (v.reg(1) & 0x40U) != 0U, v.visible_height(), v.is_gg());

    const auto nt_base = static_cast<std::uint16_t>((v.reg(2) & 0x0EU) << 10U);
    const auto sat_base = static_cast<std::uint16_t>((v.reg(5) & 0x7EU) << 7U);
    const auto spr_base = static_cast<std::uint16_t>((v.reg(6) & 0x04U) << 11U);
    std::fprintf(stderr, "derived: nt_base=$%04X  sat_base=$%04X  spr_pattern=$%04X\n", nt_base,
                 sat_base, spr_base);

    const auto vram = v.vram();
    int total_nz = 0;
    std::array<int, 8> bucket{};
    for (std::size_t i = 0; i < vram.size(); ++i) {
        if (vram[i] != 0U) {
            ++total_nz;
            ++bucket[i / 0x800U];
        }
    }
    std::fprintf(stderr, "VRAM nonzero: %d/%zu bytes;  per-$800 bucket:", total_nz, vram.size());
    for (int b : bucket) {
        std::fprintf(stderr, " %d", b);
    }
    std::fprintf(stderr, "\n");

    std::fprintf(stderr, "nonzero NT entries (of 896) at candidate bases:\n");
    for (std::uint16_t base : {std::uint16_t(0x0000), std::uint16_t(0x2000), std::uint16_t(0x2800),
                               std::uint16_t(0x3000), std::uint16_t(0x3800), nt_base}) {
        std::fprintf(stderr, "   $%04X: %d\n", base, nonzero_nt_entries(vram, base));
    }
    std::fflush(stderr);

    CHECK(vram.size() == 0x4000U); // always passes; this is a probe
}
