#pragma once

// Checked-in Irem M57 manifests currently provide raw-media ROM contracts for
// the local corpus plus a first-pass player smoke route. Exact Z80 bus
// placement, video behavior, and Irem Audio scheduling remain board-family
// implementation gaps.

#include "m57_embedded_game_manifests.hpp"

#include <string_view>

namespace mnemos::manifests::irem_m57 {

    [[nodiscard]] constexpr std::string_view
    game_manifest_toml(std::string_view set_name) noexcept {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m57
