#pragma once

// Checked-in Irem M63 manifests provide ROM contracts for the local corpus and
// feed the first-pass Wily Tower player route. Exact Z80/8039 bus timing,
// video/color PROM behavior, and sound behavior remain board-family gaps.

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
