#pragma once

#include <cstdint>
#include <span>
#include <string>

// Byte-stream -> string decoders. Reserved home for text codecs (UTF-8, Shift-JIS,
// region text) per ADR 0009; seeded with Latin-1 because ROM header text is
// effectively Latin-1/ASCII.
namespace mnemos::text {

    // Each byte maps directly to one code point (ISO-8859-1), so the result is never
    // lossy and always round-trips through encode_latin1.
    [[nodiscard]] std::string decode_latin1(std::span<const std::uint8_t> bytes);

} // namespace mnemos::text
