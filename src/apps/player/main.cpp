// Mnemos windowed player (Commit 1: bring-up only).
//
// This commit just proves the SDL3 dependency, window creation, and event
// pump work end-to-end. It opens a 960x720 borderless-resizable window
// titled "Mnemos Player" and pumps SDL events until the user closes the
// window or presses ESC. No emulation runs yet -- that arrives in
// later commits via the mnemos::frontend_sdk::player_system abstraction
// (see docs in src/frontend_sdk/).

// Use a normal main() rather than SDL's main shim; simpler with our existing
// MSVC console-subsystem defaults.
#define SDL_MAIN_HANDLED

#include <SDL3/SDL.h>

#include <cstdio>

namespace {

    constexpr int kWindowWidth = 960;
    constexpr int kWindowHeight = 720;

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Mnemos Player", kWindowWidth, kWindowHeight,
                                          SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
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
        // No render yet -- the GPU presentation pipeline comes in Commit 4.
        SDL_Delay(16);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
