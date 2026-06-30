#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters {

    struct loaded_rom final {
        std::vector<std::uint8_t> bytes; // the ROM image (extracted if the source was a .zip)
        // Effective name for the displayed title: the chosen entry name for a
        // .zip, else the path. Carries no routing weight -- the engine comes
        // from --system.
        std::string name;
        // True when the source is a directory-backed ROM set rather than a
        // flat byte stream. Arcade adapters that understand multi-file sets
        // consume `rom_path` directly in this case.
        bool directory_source{};
    };

    // Load a ROM image from `path`, transparently extracting it when the file
    // is a .zip (detected by signature). The largest STORED/DEFLATE entry is
    // taken as the ROM -- ROM-set archives are typically a single entry, and a
    // ROM always dwarfs any readme/junk alongside it. A non-archive file is
    // returned as-is. nullopt if the file can't be read, the archive is
    // malformed, or extraction fails.
    [[nodiscard]] std::optional<loaded_rom> load_rom(const std::string& path);

    // Load the file verbatim -- no archive unwrapping. Arcade families take
    // the whole multi-dump set zip or set directory as their medium; their
    // adapters resolve the entries themselves through the game declaration.
    [[nodiscard]] std::optional<loaded_rom> load_rom_verbatim(const std::string& path);

    // Strip the directory + extension off a ROM path so the result reads
    // like a title rather than a filesystem path. No further "cleanup"
    // (underscore-to-space, region-tag stripping etc.) -- silently
    // re-interpreting what the user named their file is out of scope.
    [[nodiscard]] std::string clean_rom_name(const std::string& path);

} // namespace mnemos::apps::player::adapters
