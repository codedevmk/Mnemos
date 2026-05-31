#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// Generic ASCII string helpers shared across modules (ADR 0009). Locale-independent
// by design: ROM/path text is ASCII, so std::tolower's locale sensitivity is a hazard
// here and we fold only A-Z explicitly.
namespace mnemos::common {

    [[nodiscard]] inline char to_lower(char c) noexcept {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    [[nodiscard]] inline std::string to_lower(std::string text) {
        for (char& c : text) {
            c = to_lower(c);
        }
        return text;
    }

    // Case-insensitive (ASCII) test of whether `text` ends with `suffix`.
    [[nodiscard]] inline bool ends_with_ci(std::string_view text,
                                           std::string_view suffix) noexcept {
        if (suffix.size() > text.size()) {
            return false;
        }
        const std::size_t offset = text.size() - suffix.size();
        for (std::size_t i = 0; i < suffix.size(); ++i) {
            if (to_lower(text[offset + i]) != to_lower(suffix[i])) {
                return false;
            }
        }
        return true;
    }

} // namespace mnemos::common
