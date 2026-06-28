#pragma once

// Checked-in Irem M58 manifests currently provide ROM contracts for the local
// 10-Yard Fight corpus. Exact Z80 bus timing, video, palette, radar layer, and
// M52-lineage audio behavior remain board-family implementation gaps.

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
