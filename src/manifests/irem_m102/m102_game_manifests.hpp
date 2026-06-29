#pragma once

// Checked-in Irem M102 game manifests embedded for corpus tooling lookup. This
// module is a ROM-contract surface only: Mnemos still lacks the sparse
// electromechanical medal-game board, mechanical/artwork runtime, and
// title-specific I/O needed for an executable Hill Climber route.

#include "m102_embedded_game_manifests.hpp"

#include <cstddef>
#include <string_view>

namespace mnemos::manifests::irem_m102 {

    inline constexpr std::size_t main_rom_size = 0x020000U;
    inline constexpr std::size_t ga20_rom_size = 0x100000U;
    inline constexpr std::size_t pld_region_size = 0x000200U;

    [[nodiscard]] constexpr std::string_view game_manifest_toml(std::string_view set_name) {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m102
