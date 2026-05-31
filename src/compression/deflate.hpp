#pragma once

// RFC 1951 DEFLATE encoder -- the compress counterpart to inflate_raw. LZ77
// (hash-chain match finding) + fixed-Huffman blocks, with a stored fallback so
// output never expands badly. Clean-room against RFC 1951/1950; no zlib source
// consulted. Round-trips through inflate_raw and decodes under stock zlib.

#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::compression {

    // Compress `src` to a raw DEFLATE stream (no zlib/gzip wrapper) -- feeds
    // directly back into inflate_raw.
    [[nodiscard]] std::vector<std::uint8_t> deflate_raw(std::span<const std::uint8_t> src);

    // Compress `src` to a zlib stream (RFC 1950): 2-byte header + raw DEFLATE +
    // big-endian Adler-32 trailer. This is the form a PNG IDAT chunk carries.
    [[nodiscard]] std::vector<std::uint8_t> deflate_zlib(std::span<const std::uint8_t> src);

} // namespace mnemos::compression
