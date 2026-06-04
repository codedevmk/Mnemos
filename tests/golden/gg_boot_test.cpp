// Golden-boot test for the Sega Game Gear.
//
// Boots a real Game Gear cartridge from the Z80 reset vector with the SMS core
// in Game Gear mode (12-bit CRAM + the central 160x144 LCD viewport + the PSG
// $06 stereo register), renders a fixed number of frames, and hashes the cropped
// LCD framebuffer. The hash is deterministic for a given cartridge, so once
// recorded it pins the GG boot path (mapper banking, GG VDP raster + viewport
// crop, frame-IRQ timing).
//
// Cartridges are copyrighted and never committed, so this test is DATA-GATED: it
// SKIPs cleanly unless MNEMOS_GG_ROM points at a .gg image.
//
//   MNEMOS_GG_ROM           path to the .gg cartridge image
//   MNEMOS_GG_BOOT_FRAMES   (optional) frames to render before hashing (default 200)
//   MNEMOS_GG_BOOT_SHA256   (optional) the golden framebuffer hash to assert
//
// When a ROM is present but no golden hash is set, the test still verifies the
// boot is deterministic and produces visible output, and prints the computed hash
// so it can be locked in.

#include "scheduler.hpp"
#include "sha256.hpp"
#include "sms_system.hpp"

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
    using mnemos::manifests::sms::assemble_sms;
    using mnemos::manifests::sms::sms_config;
    using mnemos::manifests::sms::sms_system;

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

    std::string sha_hex(const std::vector<std::uint8_t>& bytes) {
        return mnemos::security::cryptography::sha256(bytes).hex();
    }

    // Pack the visible LCD region row-by-row (the GG view is strided: 160 visible
    // pixels over the VDP's 256-pixel pitch) into a contiguous byte buffer so the
    // hash and uniformity check see exactly what the LCD shows.
    std::vector<std::uint8_t> pack_visible(const mnemos::chips::frame_buffer_view& fb) {
        std::vector<std::uint8_t> bytes;
        if (fb.pixels == nullptr) {
            return bytes;
        }
        const std::uint32_t stride = fb.effective_stride();
        bytes.reserve(static_cast<std::size_t>(fb.width) * fb.height * 4U);
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                const std::uint32_t px = row[x];
                bytes.push_back(static_cast<std::uint8_t>(px >> 16U));
                bytes.push_back(static_cast<std::uint8_t>(px >> 8U));
                bytes.push_back(static_cast<std::uint8_t>(px));
            }
        }
        return bytes;
    }

    bool all_same(const std::vector<std::uint8_t>& packed) {
        for (std::size_t i = 3; i < packed.size(); ++i) {
            if (packed[i] != packed[i % 3]) {
                return false;
            }
        }
        return true;
    }

    // Assemble a fresh Game Gear and render `frames` frames. GG is 60 Hz NTSC only.
    std::unique_ptr<sms_system> boot(const std::vector<std::uint8_t>& cart, std::uint64_t frames) {
        auto sys = assemble_sms(cart, {.game_gear = true});
        std::vector<mnemos::runtime::scheduled_chip> chips = {
            {&sys->vdp, 1U}, {&sys->cpu, 1U}, {&sys->psg, 1U}};
        mnemos::runtime::scheduler sched(std::move(chips), &sys->vdp);
        sched.run_frames(frames);
        return sys;
    }
} // namespace

TEST_CASE("game gear boots to a deterministic golden framebuffer", "[golden][gg]") {
    const auto rom_path = get_env("MNEMOS_GG_ROM");
    if (!rom_path) {
        SKIP("set MNEMOS_GG_ROM to a .gg cartridge image (copyrighted, never committed)");
    }

    auto cart = read_file(fs::path(*rom_path));
    if (!cart || cart->empty()) {
        SKIP("MNEMOS_GG_ROM=" << *rom_path << " could not be read as a cartridge image");
    }

    std::uint64_t frames = 200U;
    if (const auto override_frames = get_env("MNEMOS_GG_BOOT_FRAMES")) {
        frames = std::strtoull(override_frames->c_str(), nullptr, 10);
        if (frames == 0U) {
            frames = 200U;
        }
    }

    INFO("cartridge sha256: " << sha_hex(*cart));
    INFO("frames rendered: " << frames);

    auto sys = boot(*cart, frames);
    const auto fb = sys->vdp.framebuffer();

    // Game Gear mode crops the frame to the 160x144 LCD viewport.
    CHECK(fb.width == 160U);
    CHECK(fb.height == 144U);

    const auto packed = pack_visible(fb);
    REQUIRE_FALSE(packed.empty());
    const std::string hash = sha_hex(packed);

    // A real cartridge renders graphics, not a uniform blank LCD.
    CHECK_FALSE(all_same(packed));

    // ...and it must be deterministic: a second cold boot hashes identically.
    auto sys2 = boot(*cart, frames);
    CHECK(sha_hex(pack_visible(sys2->vdp.framebuffer())) == hash);

    INFO("boot framebuffer sha256: " << hash);

    const auto golden = get_env("MNEMOS_GG_BOOT_SHA256");
    if (golden) {
        CHECK(hash == *golden);
    } else {
        WARN("no golden hash set; computed boot framebuffer sha256 = "
             << hash << " (set MNEMOS_GG_BOOT_SHA256 to lock it)");
    }
}
