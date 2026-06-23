#include "nes_mapper.hpp"

#include "nes_mapper_bandai.hpp"
#include "nes_mapper_discrete.hpp"
#include "nes_mapper_jaleco.hpp"
#include "nes_mapper_konami.hpp"
#include "nes_mapper_mmc1.hpp"
#include "nes_mapper_mmc2_4.hpp"
#include "nes_mapper_mmc3.hpp"
#include "nes_mapper_mmc5.hpp"
#include "nes_mapper_namco.hpp"
#include "nes_mapper_sunsoft.hpp"
#include "nes_mapper_taito.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace mnemos::manifests::nes {

    std::unique_ptr<nes_mapper> make_mapper(int number, topology::bus& bus,
                                            chips::video::ppu2c02& ppu,
                                            std::span<const std::uint8_t> prg,
                                            std::span<std::uint8_t> chr, bool chr_is_ram) {
        const nes_mapper_build_context context{bus, ppu, prg, chr, chr_is_ram};
        switch (number) {
        case 1:
            return make_mmc1_mapper(context);
        case 2:
            return make_uxrom_mapper(context);
        case 3:
            return make_cnrom_mapper(context);
        case 4:
            return make_mmc3_mapper(context);
        case 5:
            return make_mmc5_mapper(context);
        case 7:
            return make_axrom_mapper(context);
        case 9: // MMC2 (Punch-Out!!)
            return make_mmc2_mapper(context);
        case 10: // MMC4 (Fire Emblem, Famicom Wars)
            return make_mmc4_mapper(context);
        case 11:
            return make_color_dreams_mapper(context);
        case 16: // Bandai FCG / LZ93D50
            return make_bandai_fcg_mapper(context);
        case 18: // Jaleco SS88006
            return make_jaleco_ss88006_mapper(context);
        case 33: // Taito TC0190 (no IRQ)
            return make_taito_tc0190_mapper(context);
        case 48: // Taito TC0690 (TC0190 + MMC3-style scanline IRQ)
            return make_taito_tc0690_mapper(context);
        case 19:
            return make_namco163_mapper(context);
        case 34: // BNROM (CHR-RAM) / NINA-001 (CHR-ROM)
            return make_bnrom_nina_mapper(context);
        case 21: // VRC4a/c
        case 22: // VRC2a
        case 23: // VRC2b / VRC4e/f
        case 25: // VRC2c / VRC4b/d
            return make_vrc2_4_mapper(context, number);
        case 24: // VRC6a
            return make_vrc6_mapper(context, false);
        case 26: // VRC6b (A0/A1 swapped)
            return make_vrc6_mapper(context, true);
        case 64: // RAMBO-1 / Tengen 800032
            return make_rambo1_mapper(context);
        case 66:
            return make_gxrom_mapper(context);
        case 67: // Sunsoft-3
            return make_sunsoft3_mapper(context);
        case 68:
            return make_sunsoft4_mapper(context);
        case 69:
            return make_sunsoft5b_mapper(context);
        case 71:
            return make_camerica_mapper(context);
        case 73:
            return make_vrc3_mapper(context);
        case 75:
            return make_vrc1_mapper(context);
        case 85:
            return make_vrc7_mapper(context);
        case 206:
            return make_namco118_mapper(context);
        case 0:
        default:
            return make_nrom_mapper(context);
        }
    }

} // namespace mnemos::manifests::nes
