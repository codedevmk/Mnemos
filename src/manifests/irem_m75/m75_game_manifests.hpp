#pragma once

// Checked-in Irem M75 game manifests embedded for corpus tooling lookup. The
// companion system target provides a first-pass executable Z80/Z80/YM2151/DAC
// shell for Vigilante; authentic tile/sprite priority remains a parity gap.

#include "m75_embedded_game_manifests.hpp"

#include <cstddef>
#include <string_view>

namespace mnemos::manifests::irem_m75 {

    inline constexpr std::size_t main_rom_size = 0x030000U;
    inline constexpr std::size_t sound_rom_size = 0x010000U;
    inline constexpr std::size_t char_gfx_size = 0x020000U;
    inline constexpr std::size_t sprite_gfx_size = 0x080000U;
    inline constexpr std::size_t bg_tile_gfx_size = 0x040000U;
    inline constexpr std::size_t sample_rom_size = 0x010000U;
    inline constexpr std::size_t proms_size = 0x000100U;
    inline constexpr std::size_t plds_size = 0x000600U;

    [[nodiscard]] constexpr std::string_view game_manifest_toml(std::string_view set_name) {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m75
