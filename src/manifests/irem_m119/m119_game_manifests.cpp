#include "m119_game_manifests.hpp"

namespace mnemos::manifests::irem_m119 {
    static_assert(main_rom_size == 0x080000U);
    static_assert(vdp_rom_size == 0x400000U);
    static_assert(ymz_rom_size == 0x200000U);
} // namespace mnemos::manifests::irem_m119
