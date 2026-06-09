#pragma once

// Shared by the artifact-export tools (asset_export, audio_export) so they emit
// JSON manifests the same way instead of each carrying a private escaper.

#include <string>
#include <string_view>

namespace mnemos::debug {

    // A minimal JSON string literal: quoted, with '"' and '\\' escaped. The
    // values these manifests carry are simple identifiers / filenames, so this
    // is sufficient (it does not escape control characters).
    [[nodiscard]] inline std::string json_string(std::string_view s) {
        std::string out = "\"";
        for (char c : s) {
            if (c == '"' || c == '\\') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        out.push_back('"');
        return out;
    }

} // namespace mnemos::debug
