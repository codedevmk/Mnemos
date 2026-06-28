#pragma once

// Checked-in Irem M62 manifests provide the local corpus contracts plus a
// first-pass player smoke route. Lode Runner now carries explicit board regions;
// the remaining sets still use raw-media staging contracts until each family is
// split and proven. Exact KNA video behavior and mature Irem Audio scheduling
// remain board-family implementation gaps.

#include "m62_embedded_game_manifests.hpp"

#include <string_view>

namespace mnemos::manifests::irem_m62 {

    [[nodiscard]] constexpr std::string_view
    game_manifest_toml(std::string_view set_name) noexcept {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m62
