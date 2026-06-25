#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mnemos::manifests::irem_m15 {

    // M15 coverage is currently a ROM-contract layer for the local Head On
    // corpus. The executable i8080-era board profile remains separate work.
    inline constexpr std::size_t main_rom_size = 0x2000U;
    inline constexpr std::size_t program_rom_size = 0x0400U;

    struct m15_board_params final {
        std::uint32_t cpu_clock_hz{1'996'800U};
        std::string_view rom_layout{"standard"};
    };

    [[nodiscard]] m15_board_params board_params_for(std::string_view set_name) noexcept;

} // namespace mnemos::manifests::irem_m15
