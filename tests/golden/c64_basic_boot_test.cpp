// Golden-boot test for the Commodore 64.
//
// Boots a real C64 (KERNAL + BASIC + CHARGEN ROMs) from the reset vector,
// renders a fixed number of frames, and hashes the resulting framebuffer. The
// hash is deterministic for a given set of ROMs, so once recorded it pins the
// whole boot path (PLA banking, KERNAL init, VIC raster, IRQ-driven cursor).
//
// The ROMs are copyrighted and never committed, so this test is DATA-GATED: it
// SKIPs cleanly unless MNEMOS_C64_ROM_DIR points at a directory holding them.
//
//   MNEMOS_C64_ROM_DIR   directory with the BASIC / KERNAL / CHARGEN images
//   MNEMOS_C64_BOOT_SHA256   (optional) the golden framebuffer hash to assert
//   MNEMOS_C64_BOOT_FRAMES   (optional) frames to render before hashing (default 200)
//
// When ROMs are present but no golden hash is set, the test still verifies the
// boot is deterministic and produces visible output, and prints the computed
// hash so it can be locked in. The same hash is produced by:
//
//   mnemos_runtime_cli --manifest <c64.pal.toml> --rom-dir <dir> --frames N --dump-hash

#include "c64_system.hpp"
#include "cli.hpp"
#include "scheduler.hpp"
#include "sha256.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    using mnemos::manifests::c64::assemble_c64;
    using mnemos::manifests::c64::c64_system;
    using reset_kind = mnemos::chips::reset_kind;

    // Portable getenv that does not trip MSVC's deprecation of std::getenv.
    std::optional<std::string> get_env(const char* name) {
#ifdef _WIN32
        char* buf = nullptr;
        std::size_t len = 0;
        if (_dupenv_s(&buf, &len, name) != 0 || buf == nullptr) {
            return std::nullopt;
        }
        std::string value(buf);
        std::free(buf);
        return value;
#else
        const char* value = std::getenv(name);
        if (value == nullptr) {
            return std::nullopt;
        }
        return std::string(value);
#endif
    }

    std::optional<std::vector<std::uint8_t>> read_file(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
    }

    // Find the first candidate filename in `dir` that exists and is exactly
    // `expected_size` bytes.
    std::optional<std::vector<std::uint8_t>> load_rom(const fs::path& dir,
                                                      std::initializer_list<const char*> names,
                                                      std::size_t expected_size) {
        for (const char* name : names) {
            const fs::path path = dir / name;
            std::error_code ec;
            if (!fs::exists(path, ec)) {
                continue;
            }
            auto bytes = read_file(path);
            if (bytes && bytes->size() == expected_size) {
                return bytes;
            }
        }
        return std::nullopt;
    }

    std::string sha_hex(const std::vector<std::uint8_t>& bytes) {
        return mnemos::foundation::sha256(bytes).hex();
    }

    // Boot a fresh C64 and render `frames` frames, mirroring the headless CLI's
    // reset + scheduler order so the hash matches `mnemos_runtime_cli --dump-hash`.
    std::unique_ptr<c64_system> boot(const std::vector<std::uint8_t>& basic,
                                     const std::vector<std::uint8_t>& kernal,
                                     const std::vector<std::uint8_t>& chargen,
                                     std::uint64_t frames) {
        auto sys = assemble_c64(basic, kernal, chargen);
        sys->cpu.reset(reset_kind::power_on);
        sys->cia1.reset(reset_kind::power_on);
        sys->cia2.reset(reset_kind::power_on);
        sys->sid.reset(reset_kind::power_on);
        sys->sid2.reset(reset_kind::power_on);
        sys->vic.reset(reset_kind::power_on);
        sys->drive8.reset(reset_kind::power_on);
        sys->tape.reset(reset_kind::power_on);

        // VIC first so the CPU reads the freshly advanced beam; all at phi2.
        std::vector<mnemos::runtime::scheduled_chip> chips = {
            {&sys->vic, 1U}, {&sys->cpu, 1U},    {&sys->cia1, 1U}, {&sys->cia2, 1U},
            {&sys->sid, 1U}, {&sys->drive8, 1U}, {&sys->tape, 1U}};
        mnemos::runtime::scheduler sched(std::move(chips), &sys->vic);
        sched.run_frames(frames);
        return sys;
    }

    bool framebuffer_is_uniform(const mnemos::chips::frame_buffer_view& fb) {
        const std::size_t count = static_cast<std::size_t>(fb.width) * fb.height;
        if (fb.pixels == nullptr || count == 0U) {
            return true;
        }
        for (std::size_t i = 1; i < count; ++i) {
            if (fb.pixels[i] != fb.pixels[0]) {
                return false;
            }
        }
        return true;
    }
} // namespace

TEST_CASE("c64 boots to a deterministic golden framebuffer", "[golden][c64]") {
    const auto rom_dir = get_env("MNEMOS_C64_ROM_DIR");
    if (!rom_dir) {
        SKIP("set MNEMOS_C64_ROM_DIR to a directory holding the C64 BASIC/KERNAL/CHARGEN ROMs "
             "(copyrighted, never committed -- see src/manifests/c64/ROMS.md)");
    }

    const fs::path dir(*rom_dir);
    auto basic = load_rom(dir, {"basic.901226-01.bin", "basic.bin", "basic"}, 0x2000U);
    auto kernal = load_rom(dir, {"kernal.901227-03.bin", "kernal.bin", "kernal"}, 0x2000U);
    auto chargen = load_rom(dir,
                            {"character.901225-01.bin", "characters.901225-01.bin",
                             "chargen.901225-01.bin", "chargen.bin", "characters.bin", "char.bin"},
                            0x1000U);
    if (!basic || !kernal || !chargen) {
        SKIP("MNEMOS_C64_ROM_DIR=" << dir.string()
                                   << " is missing a correctly-sized BASIC (8K) / KERNAL (8K) / "
                                      "CHARGEN (4K) image");
    }

    // Record which ROM revisions the golden is tied to.
    INFO("basic   sha256: " << sha_hex(*basic));
    INFO("kernal  sha256: " << sha_hex(*kernal));
    INFO("chargen sha256: " << sha_hex(*chargen));

    std::uint64_t frames = 200U;
    if (const auto override_frames = get_env("MNEMOS_C64_BOOT_FRAMES")) {
        frames = std::strtoull(override_frames->c_str(), nullptr, 10);
        if (frames == 0U) {
            frames = 200U;
        }
    }

    auto sys = boot(*basic, *kernal, *chargen, frames);
    const auto fb = sys->vic.framebuffer();
    const std::string hash = mnemos::tools::hash_framebuffer(fb);

    // The boot must produce visible output, not a uniform blank raster.
    CHECK_FALSE(framebuffer_is_uniform(fb));

    // ...and it must be deterministic: a second cold boot hashes identically.
    auto sys2 = boot(*basic, *kernal, *chargen, frames);
    CHECK(mnemos::tools::hash_framebuffer(sys2->vic.framebuffer()) == hash);

    INFO("frames rendered: " << frames);
    INFO("boot framebuffer sha256: " << hash);

    const auto golden = get_env("MNEMOS_C64_BOOT_SHA256");
    if (golden) {
        CHECK(hash == *golden);
    } else {
        WARN("no golden hash set; computed boot framebuffer sha256 = "
             << hash << " (set MNEMOS_C64_BOOT_SHA256 to lock it)");
    }
}
