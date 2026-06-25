// SDL3 windowed player. Boots the player_system adapter named by --system
// (genesis / sms / gg / c64 / segacd / sega32x / irem_m72 / taito_f2 / cps1 /
// cps2 / spectrum / nes / msx / amiga500) with the --rom media (zip archives are
// extracted transparently), presents its framebuffer at integer scale, streams
// audio, and routes keyboard + gamepad input. ESC quits. Optional devices
// include --fm for MSX-MUSIC/FM-PAC or SMS YM2413, --rtc for MSX RP-5C01
// clock/CMOS, and --msx2 for MSX2/V9938 video hardware.

#define SDL_MAIN_HANDLED

#include "battery_save.hpp" // .srm load/save (cartridge battery RAM persistence)
#include "cli_args.hpp"
#include "headless_commands.hpp"
#include "player_system.hpp"
#include "region.hpp"
#include "region_args.hpp"
#include "state_file.hpp"
#include "system_launch.hpp"
#include "text_overlay.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

    constexpr int kInitialWindowWidth = 1280;
    constexpr int kInitialWindowHeight = 960;

    // Streaming texture is sized for the worst-case frame across supported
    // systems: V9938 high-resolution MSX2 modes at 512 columns, Genesis VDP
    // 320x240 V30 (x2 rows for interlace), and the Irem M72's 384x256 (256x384
    // once a vertical game is rotated for TATE presentation); per-frame uploads
    // only touch the active subregion the adapter reports.
    constexpr std::uint32_t kMaxFbWidth = 512U;
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

    [[nodiscard]] std::int16_t normalize_analog_axis(Sint16 value) noexcept {
        const int shifted = static_cast<int>(value) + 32768;
        const int clamped = std::clamp(shifted, 0, 65535);
        return static_cast<std::int16_t>((clamped * 255 + 32767) / 65535);
    }

    void populate_analog_state(mnemos::frontend_sdk::controller_state& state, SDL_Gamepad* gp,
                               std::size_t analog_index) noexcept {
        if (gp == nullptr) {
            return;
        }
        const SDL_GamepadAxis x_axis =
            analog_index == 0U ? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_RIGHTX;
        const SDL_GamepadAxis y_axis =
            analog_index == 0U ? SDL_GAMEPAD_AXIS_LEFTY : SDL_GAMEPAD_AXIS_RIGHTY;
        state.aim_x = normalize_analog_axis(SDL_GetGamepadAxis(gp, x_axis));
        state.aim_y = normalize_analog_axis(SDL_GetGamepadAxis(gp, y_axis));
    }

    [[nodiscard]] bool key_pressed(const bool* keys, int key_count,
                                   SDL_Scancode scancode) noexcept {
        const int index = static_cast<int>(scancode);
        return keys != nullptr && index >= 0 && index < key_count && keys[index];
    }

    void populate_keyboard_usage(mnemos::frontend_sdk::controller_state& state, const bool* keys,
                                 int key_count) noexcept {
        if (keys == nullptr || key_count <= 0) {
            return;
        }
        const auto count = std::min<std::size_t>(static_cast<std::size_t>(key_count),
                                                 mnemos::peripheral::keyboard_usage_count);
        for (std::size_t usage = 0; usage < count; ++usage) {
            state.set_key(static_cast<std::uint16_t>(usage), keys[usage]);
        }
    }

    [[nodiscard]] const char* load_status_name(mnemos::runtime::load_status status) noexcept {
        switch (status) {
        case mnemos::runtime::load_status::ok:
            return "ok";
        case mnemos::runtime::load_status::bad_magic:
            return "bad_magic";
        case mnemos::runtime::load_status::unsupported_version:
            return "unsupported_version";
        case mnemos::runtime::load_status::manifest_mismatch:
            return "manifest_mismatch";
        case mnemos::runtime::load_status::truncated:
            return "truncated";
        case mnemos::runtime::load_status::bad_crc:
            return "bad_crc";
        case mnemos::runtime::load_status::decompress_failed:
            return "decompress_failed";
        case mnemos::runtime::load_status::chunk_rejected:
            return "chunk_rejected";
        }
        return "unknown";
    }

    void save_quick_state(mnemos::frontend_sdk::player_system* system, const std::string& path) {
        if (system == nullptr || path.empty()) {
            return;
        }
        if (!system->session_capabilities().save_state_supported) {
            std::fprintf(stderr, "[mnemos_player] save states are not supported by this system\n");
            std::fflush(stderr);
            return;
        }
        const std::vector<std::uint8_t> bytes = system->save_state();
        if (bytes.empty() || !mnemos::apps::player::adapters::save_save_state_file(path, bytes)) {
            std::fprintf(stderr, "[mnemos_player] quick save failed: %s\n", path.c_str());
        } else {
            std::fprintf(stderr, "[mnemos_player] quick saved: %s (%zu bytes)\n", path.c_str(),
                         bytes.size());
        }
        std::fflush(stderr);
    }

    void load_quick_state(mnemos::frontend_sdk::player_system* system, const std::string& path,
                          SDL_AudioStream* audio_stream) {
        if (system == nullptr || path.empty()) {
            return;
        }
        if (!system->session_capabilities().save_state_supported) {
            std::fprintf(stderr, "[mnemos_player] save states are not supported by this system\n");
            std::fflush(stderr);
            return;
        }
        const auto bytes = mnemos::apps::player::adapters::load_save_state_file(path);
        if (!bytes.has_value()) {
            std::fprintf(stderr, "[mnemos_player] quick load missing: %s\n", path.c_str());
            std::fflush(stderr);
            return;
        }
        const mnemos::runtime::load_result result = system->load_state(*bytes);
        if (!result.ok()) {
            std::fprintf(stderr, "[mnemos_player] quick load failed: %s (%s)\n", path.c_str(),
                         load_status_name(result.status));
        } else {
            if (audio_stream != nullptr) {
                SDL_ClearAudioStream(audio_stream);
            }
            std::fprintf(stderr, "[mnemos_player] quick loaded: %s\n", path.c_str());
        }
        std::fflush(stderr);
    }

} // namespace

int main(int argc, char* argv[]) {
    using mnemos::apps::player::adapters::parse_animation_record_args;
    using mnemos::apps::player::adapters::parse_capabilities_arg;
    using mnemos::apps::player::adapters::parse_dip_arg;
    using mnemos::apps::player::adapters::parse_extract_assets_args;
    using mnemos::apps::player::adapters::parse_extract_audio_args;
    using mnemos::apps::player::adapters::parse_fm_unit_arg;
    using mnemos::apps::player::adapters::parse_four_score_arg;
    using mnemos::apps::player::adapters::parse_keyboard_layout_arg;
    using mnemos::apps::player::adapters::parse_light_gun_arg;
    using mnemos::apps::player::adapters::parse_load_state_arg;
    using mnemos::apps::player::adapters::parse_mapper2_arg;
    using mnemos::apps::player::adapters::parse_mapper_arg;
    using mnemos::apps::player::adapters::parse_msx2_arg;
    using mnemos::apps::player::adapters::parse_no_autostart;
    using mnemos::apps::player::adapters::parse_region_arg;
    using mnemos::apps::player::adapters::parse_rom_args;
    using mnemos::apps::player::adapters::parse_rtc_arg;
    using mnemos::apps::player::adapters::parse_save_state_args;
    using mnemos::apps::player::adapters::parse_screenshot_args;
    using mnemos::apps::player::adapters::parse_system_arg;
    using mnemos::apps::player::adapters::srm_path_for;
    using mnemos::apps::player::adapters::state_path_for;

    const auto rom_paths = parse_rom_args(argc, argv);
    const auto system_arg = parse_system_arg(argc, argv);
    const bool autostart = !parse_no_autostart(argc, argv);
    const auto region_arg = parse_region_arg(argc, argv);
    const auto mapper_arg = parse_mapper_arg(argc, argv);
    const auto mapper2_arg = parse_mapper2_arg(argc, argv);
    const bool fm_unit = parse_fm_unit_arg(argc, argv);
    const bool light_gun = parse_light_gun_arg(argc, argv);
    const bool four_score = parse_four_score_arg(argc, argv);
    const bool rtc = parse_rtc_arg(argc, argv);
    const bool msx2 = parse_msx2_arg(argc, argv);
    const auto dip_arg = parse_dip_arg(argc, argv);
    const auto keyboard_layout_arg = parse_keyboard_layout_arg(argc, argv);
    const mnemos::apps::player::headless_requests headless{
        .screenshot = parse_screenshot_args(argc, argv),
        .save_state = parse_save_state_args(argc, argv),
        .load_state = parse_load_state_arg(argc, argv),
        .extract_assets = parse_extract_assets_args(argc, argv),
        .extract_audio = parse_extract_audio_args(argc, argv),
        .record_animation = parse_animation_record_args(argc, argv),
        .capabilities = parse_capabilities_arg(argc, argv),
    };

    auto launch =
        mnemos::apps::player::launch_system({.rom_paths = rom_paths,
                                             .system_arg = system_arg,
                                             .autostart = autostart,
                                             .region_override = region_arg,
                                             .mapper_override = mapper_arg,
                                             .mapper2_override = mapper2_arg,
                                             .fm_unit = fm_unit,
                                             .light_gun = light_gun,
                                             .four_score = four_score,
                                             .rtc = rtc,
                                             .msx2 = msx2,
                                             .dip_override = dip_arg,
                                             .keyboard_layout_override = keyboard_layout_arg});
    if (launch.exit_code != 0) {
        return launch.exit_code;
    }
    auto system = std::move(launch.system);
    const std::string quick_state_path = launch.primary_media_path.empty()
                                             ? std::string{}
                                             : state_path_for(launch.primary_media_path);

    // Diagnostic/headless sweeps over ROM corpora must not create or update
    // save files beside source media.
    std::optional<battery_save_guard> srm_guard;
    if (system && !mnemos::apps::player::has_headless_request(headless)) {
        srm_guard.emplace(system.get(), srm_path_for(launch.primary_media_path));
    }

    if (const auto exit_code =
            mnemos::apps::player::run_headless_request(system.get(), headless, argc, argv)) {
        return *exit_code;
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
                } else if (event.key.key == SDLK_F5) {
                    save_quick_state(system.get(), quick_state_path);
                } else if (event.key.key == SDLK_F9) {
                    load_quick_state(system.get(), quick_state_path, audio_stream);
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

        // Keyboard + gamepad OR'd into the same controller_state for pad-only
        // systems. Systems that advertise a dedicated keyboard port receive the
        // host keyboard as physical key usages on that port instead.
        //   Keyboard-as-pad: arrows = dpad, Z/X/C = A/B/C, A/S/D = X/Y/Z
        //             (Genesis 6-button extras), Enter/1 = Start, Backspace/5 =
        //             Select (arcade coin), LShift = Mode, 6 = service credit,
        //             F2 = test switch.
        //   Gamepad : dpad + left stick = dpad, South/East/West = A/B/C,
        //             North = X, L1/R1 = Y/Z, Start/Back = Start/Mode.
        {
            int key_count = 0;
            const bool* keys = SDL_GetKeyboardState(&key_count);
            int keyboard_port = -1;
            if (system != nullptr) {
                for (const auto& p : system->session_capabilities().input_ports) {
                    if (p.format == mnemos::frontend_sdk::input_device_format::keyboard) {
                        keyboard_port = static_cast<int>(p.port_index);
                        break;
                    }
                }
            }
            mnemos::frontend_sdk::controller_state pad{};
            if (keyboard_port < 0 && keys != nullptr) {
                pad.up = key_pressed(keys, key_count, SDL_SCANCODE_UP);
                pad.down = key_pressed(keys, key_count, SDL_SCANCODE_DOWN);
                pad.left = key_pressed(keys, key_count, SDL_SCANCODE_LEFT);
                pad.right = key_pressed(keys, key_count, SDL_SCANCODE_RIGHT);
                pad.a = key_pressed(keys, key_count, SDL_SCANCODE_Z);
                pad.b = key_pressed(keys, key_count, SDL_SCANCODE_X);
                pad.c = key_pressed(keys, key_count, SDL_SCANCODE_C);
                pad.x = key_pressed(keys, key_count, SDL_SCANCODE_A);
                pad.y = key_pressed(keys, key_count, SDL_SCANCODE_S);
                pad.z = key_pressed(keys, key_count, SDL_SCANCODE_D);
                pad.start = key_pressed(keys, key_count, SDL_SCANCODE_RETURN) ||
                            key_pressed(keys, key_count, SDL_SCANCODE_KP_ENTER) ||
                            key_pressed(keys, key_count, SDL_SCANCODE_1);
                pad.select = key_pressed(keys, key_count, SDL_SCANCODE_BACKSPACE) ||
                             key_pressed(keys, key_count, SDL_SCANCODE_5);
                pad.mode = key_pressed(keys, key_count, SDL_SCANCODE_LSHIFT) ||
                           key_pressed(keys, key_count, SDL_SCANCODE_RSHIFT);
                pad.service = key_pressed(keys, key_count, SDL_SCANCODE_6);
                pad.test = key_pressed(keys, key_count, SDL_SCANCODE_F2);
            }
            merge_gamepad(pad, gamepad);
            if (system) {
                system->apply_input(0, pad);
                if (keyboard_port >= 0) {
                    mnemos::frontend_sdk::controller_state keyboard{};
                    populate_keyboard_usage(keyboard, keys, key_count);
                    system->apply_input(keyboard_port, keyboard);
                }
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

        // Analog controls: systems that advertise analog ports consume raw
        // normalized axes. The first analog port is the gamepad left stick, the
        // second is the right stick; additional ports repeat the right stick.
        if (system) {
            std::size_t analog_index = 0U;
            for (const auto& p : system->session_capabilities().input_ports) {
                if (p.format != mnemos::frontend_sdk::input_device_format::analog) {
                    continue;
                }
                mnemos::frontend_sdk::controller_state analog{};
                populate_analog_state(analog, gamepad, analog_index);
                system->apply_input(static_cast<int>(p.port_index), analog);
                ++analog_index;
            }
        }

        // Pointer-style mouse ports consume the OS pointer even when --light-gun
        // is not active; adapters advertise the concrete port they want driven.
        if (system) {
            int mouse_port = -1;
            for (const auto& p : system->session_capabilities().input_ports) {
                if (p.format == mnemos::frontend_sdk::input_device_format::mouse) {
                    mouse_port = static_cast<int>(p.port_index);
                    break;
                }
            }
            if (mouse_port >= 0) {
                float mx = 0.0F;
                float my = 0.0F;
                const auto mouse = SDL_GetMouseState(&mx, &my);
                int ww = 0;
                int wh = 0;
                SDL_GetWindowSize(window, &ww, &wh);
                const auto fb = system->current_frame();
                mnemos::frontend_sdk::controller_state pointer{};
                pointer.trigger = (mouse & SDL_BUTTON_MASK(SDL_BUTTON_LEFT)) != 0U;
                pointer.a = (mouse & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT)) != 0U;
                pointer.b = (mouse & SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE)) != 0U;
                if (fb.width != 0U && fb.height != 0U && ww > 0 && wh > 0) {
                    const auto rect = integer_letterbox(ww, wh, static_cast<int>(fb.width),
                                                        static_cast<int>(fb.height));
                    const int rx = static_cast<int>(mx) - rect.x;
                    const int ry = static_cast<int>(my) - rect.y;
                    if (rx >= 0 && ry >= 0 && rx < rect.w && ry < rect.h) {
                        pointer.aim_x =
                            static_cast<std::int16_t>(rx * static_cast<int>(fb.width) / rect.w);
                        pointer.aim_y =
                            static_cast<std::int16_t>(ry * static_cast<int>(fb.height) / rect.h);
                    }
                }
                system->apply_input(mouse_port, pointer);
            }
        }

        // Light gun: map the mouse into the framebuffer using the same integer
        // letterbox the present path uses; the left button is the trigger. Off-window
        // -> aim (-1,-1) so the gun sees no light. The on-screen framebuffer aim pixel
        // is stashed for the crosshair overlay (-1 = off-screen). The gun is routed to
        // whichever port the adapter advertises as a light gun; if none is advertised
        // the legacy port index 1 is used (only when --light-gun was passed).
        int gun_aim_x = -1;
        int gun_aim_y = -1;
        if (system && light_gun) {
            int gun_port = -1;
            for (const auto& p : system->session_capabilities().input_ports) {
                if (p.format == mnemos::frontend_sdk::input_device_format::lightgun) {
                    gun_port = static_cast<int>(p.port_index);
                    break;
                }
            }
            if (gun_port < 0) {
                gun_port = 1; // adapter advertised no gun port: fall back to legacy index
            }
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
            system->apply_input(gun_port, gun);
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
