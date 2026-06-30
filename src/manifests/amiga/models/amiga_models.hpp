#pragma once

#include "chipsets/amiga_chipsets.hpp"
#include "region.hpp"

#include <cstddef>
#include <cstdint>

namespace mnemos::manifests::amiga {

    enum class amiga_keyboard_layout : std::uint8_t { us, qwerty_international, german, azerty };

    enum class amiga_model : std::uint8_t {
        amiga500,
        amiga500_plus,
        amiga600,
        amiga2000,
        amiga2000_ecs_1m
    };

    struct amiga_config final {
        mnemos::video_region video_region{mnemos::video_region::pal};
        amiga_keyboard_layout keyboard_layout{amiga_keyboard_layout::us};
        amiga_model model{amiga_model::amiga500};
        std::size_t fast_ram_size{};
    };

    struct amiga_model_descriptor final {
        amiga_model model{amiga_model::amiga500};
        const char* id{"amiga500"};
        const char* display_name{"Amiga 500"};
        amiga_chipset chipset{amiga_chipset::ocs};
        std::size_t chip_ram_size{};
        bool zorro2_expansion_bus{};
        bool fast_ram_configurable{};
    };

    [[nodiscard]] const amiga_model_descriptor& amiga_model_profile(amiga_model model) noexcept;

    [[nodiscard]] std::size_t amiga_fast_ram_size_for_config(const amiga_config& config,
                                                             std::size_t max_size) noexcept;

} // namespace mnemos::manifests::amiga
