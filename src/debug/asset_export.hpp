#pragma once

// System-agnostic graphics ASSET extraction over an assembled player_system.
// Walks `sys.chips()` and, for every chip that exposes an `asset_source`
// (introspection().assets()), resolves its decoded palettes + indexed graphics
// to RGB and writes them as PNGs, plus a JSON manifest describing them. Like
// debug_dump, this knows nothing about which system is loaded -- a new system
// gains asset export by implementing asset_source on its video chip.

#include "player_system.hpp"

#include <cstddef>
#include <string>

namespace mnemos::debug {

    // For each chip in `sys.chips()` exposing an asset_source:
    //   - each palette      -> <base>.<chip>.pal.<name>.png   (a swatch strip)
    //   - each graphic asset -> <base>.<chip>.<kind>.<name>.png (resolved RGB)
    // and writes one <base>.assets.json manifest listing every chip's palettes
    // and assets (name, kind, dimensions, cell grid, palette ref, source
    // address, and the written filename). <chip> is the chip's part_number
    // sanitized to a path-safe segment.
    //
    // Returns the number of PNG files successfully written. A PNG that fails to
    // write is reported on stderr and skipped (it is omitted from the count but
    // still listed in the manifest). The manifest is always written, even when
    // no chip exposes graphics (an empty chip list).
    std::size_t export_assets(const frontend_sdk::player_system& sys, const std::string& base_path);

} // namespace mnemos::debug
