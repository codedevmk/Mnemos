#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mnemos::apps::player::adapters {

    // Read the file at `path` into a byte vector. Returns nullopt if the
    // file can't be opened. Zero-byte files are returned as an empty vector,
    // not nullopt -- callers should treat empty as a load failure if they
    // need non-empty contents.
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> read_file(const std::string& path);

    // Strip the directory + extension off a ROM path so the result reads
    // like a title rather than a filesystem path. No further "cleanup"
    // (underscore-to-space, region-tag stripping etc.) -- silently
    // re-interpreting what the user named their file is out of scope.
    [[nodiscard]] std::string clean_rom_name(const std::string& path);

} // namespace mnemos::apps::player::adapters
