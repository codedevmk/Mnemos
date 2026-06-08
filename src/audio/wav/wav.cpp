#include "wav.hpp"

#include "file.hpp"

#include <array>
#include <cstddef>

namespace mnemos::audio {

    namespace {

        void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
            out.push_back(static_cast<std::uint8_t>(v & 0xFFU));
            out.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
        }
        void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
            for (int i = 0; i < 4; ++i) {
                out.push_back(
                    static_cast<std::uint8_t>((v >> (8U * static_cast<unsigned>(i))) & 0xFFU));
            }
        }
        void put_tag(std::vector<std::uint8_t>& out, const std::array<std::uint8_t, 4>& tag) {
            out.insert(out.end(), tag.begin(), tag.end());
        }

        constexpr std::array<std::uint8_t, 4> kRIFF = {'R', 'I', 'F', 'F'};
        constexpr std::array<std::uint8_t, 4> kWAVE = {'W', 'A', 'V', 'E'};
        constexpr std::array<std::uint8_t, 4> kFMT = {'f', 'm', 't', ' '};
        constexpr std::array<std::uint8_t, 4> kDATA = {'d', 'a', 't', 'a'};

    } // namespace

    std::vector<std::uint8_t> encode_wav(std::span<const std::int16_t> frames,
                                         std::uint32_t sample_rate, std::uint8_t channels) {
        const std::uint16_t ch = channels == 0U ? 1U : channels;
        const auto data_bytes = static_cast<std::uint32_t>(frames.size() * sizeof(std::int16_t));
        const std::uint16_t block_align = static_cast<std::uint16_t>(ch * 2U);
        const std::uint32_t byte_rate = sample_rate * block_align;

        std::vector<std::uint8_t> out;
        out.reserve(44U + data_bytes);

        put_tag(out, kRIFF);
        put_u32(out, 36U + data_bytes); // RIFF chunk size = 36 + data
        put_tag(out, kWAVE);

        put_tag(out, kFMT);
        put_u32(out, 16U);         // fmt chunk size (PCM)
        put_u16(out, 1U);          // audio format = PCM
        put_u16(out, ch);          // channels
        put_u32(out, sample_rate); // sample rate
        put_u32(out, byte_rate);   // byte rate
        put_u16(out, block_align); // block align
        put_u16(out, 16U);         // bits per sample

        put_tag(out, kDATA);
        put_u32(out, data_bytes);
        for (std::int16_t s : frames) {
            put_u16(out, static_cast<std::uint16_t>(s));
        }
        return out;
    }

    bool write_wav(const std::string& path, std::span<const std::int16_t> frames,
                   std::uint32_t sample_rate, std::uint8_t channels) {
        return io::write_file(path, encode_wav(frames, sample_rate, channels));
    }

} // namespace mnemos::audio
