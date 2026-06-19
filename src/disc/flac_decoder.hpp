#pragma once

#include <cstdint>
#include <optional>
#include <span>

// Minimal FLAC frame-stream decoder for the CHD `cdfl` codec (CD-DA audio).
//
// CHD cdfl stores CD audio as a bare sequence of FLAC frames -- no "fLaC" stream
// marker, no STREAMINFO metadata block -- back to back, then (for CD hunks) the
// subchannel bytes trail the audio. This decoder handles the FLAC side: it reads
// frames until the requested sample count is produced and validates each frame's
// CRC-8 header + CRC-16 footer, so a mis-decode is caught rather than yielding
// plausible-looking noise.
//
// Clean-room per RFC 9639 (FLAC) + the MAME CHD cdfl codec description; no
// third-party decoder source was used.
namespace mnemos::disc {

    // Decode `sample_pairs` interleaved stereo samples from the raw FLAC frame
    // stream `in` into `out` (which must hold sample_pairs*2 int16 values, laid
    // out L,R,L,R as host integers -- the caller packs them to the byte order its
    // consumer wants). Mono frames are duplicated to both channels. Returns the
    // number of input bytes consumed (the FLAC payload length; any trailing
    // subchannel data begins there) or nullopt on a malformed stream or a CRC
    // mismatch.
    [[nodiscard]] std::optional<std::size_t>
    flac_decode_interleaved(std::span<const std::uint8_t> in, std::uint32_t sample_pairs,
                            std::span<std::int16_t> out);

} // namespace mnemos::disc
