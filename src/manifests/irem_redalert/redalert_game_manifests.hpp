#pragma once

// Checked-in Irem Red Alert-family game manifests embedded for corpus tooling
// lookup. This module is a ROM-contract surface only: Mnemos still lacks the
// Red Alert/WW III board profile, video/audio timing, inputs, and player route.

#include "redalert_embedded_game_manifests.hpp"

#include <cstddef>
#include <string_view>

namespace mnemos::manifests::irem_redalert {

    inline constexpr std::size_t main_rom_size = 0x10000U;
    inline constexpr std::size_t audio_rom_size = 0x10000U;
    inline constexpr std::size_t proms_size = 0x0200U;

    [[nodiscard]] constexpr std::string_view game_manifest_toml(std::string_view set_name) {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_redalert
