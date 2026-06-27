#pragma once

// Checked-in Irem M62 manifests currently provide raw-media ROM contracts for
// the local corpus. Exact Z80/M6803 bus placement, KNA video behavior, and
// mature Irem Audio scheduling remain board-family implementation gaps.

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
