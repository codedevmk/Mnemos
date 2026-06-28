#pragma once

// Checked-in Irem M58 manifests provide the ROM contracts used by the first-pass
// 10-Yard Fight board route. Exact Z80 timing, video priority/color, DIP
// behavior, and audio parity remain board-family implementation gaps.

#include "m58_embedded_game_manifests.hpp"

#include <string_view>

namespace mnemos::manifests::irem_m58 {

    [[nodiscard]] constexpr std::string_view
    game_manifest_toml(std::string_view set_name) noexcept {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m58
