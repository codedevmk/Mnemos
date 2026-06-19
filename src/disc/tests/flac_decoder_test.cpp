// Synthetic unit tests for the CHD cdfl FLAC frame decoder. Hand-encode minimal
// FLAC frames (no corpus needed) and decode them back, so CI exercises the header
// parse, the CRC-8 / CRC-16 frame validation, the CONSTANT subframe, and the
// stereo interleave + mono duplication paths. The real-corpus test in
// chd_reader_test.cpp covers the FIXED/LPC/residual paths on actual discs.

#include "flac_decoder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace {

    // MSB-first bit writer, matching the decoder's bit order.
    struct bit_writer final {
        std::vector<std::uint8_t> bytes;
        std::uint32_t acc{};
        int nbits{};

        void put(std::uint32_t value, int bits) {
            for (int i = bits - 1; i >= 0; --i) {
                acc = (acc << 1) | ((value >> i) & 1U);
                if (++nbits == 8) {
                    bytes.push_back(static_cast<std::uint8_t>(acc & 0xFFU));
                    acc = 0;
                    nbits = 0;
                }
            }
        }
        void align_zero() {
            while (nbits != 0) {
                put(0, 1);
            }
        }
    };

    std::uint8_t crc8(std::span<const std::uint8_t> d) {
        std::uint8_t c = 0;
        for (std::uint8_t b : d) {
            c ^= b;
            for (int i = 0; i < 8; ++i) {
                c = static_cast<std::uint8_t>((c & 0x80U) ? (c << 1) ^ 0x07U : (c << 1));
            }
        }
        return c;
    }

    std::uint16_t crc16(std::span<const std::uint8_t> d) {
        std::uint16_t c = 0;
        for (std::uint8_t b : d) {
            c ^= static_cast<std::uint16_t>(b << 8);
            for (int i = 0; i < 8; ++i) {
                c = static_cast<std::uint16_t>((c & 0x8000U) ? (c << 1) ^ 0x8005U : (c << 1));
            }
        }
        return c;
    }

    // Encode one FLAC frame of CONSTANT subframes, one constant per channel.
    // block_size 192 (block-size code 1), 44.1 kHz, 16-bit.
    std::vector<std::uint8_t> encode_constant_frame(const std::vector<std::int16_t>& constants) {
        const int channels = static_cast<int>(constants.size());
        bit_writer w;
        w.put(0x3FFEU, 14);                                 // sync
        w.put(0, 1);                                        // reserved
        w.put(0, 1);                                        // blocking strategy (fixed)
        w.put(1, 4);                                        // block-size code 1 -> 192
        w.put(9, 4);                                        // sample-rate code 9 -> 44.1 kHz
        w.put(static_cast<std::uint32_t>(channels - 1), 4); // channel assignment (independent)
        w.put(4, 3);                                        // sample-size code 4 -> 16-bit
        w.put(0, 1);                                        // reserved
        w.put(0, 8);                                        // frame number 0 (UTF-8 single byte)
        const std::uint8_t h8 = crc8(w.bytes);              // header is now byte-aligned
        w.put(h8, 8);
        for (const std::int16_t c : constants) {
            w.put(0, 1);                              // subframe reserved
            w.put(0, 6);                              // type 0 = CONSTANT
            w.put(0, 1);                              // no wasted bits
            w.put(static_cast<std::uint16_t>(c), 16); // the constant sample
        }
        w.align_zero();
        const std::uint16_t f16 = crc16(w.bytes);
        w.put(f16, 16);
        return w.bytes;
    }

} // namespace

TEST_CASE("flac decoder reads a stereo CONSTANT frame", "[disc][flac]") {
    const std::vector<std::uint8_t> frame = encode_constant_frame({1000, -2000});
    std::vector<std::int16_t> out(192 * 2);
    const auto consumed = mnemos::disc::flac_decode_interleaved(frame, 192, out);
    REQUIRE(consumed.has_value());
    CHECK(*consumed == frame.size());
    for (int i = 0; i < 192; ++i) {
        CHECK(out[static_cast<std::size_t>(i) * 2] == 1000);
        CHECK(out[static_cast<std::size_t>(i) * 2 + 1] == -2000);
    }
}

TEST_CASE("flac decoder duplicates a mono CONSTANT frame to both channels", "[disc][flac]") {
    const std::vector<std::uint8_t> frame = encode_constant_frame({1234});
    std::vector<std::int16_t> out(192 * 2);
    const auto consumed = mnemos::disc::flac_decode_interleaved(frame, 192, out);
    REQUIRE(consumed.has_value());
    for (int i = 0; i < 192; ++i) {
        CHECK(out[static_cast<std::size_t>(i) * 2] == 1234);
        CHECK(out[static_cast<std::size_t>(i) * 2 + 1] == 1234);
    }
}

TEST_CASE("flac decoder rejects a frame with a corrupted footer CRC", "[disc][flac]") {
    std::vector<std::uint8_t> frame = encode_constant_frame({500, 500});
    frame[frame.size() - 1] ^= 0xFFU; // clobber the CRC-16
    std::vector<std::int16_t> out(192 * 2);
    CHECK_FALSE(mnemos::disc::flac_decode_interleaved(frame, 192, out).has_value());
}

TEST_CASE("flac decoder rejects a frame with a corrupted header CRC", "[disc][flac]") {
    std::vector<std::uint8_t> frame = encode_constant_frame({500, 500});
    frame[4] ^= 0xFFU; // clobber a header byte (CRC-8 will mismatch)
    std::vector<std::int16_t> out(192 * 2);
    CHECK_FALSE(mnemos::disc::flac_decode_interleaved(frame, 192, out).has_value());
}
