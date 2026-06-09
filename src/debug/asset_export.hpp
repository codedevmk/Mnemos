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

    // For each chip in `sys.chips()` exposing an asset_source and/or debug_layers:
    //   - each palette      -> <base>.<chip>.pal.<name>.png   (a swatch strip)
    //                          <base>.<chip>.pal.<name>.pal   (JASC-PAL)
    //   - each graphic asset -> <base>.<chip>.<kind>.<name>.png     (resolved RGB)
    //                          <base>.<chip>.<kind>.<name>.idx.png (palette-indexed)
    //   - each debug_layer   -> <base>.<chip>.layer.<name>.png (a composed RGB
    //                          scene -- a full plane/nametable, palette-resolved)
    // and writes one <base>.assets.json manifest listing every chip's palettes,
    // assets, and layers (name, kind, dimensions, cell grid, palette ref, source
    // address, and the written filenames). <chip> is the chip's part_number
    // sanitized to a path-safe segment. The indexed PNG is lossless (raw indices
    // + PLTE/tRNS) for tools that re-tint; the resolved PNG is for quick viewing.
    //
    // Returns the number of PNG files successfully written (swatch + resolved +
    // indexed). A file that fails to write is reported on stderr and skipped (it
    // is omitted from the count but still listed in the manifest). The manifest
    // is always written, even when no chip exposes graphics (an empty chip list).
    std::size_t export_assets(const frontend_sdk::player_system& sys, const std::string& base_path);

} // namespace mnemos::debug
