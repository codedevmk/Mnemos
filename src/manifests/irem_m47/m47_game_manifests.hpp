#pragma once

// Checked-in Irem M47 manifests provide ROM contracts for the local corpus.
// First-pass board execution is wired, while exact Z80 bus timing, video,
// color, input, and sound behavior remain board-family implementation gaps.

#include "m47_embedded_game_manifests.hpp"

#include <string_view>

namespace mnemos::manifests::irem_m47 {

    [[nodiscard]] constexpr std::string_view
    game_manifest_toml(std::string_view set_name) noexcept {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m47
