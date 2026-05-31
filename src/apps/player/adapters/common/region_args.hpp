#pragma once

#include "region.hpp" // mnemos::video_region

namespace mnemos::apps::player::adapters {

    // CLI --region override. `auto_detect` falls back to whatever the cart's
    // header bytes report; `ntsc` / `pal` win unconditionally.
    enum class region_override : std::uint8_t {
        auto_detect,
        ntsc,
        pal,
    };

    // Scan argv for `--region <ntsc|pal>` and return the corresponding
    // override. Anything else (including no flag, an unknown value, or a
    // missing argument) leaves the override at `auto_detect`. Case-insensitive
    // for the canonical spellings `ntsc` / `pal`.
    [[nodiscard]] region_override parse_region_arg(int argc, char* argv[]);

    // Apply a `--region` override on top of a cart-default video region.
    [[nodiscard]] mnemos::video_region
    resolve_video_region(region_override ov, mnemos::video_region cart_default) noexcept;

    // Short label describing where the active region came from. Used by the
    // player's startup banner ("auto-detected" / "explicit --region").
    [[nodiscard]] const char* region_source_label(region_override ov) noexcept;

} // namespace mnemos::apps::player::adapters
