# Mnemos Player

`mnemos_player` is the user-facing windowed frontend. It is **system-agnostic**:
it drives any `mnemos::frontend_sdk::player_system` implementation, so the same
binary boots any wired-in system. Currently supported: **Genesis / Mega Drive**
(`.md`, `.gen`, `.smd`, `.bin`) and **Sega Master System** (`.sms`, `.sg`); the
family is picked from the ROM file extension. C64, 32X, Sega CD, SNES, NES slot
in alongside as those adapters land.

## Stack

- **Window / GPU / input / audio**: SDL3 (fetched via `cmake/modules/MnemosFetchSDL3.cmake`,
  pinned to `release-3.2.0`, built static).
- **Render path**: SDL_GPU explicit pipeline — uploads each system's RGBA
  framebuffer to a streaming texture and presents at integer scale (default 3×
  for a 320×240 NTSC source → 960×720 window). Shaders can later add CRT /
  scanline filters without reworking the pipeline.
- **Audio**: `SDL_AudioStream` consuming the mixed stereo output drained from
  the system's audio chips each frame.
- **Input**: keyboard + `SDL_Gamepad` hot-plug, normalized to the
  system-agnostic `controller_state` then translated by the active adapter
  into the system's controller MMIO protocol.

## Build status (commit-by-commit)

| Commit | Status                                                                  |
|--------|-------------------------------------------------------------------------|
| 1      | Opens a 960×720 window, pumps events, ESC / close exits. No emulation.  |
| 2      | `player_system` interface lands under `src/frontend_sdk/`.              |
| 3      | Genesis adapter; player still doesn't render (Commit 4 wires GPU).      |
| 4      | SDL_GPU framebuffer presentation; `--rom <path>` boots and renders.     |
| 5      | Keyboard input wired through the Genesis controller MMIO.               |
| 6      | `SDL_Gamepad` support.                                                  |
| 7      | Real audio path (chip-side audio sinks → `SDL_AudioStream`).            |
| 8      | F11 fullscreen, P pause, ESC quit, FPS overlay.                         |
