#include "chipsets/amiga_chipsets.hpp"

#include "agnus.hpp"

#include <array>

namespace mnemos::manifests::amiga {

    namespace {
        constexpr std::array<amiga_chipset_descriptor, 2U> chipset_profiles{{
            amiga_chipset_descriptor{
                .chipset = amiga_chipset::ocs,
                .id = "ocs",
                .display_name = "OCS",
                .copper_address_mask = chips::video::agnus::ocs_copper_address_mask,
            },
            amiga_chipset_descriptor{
                .chipset = amiga_chipset::ecs_1m,
                .id = "ecs_1m",
                .display_name = "ECS 1 MiB Agnus",
                .copper_address_mask = chips::video::agnus::ecs_1m_copper_address_mask,
            },
        }};
    } // namespace

    const amiga_chipset_descriptor& amiga_chipset_profile(amiga_chipset chipset) noexcept {
        for (const auto& profile : chipset_profiles) {
            if (profile.chipset == chipset) {
                return profile;
            }
        }
        return chipset_profiles.front();
    }

} // namespace mnemos::manifests::amiga
