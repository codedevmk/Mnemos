#pragma once

#include <cstdint>

namespace mnemos::manifests::amiga {

    enum class amiga_chipset : std::uint8_t { ocs, ecs_1m };

    struct amiga_chipset_descriptor final {
        amiga_chipset chipset{amiga_chipset::ocs};
        const char* id{"ocs"};
        const char* display_name{"OCS"};
        std::uint32_t copper_address_mask{};
    };

    [[nodiscard]] const amiga_chipset_descriptor&
    amiga_chipset_profile(amiga_chipset chipset) noexcept;

} // namespace mnemos::manifests::amiga
