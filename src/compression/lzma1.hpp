#pragma once

// LZMA1 decoder for the CHD cdlz subset: caller-supplied (lc, lp, pb), a fixed
// known output size, and no end-of-stream marker. Not the .lzma/.xz wrapper.
//
// Clean-room against the LZMA1 format spec; no LZMA-SDK source consulted.

#include <cstdint>
#include <optional>
#include <span>

namespace mnemos::compression {

    // Decode a raw LZMA1 stream (no .lzma/.xz wrapper; CHD cdlz subset) with the
    // given (lc, lp, pb) properties into `dst`, whose size is the exact known
    // uncompressed length. Returns the number of src bytes consumed, or nullopt
    // on any malformed-stream / size-mismatch / underflow error.
    [[nodiscard]] std::optional<std::size_t> lzma1_decode(std::uint8_t lc, std::uint8_t lp,
                                                          std::uint8_t pb,
                                                          std::span<const std::uint8_t> src,
                                                          std::span<std::uint8_t> dst);

} // namespace mnemos::compression
