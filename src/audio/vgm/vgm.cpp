#include "vgm.hpp"

#include "file.hpp"

#include <array>
#include <cstddef>

namespace mnemos::audio {

    namespace {
        constexpr std::size_t header_size = 0x40; // VGM v1.50 header
        constexpr std::uint32_t vgm_version = 0x00000150U;
        // VGM data starts at 0x40; the data-offset field at 0x34 is relative to
        // its own position, so 0x40 - 0x34 = 0x0C.
        constexpr std::uint32_t data_offset_rel = 0x0CU;

        void put32(std::vector<std::uint8_t>& out, std::size_t off, std::uint32_t v) {
            out[off + 0] = static_cast<std::uint8_t>(v & 0xFFU);
            out[off + 1] = static_cast<std::uint8_t>((v >> 8U) & 0xFFU);
            out[off + 2] = static_cast<std::uint8_t>((v >> 16U) & 0xFFU);
            out[off + 3] = static_cast<std::uint8_t>((v >> 24U) & 0xFFU);
        }
    } // namespace

    std::vector<std::uint8_t> encode_vgm(const vgm_header& header,
                                         std::span<const std::uint8_t> body) {
        std::vector<std::uint8_t> out(header_size, 0U);

        out[0] = 'V';
        out[1] = 'g';
        out[2] = 'm';
        out[3] = ' ';
        put32(out, 0x08, vgm_version);
        put32(out, 0x0C, header.sn76489_clock);
        put32(out, 0x18, header.total_samples);
        put32(out, 0x24, header.rate);
        out[0x28] = static_cast<std::uint8_t>(header.sn76489_feedback & 0xFFU);
        out[0x29] = static_cast<std::uint8_t>((header.sn76489_feedback >> 8U) & 0xFFU);
        out[0x2A] = header.sn76489_shift_width;
        put32(out, 0x2C, header.ym2612_clock);
        put32(out, 0x34, data_offset_rel);

        out.insert(out.end(), body.begin(), body.end());

        // EoF offset at 0x04 is relative to 0x04, i.e. total size - 4.
        put32(out, 0x04, static_cast<std::uint32_t>(out.size()) - 4U);
        return out;
    }

    bool write_vgm(const std::string& path, const vgm_header& header,
                   std::span<const std::uint8_t> body) {
        return io::write_file(path, encode_vgm(header, body));
    }

} // namespace mnemos::audio
