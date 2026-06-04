// SDL3 windowed player. Boots whichever player_system adapter the ROM's file
// extension selects (Genesis / SMS / C64), presents its framebuffer at integer
// scale, streams audio, and routes keyboard + gamepad input. ESC quits.

#define SDL_MAIN_HANDLED

#include "adapter_registry.hpp"
#include "battery_save.hpp" // .srm load/save (cartridge battery RAM persistence)
#include "c64_adapter.hpp"  // force_link (the C64 has no cart-header region byte)
#include "chip.hpp"
#include "cli_args.hpp"
#include "debug_dump.hpp"
#include "genesis_adapter.hpp" // force_link + manifests::genesis::parse_market
#include "genesis_region.hpp"
#include "player_system.hpp"
#include "region.hpp"
#include "region_args.hpp"
#include "rom_loader.hpp"
#include "sms_adapter.hpp" // force_link + manifests::sms::parse_market
#include "sms_region.hpp"
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
#include <vector>

namespace {

    constexpr int kInitialWindowWidth = 1280;
    constexpr int kInitialWindowHeight = 960;

    // Streaming texture is sized for the worst-case Genesis VDP frame
    // (320x240 V30, x2 for interlace); per-frame uploads only touch the
    // active subregion the adapter reports.
    constexpr std::uint32_t kMaxFbWidth = 320U;
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

} // namespace

int main(int argc, char* argv[]) {
    using mnemos::apps::player::adapters::clean_rom_name;
    using mnemos::apps::player::adapters::detect_family;
    using mnemos::apps::player::adapters::family_label;
    using mnemos::apps::player::adapters::input_for_frame;
    using mnemos::apps::player::adapters::load_rom;
    using mnemos::apps::player::adapters::parse_mapper_arg;
    using mnemos::apps::player::adapters::parse_no_autostart;
    using mnemos::apps::player::adapters::parse_press_events;
    using mnemos::apps::player::adapters::parse_region_arg;
    using mnemos::apps::player::adapters::parse_rom_args;
    using mnemos::apps::player::adapters::parse_screenshot_args;
    using mnemos::apps::player::adapters::region_source_label;
    using mnemos::apps::player::adapters::resolve_video_region;
    using mnemos::apps::player::adapters::srm_path_for;
    using mnemos::apps::player::adapters::system_family;

    const auto rom_paths = parse_rom_args(argc, argv);
    const bool autostart = !parse_no_autostart(argc, argv);
    const auto region_arg = parse_region_arg(argc, argv);
    const auto mapper_arg = parse_mapper_arg(argc, argv);
    const auto screenshot = parse_screenshot_args(argc, argv);

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
        auto loaded = load_rom(rom_paths.front());
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
        const auto family = detect_family(loaded->name);
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

        std::string_view family_id = "genesis";
        switch (family) {
        case system_family::sms:
            family_id = "sms";
            break;
        case system_family::gg:
            family_id = "gg";
            break;
        case system_family::c64:
            family_id = "c64";
            break;
        case system_family::genesis:
            break;
        }
        system = mnemos::frontend_sdk::adapter_registry::instance().create(
            family_id, {.rom = std::move(loaded->bytes),
                        .video_region = video,
                        .display_name = clean_rom_name(loaded->name),
                        .additional_media = std::move(additional_media),
                        .autostart = autostart,
                        .mapper_override = mapper_arg.value_or(std::string{})});
        if (system && system->media_count() > 1U) {
            std::fprintf(stderr, "[mnemos_player] media set: %zu disks (F6 swaps)\n",
                         system->media_count());
        }
        if (!system) {
            std::fprintf(stderr, "[mnemos_player] no adapter registered for family '%.*s'\n",
                         static_cast<int>(family_id.size()), family_id.data());
            return 1;
        }
        // Load any existing .srm before the first frame; the guard writes it back
        // on exit, keyed off the on-disk ROM path (so it sits beside the cart even
        // when the image came from a .zip). Skipped under --screenshot: that
        // headless render/diagnostic path (parity sweeps over a read-only ROM
        // corpus) must not drop saves beside the ROMs.
        if (!screenshot) {
            srm_guard.emplace(system.get(), srm_path_for(rom_paths.front()));
        }
    }

    // Headless path: step --frames, dump PPM + per-chip sidecars, exit. No
    // window, no GPU. All system-specific knowledge lives behind player_system
    // -- the adapter publishes its chip list and each chip's introspection
    // surface advertises memory views, debug layers, and the CPU trace target.
    if (screenshot) {
        if (!system) {
            std::fprintf(stderr, "--screenshot requires --rom\n");
            return 1;
        }
        std::uint64_t trace_frame = 0;
        const std::string trace_path = screenshot->path + ".cpu_trace.csv";
        mnemos::debug::trace_csv_session trace(*system, trace_path, trace_frame);

        // Scripted input (`--press <button>@<frame>[+duration]`) so headless runs
        // can drive a game past intro/menu screens. Sampled before each frame.
        const auto press_events = parse_press_events(argc, argv);
        for (std::uint64_t i = 0; i < screenshot->frames; ++i) {
            trace_frame = i + 1U;
            if (!press_events.empty()) {
                system->apply_input(0, input_for_frame(press_events, i + 1U));
            }
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
        if (trace.active()) {
            std::fprintf(stderr, "[mnemos_player] wrote %s\n", trace_path.c_str());
        }
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
    // flags are required at device creation.
    SDL_GPUDevice* device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL, true,
        nullptr);
    if (device == nullptr) {
        std::fprintf(stderr, "SDL_CreateGPUDevice failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
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
    {
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        if (ids != nullptr && count > 0) {
            gamepad = SDL_OpenGamepad(ids[0]);
            if (gamepad != nullptr) {
                std::fprintf(stderr, "[mnemos_player] gamepad attached: %s\n",
                             SDL_GetGamepadName(gamepad));
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
            if (gamepad != nullptr) {
                constexpr Sint16 kAxisThreshold = 16384; // ~half deflection
                const auto lx = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
                const auto ly = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
                pad.up |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP) ||
                          ly < -kAxisThreshold;
                pad.down |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ||
                            ly > kAxisThreshold;
                pad.left |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ||
                            lx < -kAxisThreshold;
                pad.right |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ||
                             lx > kAxisThreshold;
                pad.a |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
                pad.b |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST);
                pad.c |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST);
                pad.x |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH);
                pad.y |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
                pad.z |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
                pad.start |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START);
                pad.mode |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK);
            }
            if (system) {
                system->apply_input(0, pad);
            }
        }

        // Drive emulation: step a game frame only when the wall-clock pacer
        // says it's due. On a 60 Hz display vsync running a 50 Hz PAL cart,
        // this means roughly every 6th render frame skips step_one_frame()
        // and just re-presents the same VDP framebuffer -- keeping the game
        // and its audio at the right rate. While paused we skip stepping
        // entirely but keep rendering / accepting input.
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
                // Copy framebuffer into the transfer buffer as a packed
                // src_w*src_h image. When stride > width (H32 mode etc.) the
                // per-row copy avoids bleeding the stale stride tail.
                void* mapped = SDL_MapGPUTransferBuffer(device, xfer, true);
                if (mapped != nullptr) {
                    if (src_stride == src_w) {
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
                    SDL_UnmapGPUTransferBuffer(device, xfer);
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
