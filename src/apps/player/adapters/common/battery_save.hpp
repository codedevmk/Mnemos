#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace mnemos::apps::player::adapters {

    // Derive the battery-save file path that sits beside a ROM: the ROM path with
    // its extension replaced by ".srm" (appended when the name has no extension).
    // A leading-dot basename (".sram") is treated as having no extension, so it
    // keeps its body and only gains the suffix. Pure -- no filesystem access.
    [[nodiscard]] std::string srm_path_for(std::string_view rom_path);

    // Load a previously saved battery image at `srm_path` into `dst` (the live
    // SRAM backing store). Copies min(file, dst) bytes, leaving any tail at its
    // powered-on value, so a size mismatch never overflows. Returns false (and
    // leaves `dst` untouched) when `dst` is empty or no save file exists -- a
    // first run with no .srm is the normal not-an-error case.
    bool load_battery_ram(const std::string& srm_path, std::span<std::uint8_t> dst);

    // Persist `bytes` to `srm_path` atomically: write a sibling temp file then
    // rename it over the target, so an interrupted write can never truncate or
    // corrupt an existing save. No-op (returns false) for an empty image.
    bool save_battery_ram(const std::string& srm_path, std::span<const std::uint8_t> bytes);

} // namespace mnemos::apps::player::adapters
