// Mnemos windowed player.
//
// Commit 4 wires SDL_GPU framebuffer presentation. The player accepts
//   --rom <path-to-cartridge.md|.bin|.gen>
// to boot the Genesis adapter and present its VDP framebuffer at integer
// scale (3x by default; 960x720 window for a 320x240 PAL frame). Without
// --rom the window opens to a dim clear color so the SDL3 / SDL_GPU bring-
// up is observable on its own.
//
// Audio + input still off (commits 5-7). Hold ESC or close the window to
// quit.

#define SDL_MAIN_HANDLED

#include "chip.hpp"
#include "genesis_adapter.hpp"
#include "genesis_vdp.hpp"
#include "player_system.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

    // Initial window logical size. The Genesis worst-case visible frame is
    // 320x240 (PAL V30 or NTSC V30) -- at integer-scale 4 that is 1280x960
    // physical pixels, which fits comfortably on a 1080p monitor. SDL gives
    // us a DPI-aware swapchain, so on a 125%/150% scaled display we get a
    // bigger physical pixel grid and the integer-letterbox math still picks
    // the largest N that fits.
    constexpr int kInitialWindowWidth = 1280;
    constexpr int kInitialWindowHeight = 960;

    // Max framebuffer the Genesis VDP can produce (PAL interlace-2 worst case).
    // The streaming texture is sized for the max; per-frame uploads only touch
    // the active width x height region the adapter reports.
    constexpr std::uint32_t kMaxFbWidth = 320U;
    constexpr std::uint32_t kMaxFbHeight = 480U;

    std::optional<std::string> parse_rom_arg(int argc, char* argv[]) {
        for (int i = 1; i < argc - 1; ++i) {
            const std::string a = argv[i];
            if (a == "--rom" || a == "-r") {
                return std::string{argv[i + 1]};
            }
        }
        return std::nullopt;
    }

    // --region pal|ntsc|auto (default: auto). Returns empty optional for "auto"
    // (the adapter detects from the ROM header); explicit values override.
    enum class region_override {
        auto_detect,
        ntsc,
        pal,
    };

    region_override parse_region_arg(int argc, char* argv[]) {
        for (int i = 1; i < argc - 1; ++i) {
            const std::string a = argv[i];
            if (a == "--region") {
                const std::string v = argv[i + 1];
                if (v == "pal" || v == "PAL") {
                    return region_override::pal;
                }
                if (v == "ntsc" || v == "NTSC") {
                    return region_override::ntsc;
                }
            }
        }
        return region_override::auto_detect;
    }

    // --screenshot <path.ppm> --frames N: run N video frames headlessly (no window,
    // no GPU init) and dump the resulting VDP framebuffer as a PPM. The pair is
    // designed for offline iteration on rendering bugs without needing a human
    // to press a hotkey at the right moment.
    struct screenshot_request final {
        std::string path;
        std::uint64_t frames{};
    };

    std::optional<screenshot_request> parse_screenshot_args(int argc, char* argv[]) {
        std::optional<std::string> path;
        std::optional<std::uint64_t> frames;
        for (int i = 1; i < argc - 1; ++i) {
            const std::string a = argv[i];
            if (a == "--screenshot") {
                path = std::string{argv[i + 1]};
            } else if (a == "--frames") {
                frames = std::strtoull(argv[i + 1], nullptr, 10);
            }
        }
        if (path && frames) {
            return screenshot_request{*path, *frames};
        }
        return std::nullopt;
    }

    // Decode a Genesis CRAM entry (9-bit BGR, 3 bits per channel) to 0x00RRGGBB
    // using the standard intensity LUT for normal brightness.
    std::uint32_t cram_entry_to_rgb(std::uint16_t cram_word) {
        constexpr std::array<std::uint8_t, 8> lut = {0, 36, 73, 109, 146, 182, 219, 255};
        const std::uint8_t r = lut[(cram_word >> 1) & 0x7];
        const std::uint8_t g = lut[(cram_word >> 5) & 0x7];
        const std::uint8_t b = lut[(cram_word >> 9) & 0x7];
        return (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) |
               static_cast<std::uint32_t>(b);
    }

    // Render the entire plane A of a Genesis VDP (ignoring scroll, window,
    // sprites) into a PPM. Diagnostic for debugging plane-data placement.
    int scroll_size_cells(int sz_bits) {
        constexpr int sizes[4] = {32, 64, 64, 128};
        return sizes[sz_bits & 3];
    }

    bool dump_plane_a_ppm(
        const mnemos::chips::video::genesis_vdp& vdp, const std::string& path) {
        const int hsz_cells = scroll_size_cells(vdp.reg(16) & 0x03);
        const int vsz_cells = scroll_size_cells((vdp.reg(16) >> 4) & 0x03);
        const std::uint32_t nt_base = (static_cast<std::uint32_t>(vdp.reg(2)) & 0x38U) << 10U;
        const int plane_w = hsz_cells * 8;
        const int plane_h = vsz_cells * 8;
        std::vector<std::uint32_t> buf(static_cast<std::size_t>(plane_w) * plane_h, 0U);
        for (int cy = 0; cy < vsz_cells; ++cy) {
            for (int cx = 0; cx < hsz_cells; ++cx) {
                const std::uint32_t nt_offset =
                    static_cast<std::uint32_t>(cy * hsz_cells + cx) * 2U;
                const std::uint16_t nt = vdp.vram16(nt_base + nt_offset);
                const int tile = nt & 0x7FF;
                const bool hf = ((nt >> 11) & 1) != 0;
                const bool vf = ((nt >> 12) & 1) != 0;
                const int pal = (nt >> 13) & 3;
                // Each tile = 32 bytes (8x8 4bpp); rows are 4 bytes each.
                for (int fy = 0; fy < 8; ++fy) {
                    const int row = vf ? (7 - fy) : fy;
                    for (int fx = 0; fx < 8; ++fx) {
                        const int col = hf ? (7 - fx) : fx;
                        const std::uint32_t pat_addr =
                            static_cast<std::uint32_t>(tile) * 32U + row * 4U + (col / 2);
                        const std::uint16_t word = vdp.vram16(pat_addr & ~1U);
                        const std::uint8_t byte = static_cast<std::uint8_t>(
                            (pat_addr & 1) ? (word & 0xFF) : (word >> 8));
                        const std::uint8_t color = (col & 1) ? (byte & 0xF) : (byte >> 4);
                        const std::uint16_t cram = vdp.cram(pal * 16 + color);
                        const std::uint32_t rgb = color == 0 ? 0U : cram_entry_to_rgb(cram);
                        buf[(cy * 8 + fy) * plane_w + (cx * 8 + fx)] = rgb;
                    }
                }
            }
        }
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return false;
        }
        out << "P6\n" << plane_w << " " << plane_h << "\n255\n";
        for (auto p : buf) {
            const char rgb[3] = {static_cast<char>((p >> 16) & 0xFF),
                                 static_cast<char>((p >> 8) & 0xFF),
                                 static_cast<char>(p & 0xFF)};
            out.write(rgb, 3);
        }
        return out.good();
    }

    bool dump_framebuffer_ppm(const mnemos::chips::frame_buffer_view& fb,
                              const std::string& path) {
        if (fb.pixels == nullptr || fb.width == 0U || fb.height == 0U) {
            return false;
        }
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            return false;
        }
        const std::uint32_t stride = fb.effective_stride();
        out << "P6\n" << fb.width << " " << fb.height << "\n255\n";
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * stride;
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                const std::uint32_t p = row[x];
                const char rgb[3] = {static_cast<char>((p >> 16U) & 0xFFU),
                                     static_cast<char>((p >> 8U) & 0xFFU),
                                     static_cast<char>(p & 0xFFU)};
                out.write(rgb, 3);
            }
        }
        return out.good();
    }

    std::optional<std::vector<std::uint8_t>> read_file(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                         std::istreambuf_iterator<char>());
    }

    // Largest integer N such that an N-times scaled source fits inside the
    // window without cropping. Centred destination rect; the area outside
    // the rect is left at the previous swapchain clear (we LOAD-clear each
    // present so it stays a solid black letterbox).
    struct dst_rect {
        int x{}, y{}, w{}, h{};
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
    const auto rom_path = parse_rom_arg(argc, argv);
    const auto region_arg = parse_region_arg(argc, argv);
    const auto screenshot = parse_screenshot_args(argc, argv);

    // Build the player_system upfront so a bad ROM fails before we open a
    // window. With no --rom we just open the window and idle.
    std::unique_ptr<mnemos::frontend_sdk::player_system> system;
    if (rom_path) {
        auto bytes = read_file(*rom_path);
        if (!bytes || bytes->empty()) {
            std::fprintf(stderr, "could not read ROM: %s\n", rom_path->c_str());
            return 1;
        }
        using mnemos::manifests::genesis::genesis_config;
        const auto detected = mnemos::apps::player::adapters::genesis::detect_region(*bytes);
        const auto region = region_arg == region_override::pal    ? genesis_config::region::pal
                            : region_arg == region_override::ntsc ? genesis_config::region::ntsc
                                                                  : detected;
        std::fprintf(stderr, "[mnemos_player] region: %s (%s)\n",
                     region == genesis_config::region::pal ? "PAL" : "NTSC",
                     region_arg == region_override::auto_detect ? "auto-detected"
                                                                : "explicit --region");
        std::fflush(stderr);
        system = std::make_unique<mnemos::apps::player::adapters::genesis::genesis_adapter>(
            std::move(*bytes), genesis_config{.video_region = region});
    }

    // Headless screenshot path: step the requested number of frames, dump the
    // framebuffer, exit. No window, no GPU. Designed so we can iterate on
    // rendering bugs offline without driving the UI by hand.
    if (screenshot) {
        if (!system) {
            std::fprintf(stderr, "--screenshot requires --rom\n");
            return 1;
        }
        for (std::uint64_t i = 0; i < screenshot->frames; ++i) {
            system->step_one_frame();
        }
        const auto fb = system->current_frame();
        if (!dump_framebuffer_ppm(fb, screenshot->path)) {
            std::fprintf(stderr, "could not write screenshot: %s\n", screenshot->path.c_str());
            return 1;
        }
        std::fprintf(stderr, "[mnemos_player] wrote %s (%ux%u after %llu frames)\n",
                     screenshot->path.c_str(), fb.width, fb.height,
                     static_cast<unsigned long long>(screenshot->frames));

        // VDP state dump (Genesis-specific). Helps diagnose scroll/plane bugs
        // when paired with the framebuffer PPM.
        auto* genesis = dynamic_cast<mnemos::apps::player::adapters::genesis::genesis_adapter*>(
            system.get());
        if (genesis != nullptr) {
            const auto& vdp = genesis->system().vdp;
            // Always dump the full plane A alongside the visible-window PPM
            // so we can see what's actually in VRAM (independent of scroll).
            const std::string plane_path = screenshot->path + ".plane_a.ppm";
            if (dump_plane_a_ppm(vdp, plane_path)) {
                std::fprintf(stderr, "[mnemos_player] wrote %s (full plane A)\n",
                             plane_path.c_str());
            }
            // Dump VRAM (64KB) as binary so it can be diffed byte-for-byte
            // against an external reference at the same point.
            const std::string vram_path = screenshot->path + ".vram.bin";
            std::ofstream vout(vram_path, std::ios::binary);
            if (vout) {
                for (std::uint32_t a = 0; a < 0x10000U; a += 2) {
                    const std::uint16_t w = vdp.vram16(a);
                    const char bytes[2] = {static_cast<char>(w >> 8),
                                           static_cast<char>(w & 0xFF)};
                    vout.write(bytes, 2);
                }
                std::fprintf(stderr, "[mnemos_player] wrote %s (64KB VRAM)\n",
                             vram_path.c_str());
            }
            // 68K work RAM (64KB at $E00000-$FFFFFF, mirrored). Flat byte
            // layout so an external reference's work-RAM image can be diffed
            // directly with memcmp.
            const std::string wram_path = screenshot->path + ".wram.bin";
            std::ofstream wout(wram_path, std::ios::binary);
            if (wout) {
                const auto& ram = genesis->system().work_ram;
                wout.write(reinterpret_cast<const char*>(ram.data()),
                           static_cast<std::streamsize>(ram.size()));
                std::fprintf(stderr, "[mnemos_player] wrote %s (64KB 68K work RAM)\n",
                             wram_path.c_str());
            }
            std::fprintf(stderr, "[vdp] regs:");
            for (int i = 0; i < 24; ++i) {
                std::fprintf(stderr, " %02d=%02X", i, vdp.reg(i));
            }
            std::fprintf(stderr, "\n[vdp] reg10/16 plane-size: HSZ=%d VSZ=%d  "
                                 "vscroll-mode=%s  hscroll-mode=R0Bbits01=%d\n",
                         vdp.reg(16) & 0x03, (vdp.reg(16) >> 4) & 0x03,
                         (vdp.reg(11) & 0x04) ? "per-2-cells" : "full-plane",
                         vdp.reg(11) & 0x03);
            std::fprintf(stderr, "[vdp] hscroll-base=$%04X   nameA=$%04X  nameB=$%04X\n",
                         (vdp.reg(13) & 0x3F) << 10, (vdp.reg(2) & 0x38) << 10,
                         (vdp.reg(4) & 0x07) << 13);
            std::fprintf(stderr, "[vdp] vsram[0..3]: %04X %04X %04X %04X  (plane A col 0/1, B col 0/1)\n",
                         vdp.vsram(0), vdp.vsram(1), vdp.vsram(2), vdp.vsram(3));
            std::fflush(stderr);
        }
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

    // SDL_GPU device: D3D12 on Windows by default, with the other backends
    // available as fallback. SPIRV/DXIL/MSL flags here advertise the shader
    // formats we COULD provide -- we never actually create a shader, only use
    // SDL_BlitGPUTexture, but the flags are required at device creation.
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

    // Streaming texture: full max-Genesis size, sampled with NEAREST so the
    // integer-scale upscale stays crisp. The framebuffer bytes the VDP hands
    // us are 0x00RRGGBB little-endian -> BGRA8 byte order.
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

    // Transfer buffer the framebuffer upload streams through. One per frame
    // is overkill; we keep a single persistent one and cycle it (the SDL_GPU
    // upload API supports per-call cycle when the resource is in flight).
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

    bool logged_dims = false;
    int dump_index = 0;
    bool dump_requested = false;
    bool paused = false;
    bool fullscreen = false;

    // Pace the loop at the GAME's native frame rate (e.g. 50 Hz for a PAL
    // cart), not vsync. If display is 60 Hz vsync and the game is 50 Hz PAL,
    // running step_one_frame() at display rate would over-clock the game by
    // 1.2x; audio production then outpaces SDL's consumption (we declared
    // the chip's native rate) and the queue grows until playback is audibly
    // late/choppy. Software-pacing on Uint64 high-res ticks fixes both the
    // audio drift and the gameplay speed.
    const double target_fps =
        system ? system->region().frames_per_second_x1000 / 1000.0 : 60.0;
    const Uint64 perf_freq = SDL_GetPerformanceFrequency();
    const double frame_ticks = static_cast<double>(perf_freq) / target_fps;
    Uint64 next_frame_at = SDL_GetPerformanceCounter();

    // Open an audio stream once we know the system's native sample rate.
    // Genesis NTSC = ~53267 Hz, PAL = ~52781 Hz; SDL resamples to the device's
    // rate transparently. Stereo s16 throughout. We push samples per frame
    // from the adapter; SDL pulls them at the device's pace.
    SDL_AudioStream* audio_stream = nullptr;
    if (system) {
        const auto chunk = system->drain_audio(); // probe for sample rate
        const std::uint32_t rate = chunk.sample_rate != 0U ? chunk.sample_rate : 48000U;
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_S16;
        spec.channels = 2;
        spec.freq = static_cast<int>(rate);
        audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr,
                                                 nullptr);
        if (audio_stream != nullptr) {
            SDL_ResumeAudioStreamDevice(audio_stream);
            std::fprintf(stderr, "[mnemos_player] audio: %u Hz stereo s16\n", rate);
        } else {
            std::fprintf(stderr, "[mnemos_player] SDL_OpenAudioDeviceStream failed: %s\n",
                         SDL_GetError());
        }
    }

    // Open whichever gamepad is currently plugged in for port 0, and watch
    // for hot-plug events to swap if a different controller arrives. Keyboard
    // input is always sampled too -- both feed the same controller_state.
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

        // Build the per-frame controller_state for port 0 from both inputs
        // available: the keyboard (always sampled) and any attached gamepad
        // (analog stick + dpad + face buttons), OR'd together so the player
        // can switch between them mid-session without rebinding.
        //   Keyboard: Arrows = D-pad, Z = A, X = B, C = C, Enter = Start
        //   Gamepad : DPad+left stick = D-pad, South = A, East = B, West = C,
        //             Start = Start, Back = Select (when 6-button lands)
        // Adapters that don't expose all of these (SMS, C64, ...) ignore the
        // extras.
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
            pad.start = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER];
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
                pad.start |= SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START);
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
                // Drain audio the frame produced and feed it to SDL_AudioStream,
                // which buffers + resamples to the device rate transparently.
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

            // F12: dump the raw VDP framebuffer to a PPM file we can inspect.
            // The PPM captures *exactly* what the chip wrote (width x height,
            // ignoring stride padding), so we can see what's in source rows
            // without any presentation transform applied.
            if (dump_requested && fb.pixels != nullptr && src_w > 0U && src_h > 0U) {
                char path[64];
                std::snprintf(path, sizeof(path), "mnemos_player_frame_%03d.ppm", dump_index++);
                std::ofstream out(path, std::ios::binary);
                if (out) {
                    out << "P6\n" << src_w << " " << src_h << "\n255\n";
                    for (std::uint32_t y = 0; y < src_h; ++y) {
                        const std::uint32_t* row = fb.pixels + static_cast<std::size_t>(y) * src_stride;
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
                // Copy framebuffer into the transfer buffer as a packed src_w*src_h
                // image: when stride > width (mode-switched chips like Genesis VDP
                // in H32 mode), iterate rows and copy only the visible prefix so the
                // stale tail of each storage row does not bleed into the texture.
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

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr) {
            std::fprintf(stderr, "SDL_AcquireGPUCommandBuffer failed: %s\n", SDL_GetError());
            running = false;
            break;
        }

        // Stream this frame's pixels into the streaming texture.
        if (system && src_w > 0U && src_h > 0U) {
            SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
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
            SDL_EndGPUCopyPass(copy_pass);
        }

        // Acquire the swapchain texture (may be nullptr if the window is hidden
        // or being resized -- skip presenting that frame cleanly).
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
            // Log first frame, and any later frame whose dims change (game mode
            // switches between V28/V30 etc).
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
            // First clear the entire swapchain in its own render pass, then blit
            // the source into the letterbox dst rect with LOAD (preserve clear).
            // Doing the clear separately from the blit avoids any ambiguity about
            // whether BlitInfo's load_op = CLEAR clears the WHOLE destination or
            // only the dst region -- some backends treat the two differently.
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
            // No system: just clear so the user sees a window in a known state.
            SDL_GPUColorTargetInfo target{};
            target.texture = swap;
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            target.clear_color = {0.05F, 0.05F, 0.08F, 1.0F};
            SDL_GPURenderPass* rp = SDL_BeginGPURenderPass(cmd, &target, 1, nullptr);
            SDL_EndGPURenderPass(rp);
        }

        SDL_SubmitGPUCommandBuffer(cmd);
    }

    if (gamepad != nullptr) {
        SDL_CloseGamepad(gamepad);
    }
    if (audio_stream != nullptr) {
        SDL_DestroyAudioStream(audio_stream);
    }
    SDL_ReleaseGPUTransferBuffer(device, xfer);
    SDL_ReleaseGPUTexture(device, tex);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
