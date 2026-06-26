#pragma once

// Checked-in Irem M90 game manifests embedded for corpus tooling lookup. The
// companion system target provides a first-pass executable V35/Z80/YM2151/DAC
// shell; GA25 video and complete M90/M97/M99 roster parity remain gaps.

#include "m90_embedded_game_manifests.hpp"

#include <cstddef>
#include <string_view>

namespace mnemos::manifests::irem_m90 {

    inline constexpr std::size_t main_rom_size = 0x100000U;
    inline constexpr std::size_t sound_rom_size = 0x010000U;
    inline constexpr std::size_t sample_rom_size = 0x020000U;

    [[nodiscard]] constexpr std::string_view game_manifest_toml(std::string_view set_name) {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m90
