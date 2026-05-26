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

    // Refresh rate per video_region in natural units (Hz), indexed by the
    // enum's underlying value. NTSC is rounded to 60 (real hardware is
    // 60000/1001 = 59.94 Hz); when we later need that precision, swap the
    // 60.0 below for 60000.0 / 1001.0.
    inline constexpr std::array<double, 2> target_fps{60.0, 50.0};

    // Same values scaled by 1000 to fit an integer -- this is what the
    // SDK's video_region struct carries on the wire.
    inline constexpr std::array<std::uint32_t, 2> fps_x1000{60'000U, 50'000U};

} // namespace mnemos
