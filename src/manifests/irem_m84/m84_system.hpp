#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mnemos::manifests::irem_m84 {

    // M84 coverage is currently a ROM-contract layer for Hammerin' Harry split
    // sets in the local corpus. Executable video/sound/IO timing remains open.
    inline constexpr std::size_t main_rom_size = 0x100000U;
    inline constexpr std::size_t hharryb_plds_size = 0x0104U;
    inline constexpr std::size_t hharryu_plds_size = 0x0345U;

    struct m84_board_params final {
        std::uint16_t dip_default{0xFDBFU};
        std::string_view rom_layout{"standard"};
    };

    [[nodiscard]] m84_board_params board_params_for(std::string_view set_name) noexcept;

} // namespace mnemos::manifests::irem_m84
