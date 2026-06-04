#pragma once

// Single source of truth for the SMS system manifests at runtime.
//
// The canonical descriptions live in the on-disk .toml files beside this
// header; CMake embeds each one verbatim into sms_embedded_manifests.hpp (a
// generated header) so the player carries them with no filesystem dependency
// and tests share the exact same bytes (no hand-copied, drift-prone duplicates).
//
// Selection mirrors sms_config: PAL vs NTSC by region, and the cartridge mapper
// (Sega / Codemasters / Korean) by the resolved kind the caller passes -- the
// adapter resolves it via resolve_mapper() from cart-header detection + config.

#include "region.hpp"     // mnemos::video_region
#include "sms_system.hpp" // sms_config::mapper

#include "sms_embedded_manifests.hpp" // generated; see sms_embedded_manifests.hpp.in

#include <string_view>

namespace mnemos::manifests::sms {

    // Pick the embedded manifest for the region + resolved mapper kind. `kind`
    // must be a concrete choice (sega/codemasters/korean), not `automatic` --
    // callers resolve via resolve_mapper() first; `automatic` falls back to Sega.
    [[nodiscard]] constexpr std::string_view manifest_toml(mnemos::video_region region,
                                                           sms_config::mapper kind) noexcept {
        const bool pal = region == mnemos::video_region::pal;
        switch (kind) {
        case sms_config::mapper::korean:
            return pal ? embedded::sms_pal_korean_toml : embedded::sms_ntsc_korean_toml;
        case sms_config::mapper::korean_msx:
        case sms_config::mapper::korean_msx_nemesis:
            return pal ? embedded::sms_pal_korean_msx_toml : embedded::sms_ntsc_korean_msx_toml;
        case sms_config::mapper::korean_hicom:
            return pal ? embedded::sms_pal_korean_hicom_toml : embedded::sms_ntsc_korean_hicom_toml;
        case sms_config::mapper::korean_janggun:
            return pal ? embedded::sms_pal_korean_janggun_toml
                       : embedded::sms_ntsc_korean_janggun_toml;
        case sms_config::mapper::codemasters:
            return pal ? embedded::sms_pal_codemasters_toml : embedded::sms_ntsc_codemasters_toml;
        case sms_config::mapper::sega:
        case sms_config::mapper::automatic:
        default:
            return pal ? embedded::sms_pal_toml : embedded::sms_ntsc_toml;
        }
    }

} // namespace mnemos::manifests::sms
