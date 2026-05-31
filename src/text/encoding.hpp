#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// String -> byte-stream encoders. Reserved home for text codecs (UTF-8, Shift-JIS,
// region text) per ADR 0009; seeded with Latin-1 because ROM header text is
// effectively Latin-1/ASCII.
namespace mnemos::text {

    // Takes the low 8 bits of each char (ISO-8859-1). Inverse of decode_latin1 for
    // any byte 0..255.
    [[nodiscard]] std::vector<std::uint8_t> encode_latin1(std::string_view text);

} // namespace mnemos::text
