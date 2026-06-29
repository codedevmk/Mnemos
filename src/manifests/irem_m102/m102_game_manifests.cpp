#include "m102_game_manifests.hpp"

namespace mnemos::manifests::irem_m102 {
    static_assert(main_rom_size == 0x020000U);
    static_assert(ga20_rom_size == 0x100000U);
    static_assert(pld_region_size == 0x000200U);
} // namespace mnemos::manifests::irem_m102
