#pragma once

// Checked-in Irem M107 game manifests embedded for player/tooling lookup. The
// generated embedded header is built from src/manifests/irem_m107/games/*.toml,
// keeping runtime resolution independent of loose game.toml sidecars.

#include "m107_embedded_game_manifests.hpp"

#include <string_view>

namespace mnemos::manifests::irem_m107 {

    [[nodiscard]] constexpr std::string_view game_manifest_toml(std::string_view set_name) {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m107
