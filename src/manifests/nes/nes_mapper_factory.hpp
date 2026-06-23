#pragma once

#include "nes_mapper.hpp"

#include <cstdint>
#include <span>

namespace mnemos::manifests::nes {

    struct nes_mapper_build_context {
        topology::bus& bus;
        chips::video::ppu2c02& ppu;
        std::span<const std::uint8_t> prg;
        std::span<std::uint8_t> chr;
        bool chr_is_ram;
    };

} // namespace mnemos::manifests::nes
