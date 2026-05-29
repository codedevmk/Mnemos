#pragma once

// Single source of truth for the C64 system manifests at runtime. CMake embeds
// the canonical .toml files (see c64_embedded_manifests.hpp.in) so the player +
// tests share the exact same bytes.

#include "c64_system.hpp" // c64_config

#include "c64_embedded_manifests.hpp" // generated

#include <string_view>

namespace mnemos::manifests::c64 {

    [[nodiscard]] constexpr std::string_view manifest_toml(c64_config::region region) noexcept {
        return region == c64_config::region::ntsc ? embedded::c64_ntsc_toml
                                                  : embedded::c64_pal_toml;
    }

} // namespace mnemos::manifests::c64
