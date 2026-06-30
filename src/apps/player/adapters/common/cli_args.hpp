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
    // by the family adapter (the SMS adapter accepts sega/codemasters/korean;
    // the MSX adapter accepts ascii8/ascii8-sram8/ascii16/ascii16-sram2/
    // konami/konami-scc/korean-msx/korean-msx-nemesis); an unrecognised name
    // leaves the adapter on auto-detect.
    [[nodiscard]] std::optional<std::string> parse_mapper_arg(int argc, char* argv[]);

    // Same mapper vocabulary as `--mapper`, but reserved for a system's second
    // cartridge/media slot. MSX and MSX2 use this for cartridge slot 2.
    [[nodiscard]] std::optional<std::string> parse_mapper2_arg(int argc, char* argv[]);

    // Enable an optional FM expansion where the selected system exposes one:
    // SMS YM2413 or MSX-MUSIC/FM-PAC via `--fm`. Other systems ignore the flag.
    [[nodiscard]] bool parse_fm_unit_arg(int argc, char* argv[]);

    // Plug a light gun into the selected system's gun port (`--light-gun`,
    // applied to port index 1). Other systems ignore the flag.
    [[nodiscard]] bool parse_light_gun_arg(int argc, char* argv[]);

    // Plug a 4-player multitap into the selected system's ports (`--four-score`, the
    // NES Four Score). Other systems ignore the flag.
    [[nodiscard]] bool parse_four_score_arg(int argc, char* argv[]);

    // Enable optional battery-backed RTC hardware where the selected system has
    // one (`--rtc`; currently MSX RP-5C01). Other systems ignore the flag.
    [[nodiscard]] bool parse_rtc_arg(int argc, char* argv[]);

    // Select MSX2-class video hardware (`--msx2`; V9938). Other systems ignore
    // the flag.
    [[nodiscard]] bool parse_msx2_arg(int argc, char* argv[]);

    // Scan argv for `--dip <value>` (hex with 0x prefix, or decimal) and
    // return the 16-bit DIP bank, or nullopt when absent/malformed.
    [[nodiscard]] std::optional<std::uint16_t> parse_dip_arg(int argc, char* argv[]);

    // --screenshot <path> [--frames N]: headless run, dump the resulting
    // framebuffer (a PNG when `path` ends in .png, otherwise a PPM), exit.
    // Missing --frames captures the boot frame; the path must be present and must
    // not be another option.

    // Select the physical keyboard layout token used by computer adapters that
    // expose a raw keyboard matrix (`--keyboard-layout <token>`). Missing,
    // empty, or option-shaped values leave the adapter default in place.
    [[nodiscard]] std::optional<std::string> parse_keyboard_layout_arg(int argc, char* argv[]);

    // Select an Amiga machine configuration token (`--amiga-model <token>`).
    // Currently consumed by `--system amiga2000`: base/ocs keeps the baseline
    // 512 KiB OCS setup, ecs-1m/ks2 selects the common ECS + 1 MiB upgrade.
    [[nodiscard]] std::optional<std::string> parse_amiga_model_arg(int argc, char* argv[]);

    struct screenshot_request final {
        std::string path;
        std::uint64_t frames{};
    };

    [[nodiscard]] std::optional<screenshot_request> parse_screenshot_args(int argc, char* argv[]);

    // --save-state <path> [--frames N]: headless run, advances N frames
    // (default 0), writes the raw Mnemos save-state container, and exits.
    // The path must be present and must not be another option.
    struct save_state_request final {
        std::string path;
        std::uint64_t frames{};
    };

    [[nodiscard]] std::optional<save_state_request> parse_save_state_args(int argc, char* argv[]);

    // --dump-battery <path> [--frames N]: headless run, advances N frames
    // (default 0), writes the live battery-backed media image, and exits. The
    // path must be present and must not be another option.
    struct dump_battery_request final {
        std::string path;
        std::uint64_t frames{};
    };

    [[nodiscard]] std::optional<dump_battery_request> parse_dump_battery_args(int argc,
                                                                              char* argv[]);

    // --load-state <path>: restore a raw Mnemos save-state container before
    // running a headless operation or entering the windowed player.
    [[nodiscard]] std::optional<std::string> parse_load_state_arg(int argc, char* argv[]);

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

    // --capabilities: headless frontend query. Loads the requested system/media,
    // prints a deterministic capability/control summary, and exits.
    [[nodiscard]] bool parse_capabilities_arg(int argc, char* argv[]);

    // Validate headless command syntax before the player launches a system or
    // opens SDL. Returns the first user-facing error, or nullopt when all
    // present headless flags are well-formed.
    [[nodiscard]] std::optional<std::string> validate_headless_request_args(int argc,
                                                                            char* argv[]);

    // --help / -h: print the usage screen and exit before loading any media.
    // A bare presence flag -- it consumes no value and is recognised wherever it
    // appears on the command line.
    [[nodiscard]] bool parse_help_arg(int argc, char* argv[]);

    enum class animation_record_format : std::uint8_t {
        gif,
        movie_frames,
    };

    // --record-gif <path.gif> --frames N: headless run that records N frames as
    // an animated GIF. --record-movie <base> --frames N writes a PNG frame
    // sequence and `<base>.movie.json` for an external video plugin/encoder.
    // Both forms require a positive frame count; a missing or option-shaped
    // output disables the path.
    struct animation_record_request final {
        std::string output;
        std::uint64_t frames{};
        animation_record_format format{animation_record_format::gif};
    };

    [[nodiscard]] std::optional<animation_record_request> parse_animation_record_args(int argc,
                                                                                      char* argv[]);

    // Frames a scripted button is held when `--press <button>@<frame>` gives no
    // explicit `+duration` (long enough for a 60 Hz poll to catch the press).
    inline constexpr std::uint64_t press_default_duration = 4U;

    // A scripted controller input for the headless path: hold `button` on a
    // zero-based input port for `duration` frames starting at the 1-based
    // `frame`. Parsed from `--press [pN:]<button>@<frame>[+duration]`
    // (e.g. `--press start@60+12`, `--press p2:select@120+4`), or
    // `--press [pN:]paddle=<value>@<frame>[+duration]` for an analog rotary
    // position.
    struct press_event final {
        std::uint32_t port_index{};
        std::string button;
        std::uint64_t frame{};
        std::uint64_t duration{press_default_duration};
        std::optional<std::uint16_t> paddle_value;
    };

    // Every `--press [pN:]<button>@<frame>[+duration]` event, in command-line
    // order. Recognised buttons: up/down/left/right/start/select/a/b/c/x/y/z/mode,
    // plus arcade service/test inputs and paddle=<0..65535> rotary values.
    // Specs with an unknown button, bad port, or unparseable frame are skipped.
    [[nodiscard]] std::vector<press_event> parse_press_events(int argc, char* argv[]);

    // The controller_state implied by `events` for `port_index` at the given
    // 1-based frame: every event whose [frame, frame+duration) window contains
    // `frame` sets its button. No events / no match -> all-released.
    [[nodiscard]] mnemos::peripheral::controller_state
    input_for_frame(const std::vector<press_event>& events, std::uint64_t frame,
                    std::uint32_t port_index = 0) noexcept;

} // namespace mnemos::apps::player::adapters
