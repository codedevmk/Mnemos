#pragma once

// Checked-in Irem M78 game manifests embedded for corpus tooling lookup. This
// module is a ROM-contract surface only: Mnemos still lacks the title-specific
// dual-Z80 board profile, video-register model, communications, and no-dump
// M72-audio sample ROM evidence needed for an executable BJ92 route.

#include "m78_embedded_game_manifests.hpp"

#include <cstddef>
#include <string_view>

namespace mnemos::manifests::irem_m78 {

    inline constexpr std::size_t main_rom_size = 0x010000U;
    inline constexpr std::size_t audio_rom_size = 0x010000U;
    inline constexpr std::size_t tiles_rom_size = 0x030000U;
    inline constexpr std::size_t tiles2_rom_size = 0x030000U;
    inline constexpr std::size_t m72_audio_rom_size = 0x040000U;
    inline constexpr std::size_t proms_size = 0x000200U;

    [[nodiscard]] constexpr std::string_view game_manifest_toml(std::string_view set_name) {
        for (const auto& [name, toml] : embedded::game_manifests) {
            if (name == set_name) {
                return toml;
            }
        }
        return {};
    }

} // namespace mnemos::manifests::irem_m78
