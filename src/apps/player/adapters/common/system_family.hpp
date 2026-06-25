#pragma once

#include <optional>
#include <string>

namespace mnemos::apps::player::adapters {

    // Which adapter to wire for a ROM. Extended as new systems land.
    enum class system_family {
        genesis,
        sms,
        gg,
        c64,
        segacd,
        sega32x,
        irem_m72,
        irem_m81,
        irem_m82,
        taito_f2,
        capcom_cps1,
        capcom_cps2,
        spectrum,
        nes,
        msx,
        msx2,
        amiga500
    };

    // Map a `--system` name to its family. The accepted names are exactly the
    // adapter-registry family ids -- genesis, sms, gg, c64, segacd, sega32x,
    // irem_m72, irem_m81, irem_m82, taito_f2, cps1, cps2, spectrum, nes, msx,
    // amiga500 -- case-insensitive. nullopt for anything else. The engine is
    // always chosen by this name, never inferred from the ROM filename.
    [[nodiscard]] std::optional<system_family> family_from_name(const std::string& name) noexcept;

    // The adapter-registry id for `family` ("genesis", "sms", ...).
    [[nodiscard]] const char* family_id(system_family family) noexcept;

    // Display label for `family` ("SMS" / "Game Gear" / "Genesis" / "C64"). Used
    // by the startup banner and the status overlay.
    [[nodiscard]] const char* family_label(system_family family) noexcept;

    // Every accepted `--system` name as one comma-separated list, for usage and
    // error text.
    [[nodiscard]] const char* family_names() noexcept;

} // namespace mnemos::apps::player::adapters
