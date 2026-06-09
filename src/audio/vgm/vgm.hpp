#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mnemos::audio {

    // Header configuration for a VGM (Video Game Music) log -- the standard
    // register-write song format. Only the fields Mnemos currently emits are
    // exposed; absent chips leave their clock at 0. Clocks are in Hz.
    struct vgm_header final {
        std::uint32_t sn76489_clock{0};
        std::uint32_t ym2612_clock{0};
        std::uint16_t sn76489_feedback{0x0009}; // SMS/GG default tapped bits
        std::uint8_t sn76489_shift_width{16};   // SMS/GG default
        std::uint32_t rate{60};                 // playback rate hint (Hz)
        std::uint32_t total_samples{0};         // total length in 44.1 kHz samples
    };

    // Wrap a pre-encoded VGM command `body` (a stream of 0x5n/0x4F/0x52.. writes,
    // 0x62/0x63 frame waits, terminated by 0x66) in a v1.50 VGM container: a
    // 64-byte header (magic, version, clocks, data offset 0x40, EoF offset) then
    // the body. The body is emitted uncompressed (a valid .vgm; players also
    // accept gzip .vgz, which we do not produce).
    [[nodiscard]] std::vector<std::uint8_t> encode_vgm(const vgm_header& header,
                                                       std::span<const std::uint8_t> body);

    // Encode and write to `path`. Returns false if the file can't be written.
    [[nodiscard]] bool write_vgm(const std::string& path, const vgm_header& header,
                                 std::span<const std::uint8_t> body);

} // namespace mnemos::audio
