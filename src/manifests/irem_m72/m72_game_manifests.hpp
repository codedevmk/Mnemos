#pragma once

// Runtime access to the canonical checked-in Irem M72 game manifests. The
// generated embedded header is built from src/manifests/irem_m72/games/*.toml,
// keeping the player independent from the source tree while still sharing the
// exact same declarations validated by the manifest tests.

#include "m72_embedded_game_manifests.hpp"

#include <string_view>

namespace mnemos::manifests::irem_m72 {

    [[nodiscard]] constexpr std::string_view
    game_manifest_toml(std::string_view set_name) noexcept {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m72
