#pragma once

#include <string>

namespace mnemos::apps::player::adapters {

    // Which adapter to wire for a given ROM. Extended as new systems land.
    enum class system_family { genesis, sms };

    // Pick the adapter from the ROM path's extension:
    //   .sms / .sg               -> SMS
    //   anything else (incl. no extension, .md / .gen / .smd / .bin / .68k)
    //                            -> Genesis
    // Case-insensitive on the extension. No content sniffing.
    [[nodiscard]] system_family detect_family(const std::string& path) noexcept;

    // Display label for `family` ("SMS" / "Genesis"). Used by the startup
    // banner and the status overlay.
    [[nodiscard]] const char* family_label(system_family family) noexcept;

} // namespace mnemos::apps::player::adapters
