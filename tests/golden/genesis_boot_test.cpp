// Golden-boot test for the Sega Genesis / Mega Drive.
//
// Boots a real Genesis cartridge from the 68000 reset vectors, renders a fixed
// number of frames, and hashes the resulting framebuffer. The hash is deterministic
// for a given cartridge + region, so once recorded it pins the whole boot path (68000
// execution, the VDP command/DMA/raster pipeline, and the dual-CPU schedule).
//
// Cartridges are copyrighted and never committed, so this test is DATA-GATED: it
// SKIPs cleanly unless MNEMOS_GENESIS_ROM points at a .md/.bin/.gen image.
//
//   MNEMOS_GENESIS_ROM           path to the cartridge image
//   MNEMOS_GENESIS_REGION        (optional) "ntsc" (default) or "pal"
//   MNEMOS_GENESIS_BOOT_FRAMES   (optional) frames to render before hashing (default 120)
//   MNEMOS_GENESIS_BOOT_SHA256   (optional) the golden framebuffer hash to assert

#include "cli.hpp"
#include "genesis_system.hpp"
#include "scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {
    namespace fs = std::filesystem;
    using mnemos::manifests::genesis::assemble_genesis;
    using mnemos::manifests::genesis::genesis_config;
    using mnemos::manifests::genesis::genesis_system;

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

    // Assemble a fresh Genesis and render `frames` frames on the master-clock schedule
    // (VDP /1, 68000 /7, Z80 /15, YM2612 /7, PSG /15); the VDP drives frame boundaries.
    std::unique_ptr<genesis_system> boot(std::vector<std::uint8_t> cart,
                                         genesis_config::region region, std::uint64_t frames) {
        auto sys = assemble_genesis(std::move(cart), {.video_region = region});
        std::vector<mnemos::runtime::scheduled_chip> chips = {{&sys->vdp, 1U},
                                                              {&sys->cpu, 7U},
                                                              {&sys->z80_gate, 15U},
                                                              {&sys->fm, 7U},
                                                              {&sys->psg, 15U}};
        mnemos::runtime::scheduler sched(std::move(chips), &sys->vdp);
        sched.run_frames(frames);
        return sys;
    }

    bool framebuffer_is_uniform(const mnemos::chips::frame_buffer_view& fb) {
        if (fb.pixels == nullptr || fb.width == 0U || fb.height == 0U) {
            return true;
        }
        const std::uint32_t stride = fb.effective_stride();
        const std::uint32_t first = fb.pixels[0];
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                if (row[x] != first) {
                    return false;
                }
            }
        }
        return true;
    }
} // namespace

TEST_CASE("genesis boots to a deterministic golden framebuffer", "[golden][genesis]") {
    const auto rom_path = get_env("MNEMOS_GENESIS_ROM");
    if (!rom_path) {
        SKIP("set MNEMOS_GENESIS_ROM to a Genesis cartridge image (copyrighted, never committed)");
    }

    auto cart = read_file(fs::path(*rom_path));
    if (!cart || cart->empty()) {
        SKIP("MNEMOS_GENESIS_ROM=" << *rom_path << " could not be read as a cartridge image");
    }

    auto region = genesis_config::region::ntsc;
    if (const auto region_env = get_env("MNEMOS_GENESIS_REGION");
        region_env && (*region_env == "pal" || *region_env == "PAL")) {
        region = genesis_config::region::pal;
    }

    std::uint64_t frames = 120U;
    if (const auto override_frames = get_env("MNEMOS_GENESIS_BOOT_FRAMES")) {
        frames = std::strtoull(override_frames->c_str(), nullptr, 10);
        if (frames == 0U) {
            frames = 120U;
        }
    }

    INFO("region: " << (region == genesis_config::region::pal ? "pal" : "ntsc"));
    INFO("frames rendered: " << frames);

    auto sys = boot(*cart, region, frames);
    const auto fb = sys->vdp.framebuffer();
    const std::string hash = mnemos::tools::hash_framebuffer(fb);

    // A real cartridge renders graphics, not a uniform blank raster.
    CHECK_FALSE(framebuffer_is_uniform(fb));

    // ...and the boot is deterministic: a second cold boot hashes identically.
    auto sys2 = boot(*cart, region, frames);
    CHECK(mnemos::tools::hash_framebuffer(sys2->vdp.framebuffer()) == hash);

    INFO("boot framebuffer sha256: " << hash);

    const auto golden = get_env("MNEMOS_GENESIS_BOOT_SHA256");
    if (golden) {
        CHECK(hash == *golden);
    } else {
        WARN("no golden hash set; computed boot framebuffer sha256 = "
             << hash << " (set MNEMOS_GENESIS_BOOT_SHA256 to lock it)");
    }
}
