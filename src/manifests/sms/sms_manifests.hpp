#pragma once

// Single source of truth for the SMS system manifests at runtime.
//
// The canonical descriptions live in the on-disk .toml files beside this
// header; CMake embeds each one verbatim into sms_embedded_manifests.hpp (a
// generated header) so the player carries them with no filesystem dependency
// and tests share the exact same bytes (no hand-copied, drift-prone duplicates).
//
// Selection mirrors sms_config: PAL vs NTSC by region, Sega vs Codemasters
// mapper by `codemasters` (the adapter sets it from cart-header detection).

#include "region.hpp" // mnemos::video_region

#include "sms_embedded_manifests.hpp" // generated; see sms_embedded_manifests.hpp.in

#include <string_view>

namespace mnemos::manifests::sms {

    [[nodiscard]] constexpr std::string_view manifest_toml(mnemos::video_region region,
                                                           bool codemasters) noexcept {
        const bool pal = region == mnemos::video_region::pal;
        if (codemasters) {
            return pal ? embedded::sms_pal_codemasters_toml : embedded::sms_ntsc_codemasters_toml;
        }
        return pal ? embedded::sms_pal_toml : embedded::sms_ntsc_toml;
    }

} // namespace mnemos::manifests::sms
