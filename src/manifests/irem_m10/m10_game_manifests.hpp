#pragma once

// Checked-in Irem M10/M11 game manifests embedded for corpus tooling lookup.
// The executable profile uses the native Intel 8085/8080 core with first-pass
// board video/audio pending final discrete color, sound, and raster proof.

#include "m10_embedded_game_manifests.hpp"
#include "m10_rom_layout.hpp"

#include <string_view>

namespace mnemos::manifests::irem_m10 {

    [[nodiscard]] constexpr std::string_view game_manifest_toml(std::string_view set_name) {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m10
