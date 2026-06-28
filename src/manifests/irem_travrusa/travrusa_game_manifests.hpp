#pragma once

// Checked-in Irem Traverse USA/Zippy Race manifests currently provide ROM-set
// contracts only. The Z80 board route, encrypted MotoRace handling, video,
// color, input, and Irem Audio behavior remain implementation gaps.

#include "travrusa_embedded_game_manifests.hpp"

#include <string_view>

namespace mnemos::manifests::irem_travrusa {

    [[nodiscard]] constexpr std::string_view
    game_manifest_toml(std::string_view set_name) noexcept {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_travrusa
