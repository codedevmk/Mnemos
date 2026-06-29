#pragma once

// Checked-in Irem M119 game manifests embedded for corpus tooling lookup. This
// module is a ROM-contract surface only: Mnemos still lacks the SH-3-class CPU,
// UPD94244-210 VDP, and YMZ280B devices needed for an executable M119 route.

#include "m119_embedded_game_manifests.hpp"

#include <cstddef>
#include <string_view>

namespace mnemos::manifests::irem_m119 {

    inline constexpr std::size_t main_rom_size = 0x080000U;
    inline constexpr std::size_t vdp_rom_size = 0x400000U;
    inline constexpr std::size_t ymz_rom_size = 0x200000U;

    [[nodiscard]] constexpr std::string_view game_manifest_toml(std::string_view set_name) {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m119
