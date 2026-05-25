// Golden-boot test for the Sega Master System.
//
// Boots a real SMS cartridge from the Z80 reset vector, renders a fixed number of
// frames, and hashes the resulting framebuffer. The hash is deterministic for a
// given cartridge + region, so once recorded it pins the whole boot path (mapper
// banking, VDP Mode-4 raster, frame-IRQ timing, PSG-free rendering).
//
// Cartridges are copyrighted and never committed, so this test is DATA-GATED: it
// SKIPs cleanly unless MNEMOS_SMS_ROM points at a .sms image.
//
//   MNEMOS_SMS_ROM           path to the .sms cartridge image
//   MNEMOS_SMS_REGION        (optional) "ntsc" (default) or "pal"
//   MNEMOS_SMS_BOOT_FRAMES   (optional) frames to render before hashing (default 200)
//   MNEMOS_SMS_BOOT_SHA256   (optional) the golden framebuffer hash to assert
//
// When a ROM is present but no golden hash is set, the test still verifies the boot
// is deterministic and produces visible output, and prints the computed hash so it
// can be locked in. The same hash is produced by:
//
//   mnemos_runtime_cli --manifest <sms.ntsc.toml> --cart <rom> --frames N --dump-hash

#include "cli.hpp"
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
        return mnemos::foundation::sha256(bytes).hex();
    }

    // Assemble a fresh SMS and render `frames` frames, mirroring the headless CLI's
    // assemble + scheduler order so the hash matches `mnemos_runtime_cli --dump-hash`.
    std::unique_ptr<sms_system> boot(const std::vector<std::uint8_t>& cart,
                                     sms_config::region region, std::uint64_t frames) {
        auto sys = assemble_sms(cart, {.video_region = region});
        // The VDP drives frame boundaries; all SMS chips run at the Z80 clock.
        std::vector<mnemos::runtime::scheduled_chip> chips = {
            {&sys->vdp, 1U}, {&sys->cpu, 1U}, {&sys->psg, 1U}};
        mnemos::runtime::scheduler sched(std::move(chips), &sys->vdp);
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

TEST_CASE("sms boots to a deterministic golden framebuffer", "[golden][sms]") {
    const auto rom_path = get_env("MNEMOS_SMS_ROM");
    if (!rom_path) {
        SKIP("set MNEMOS_SMS_ROM to a .sms cartridge image (copyrighted, never committed)");
    }

    auto cart = read_file(fs::path(*rom_path));
    if (!cart || cart->empty()) {
        SKIP("MNEMOS_SMS_ROM=" << *rom_path << " could not be read as a cartridge image");
    }

    auto region = sms_config::region::ntsc;
    if (const auto region_env = get_env("MNEMOS_SMS_REGION");
        region_env && (*region_env == "pal" || *region_env == "PAL")) {
        region = sms_config::region::pal;
    }

    std::uint64_t frames = 200U;
    if (const auto override_frames = get_env("MNEMOS_SMS_BOOT_FRAMES")) {
        frames = std::strtoull(override_frames->c_str(), nullptr, 10);
        if (frames == 0U) {
            frames = 200U;
        }
    }

    // Record which cartridge the golden is tied to.
    INFO("cartridge sha256: " << sha_hex(*cart));
    INFO("region: " << (region == sms_config::region::pal ? "pal" : "ntsc"));
    INFO("frames rendered: " << frames);

    auto sys = boot(*cart, region, frames);
    const auto fb = sys->vdp.framebuffer();
    const std::string hash = mnemos::tools::hash_framebuffer(fb);

    // A real cartridge renders graphics, not a uniform blank raster.
    CHECK_FALSE(framebuffer_is_uniform(fb));

    // ...and it must be deterministic: a second cold boot hashes identically.
    auto sys2 = boot(*cart, region, frames);
    CHECK(mnemos::tools::hash_framebuffer(sys2->vdp.framebuffer()) == hash);

    INFO("boot framebuffer sha256: " << hash);

    const auto golden = get_env("MNEMOS_SMS_BOOT_SHA256");
    if (golden) {
        CHECK(hash == *golden);
    } else {
        WARN("no golden hash set; computed boot framebuffer sha256 = "
             << hash << " (set MNEMOS_SMS_BOOT_SHA256 to lock it)");
    }
}
