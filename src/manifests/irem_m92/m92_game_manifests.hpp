#pragma once

// Checked-in Irem M92 game manifests embedded for corpus tooling lookup. The
// companion system target provides a first-pass executable V33/V35/GA20 shell;
// encrypted V35 programs and GA21/GA22 video remain authenticity gaps.

#include "m92_embedded_game_manifests.hpp"

#include <cstddef>
#include <string_view>

namespace mnemos::manifests::irem_m92 {

    inline constexpr std::size_t main_rom_size = 0x100000U;
    inline constexpr std::size_t sound_rom_size = 0x020000U;
    inline constexpr std::size_t plds_rom_size = 0x000C00U;

    [[nodiscard]] constexpr std::string_view game_manifest_toml(std::string_view set_name) {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m92
