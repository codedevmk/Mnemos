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

#include "genesis_adapter.hpp"
#include "player_system.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

    constexpr int kInitialWindowWidth = 960;
    constexpr int kInitialWindowHeight = 720;

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

    // Build the player_system upfront so a bad ROM fails before we open a
    // window. With no --rom we just open the window and idle.
    std::unique_ptr<mnemos::frontend_sdk::player_system> system;
    if (rom_path) {
        auto bytes = read_file(*rom_path);
        if (!bytes || bytes->empty()) {
            std::fprintf(stderr, "could not read ROM: %s\n", rom_path->c_str());
            return 1;
        }
        system = std::make_unique<
            mnemos::apps::player::adapters::genesis::genesis_adapter>(std::move(*bytes));
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Mnemos Player", kInitialWindowWidth,
                                          kInitialWindowHeight, SDL_WINDOW_RESIZABLE);
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
                }
                break;
            default:
                break;
            }
        }

        // Drive emulation: exactly one video frame per present (vsync paces us).
        std::uint32_t src_w = 0U;
        std::uint32_t src_h = 0U;
        if (system) {
            system->step_one_frame();
            const auto fb = system->current_frame();
            src_w = fb.width;
            src_h = fb.height;

            if (fb.pixels != nullptr && src_w > 0U && src_h > 0U) {
                // Copy framebuffer into the transfer buffer (row-major, no padding).
                void* mapped = SDL_MapGPUTransferBuffer(device, xfer, true);
                if (mapped != nullptr) {
                    SDL_memcpy(mapped, fb.pixels,
                               static_cast<size_t>(src_w) * src_h * sizeof(std::uint32_t));
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
            blit.load_op = SDL_GPU_LOADOP_CLEAR;
            blit.clear_color = {0.0F, 0.0F, 0.0F, 1.0F};
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

    SDL_ReleaseGPUTransferBuffer(device, xfer);
    SDL_ReleaseGPUTexture(device, tex);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
