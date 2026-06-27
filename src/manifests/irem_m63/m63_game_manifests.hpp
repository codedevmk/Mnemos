#pragma once

// Checked-in Irem M63 manifests currently provide ROM contracts for the local
// corpus. Exact Z80/8039 bus timing, video, color, and sound behavior remain
// board-family implementation gaps.

#include "m63_embedded_game_manifests.hpp"

#include <string_view>

namespace mnemos::manifests::irem_m63 {

    [[nodiscard]] constexpr std::string_view
    game_manifest_toml(std::string_view set_name) noexcept {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m63
