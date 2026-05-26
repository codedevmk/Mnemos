#pragma once

// Project-wide region taxonomy.
//
// Two truly generic concepts live here, plus the policy that maps one to the
// other. NO system-specific anything (no Genesis country bytes, no SMS country
// nibbles, no cartridge-header field locations) -- those belong to whichever
// system module owns the bytes they parse.
//
//   `video_region`        the categorical NTSC / PAL the host console runs at.
//                         A console property; the user picks it, the cart only
//                         hints.
//   `market`              the consumer market a cart targets. Each system module
//                         parses its own bytes into one of these.
//   `default_video_for`   the project's policy for picking a video_region from
//                         a market when the user hasn't overridden it.
//
// System modules: parse your own bytes, map them to a `market`, hand that to
// `default_video_for`. Never duplicate the enums; never re-invent the policy.

#include <cstdint>

namespace mnemos {

    // The CONSOLE's video timing. Property of the host hardware (or the
    // emulator config); not encoded in cartridge bytes.
    enum class video_region : std::uint8_t {
        ntsc, // ~60 Hz, 525-line family
        pal,  // ~50 Hz, 625-line family
    };

    // The consumer market a cart targets. A categorical view of the cart-side
    // metadata, system-agnostic. Each system module parses its own bytes into
    // one of these; the policy below maps the result to a default video_region.
    enum class market : std::uint8_t {
        japan,        // NTSC-J territories
        americas,     // USA / Canada / Brazil (NTSC-M)
        europe,       // Europe + Oceania (PAL)
        multi_region, // cart targets two or more of the above
        unknown,      // header missing / unrecognised
    };

    // Default video_region for a given market. The only PAL-pure case is
    // Europe; everything else defaults to NTSC. The user can always override.
    [[nodiscard]] constexpr video_region default_video_for(market m) noexcept {
        return m == market::europe ? video_region::pal : video_region::ntsc;
    }

    // Nominal frame rate of each video standard, scaled by 1000 so both fit
    // an integer (NTSC ~59.94, PAL 50.00). The rate is a property of the
    // video standard itself, not the system: any NTSC console runs ~60 Hz,
    // any PAL console runs 50 Hz. System adapters must consult this rather
    // than restating the constants per-system.
    [[nodiscard]] constexpr std::uint32_t frames_per_second_x1000(video_region r) noexcept {
        return r == video_region::pal ? 50000U : 60000U;
    }

    // Floating-point view of the same rate (44.1 kHz / fps math, audio
    // resampler scaling, ...). Whole-fps approximation -- tighter pinning
    // would track the actual ~59.94 / 50.00 values, but every audio chip
    // currently in the project tolerates whole-fps within its resampler.
    [[nodiscard]] constexpr double frames_per_second(video_region r) noexcept {
        return static_cast<double>(frames_per_second_x1000(r)) / 1000.0;
    }

} // namespace mnemos
