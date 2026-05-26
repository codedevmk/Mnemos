#pragma once

#include <array>
#include <cstdint>

namespace mnemos {

    enum class video_region : std::uint8_t { ntsc, pal };

    enum class market : std::uint8_t {
        japan,
        americas,
        europe,
        multi_region,
        unknown,
    };

    [[nodiscard]] constexpr video_region default_video_for(market m) noexcept {
        return m == market::europe ? video_region::pal : video_region::ntsc;
    }

    // Frame rate per video_region, *1000 so NTSC's ~59.94 and PAL's 50.00
    // both fit an integer. Indexed by the enum's underlying value.
    inline constexpr std::array<std::uint32_t, 2> fps_x1000{60'000U, 50'000U};

} // namespace mnemos
