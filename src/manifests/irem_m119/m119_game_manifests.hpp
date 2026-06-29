#pragma once

// Checked-in Irem M119 game manifests embedded for corpus tooling lookup. The
// executable route is first-pass: SH7708/SH-3, uPD94244-210, and YMZ280B are
// explicit Mnemos chips, while graphics/sound/timing authenticity is still
// tracked separately from this ROM contract.

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
