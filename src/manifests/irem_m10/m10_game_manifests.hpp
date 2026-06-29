#pragma once

// Checked-in Irem M10/M11 game manifests embedded for corpus tooling lookup.
// This module is a ROM-contract surface only: Mnemos does not yet have the
// early M10/M11 8080-class CPU, discrete sound, or color PROM board profile.

#include "m10_embedded_game_manifests.hpp"

#include <cstddef>
#include <string_view>

namespace mnemos::manifests::irem_m10 {

    inline constexpr std::size_t main_rom_size = 0x10000U;
    inline constexpr std::size_t tiles_rom_size = 0x0800U;

    [[nodiscard]] constexpr std::string_view game_manifest_toml(std::string_view set_name) {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m10
