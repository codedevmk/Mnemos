#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mnemos::audio {

    // Encode interleaved 16-bit PCM frames as a canonical RIFF/WAVE byte stream
    // (WAVE_FORMAT_PCM, little-endian). When `channels > 1` the input is
    // L,R,...-interleaved; `channels` of 0 is treated as 1. Loop points are NOT
    // represented (no `smpl` chunk) -- callers that need them carry the metadata
    // separately, the way the asset exporter records palette transparency in JSON.
    [[nodiscard]] std::vector<std::uint8_t> encode_wav(std::span<const std::int16_t> frames,
                                                       std::uint32_t sample_rate,
                                                       std::uint8_t channels);

    // Encode and write to `path`. Returns false if the file can't be written.
    [[nodiscard]] bool write_wav(const std::string& path, std::span<const std::int16_t> frames,
                                 std::uint32_t sample_rate, std::uint8_t channels);

} // namespace mnemos::audio
