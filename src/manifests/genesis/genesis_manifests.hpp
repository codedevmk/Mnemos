#pragma once

// Single source of truth for the Genesis system manifests at runtime.
//
// The canonical descriptions live in the on-disk .toml files beside this
// header; CMake embeds each one verbatim into genesis_embedded_manifests.hpp (a
// generated header) so the player carries them with no filesystem dependency
// and tests share the exact same bytes (no hand-copied, drift-prone duplicates).

#include "region.hpp" // mnemos::video_region

#include "genesis_embedded_manifests.hpp" // generated; see the .hpp.in

#include <string_view>

namespace mnemos::manifests::genesis {

    [[nodiscard]] constexpr std::string_view manifest_toml(mnemos::video_region region) noexcept {
        return region == mnemos::video_region::pal ? embedded::genesis_pal_toml
                                                   : embedded::genesis_ntsc_toml;
    }

} // namespace mnemos::manifests::genesis
