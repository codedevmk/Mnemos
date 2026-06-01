#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters {

    // Scan argv for `--rom <path>` (or `-r <path>`) and return the path.
    [[nodiscard]] std::optional<std::string> parse_rom_arg(int argc, char* argv[]);

    // All media paths in command-line order: every `--rom`/`-r`/`--disk <path>`.
    // The first is the primary image (it selects the system); the rest are
    // additional media -- e.g. the further disks of a multi-disk C64 game,
    // swappable at runtime. Empty when no media flag is present.
    [[nodiscard]] std::vector<std::string> parse_rom_args(int argc, char* argv[]);

    // Whether autostart is disabled via `--no-autostart`. Autostart is on by
    // default (a disk/tape C64 types LOAD/RUN itself), so this returns true only
    // when the user opts out to drop to a bare prompt.
    [[nodiscard]] bool parse_no_autostart(int argc, char* argv[]);

    // --screenshot <path.ppm> --frames N: headless run, dump the resulting
    // VDP framebuffer as PPM, exit. Both flags must be present together; a
    // missing or unparseable value disables the headless path.
    struct screenshot_request final {
        std::string path;
        std::uint64_t frames{};
    };

    [[nodiscard]] std::optional<screenshot_request> parse_screenshot_args(int argc, char* argv[]);

} // namespace mnemos::apps::player::adapters
