#pragma once

// Shared OPN/OPM (Yamaha FM) output-pipeline tables. The YM2612 (OPN2) and
// YM2151 (OPM) build the same log-sine and exponential tables to convert a
// phase into a log-domain sine and the summed log attenuation back to linear.
// They are defined once here -- `inline` yields a single instance across
// translation units -- so the two cores cannot drift. See
// THIRD-PARTY-REFERENCES.md for the hardware basis.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mnemos::chips::audio::fm {

    inline constexpr double pi = 3.14159265358979323846;

    // 10-bit phase -> 12-bit log-sin. The first quadrant is computed, then
    // folded across all four.
    inline const std::array<std::uint16_t, 1024> sin_table = [] {
        std::array<std::uint16_t, 1024> t{};
        for (std::size_t i = 0; i < 256; ++i) {
            const double phase = (static_cast<double>(i) + 0.5) / 256.0 * (pi / 2.0);
            double s = std::sin(phase);
            if (s < 0.000001) {
                s = 0.000001;
            }
            auto v = static_cast<std::uint16_t>(-std::log2(s) * 256.0 + 0.5);
            if (v > 0x0FFFU) {
                v = 0x0FFFU;
            }
            t[i] = v;
        }
        for (std::size_t i = 0; i < 256; ++i) {
            t[256 + i] = t[255 - i]; // Q2 mirror
            t[512 + i] = t[i];       // Q3 (sign handled separately)
            t[768 + i] = t[255 - i]; // Q4 mirror
        }
        return t;
    }();

    // 2^(13 - i/256) -- converts the summed log attenuation back to linear.
    inline const std::array<std::uint16_t, 1024> exp_table = [] {
        std::array<std::uint16_t, 1024> t{};
        for (std::size_t i = 0; i < 1024; ++i) {
            const double e = std::pow(2.0, 13.0 - static_cast<double>(i) / 256.0);
            t[i] = static_cast<std::uint16_t>(e + 0.5);
        }
        return t;
    }();

} // namespace mnemos::chips::audio::fm
