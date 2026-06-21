// SDL3 windowed player. Boots the player_system adapter named by --system
// (genesis / sms / gg / c64 / segacd / sega32x / irem_m72 / cps1 / cps2) with the --rom media (zip
// archives are extracted transparently), presents its framebuffer at integer
// scale, streams audio, and routes keyboard + gamepad input. ESC quits.

#define SDL_MAIN_HANDLED

#include "adapter_registry.hpp"
#include "animation_export.hpp"     // --record-gif / --record-movie
#include "asset_export.hpp"         // --extract-assets: decoded graphics -> PNG + JSON
#include "audio_export.hpp"         // --extract-audio: decoded PCM samples -> WAV + JSON
#include "battery_save.hpp"         // .srm load/save (cartridge battery RAM persistence)
#include "c64_adapter.hpp"          // force_link (the C64 has no cart-header region byte)
#include "capability_discovery.hpp" // --capabilities: headless capability/control summary
#include "capcom_cps1_adapter.hpp"  // force_link (arcade: no cart region byte)
#include "capcom_cps2_adapter.hpp"  // force_link (arcade: encrypted ROM-set board)
#include "chip.hpp"
#include "cli_args.hpp"
#include "debug_dump.hpp"
#include "genesis_adapter.hpp" // force_link + manifests::genesis::parse_market
#include "genesis_region.hpp"
#include "irem_m72_adapter.hpp" // force_link (arcade: no cart region byte)
#include "nes_adapter.hpp"      // force_link (Nintendo NES / NROM)
#include "player_system.hpp"
#include "region.hpp"
#include "region_args.hpp"
#include "rom_loader.hpp"
#include "sega32x_adapter.hpp" // force_link (Sega 32X: cart + the three boot ROMs)
#include "segacd_adapter.hpp"  // force_link (Sega CD: BIOS boot ROM + disc image)
#include "sms_adapter.hpp"     // force_link + manifests::sms::parse_market
#include "sms_region.hpp"
#include "spectrum_adapter.hpp" // force_link (ZX Spectrum 48K)
#include "system_family.hpp"
#include "text_overlay.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

    constexpr int kInitialWindowWidth = 1280;
    constexpr int kInitialWindowHeight = 960;

    // `--swap-disk <frame>` (repeatable): flip to the next removable-media slot at
    // that frame, so headless runs can drive an FDS multi-side game onto side B
    // (the interactive equivalent is F6). Frames are 1-based, matching --press.
    [[nodiscard]] std::vector<std::uint64_t> parse_swap_frames(int argc, char* argv[]) {
        std::vector<std::uint64_t> frames;
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::string(argv[i]) == "--swap-disk") {
                frames.push_back(std::strtoull(argv[i + 1], nullptr, 10));
            }
        }
        return frames;
    }

    void apply_disk_swaps(mnemos::frontend_sdk::player_system& sys,
                          const std::vector<std::uint64_t>& frames, std::uint64_t frame) {
        if (sys.media_count() <= 1U) {
            return;
        }
        for (const std::uint64_t f : frames) {
            if (f == frame) {
                sys.insert_media((sys.current_media_index() + 1U) % sys.media_count());
            }
        }
    }

    // Streaming texture is sized for the worst-case frame across supported
    // systems: Genesis VDP 320x240 V30 (x2 rows for interlace) and the Irem
    // M72's 384x256 (256x384 once a vertical game is rotated for TATE
    // presentation); per-frame uploads only touch the active subregion the
    // adapter reports.
    constexpr std::uint32_t kMaxFbWidth = 384U;
    constexpr std::uint32_t kMaxFbHeight = 480U;

    // Status overlay: two lines of 8x8 monospaced text, anchored to the
    // top-right of the swapchain. Sized to fit the longest line we expect
    // to draw (~80 chars including a generous cart title); excess width
    // is just unused buffer.
    constexpr int kOverlayMaxCols = 80;
    constexpr int kOverlayBufW = kOverlayMaxCols * mnemos::apps::player::kGlyphWidth;
    constexpr int kOverlayLines = 2;
    constexpr int kOverlayPad = 4;
    constexpr int kOverlayBufH =
        kOverlayLines * mnemos::apps::player::kGlyphHeight + (kOverlayLines + 1) * kOverlayPad;
    constexpr std::uint32_t kOverlayBgColor = 0xFF000000U; // opaque black panel
    constexpr std::uint32_t kOverlayFgColor = 0xFFFFFFFFU; // white text
    constexpr int kOverlayScreenMargin = 8;

    struct dst_rect {
        int x{}, y{}, w{}, h{};
    };

    // Owns a cartridge's .srm battery save for the player's lifetime: loads the
    // saved bytes into the adapter's live SRAM on construction, and writes them
    // back on destruction -- but only when they actually changed since boot, so
    // carts without battery RAM (empty span) and sessions that never wrote a save
    // leave the filesystem untouched. Held at main scope so every exit path
    // (normal quit, --screenshot return, error returns) persists through it.
    class battery_save_guard {
      public:
        battery_save_guard(mnemos::frontend_sdk::player_system* sys, std::string path)
            : sys_(sys), path_(std::move(path)) {
            const auto sram = ram();
            if (sram.empty()) {
                return; // no battery RAM on this cart -- nothing to persist
            }
            if (mnemos::apps::player::adapters::load_battery_ram(path_, sram)) {
                std::fprintf(stderr, "[mnemos_player] loaded battery save: %s\n", path_.c_str());
                std::fflush(stderr);
            }
            loaded_.assign(sram.begin(), sram.end()); // snapshot to detect changes
        }
        battery_save_guard(const battery_save_guard&) = delete;
        battery_save_guard& operator=(const battery_save_guard&) = delete;
        ~battery_save_guard() {
            const auto sram = ram();
            if (sram.empty()) {
                return;
            }
            const bool changed = sram.size() != loaded_.size() ||
                                 !std::equal(sram.begin(), sram.end(), loaded_.begin());
            if (changed && mnemos::apps::player::adapters::save_battery_ram(path_, sram)) {
                std::fprintf(stderr, "[mnemos_player] wrote battery save: %s (%zu bytes)\n",
                             path_.c_str(), sram.size());
                std::fflush(stderr);
            }
        }

      private:
        [[nodiscard]] std::span<std::uint8_t> ram() const noexcept {
            return (sys_ != nullptr && !path_.empty()) ? sys_->battery_ram()
                                                       : std::span<std::uint8_t>{};
        }
        mnemos::frontend_sdk::player_system* sys_;
        std::string path_;
        std::vector<std::uint8_t> loaded_;
    };

    dst_rect integer_letterbox(int window_w, int window_h, int src_w, int src_h) {
        if (src_w <= 0 || src_h <= 0 || window_w <= 0 || window_h <= 0) {
            return {0, 0, window_w, window_h};
        }
        const int scale_w = window_w / src_w;
        const int scale_h = window_h / src_h;
        int scale = std::min(scale_w, scale_h);
        if (scale < 1) {
            scale = 1;
        }
        const int dst_w = src_w * scale;
        const int dst_h = src_h * scale;
        return {
            .x = (window_w - dst_w) / 2,
            .y = (window_h - dst_h) / 2,
            .w = dst_w,
            .h = dst_h,
        };
    }

    // OR an SDL gamepad's current buttons + left stick into a controller_state (so a
    // pad and the keyboard can both drive a port).
    void merge_gamepad(mnemos::frontend_sdk::controller_state& pad, SDL_Gamepad* gp) {
        if (gp == nullptr) {
            return;
        }
        constexpr Sint16 kAxisThreshold = 16384; // ~half deflection
        const auto lx = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX);
        const auto ly = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY);
        pad.up |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -kAxisThreshold;
        pad.down |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > kAxisThreshold;
        pad.left |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -kAxisThreshold;
        pad.right |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > kAxisThreshold;
        pad.a |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_SOUTH);
        pad.b |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_EAST);
        pad.c |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_WEST);
        pad.x |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_NORTH);
        pad.y |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        pad.z |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        pad.start |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_START);
        pad.mode |= SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_BACK);
    }

} // namespace

int main(int argc, char* argv[]) {
    using mnemos::apps::player::adapters::clean_rom_name;
    using mnemos::apps::player::adapters::family_from_name;
    using mnemos::apps::player::adapters::family_id;
    using mnemos::apps::player::adapters::family_label;
    using mnemos::apps::player::adapters::family_names;
    using mnemos::apps::player::adapters::input_for_frame;
    using mnemos::apps::player::adapters::load_rom;
    using mnemos::apps::player::adapters::load_rom_verbatim;
    using mnemos::apps::player::adapters::parse_animation_record_args;
    using mnemos::apps::player::adapters::parse_capabilities_arg;
    using mnemos::apps::player::adapters::parse_extract_assets_args;
    using mnemos::apps::player::adapters::parse_extract_audio_args;
    using mnemos::apps::player::adapters::parse_fm_unit_arg;
    using mnemos::apps::player::adapters::parse_four_score_arg;
    using mnemos::apps::player::adapters::parse_light_gun_arg;
    using mnemos::apps::player::adapters::parse_mapper_arg;
    using mnemos::apps::player::adapters::parse_no_autostart;
    using mnemos::apps::player::adapters::parse_press_events;
    using mnemos::apps::player::adapters::parse_region_arg;
    using mnemos::apps::player::adapters::parse_rom_args;
    using mnemos::apps::player::adapters::parse_screenshot_args;
    using mnemos::apps::player::adapters::parse_system_arg;
    using mnemos::apps::player::adapters::region_source_label;
    using mnemos::apps::player::adapters::resolve_video_region;
    using mnemos::apps::player::adapters::srm_path_for;
    using mnemos::apps::player::adapters::system_family;

    const auto rom_paths = parse_rom_args(argc, argv);
    const auto system_arg = parse_system_arg(argc, argv);
    const bool autostart = !parse_no_autostart(argc, argv);
    const auto region_arg = parse_region_arg(argc, argv);
    const auto mapper_arg = parse_mapper_arg(argc, argv);
    const bool fm_unit = parse_fm_unit_arg(argc, argv);
    const bool light_gun = parse_light_gun_arg(argc, argv);
    const bool four_score = parse_four_score_arg(argc, argv);
    const auto dip_arg = mnemos::apps::player::adapters::parse_dip_arg(argc, argv);
    const auto screenshot = parse_screenshot_args(argc, argv);
    const auto extract = parse_extract_assets_args(argc, argv);
    const auto extract_audio = parse_extract_audio_args(argc, argv);
    const bool capabilities = parse_capabilities_arg(argc, argv);
    const auto record_animation = parse_animation_record_args(argc, argv);

    const auto resolve_video = [region_arg](mnemos::video_region cart_default) {
        return resolve_video_region(region_arg, cart_default);
    };
    const char* region_source = region_source_label(region_arg);

    std::unique_ptr<mnemos::frontend_sdk::player_system> system;
    // Persists the cartridge battery RAM across runs (interactive playback only --
    // gated on !screenshot at the emplace below). Declared after `system` so it
    // destructs first, while the adapter (and its SRAM span) is still alive.
    std::optional<battery_save_guard> srm_guard;
    if (!rom_paths.empty()) {
        // The engine is named explicitly: `--system <name> --rom <file>`. ROM
        // filenames carry no weight -- a zipped image routes by the name alone.
        if (!system_arg) {
            std::fprintf(stderr,
                         "[mnemos_player] --system <name> is required with --rom "
                         "(one of: %s)\n",
                         family_names());
            return 1;
        }
        const auto family_opt = family_from_name(*system_arg);
        if (!family_opt) {
            std::fprintf(stderr, "[mnemos_player] unknown system '%s' (one of: %s)\n",
                         system_arg->c_str(), family_names());
            return 1;
        }
        const system_family family = *family_opt;
        // Arcade sets ARE their archive: the adapter resolves the dump
        // entries through the game declaration inside, so no unwrapping.
        const bool arcade_family = family == system_family::irem_m72 ||
                                   family == system_family::capcom_cps1 ||
                                   family == system_family::capcom_cps2;
        auto loaded =
            arcade_family ? load_rom_verbatim(rom_paths.front()) : load_rom(rom_paths.front());
        if (!loaded || loaded->bytes.empty()) {
            std::fprintf(stderr, "could not read ROM: %s\n", rom_paths.front().c_str());
            return 1;
        }
        // Any further media paths are additional images -- the rest of a
        // multi-disk set the C64 adapter can swap between at runtime.
        std::vector<std::vector<std::uint8_t>> additional_media;
        for (std::size_t i = 1; i < rom_paths.size(); ++i) {
            auto extra = load_rom(rom_paths[i]);
            if (!extra || extra->bytes.empty()) {
                std::fprintf(stderr, "could not read media: %s\n", rom_paths[i].c_str());
                return 1;
            }
            additional_media.push_back(std::move(extra->bytes));
        }
        // Default video standard before any --region override: the cartridge
        // consoles carry a region byte in their header; the C64 does not, so it
        // defaults to PAL (its core market, and the manifest/c64_config default).
        mnemos::video_region cart_default = mnemos::video_region::ntsc;
        switch (family) {
        case system_family::sms:
            cart_default =
                mnemos::default_video_for(mnemos::manifests::sms::parse_market(loaded->bytes));
            break;
        case system_family::gg:
            cart_default = mnemos::video_region::ntsc; // Game Gear is 60 Hz only
            break;
        case system_family::genesis:
            cart_default =
                mnemos::default_video_for(mnemos::manifests::genesis::parse_market(loaded->bytes));
            break;
        case system_family::c64:
            cart_default = mnemos::video_region::pal;
            break;
        case system_family::spectrum:
            cart_default = mnemos::video_region::pal; // 50 Hz machine
            break;
        case system_family::nes:
            // iNES carries no region byte, so default to NTSC; PAL (50 Hz, 312-line
            // 2A07 timing) is selectable with --region pal.
            cart_default = mnemos::video_region::ntsc;
            break;
        case system_family::sega32x:
            // 32X carts carry a Genesis-style header with the region byte.
            cart_default =
                mnemos::default_video_for(mnemos::manifests::genesis::parse_market(loaded->bytes));
            break;
        case system_family::segacd:
            // Region comes from the disc/BIOS, not a cart header byte; keep the
            // NTSC default set above (also silences -Wswitch on this enum).
            break;
        case system_family::irem_m72:
        case system_family::capcom_cps1:
        case system_family::capcom_cps2:
            // Arcade boards have no region byte; the adapter reports the
            // board's own raster through region().
            break;
        }
        const auto video = resolve_video(cart_default);
        std::fprintf(stderr, "[mnemos_player] system: %s  region: %s (%s)\n", family_label(family),
                     video == mnemos::video_region::pal ? "PAL" : "NTSC", region_source);
        std::fflush(stderr);

        // Ensure each adapter's static-init self-registration with the
        // adapter_registry actually links in. Without these calls a static-
        // library adapter that the binary doesn't otherwise reference can be
        // dropped, silently disabling its registration. Adding a new system
        // means adding one more force_link() call here.
        mnemos::apps::player::adapters::genesis::force_link();
        mnemos::apps::player::adapters::sms::force_link();
        mnemos::apps::player::adapters::c64::force_link();
        mnemos::apps::player::adapters::segacd::force_link();
        mnemos::apps::player::adapters::sega32x::force_link();
        mnemos::apps::player::adapters::irem_m72::force_link();
        mnemos::apps::player::adapters::capcom_cps1::force_link();
        mnemos::apps::player::adapters::capcom_cps2::force_link();
        mnemos::apps::player::adapters::spectrum::force_link();
        mnemos::apps::player::adapters::nes::force_link();

        // Sega CD boots its BIOS as the program ROM; the file the user loaded is
        // the CD image (passed by path so disc_image can resolve .cue tracks).
        // Every other family runs the loaded file directly.
        std::vector<std::uint8_t> primary_rom = std::move(loaded->bytes);
        std::string disc_path;
        if (family == system_family::segacd) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in BIOS path, not hot-path
#endif
            const char* bios_env = std::getenv("MNEMOS_SEGACD_BIOS");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            if (bios_env == nullptr) {
                std::fprintf(
                    stderr, "[mnemos_player] Sega CD needs MNEMOS_SEGACD_BIOS set to a BIOS ROM\n");
                return 1;
            }
            auto bios = load_rom(bios_env);
            if (!bios || bios->bytes.empty()) {
                std::fprintf(stderr, "[mnemos_player] could not read Sega CD BIOS: %s\n", bios_env);
                return 1;
            }
            disc_path = rom_paths.front();
            primary_rom = std::move(bios->bytes);
        }
        // The 32X boots through three adapter ROMs (master SH-2 / slave SH-2 /
        // 68000 vector overlay) found in MNEMOS_32X_BIOS_DIR by their canonical
        // names. Without them the cart's own vectors run on the bare Genesis
        // side -- the SH-2 handshake then never starts, so warn loudly.
        std::vector<std::vector<std::uint8_t>> bios_images;
        if (family == system_family::sega32x) {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in BIOS path, not hot-path
#endif
            const char* bios_dir = std::getenv("MNEMOS_32X_BIOS_DIR");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
            if (bios_dir == nullptr) {
                std::fprintf(stderr, "[mnemos_player] MNEMOS_32X_BIOS_DIR unset; booting without "
                                     "the 32X boot ROMs (most carts will not start)\n");
            } else {
                const std::string dir{bios_dir};
                for (const char* name : {"32X_M_BIOS.bin", "32X_S_BIOS.bin", "32X_G_BIOS.bin"}) {
                    auto image = load_rom(dir + "/" + name);
                    if (!image || image->bytes.empty()) {
                        std::fprintf(stderr, "[mnemos_player] could not read 32X boot ROM: %s/%s\n",
                                     dir.c_str(), name);
                        return 1;
                    }
                    bios_images.push_back(std::move(image->bytes));
                }
            }
        }
        // A ZX Spectrum snapshot (.z80/.sna) resumes a game on top of the 48K
        // system ROM (from MNEMOS_SPECTRUM_ROM), routed in as additional_media; a
        // 16 KiB .rom is itself the system ROM and boots directly.
        if (family == system_family::spectrum) {
            std::string ext = rom_paths.front().size() >= 4
                                  ? rom_paths.front().substr(rom_paths.front().size() - 4)
                                  : std::string{};
            for (char& ch : ext) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (ext == ".z80" || ext == ".sna") {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in BIOS path, not hot-path
#endif
                const char* bios_env = std::getenv("MNEMOS_SPECTRUM_ROM");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
                if (bios_env == nullptr) {
                    std::fprintf(stderr, "[mnemos_player] a Spectrum snapshot needs "
                                         "MNEMOS_SPECTRUM_ROM set to a 48K system ROM\n");
                    return 1;
                }
                auto bios = load_rom(bios_env);
                if (!bios || bios->bytes.size() < 0x4000U) {
                    std::fprintf(stderr, "[mnemos_player] could not read Spectrum ROM: %s\n",
                                 bios_env);
                    return 1;
                }
                additional_media.insert(additional_media.begin(), std::move(primary_rom));
                primary_rom = std::move(bios->bytes);
            }
        }
        // A Famicom Disk System disk (.fds) boots on the FDS BIOS (DISKSYS.ROM from
        // MNEMOS_FDS_BIOS, raw or zipped); passed as the NES adapter's bios image, it
        // makes the adapter build the RP2C33 RAM adapter. A .nes cart needs no BIOS.
        if (family == system_family::nes) {
            std::string ext = rom_paths.front().size() >= 4
                                  ? rom_paths.front().substr(rom_paths.front().size() - 4)
                                  : std::string{};
            for (char& ch : ext) {
                ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            if (ext == ".fds") {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in BIOS path, not hot-path
#endif
                const char* bios_env = std::getenv("MNEMOS_FDS_BIOS");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
                if (bios_env == nullptr) {
                    std::fprintf(stderr, "[mnemos_player] a Famicom Disk System disk needs "
                                         "MNEMOS_FDS_BIOS set to the 8 KiB DISKSYS.ROM\n");
                    return 1;
                }
                auto bios = load_rom(bios_env);
                if (!bios || bios->bytes.size() < 0x2000U) {
                    std::fprintf(stderr, "[mnemos_player] could not read FDS BIOS: %s\n", bios_env);
                    return 1;
                }
                bios_images.push_back(std::move(bios->bytes));
            }
        }
        system = mnemos::frontend_sdk::adapter_registry::instance().create(
            family_id(family), {.rom = std::move(primary_rom),
                                .video_region = video,
                                .display_name = clean_rom_name(loaded->name),
                                .additional_media = std::move(additional_media),
                                .autostart = autostart,
                                .dip_override = dip_arg,
                                .mapper_override = mapper_arg.value_or(std::string{}),
                                .fm_unit = fm_unit,
                                .light_gun = light_gun,
                                .four_score = four_score,
                                .disc_path = std::move(disc_path),
                                .rom_path = rom_paths.front(),
                                .bios_images = std::move(bios_images)});
        if (system && system->media_count() > 1U) {
            std::fprintf(stderr, "[mnemos_player] media set: %zu disks (F6 swaps)\n",
                         system->media_count());
        }
        if (!system) {
            std::fprintf(stderr, "[mnemos_player] no adapter registered for family '%s'\n",
                         family_id(family));
            return 1;
        }
        // Load any existing .srm before the first frame; the guard writes it back
        // on exit, keyed off the on-disk ROM path (so it sits beside the cart even
        // when the image came from a .zip). Skipped under the headless
        // --screenshot / --extract-assets / --extract-audio / --capabilities /
        // recording paths: those diagnostic sweeps over a read-only ROM corpus
        // must not drop saves beside the ROMs.
        if (!screenshot && !extract && !extract_audio && !capabilities && !record_animation) {
            srm_guard.emplace(system.get(), srm_path_for(rom_paths.front()));
        }
    }

    // Headless capability path: the frontend consumes the same manifest future
    // GUI/tooling controls will use, then exits before SDL startup.
    if (capabilities) {
        if (!system) {
            std::fprintf(stderr, "--capabilities requires --rom\n");
            return 1;
        }
        const auto manifest = mnemos::debug::discover_dump_capabilities(*system);
        const std::string summary = mnemos::debug::format_capability_summary(manifest);
        std::fwrite(summary.data(), 1U, summary.size(), stdout);
        std::fflush(stdout);
        return 0;
    }

    // Headless path: step --frames, dump the framebuffer (PNG when the
    // --screenshot path ends in .png, else PPM) + per-chip sidecars, exit. No
    // window, no GPU. All system-specific knowledge lives behind player_system
    // -- the adapter publishes its chip list and each chip's introspection
    // surface advertises memory views, debug layers, and the CPU trace target.
    if (screenshot) {
        if (!system) {
            std::fprintf(stderr, "--screenshot requires --rom\n");
            return 1;
        }
        // Per-instruction CPU trace (MNEMOS_CPU_TRACE=1): opt-in -- the CSV
        // write dominates headless runtime and bloats sweep outputs, so plain
        // screenshot runs skip it.
        std::uint64_t trace_frame = 0;
        const std::string trace_path = screenshot->path + ".cpu_trace.csv";
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in debug knob
#endif
        const char* trace_env = std::getenv("MNEMOS_CPU_TRACE");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        std::optional<mnemos::debug::trace_csv_session> trace;
        if (trace_env != nullptr && trace_env[0] != '\0' && trace_env[0] != '0') {
            trace.emplace(*system, trace_path, trace_frame);
        }

        // Scripted input (`--press <button>@<frame>[+duration]`) so headless runs
        // can drive a game past intro/menu screens. Sampled before each frame.
        const auto press_events = parse_press_events(argc, argv);
        const auto swap_frames = parse_swap_frames(argc, argv);
        for (std::uint64_t i = 0; i < screenshot->frames; ++i) {
            trace_frame = i + 1U;
            if (!press_events.empty()) {
                system->apply_input(0, input_for_frame(press_events, i + 1U));
            }
            apply_disk_swaps(*system, swap_frames, i + 1U);
            system->step_one_frame();
        }

        if (!mnemos::debug::dump_screenshot_artifacts(*system, screenshot->path)) {
            std::fprintf(stderr, "could not write screenshot: %s\n", screenshot->path.c_str());
            return 1;
        }
        const auto fb = system->current_frame();
        std::fprintf(stderr, "[mnemos_player] wrote %s (%ux%u after %llu frames)\n",
                     screenshot->path.c_str(), fb.width, fb.height,
                     static_cast<unsigned long long>(screenshot->frames));
        if (trace && trace->active()) {
            std::fprintf(stderr, "[mnemos_player] wrote %s\n", trace_path.c_str());
            if (trace->trace_count() > 1U) {
                std::fprintf(stderr, "[mnemos_player] wrote %zu CPU trace files\n",
                             trace->trace_count());
            }
        }
        std::fflush(stderr);
        return 0;
    }

    // Headless animation-record path: step N frames while capturing every
    // framebuffer. GIF is a preview container; movie output is a lossless PNG
    // sequence plus manifest that an external plugin can transcode.
    if (record_animation) {
        if (!system) {
            std::fprintf(stderr, "--record-gif/--record-movie requires --rom\n");
            return 1;
        }
        const auto press_events = parse_press_events(argc, argv);
        std::vector<mnemos::debug::animation_frame> frames;
        frames.reserve(static_cast<std::size_t>(record_animation->frames));
        for (std::uint64_t i = 0; i < record_animation->frames; ++i) {
            if (!press_events.empty()) {
                system->apply_input(0, input_for_frame(press_events, i + 1U));
            }
            system->step_one_frame();
            auto frame = mnemos::debug::capture_animation_frame(*system);
            if (!frame) {
                std::fprintf(stderr, "[mnemos_player] could not capture frame %llu\n",
                             static_cast<unsigned long long>(i + 1U));
                return 1;
            }
            frames.push_back(std::move(*frame));
        }

        const std::uint32_t fps_x1000 = system->region().frames_per_second_x1000;
        if (record_animation->format ==
            mnemos::apps::player::adapters::animation_record_format::gif) {
            if (!mnemos::debug::write_gif_animation(record_animation->output, frames, fps_x1000)) {
                std::fprintf(stderr, "[mnemos_player] could not write animated GIF: %s\n",
                             record_animation->output.c_str());
                return 1;
            }
            std::fprintf(stderr, "[mnemos_player] wrote animated GIF %s (%llu frames)\n",
                         record_animation->output.c_str(),
                         static_cast<unsigned long long>(record_animation->frames));
        } else {
            const auto result = mnemos::debug::write_movie_frame_sequence(record_animation->output,
                                                                          frames, fps_x1000);
            if (result.frames_written != frames.size()) {
                std::fprintf(stderr,
                             "[mnemos_player] incomplete movie frame sequence: %zu/%zu frames\n",
                             result.frames_written, frames.size());
                return 1;
            }
            std::fprintf(stderr,
                         "[mnemos_player] wrote movie frame sequence %s.* (%zu frames, manifest "
                         "%s)\n",
                         record_animation->output.c_str(), result.frames_written,
                         result.manifest_path.c_str());
        }
        std::fflush(stderr);
        return 0;
    }

    // Headless asset-extraction path: step --extract-frames frames, then decode
    // every chip's graphics (palettes, tiles, sprites) to <base>.* PNG + JSON
    // and exit. Like --screenshot it drives the system only through
    // player_system + the chip introspection surface, so it works for any
    // adapter that implements an asset_source.
    if (extract) {
        if (!system) {
            std::fprintf(stderr, "--extract-assets requires --rom\n");
            return 1;
        }
        const auto press_events = parse_press_events(argc, argv);
        for (std::uint64_t i = 0; i < extract->frames; ++i) {
            if (!press_events.empty()) {
                system->apply_input(0, input_for_frame(press_events, i + 1U));
            }
            system->step_one_frame();
        }
        const std::size_t count = mnemos::debug::export_assets(*system, extract->base);
        std::fprintf(
            stderr, "[mnemos_player] extracted %zu asset image(s) to %s.* after %llu frames\n",
            count, extract->base.c_str(), static_cast<unsigned long long>(extract->frames));
        std::fflush(stderr);
        return 0;
    }

    // Headless audio-extraction path: step --extract-frames frames (let sample
    // memory fill), then write every chip's PCM samples to <base>.* WAV + JSON
    // and exit. Same introspection-only drive as --extract-assets, via
    // audio_source.
    if (extract_audio) {
        if (!system) {
            std::fprintf(stderr, "--extract-audio requires --rom\n");
            return 1;
        }
        const auto press_events = parse_press_events(argc, argv);
        const auto swap_frames = parse_swap_frames(argc, argv);
        // Record the rendered output (what the machine actually plays -- the only
        // audio export that works for synth chips like the NES APU); the stepping
        // also leaves the chips in their final state for export_audio's snapshot.
        const std::size_t rendered = mnemos::debug::export_rendered_audio(
            *system, extract_audio->frames, extract_audio->base, [&](std::uint64_t i) {
                if (!press_events.empty()) {
                    system->apply_input(0, input_for_frame(press_events, i + 1U));
                }
                apply_disk_swaps(*system, swap_frames, i + 1U);
            });
        const std::size_t count = mnemos::debug::export_audio(*system, extract_audio->base);
        std::fprintf(stderr,
                     "[mnemos_player] extracted %zu stored sample(s) + %zu rendered frame(s) to "
                     "%s.* after %llu frames\n",
                     count, rendered, extract_audio->base.c_str(),
                     static_cast<unsigned long long>(extract_audio->frames));
        std::fflush(stderr);
        return 0;
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window =
        SDL_CreateWindow("Mnemos Player", kInitialWindowWidth, kInitialWindowHeight,
                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // We never create a shader (only SDL_BlitGPUTexture), but the format
    // flags are required at device creation. On Windows prefer D3D12;
    // MNEMOS_GPU_DRIVER overrides, and a failed named driver falls back to
    // SDL's own choice.
    constexpr SDL_GPUShaderFormat kGpuFormats =
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL;
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996) // std::getenv: opt-in overrides, not hot-path
#endif
    const char* gpu_driver = std::getenv("MNEMOS_GPU_DRIVER");
    // GPU debug/validation is opt-in (MNEMOS_GPU_DEBUG=1): it costs the present
    // path heavily -- with it on, even the debug player drops well below 60 while
    // the emulator still has headroom -- and the pinned SDL3's Vulkan backend
    // trips swapchain-semaphore validation. Default off in every build; enable it
    // only when actually debugging the GPU path.
    const char* gpu_debug_env = std::getenv("MNEMOS_GPU_DEBUG");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    const bool gpu_debug =
        gpu_debug_env != nullptr && gpu_debug_env[0] != '\0' && gpu_debug_env[0] != '0';
#if defined(_WIN32)
    if (gpu_driver == nullptr) {
        gpu_driver = "direct3d12";
    }
#endif
    SDL_GPUDevice* device = SDL_CreateGPUDevice(kGpuFormats, gpu_debug, gpu_driver);
    if (device == nullptr && gpu_driver != nullptr) {
        std::fprintf(stderr, "[mnemos_player] GPU driver '%s' unavailable (%s); falling back\n",
                     gpu_driver, SDL_GetError());
        device = SDL_CreateGPUDevice(kGpuFormats, gpu_debug, nullptr);
    }
    if (device == nullptr) {
        std::fprintf(stderr, "SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::fprintf(stderr, "[mnemos_player] gpu driver: %s\n", SDL_GetGPUDeviceDriver(device));
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        std::fprintf(stderr, "SDL_ClaimWindowForGPUDevice failed: %s\n", SDL_GetError());
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Adapter framebuffers are 0x00RRGGBB little-endian -> BGRA8 byte order.
    SDL_GPUTextureCreateInfo tex_ci{};
    tex_ci.type = SDL_GPU_TEXTURETYPE_2D;
    tex_ci.format = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    tex_ci.width = kMaxFbWidth;
    tex_ci.height = kMaxFbHeight;
    tex_ci.layer_count_or_depth = 1;
    tex_ci.num_levels = 1;
    tex_ci.sample_count = SDL_GPU_SAMPLECOUNT_1;
    tex_ci.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    SDL_GPUTexture* tex = SDL_CreateGPUTexture(device, &tex_ci);
    if (tex == nullptr) {
        std::fprintf(stderr, "SDL_CreateGPUTexture failed: %s\n", SDL_GetError());
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // One persistent transfer buffer, cycled per upload.
    SDL_GPUTransferBufferCreateInfo xfer_ci{};
    xfer_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    xfer_ci.size = kMaxFbWidth * kMaxFbHeight * 4U;
    SDL_GPUTransferBuffer* xfer = SDL_CreateGPUTransferBuffer(device, &xfer_ci);
    if (xfer == nullptr) {
        std::fprintf(stderr, "SDL_CreateGPUTransferBuffer failed: %s\n", SDL_GetError());
        SDL_ReleaseGPUTexture(device, tex);
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Status-overlay GPU resources: a small texture + its own transfer
    // buffer. CPU-side scratch lives in a std::vector so the text-blit
    // helper can write into it without GPU mapping; once the panel's
    // current contents are composed the bytes get uploaded as a single
    // copy each frame.
    SDL_GPUTextureCreateInfo otex_ci = tex_ci;
    otex_ci.width = static_cast<Uint32>(kOverlayBufW);
    otex_ci.height = static_cast<Uint32>(kOverlayBufH);
    SDL_GPUTexture* otex = SDL_CreateGPUTexture(device, &otex_ci);

    SDL_GPUTransferBufferCreateInfo oxfer_ci{};
    oxfer_ci.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    oxfer_ci.size = static_cast<Uint32>(kOverlayBufW * kOverlayBufH) * 4U;
    SDL_GPUTransferBuffer* oxfer = SDL_CreateGPUTransferBuffer(device, &oxfer_ci);

    std::vector<std::uint32_t> overlay_pixels(static_cast<std::size_t>(kOverlayBufW * kOverlayBufH),
                                              0U);

    if (otex == nullptr || oxfer == nullptr) {
        std::fprintf(stderr, "overlay GPU resources failed: %s\n", SDL_GetError());
        if (oxfer != nullptr)
            SDL_ReleaseGPUTransferBuffer(device, oxfer);
        if (otex != nullptr)
            SDL_ReleaseGPUTexture(device, otex);
        SDL_ReleaseGPUTransferBuffer(device, xfer);
        SDL_ReleaseGPUTexture(device, tex);
        SDL_ReleaseWindowFromGPUDevice(device, window);
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool logged_dims = false;
    int dump_index = 0;
    bool dump_requested = false;
    bool paused = false;
    bool fullscreen = false;

    // Software-pace the loop at the game's native frame rate; pacing against
    // vsync would over- or under-clock games whose rate doesn't match the
    // display, drifting both gameplay speed and audio queue depth.
    const double target_fps = system ? system->region().frames_per_second_x1000 / 1000.0 : 60.0;
    const Uint64 perf_freq = SDL_GetPerformanceFrequency();
    const double frame_ticks = static_cast<double>(perf_freq) / target_fps;
    Uint64 next_frame_at = SDL_GetPerformanceCounter();

    // FPS counter: count emulator frames actually stepped over a rolling
    // ~1-second window. Reports steady-state target_fps unless the
    // emulator is under-performing (drops show up here, not in render
    // latency).
    Uint64 fps_window_start = SDL_GetPerformanceCounter();
    int fps_window_frames = 0;
    int displayed_fps = static_cast<int>(target_fps + 0.5);
    // Cumulative emulator frame counter -- shown on the HUD so a
    // screenshot pins down the exact frame being viewed (useful for bug
    // reports and reference A/Bs).
    std::uint64_t total_frames = 0;

    // System-spec line composed once from whatever the adapter publishes
    // ("System: Genesis | Region: NTSC | Cart: ..."). Stays stable for
    // the whole session, so build it here and reuse the string each
    // frame.
    std::string spec_line;
    if (system) {
        const auto& spec = system->system_spec();
        for (std::size_t i = 0; i < spec.size(); ++i) {
            if (i > 0U) {
                spec_line += " | ";
            }
            spec_line += spec[i].label;
            spec_line += ": ";
            spec_line += spec[i].value;
        }
    }
    const int spec_pixel_w = mnemos::apps::player::text_pixel_width(spec_line);
    // The FPS line is "FPS: NNN  F: NNNNNNNN" with both numbers
    // right-justified ("%3d" / "%8llu") so digit-count changes don't shift
    // characters around -- the field width is a constant 21 cells.
    constexpr int kFpsLinePixelW = 21 * mnemos::apps::player::kGlyphWidth;
    const int panel_text_w = std::max(spec_pixel_w, kFpsLinePixelW);
    const int panel_w = std::min(panel_text_w + 2 * kOverlayPad, kOverlayBufW);
    constexpr int panel_h = kOverlayBufH;

    // The adapter reports its native stereo s16 sample rate via drain_audio();
    // SDL_AudioStream resamples to the device rate.
    SDL_AudioStream* audio_stream = nullptr;
    if (system) {
        const auto chunk = system->drain_audio(); // probe for sample rate
        const std::uint32_t rate = chunk.sample_rate != 0U ? chunk.sample_rate : 48000U;
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq = static_cast<int>(rate);
        audio_stream =
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (audio_stream != nullptr) {
            SDL_ResumeAudioStreamDevice(audio_stream);
            std::fprintf(stderr, "[mnemos_player] audio: %u Hz stereo s16\n", rate);
        } else {
            std::fprintf(stderr, "[mnemos_player] SDL_OpenAudioDeviceStream failed: %s\n",
                         SDL_GetError());
        }
    }

    SDL_Gamepad* gamepad = nullptr;
    // Four Score: up to three extra pads on ports 1-3 (gamepads 2-4).
    std::array<SDL_Gamepad*, 3> fs_pads{};
    {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (ids != nullptr && count > 0) {
            gamepad = SDL_OpenGamepad(ids[0]);
            if (gamepad != nullptr) {
                std::fprintf(stderr, "[mnemos_player] gamepad attached: %s\n",
                             SDL_GetGamepadName(gamepad));
            }
            if (four_score) {
                for (int i = 1; i < count && i <= 3; ++i) {
                    fs_pads[static_cast<std::size_t>(i - 1)] = SDL_OpenGamepad(ids[i]);
                }
            }
            SDL_free(ids);
        }
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.key == SDLK_F12) {
                    dump_requested = true;
                } else if (event.key.key == SDLK_F11) {
                    fullscreen = !fullscreen;
                    SDL_SetWindowFullscreen(window, fullscreen);
                } else if (event.key.key == SDLK_P) {
                    paused = !paused;
                    std::fprintf(stderr, "[mnemos_player] %s\n", paused ? "paused" : "resumed");
                    std::fflush(stderr);
                } else if (event.key.key == SDLK_F6) {
                    // Swap to the next disk in a multi-disk set, the way you'd
                    // flip the floppy when a game asks for the next disk.
                    if (system && system->media_count() > 1U) {
                        const std::size_t next =
                            (system->current_media_index() + 1U) % system->media_count();
                        if (system->insert_media(next)) {
                            std::fprintf(stderr, "[mnemos_player] inserted disk %zu/%zu\n",
                                         next + 1U, system->media_count());
                            std::fflush(stderr);
                        }
                    }
                }
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (gamepad == nullptr) {
                    gamepad = SDL_OpenGamepad(event.gdevice.which);
                    if (gamepad != nullptr) {
                        std::fprintf(stderr, "[mnemos_player] gamepad attached: %s\n",
                                     SDL_GetGamepadName(gamepad));
                    }
                }
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (gamepad != nullptr &&
                    SDL_GetJoystickID(SDL_GetGamepadJoystick(gamepad)) == event.gdevice.which) {
                    SDL_CloseGamepad(gamepad);
                    gamepad = nullptr;
                    std::fprintf(stderr, "[mnemos_player] gamepad removed\n");
                }
                break;
            default:
                break;
            }
        }

        // Keyboard + gamepad OR'd into the same controller_state so the user
        // can switch input mid-session. Adapters ignore buttons their hardware
        // doesn't have.
        //   Keyboard: arrows = dpad, Z/X/C = A/B/C, A/S/D = X/Y/Z (Genesis
        //             6-button extras), Enter = Start, LShift = Mode.
        //   Gamepad : dpad + left stick = dpad, South/East/West = A/B/C,
        //             North = X, L1/R1 = Y/Z, Start/Back = Start/Mode.
        {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            mnemos::frontend_sdk::controller_state pad{};
            pad.up = keys[SDL_SCANCODE_UP];
            pad.down = keys[SDL_SCANCODE_DOWN];
            pad.left = keys[SDL_SCANCODE_LEFT];
            pad.right = keys[SDL_SCANCODE_RIGHT];
            pad.a = keys[SDL_SCANCODE_Z];
            pad.b = keys[SDL_SCANCODE_X];
            pad.c = keys[SDL_SCANCODE_C];
            pad.x = keys[SDL_SCANCODE_A];
            pad.y = keys[SDL_SCANCODE_S];
            pad.z = keys[SDL_SCANCODE_D];
            pad.start = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER];
            pad.mode = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
            merge_gamepad(pad, gamepad);
            if (system) {
                system->apply_input(0, pad);
            }
            // Four Score: pads 2-4 from the additional gamepads on ports 1-3.
            if (system && four_score) {
                for (int port = 1; port <= 3; ++port) {
                    mnemos::frontend_sdk::controller_state extra{};
                    merge_gamepad(extra, fs_pads[static_cast<std::size_t>(port - 1)]);
                    system->apply_input(port, extra);
                }
            }
        }

        // Light gun (port index 1): map the mouse into the framebuffer using the
        // same integer letterbox the present path uses; the left button is the
        // trigger. Off-window -> aim (-1,-1) so the gun sees no light. The on-screen
        // framebuffer aim pixel is stashed for the crosshair overlay (-1 = off-screen).
        int gun_aim_x = -1;
        int gun_aim_y = -1;
        if (system && light_gun) {
            float mx = 0.0F;
            float my = 0.0F;
            const auto mouse = SDL_GetMouseState(&mx, &my);
            int ww = 0;
            int wh = 0;
            SDL_GetWindowSize(window, &ww, &wh);
            const auto fb = system->current_frame();
            mnemos::frontend_sdk::controller_state gun{};
            gun.trigger = (mouse & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0U;
            if (fb.width != 0U && fb.height != 0U && ww > 0 && wh > 0) {
                const auto rect = integer_letterbox(ww, wh, static_cast<int>(fb.width),
                                                    static_cast<int>(fb.height));
                const int rx = static_cast<int>(mx) - rect.x;
                const int ry = static_cast<int>(my) - rect.y;
                if (rx >= 0 && ry >= 0 && rx < rect.w && ry < rect.h) {
                    gun_aim_x = rx * static_cast<int>(fb.width) / rect.w;
                    gun_aim_y = ry * static_cast<int>(fb.height) / rect.h;
                    gun.aim_x = static_cast<std::int16_t>(gun_aim_x);
                    gun.aim_y = static_cast<std::int16_t>(gun_aim_y);
                }
            }
            system->apply_input(1, gun);
            // Hide the OS cursor while aiming on-screen so only the crosshair shows;
            // restore it when the aim leaves the framebuffer.
            if (gun_aim_x >= 0) {
                SDL_HideCursor();
            } else {
                SDL_ShowCursor();
            }
        }

        // Drive emulation: step a game frame only when the wall-clock pacer
        // says it's due. On a 60 Hz display vsync running a 50 Hz PAL cart,
        // this means roughly every 6th render frame skips step_one_frame()
        // and just re-presents the same VDP framebuffer -- keeping the game
        // and its audio at the right rate. While paused we skip stepping
        // entirely but keep rendering / accepting input.
        // Vertical (TATE) systems are rotated 90 degrees clockwise at the
        // transfer-buffer copy; downstream (upload/letterbox/blit) then sees
        // the swapped dimensions.
        const bool rotate_vertical =
            system != nullptr &&
            system->region().orientation == mnemos::frontend_sdk::display_orientation::vertical;
        std::uint32_t src_w = 0U;
        std::uint32_t src_h = 0U;
        const Uint64 now_ticks = SDL_GetPerformanceCounter();
        const bool frame_due = !paused && now_ticks >= next_frame_at;
        if (frame_due) {
            next_frame_at += static_cast<Uint64>(frame_ticks);
            // If we drifted far behind (e.g. window dragged), don't try to
            // catch up infinitely -- skip ahead.
            if (now_ticks > next_frame_at + static_cast<Uint64>(frame_ticks * 4)) {
                next_frame_at = now_ticks + static_cast<Uint64>(frame_ticks);
            }
        }
        if (system) {
            if (frame_due) {
                system->step_one_frame();
                ++fps_window_frames;
                ++total_frames;
                if (audio_stream != nullptr) {
                    const auto audio = system->drain_audio();
                    if (audio.samples != nullptr && audio.frame_count > 0U) {
                        SDL_PutAudioStreamData(
                            audio_stream, audio.samples,
                            static_cast<int>(audio.frame_count * 2U * sizeof(std::int16_t)));
                    }
                }
            }
            const auto fb = system->current_frame();
            src_w = fb.width;
            src_h = fb.height;
            const std::uint32_t src_stride = fb.effective_stride();

            // F12: dump the raw framebuffer as PPM (width x height, no stride).
            if (dump_requested && fb.pixels != nullptr && src_w > 0U && src_h > 0U) {
                char path[64];
                std::snprintf(path, sizeof(path), "mnemos_player_frame_%03d.ppm", dump_index++);
                std::ofstream out(path, std::ios::binary);
                if (out) {
                    out << "P6\n" << src_w << " " << src_h << "\n255\n";
                    for (std::uint32_t y = 0; y < src_h; ++y) {
                        const std::uint32_t* row =
                            fb.pixels + static_cast<std::size_t>(y) * src_stride;
                        for (std::uint32_t x = 0; x < src_w; ++x) {
                            const std::uint32_t p = row[x];
                            const char rgb[3] = {static_cast<char>((p >> 16U) & 0xFFU),
                                                 static_cast<char>((p >> 8U) & 0xFFU),
                                                 static_cast<char>(p & 0xFFU)};
                            out.write(rgb, 3);
                        }
                    }
                    std::fprintf(stderr, "[mnemos_player] dumped %s (%ux%u)\n", path, src_w, src_h);
                    std::fflush(stderr);
                }
                dump_requested = false;
            }

            if (fb.pixels != nullptr && src_w > 0U && src_h > 0U) {
                using mnemos::apps::player::draw_crosshair;
                // Copy framebuffer into the transfer buffer as a packed
                // image. When stride > width (H32 mode etc.) the per-row copy
                // avoids bleeding the stale stride tail. A vertical (TATE)
                // system is rotated 90 degrees clockwise here, so everything
                // downstream just sees a src_h x src_w image.
                void* mapped = SDL_MapGPUTransferBuffer(device, xfer, true);
                if (mapped != nullptr) {
                    if (rotate_vertical) {
                        auto* out = static_cast<std::uint32_t*>(mapped);
                        for (std::uint32_t y = 0; y < src_w; ++y) {
                            for (std::uint32_t x = 0; x < src_h; ++x) {
                                out[static_cast<std::size_t>(y) * src_h + x] =
                                    fb.pixels[static_cast<std::size_t>(src_h - 1U - x) *
                                                  src_stride +
                                              y];
                            }
                        }
                    } else if (src_stride == src_w) {
                        SDL_memcpy(mapped, fb.pixels,
                                   static_cast<size_t>(src_w) * src_h * sizeof(std::uint32_t));
                    } else {
                        auto* dst_row = static_cast<std::uint32_t*>(mapped);
                        const std::uint32_t* src_row = fb.pixels;
                        for (std::uint32_t y = 0; y < src_h; ++y) {
                            SDL_memcpy(dst_row, src_row, src_w * sizeof(std::uint32_t));
                            dst_row += src_w;
                            src_row += src_stride;
                        }
                    }
                    // Light-gun reticle at the aim point. The packed copy above is
                    // src_w x src_h; rotation isn't handled here so the crosshair is
                    // drawn only in the non-rotated path.
                    if (!rotate_vertical && gun_aim_x >= 0) {
                        draw_crosshair(0x00FF0000U, static_cast<std::uint32_t*>(mapped),
                                       static_cast<int>(src_w), static_cast<int>(src_h), gun_aim_x,
                                       gun_aim_y, 4);
                    }
                    SDL_UnmapGPUTransferBuffer(device, xfer);
                }
                if (rotate_vertical) {
                    const std::uint32_t rotated_w = src_h;
                    src_h = src_w;
                    src_w = rotated_w;
                }
            }
        }

        // Refresh the displayed FPS at ~1 Hz from the rolling emulation-
        // frame counter, then compose the overlay panel into the CPU
        // scratch buffer and upload it. Update cadence is intentionally
        // slow so the number is readable rather than dancing per frame.
        {
            const Uint64 now = SDL_GetPerformanceCounter();
            const Uint64 elapsed = now - fps_window_start;
            if (elapsed >= perf_freq) {
                displayed_fps = static_cast<int>(
                    (static_cast<double>(fps_window_frames) * static_cast<double>(perf_freq)) /
                    static_cast<double>(elapsed));
                fps_window_start = now;
                fps_window_frames = 0;
            }
        }
        {
            using mnemos::apps::player::draw_text;
            using mnemos::apps::player::fill_rect;
            using mnemos::apps::player::kGlyphHeight;
            // Clear the whole scratch buffer so the strip past panel_w
            // stays transparent-black (the blit's source region is
            // clipped to [0, panel_w] anyway, but a clean buffer keeps
            // partial-tail garbage out if the size ever changes).
            std::fill(overlay_pixels.begin(), overlay_pixels.end(), 0U);
            fill_rect(kOverlayBgColor, overlay_pixels.data(), kOverlayBufW, kOverlayBufH, 0, 0,
                      panel_w, panel_h);

            char fps_line[32];
            std::snprintf(fps_line, sizeof(fps_line), "FPS: %3d  F: %8llu", displayed_fps,
                          static_cast<unsigned long long>(total_frames));
            draw_text(fps_line, kOverlayFgColor, overlay_pixels.data(), kOverlayBufW, kOverlayBufH,
                      kOverlayPad, kOverlayPad);
            if (!spec_line.empty()) {
                draw_text(spec_line, kOverlayFgColor, overlay_pixels.data(), kOverlayBufW,
                          kOverlayBufH, kOverlayPad, kOverlayPad + kGlyphHeight + kOverlayPad);
            }

            void* mapped = SDL_MapGPUTransferBuffer(device, oxfer, true);
            if (mapped != nullptr) {
                SDL_memcpy(mapped, overlay_pixels.data(),
                           overlay_pixels.size() * sizeof(std::uint32_t));
                SDL_UnmapGPUTransferBuffer(device, oxfer);
            }
        }

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr) {
            std::fprintf(stderr, "SDL_AcquireGPUCommandBuffer failed: %s\n", SDL_GetError());
            running = false;
            break;
        }

        // Single copy pass uploads both the framebuffer (when present)
        // and the overlay panel each frame.
        {
            SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
            if (system && src_w > 0U && src_h > 0U) {
                SDL_GPUTextureTransferInfo src{};
                src.transfer_buffer = xfer;
                src.offset = 0;
                src.pixels_per_row = src_w;
                src.rows_per_layer = src_h;
                SDL_GPUTextureRegion dst{};
                dst.texture = tex;
                dst.x = 0;
                dst.y = 0;
                dst.w = src_w;
                dst.h = src_h;
                dst.d = 1;
                SDL_UploadToGPUTexture(copy_pass, &src, &dst, true);
            }
            SDL_GPUTextureTransferInfo osrc{};
            osrc.transfer_buffer = oxfer;
            osrc.offset = 0;
            osrc.pixels_per_row = static_cast<Uint32>(kOverlayBufW);
            osrc.rows_per_layer = static_cast<Uint32>(kOverlayBufH);
            SDL_GPUTextureRegion odst{};
            odst.texture = otex;
            odst.x = 0;
            odst.y = 0;
            odst.w = static_cast<Uint32>(kOverlayBufW);
            odst.h = static_cast<Uint32>(kOverlayBufH);
            odst.d = 1;
            SDL_UploadToGPUTexture(copy_pass, &osrc, &odst, true);
            SDL_EndGPUCopyPass(copy_pass);
        }

        // Swapchain texture may be null while hidden / resizing; skip the frame.
        SDL_GPUTexture* swap = nullptr;
        Uint32 swap_w = 0;
        Uint32 swap_h = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swap, &swap_w, &swap_h)) {
            SDL_SubmitGPUCommandBuffer(cmd);
            continue;
        }
        if (swap == nullptr) {
            SDL_SubmitGPUCommandBuffer(cmd);
            continue;
        }

        if (system && src_w > 0U && src_h > 0U) {
            const auto rect = integer_letterbox(static_cast<int>(swap_w), static_cast<int>(swap_h),
                                                static_cast<int>(src_w), static_cast<int>(src_h));
            // Log on first frame + any dim change (V28 <-> V30 etc.).
            static std::uint32_t last_w = 0U;
            static std::uint32_t last_h = 0U;
            if (!logged_dims || src_w != last_w || src_h != last_h) {
                std::fprintf(stderr,
                             "[mnemos_player] swapchain=%ux%u  src=%ux%u  "
                             "dst=%dx%d at (%d,%d) (scale %d)\n",
                             swap_w, swap_h, src_w, src_h, rect.w, rect.h, rect.x, rect.y,
                             src_w > 0U ? rect.w / static_cast<int>(src_w) : 0);
                std::fflush(stderr);
                logged_dims = true;
                last_w = src_w;
                last_h = src_h;
            }
            // Clear the whole swapchain in its own pass, then blit with LOAD.
            // Some backends scope BlitInfo's CLEAR to the dst region rather
            // than the whole target, so the separate clear avoids that.
            {
                SDL_GPUColorTargetInfo target{};
                target.texture = swap;
                target.load_op = SDL_GPU_LOADOP_CLEAR;
                target.store_op = SDL_GPU_STOREOP_STORE;
                target.clear_color = {0.0F, 0.0F, 0.0F, 1.0F};
                SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
                SDL_EndGPURenderPass(rp);
            }
            SDL_GPUBlitInfo blit{};
            blit.source.texture = tex;
            blit.source.x = 0;
            blit.source.y = 0;
            blit.source.w = src_w;
            blit.source.h = src_h;
            blit.destination.texture = swap;
            blit.destination.x = static_cast<Uint32>(rect.x);
            blit.destination.y = static_cast<Uint32>(rect.y);
            blit.destination.w = static_cast<Uint32>(rect.w);
            blit.destination.h = static_cast<Uint32>(rect.h);
            blit.load_op = SDL_GPU_LOADOP_LOAD; // preserve the explicit clear
            blit.filter = SDL_GPU_FILTER_NEAREST;
            SDL_BlitGPUTexture(cmd, &blit);
        } else {
            // No system loaded: clear to a known background.
            SDL_GPUColorTargetInfo target{};
            target.texture = swap;
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            target.clear_color = {0.05F, 0.05F, 0.08F, 1.0F};
            SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
            SDL_EndGPURenderPass(rp);
        }

        // Status overlay: blit the panel's used [0, panel_w] subregion to
        // the top-right of the swapchain. The blit is an opaque copy
        // (SDL_GPU has no shader-free alpha blend), so the panel sits as
        // a small black rectangle over the corner of the framebuffer.
        {
            const int dst_x = static_cast<int>(swap_w) - panel_w - kOverlayScreenMargin;
            const int dst_y = kOverlayScreenMargin;
            if (dst_x >= 0 && dst_y >= 0 && dst_x + panel_w <= static_cast<int>(swap_w) &&
                dst_y + panel_h <= static_cast<int>(swap_h)) {
                SDL_GPUBlitInfo oblit{};
                oblit.source.texture = otex;
                oblit.source.x = 0;
                oblit.source.y = 0;
                oblit.source.w = static_cast<Uint32>(panel_w);
                oblit.source.h = static_cast<Uint32>(panel_h);
                oblit.destination.texture = swap;
                oblit.destination.x = static_cast<Uint32>(dst_x);
                oblit.destination.y = static_cast<Uint32>(dst_y);
                oblit.destination.w = static_cast<Uint32>(panel_w);
                oblit.destination.h = static_cast<Uint32>(panel_h);
                oblit.load_op = SDL_GPU_LOADOP_LOAD;
                oblit.filter = SDL_GPU_FILTER_NEAREST;
                SDL_BlitGPUTexture(cmd, &oblit);
            }
        }

        SDL_SubmitGPUCommandBuffer(cmd);
    }

    if (gamepad != nullptr) {
        SDL_CloseGamepad(gamepad);
    }
    for (SDL_Gamepad* gp : fs_pads) {
        if (gp != nullptr) {
            SDL_CloseGamepad(gp);
        }
    }
    if (audio_stream != nullptr) {
        SDL_DestroyAudioStream(audio_stream);
    }
    SDL_ReleaseGPUTransferBuffer(device, oxfer);
    SDL_ReleaseGPUTexture(device, otex);
    SDL_ReleaseGPUTransferBuffer(device, xfer);
    SDL_ReleaseGPUTexture(device, tex);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
