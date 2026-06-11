#pragma once

#include "peripheral.hpp" // controller_state

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters {

    // Scan argv for `--rom <path>` (or `-r <path>`) and return the path.
    [[nodiscard]] std::optional<std::string> parse_rom_arg(int argc, char* argv[]);

    // Scan argv for `--system <name>` (or `-s <name>`): which emulation engine
    // runs the media. Required whenever media is given -- the engine is chosen
    // by this name (a registry family id), never inferred from the ROM
    // filename. nullopt when the flag is absent or has no value.
    [[nodiscard]] std::optional<std::string> parse_system_arg(int argc, char* argv[]);

    // All media paths in command-line order: every `--rom`/`-r`/`--disk <path>`.
    // The first is the primary image (it selects the system); the rest are
    // additional media -- e.g. the further disks of a multi-disk C64 game,
    // swappable at runtime. Empty when no media flag is present.
    [[nodiscard]] std::vector<std::string> parse_rom_args(int argc, char* argv[]);

    // Whether autostart is disabled via `--no-autostart`. Autostart is on by
    // default (a disk/tape C64 types LOAD/RUN itself), so this returns true only
    // when the user opts out to drop to a bare prompt.
    [[nodiscard]] bool parse_no_autostart(int argc, char* argv[]);

    // Scan argv for `--mapper <name>` and return the lowercased value, or
    // nullopt when the flag is absent or has no value. The value is interpreted
    // by the family adapter (the SMS adapter accepts sega/codemasters/korean);
    // an unrecognised name leaves the adapter on auto-detect.
    [[nodiscard]] std::optional<std::string> parse_mapper_arg(int argc, char* argv[]);

    // Scan argv for `--dip <value>` (hex with 0x prefix, or decimal) and
    // return the 16-bit DIP bank, or nullopt when absent/malformed.
    [[nodiscard]] std::optional<std::uint16_t> parse_dip_arg(int argc, char* argv[]);

    // --screenshot <path.ppm> --frames N: headless run, dump the resulting
    // VDP framebuffer as PPM, exit. Both flags must be present together; a
    // missing or unparseable value disables the headless path.
    struct screenshot_request final {
        std::string path;
        std::uint64_t frames{};
    };

    [[nodiscard]] std::optional<screenshot_request> parse_screenshot_args(int argc, char* argv[]);

    // --extract-assets <base> [--extract-frames N]: headless run that advances N
    // frames (default 0 -- extract at the boot state) then writes the decoded
    // graphics assets (palettes, tiles, sprites) of every chip to `<base>.*`
    // PNG/JSON files, and exits. Only `--extract-assets <base>` is required; a
    // missing or empty base disables the path.
    struct extract_assets_request final {
        std::string base;
        std::uint64_t frames{};
    };

    [[nodiscard]] std::optional<extract_assets_request> parse_extract_assets_args(int argc,
                                                                                  char* argv[]);

    // --extract-audio <base> [--extract-frames N]: the audio analogue of
    // --extract-assets. Headless run that advances N frames (default 0) then
    // writes every chip's PCM samples to `<base>.*` WAV/JSON files, and exits.
    // Only `--extract-audio <base>` is required; a missing or empty base
    // disables the path.
    struct extract_audio_request final {
        std::string base;
        std::uint64_t frames{};
    };

    [[nodiscard]] std::optional<extract_audio_request> parse_extract_audio_args(int argc,
                                                                                char* argv[]);

    // Frames a scripted button is held when `--press <button>@<frame>` gives no
    // explicit `+duration` (long enough for a 60 Hz poll to catch the press).
    inline constexpr std::uint64_t press_default_duration = 4U;

    // A scripted controller input for the headless path: hold `button` on port 1
    // for `duration` frames starting at the 1-based `frame`. Parsed from
    // `--press <button>@<frame>[+duration]` (e.g. `--press start@60+12`).
    struct press_event final {
        std::string button;
        std::uint64_t frame{};
        std::uint64_t duration{press_default_duration};
    };

    // Every `--press <button>@<frame>[+duration]` event, in command-line order.
    // Recognised buttons: up/down/left/right/start/select/a/b/c/x/y/z/mode.
    // Specs with an unknown button or unparseable frame are skipped.
    [[nodiscard]] std::vector<press_event> parse_press_events(int argc, char* argv[]);

    // The port-1 controller_state implied by `events` at the given 1-based frame:
    // every event whose [frame, frame+duration) window contains `frame` sets its
    // button. No events / no match -> all-released.
    [[nodiscard]] mnemos::peripheral::controller_state
    input_for_frame(const std::vector<press_event>& events, std::uint64_t frame) noexcept;

} // namespace mnemos::apps::player::adapters
