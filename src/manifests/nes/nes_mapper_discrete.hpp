#pragma once

#include "nes_mapper.hpp"

#include <cstdint>
#include <memory>
#include <span>

namespace mnemos::manifests::nes {

    struct nes_mapper_build_context {
        topology::bus& bus;
        chips::video::ppu2c02& ppu;
        std::span<const std::uint8_t> prg;
        std::span<std::uint8_t> chr;
        bool chr_is_ram;
    };

    [[nodiscard]] std::unique_ptr<nes_mapper> make_nrom_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper> make_uxrom_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper> make_cnrom_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper> make_axrom_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper> make_gxrom_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_color_dreams_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_camerica_mapper(nes_mapper_build_context context);
    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_bnrom_nina_mapper(nes_mapper_build_context context);

} // namespace mnemos::manifests::nes
