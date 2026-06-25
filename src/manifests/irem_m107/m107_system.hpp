#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mnemos::manifests::irem_m107 {

    // M107 contracts are ROM-layout proof only until the executable M107 board
    // profile lands. Keep them separate from M72/M81/M82 so Fire Barrel-family
    // sets cannot be misreported as an older board wiring.
    inline constexpr std::size_t main_rom_size = 0x100000U;
    inline constexpr std::size_t sound_rom_size = 0x020000U;

    struct m107_board_params final {
        std::uint16_t dip_default{0xFFFFU};
        std::string_view rom_layout{"standard"};
    };

    [[nodiscard]] m107_board_params board_params_for(std::string_view set_name) noexcept;

} // namespace mnemos::manifests::irem_m107
