#pragma once

// Checked-in Irem M27 manifests currently provide ROM contracts for the local
// corpus. Exact M27 CPU timing, video, color, inputs, and sound behavior remain
// board-family implementation gaps.

#include "m27_embedded_game_manifests.hpp"

#include <string_view>

namespace mnemos::manifests::irem_m27 {

    [[nodiscard]] constexpr std::string_view
    game_manifest_toml(std::string_view set_name) noexcept {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m27
