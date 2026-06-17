#pragma once

// RFC 1951 DEFLATE decoder (the compression method used by .zip entries).
// Single-shot, no allocation, memory-safe: the caller supplies the whole
// compressed buffer and a destination of the known uncompressed size.
//
// Clean-room against RFC 1951; no zlib source consulted.

#include <cstdint>
#include <optional>
#include <span>

namespace mnemos::compression {

    // Decompress a raw DEFLATE stream (no zlib/gzip wrapper) from `src` into
    // `dst`. Returns the number of bytes written, or nullopt on any malformed
    // input / oversize back-reference / output overflow.
    [[nodiscard]] std::optional<std::size_t> inflate_raw(std::span<const std::uint8_t> src,
                                                         std::span<std::uint8_t> dst) noexcept;

    // As above, but also reports how many input bytes were consumed up to and
    // including the final block (rounded up to the byte the bit reader stopped
    // on). Lets a caller resume a second stream packed immediately after the
    // first (e.g. a CHD CD hunk's subcode lane following its sector lane).
    [[nodiscard]] std::optional<std::size_t> inflate_raw(std::span<const std::uint8_t> src,
                                                         std::span<std::uint8_t> dst,
                                                         std::size_t& bytes_consumed) noexcept;

} // namespace mnemos::compression
