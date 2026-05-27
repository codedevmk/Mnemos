#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace mnemos::apps::player::adapters {

    // Scan argv for `--rom <path>` (or `-r <path>`) and return the path.
    [[nodiscard]] std::optional<std::string> parse_rom_arg(int argc, char* argv[]);

    // --screenshot <path.ppm> --frames N: headless run, dump the resulting
    // VDP framebuffer as PPM, exit. Both flags must be present together; a
    // missing or unparseable value disables the headless path.
    struct screenshot_request final {
        std::string path;
        std::uint64_t frames{};
    };

    [[nodiscard]] std::optional<screenshot_request> parse_screenshot_args(int argc, char* argv[]);

} // namespace mnemos::apps::player::adapters
