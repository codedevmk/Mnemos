#pragma once

// Checked-in Irem M15 game manifests embedded for corpus tooling lookup. The
// generated embedded header is built from src/manifests/irem_m15/games/*.toml.

#include "m15_embedded_game_manifests.hpp"

#include <string_view>

namespace mnemos::manifests::irem_m15 {

    [[nodiscard]] constexpr std::string_view game_manifest_toml(std::string_view set_name) {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m15
