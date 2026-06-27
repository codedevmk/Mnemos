#pragma once

// Checked-in Irem M14 manifests currently provide ROM contracts for the local
// corpus. Exact 8085 bus timing, video, color, and sound behavior remain
// board-family implementation gaps.

#include "m14_embedded_game_manifests.hpp"

#include <string_view>

namespace mnemos::manifests::irem_m14 {

    [[nodiscard]] constexpr std::string_view
    game_manifest_toml(std::string_view set_name) noexcept {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m14
