#pragma once

// Filename helpers shared by the artifact-dumping tools (debug_dump,
// asset_export). Kept header-only so both translation units share one
// definition instead of each carrying its own copy.

#include <cctype>
#include <string>
#include <string_view>

namespace mnemos::debug {

    // Lowercase a string and replace every non-alphanumeric byte with '_', so a
    // chip's part_number ("mem.chip-1") becomes a filesystem-safe path segment
    // ("mem_chip_1").
    [[nodiscard]] inline std::string sanitize_id(std::string_view raw) {
        std::string out;
        out.reserve(raw.size());
        for (char c : raw) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            } else {
                out.push_back('_');
            }
        }
        return out;
    }

    // The filename portion of a path (everything after the last '/' or '\\'),
    // or the whole string when it has no separator.
    [[nodiscard]] inline std::string_view path_basename(std::string_view path) {
        const auto pos = path.find_last_of("/\\");
        return pos == std::string_view::npos ? path : path.substr(pos + 1);
    }

} // namespace mnemos::debug
