#include "models/amiga_models.hpp"

#include "amiga_memory_sizes.hpp"

#include <algorithm>
#include <array>

namespace mnemos::manifests::amiga {

    namespace {
        constexpr std::array<amiga_model_descriptor, 5U> model_profiles{{
            amiga_model_descriptor{
                .model = amiga_model::amiga500,
                .id = "amiga500",
                .display_name = "Amiga 500",
                .chipset = amiga_chipset::ocs,
                .chip_ram_size = amiga_size_512k,
                .zorro2_expansion_bus = false,
                .fast_ram_configurable = false,
            },
            amiga_model_descriptor{
                .model = amiga_model::amiga500_plus,
                .id = "amiga500plus",
                .display_name = "Amiga 500+",
                .chipset = amiga_chipset::ecs_1m,
                .chip_ram_size = amiga_size_1m,
                .zorro2_expansion_bus = false,
                .fast_ram_configurable = false,
            },
            amiga_model_descriptor{
                .model = amiga_model::amiga600,
                .id = "amiga600",
                .display_name = "Amiga 600",
                .chipset = amiga_chipset::ecs_1m,
                .chip_ram_size = amiga_size_1m,
                .zorro2_expansion_bus = false,
                .fast_ram_configurable = false,
            },
            amiga_model_descriptor{
                .model = amiga_model::amiga2000,
                .id = "amiga2000",
                .display_name = "Amiga 2000",
                .chipset = amiga_chipset::ocs,
                .chip_ram_size = amiga_size_512k,
                .zorro2_expansion_bus = true,
                .fast_ram_configurable = true,
            },
            amiga_model_descriptor{
                .model = amiga_model::amiga2000_ecs_1m,
                .id = "amiga2000-ecs-1m",
                .display_name = "Amiga 2000 ECS 1 MiB Agnus",
                .chipset = amiga_chipset::ecs_1m,
                .chip_ram_size = amiga_size_1m,
                .zorro2_expansion_bus = true,
                .fast_ram_configurable = true,
            },
        }};
    } // namespace

    const amiga_model_descriptor& amiga_model_profile(amiga_model model) noexcept {
        for (const auto& profile : model_profiles) {
            if (profile.model == model) {
                return profile;
            }
        }
        return model_profiles.front();
    }

    std::size_t amiga_fast_ram_size_for_config(const amiga_config& config,
                                               std::size_t max_size) noexcept {
        const auto& model = amiga_model_profile(config.model);
        if (!model.fast_ram_configurable) {
            return 0U;
        }
        return std::min(config.fast_ram_size, max_size);
    }

} // namespace mnemos::manifests::amiga
