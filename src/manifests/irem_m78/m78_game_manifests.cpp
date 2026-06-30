#include "m78_game_manifests.hpp"

namespace mnemos::manifests::irem_m78 {
    static_assert(main_rom_size == 0x010000U);
    static_assert(audio_rom_size == 0x010000U);
    static_assert(tiles_rom_size == 0x030000U);
    static_assert(tiles2_rom_size == 0x030000U);
    static_assert(m72_audio_rom_size == 0x040000U);
    static_assert(proms_size == 0x000200U);
} // namespace mnemos::manifests::irem_m78
