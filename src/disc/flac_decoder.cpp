#include "flac_decoder.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace mnemos::disc {

    namespace {

        // ---- CRC-8 (poly 0x07) for the frame header, CRC-16 (poly 0x8005) for the
        // frame footer, both init 0, MSB-first over whole bytes (RFC 9639 §9.1). A
        // correct decode lands the bit reader exactly on each CRC boundary, so a
        // mismatch means the frame parse desynced -- the decoder's own self-check.
        [[nodiscard]] std::uint8_t crc8(std::span<const std::uint8_t> data) noexcept {
            static const auto table = [] {
                std::array<std::uint8_t, 256> t{};
                for (int i = 0; i < 256; ++i) {
                    auto c = static_cast<std::uint8_t>(i);
                    for (int b = 0; b < 8; ++b) {
                        c = static_cast<std::uint8_t>((c & 0x80U) ? (c << 1) ^ 0x07U : (c << 1));
                    }
                    t[static_cast<std::size_t>(i)] = c;
                }
                return t;
            }();
            std::uint8_t crc = 0;
            for (const std::uint8_t byte : data) {
                crc = table[crc ^ byte];
            }
            return crc;
        }

        [[nodiscard]] std::uint16_t crc16(std::span<const std::uint8_t> data) noexcept {
            static const auto table = [] {
                std::array<std::uint16_t, 256> t{};
                for (int i = 0; i < 256; ++i) {
                    auto c = static_cast<std::uint16_t>(i << 8);
                    for (int b = 0; b < 8; ++b) {
                        c = static_cast<std::uint16_t>((c & 0x8000U) ? (c << 1) ^ 0x8005U
                                                                     : (c << 1));
                    }
                    t[static_cast<std::size_t>(i)] = c;
                }
                return t;
            }();
            std::uint16_t crc = 0;
            for (const std::uint8_t byte : data) {
                crc = static_cast<std::uint16_t>((crc << 8) ^ table[(crc >> 8) ^ byte]);
            }
            return crc;
        }

        // MSB-first bit reader over a byte span. Tracks the byte cursor so the frame
        // CRCs can be taken over the exact input ranges at byte-aligned boundaries.
        struct bit_reader final {
            std::span<const std::uint8_t> in;
            std::size_t byte_pos{};
            std::uint32_t bit_buf{};
            int bit_count{};
            bool error{};

            std::uint32_t read(int n) noexcept {
                if (error || n <= 0 || n > 32) {
                    error = error || n != 0;
                    return 0;
                }
                while (bit_count < n) {
                    if (byte_pos >= in.size()) {
                        error = true;
                        return 0;
                    }
                    bit_buf = (bit_buf << 8) | in[byte_pos++];
                    bit_count += 8;
                }
                bit_count -= n;
                return (bit_buf >> bit_count) &
                       (n == 32 ? 0xFFFFFFFFU : ((1U << static_cast<unsigned>(n)) - 1U));
            }

            std::int32_t read_signed(int n) noexcept {
                const std::uint32_t u = read(n);
                if (n <= 0 || n >= 32) {
                    return static_cast<std::int32_t>(u);
                }
                const std::int32_t s = static_cast<std::int32_t>(u << (32 - n));
                return s >> (32 - n);
            }

            std::uint32_t read_unary() noexcept {
                std::uint32_t count = 0;
                while (read(1) == 0U && !error) {
                    if (++count > 1'000'000U) {
                        error = true;
                        return 0;
                    }
                }
                return count;
            }

            std::uint64_t read_utf8() noexcept {
                const std::uint32_t x = read(8);
                if (error) {
                    return 0;
                }
                if ((x & 0x80U) == 0U) {
                    return x;
                }
                int cont = 0;
                std::uint32_t mask = 0x40U;
                while ((x & mask) != 0U) {
                    ++cont;
                    mask >>= 1U;
                    if (cont > 6) {
                        error = true;
                        return 0;
                    }
                }
                if (cont < 1) {
                    error = true;
                    return 0;
                }
                std::uint64_t v = x & (mask - 1U);
                for (int i = 0; i < cont; ++i) {
                    const std::uint32_t c = read(8);
                    if ((c & 0xC0U) != 0x80U) {
                        error = true;
                        return 0;
                    }
                    v = (v << 6U) | (c & 0x3FU);
                }
                return v;
            }

            void align_byte() noexcept { bit_count &= ~7; }

            // Byte index consumed so far; meaningful only at a byte-aligned point.
            [[nodiscard]] std::size_t aligned_index() const noexcept {
                return byte_pos - static_cast<std::size_t>(bit_count / 8);
            }
        };

        // Block-size code table (RFC 9639 §9.1.2); 0/6/7 read an explicit value.
        constexpr std::array<int, 16> k_fixed_block_size = {
            0, 192, 576, 1152, 2304, 4608, -1, -1, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};

        // FIXED predictor polynomial coefficients per order 0..4.
        constexpr std::array<std::array<int, 4>, 5> k_fixed_coef = {
            {{0, 0, 0, 0}, {1, 0, 0, 0}, {2, -1, 0, 0}, {3, -3, 1, 0}, {4, -6, 4, -1}}};

        struct frame_header final {
            int block_size{};
            int channels{};
            int channel_assignment{};
            int bits_per_sample{};
        };

        // Frame header: 14-bit sync, 1 reserved, 1 blocking-strategy, then block /
        // sample-rate / channel / sample-size fields, the UTF-8 coded number, any
        // explicit block-size / sample-rate bytes, and the CRC-8. The reserved +
        // blocking-strategy bit (which the bare 14+1 read in some references drops)
        // is read explicitly here -- the header CRC-8 below proves the alignment.
        [[nodiscard]] bool parse_frame_header(bit_reader& br, frame_header& h) {
            const std::size_t frame_start = br.aligned_index();
            if (br.read(14) != 0x3FFEU) {
                return false;
            }
            if (br.read(1) != 0U) {
                return false; // reserved bit must be 0
            }
            (void)br.read(1); // blocking strategy (unused: we decode by sample count)

            const std::uint32_t bs_code = br.read(4);
            const std::uint32_t sr_code = br.read(4);
            const std::uint32_t ch_asn = br.read(4);
            const std::uint32_t ss_code = br.read(3);
            if (br.read(1) != 0U || br.error) {
                return false; // reserved bit must be 0
            }

            (void)br.read_utf8(); // frame/sample number
            if (br.error) {
                return false;
            }

            int blk = k_fixed_block_size[bs_code & 0xFU];
            if (blk < 0) {
                if (bs_code == 6U) {
                    blk = static_cast<int>(br.read(8) + 1U);
                } else if (bs_code == 7U) {
                    blk = static_cast<int>(br.read(16) + 1U);
                } else {
                    return false;
                }
            }
            h.block_size = blk;

            switch (sr_code) {
            case 12:
                (void)br.read(8);
                break;
            case 13:
            case 14:
                (void)br.read(16);
                break;
            case 15:
                return false; // invalid
            default:
                break; // 0-11: from streaminfo / fixed table (unused)
            }

            if (ch_asn < 8U) {
                h.channels = static_cast<int>(ch_asn) + 1;
                h.channel_assignment = static_cast<int>(ch_asn);
            } else if (ch_asn < 11U) {
                h.channels = 2;
                h.channel_assignment = static_cast<int>(ch_asn);
            } else {
                return false;
            }

            switch (ss_code) {
            case 0:
                h.bits_per_sample = 0;
                break; // from streaminfo
            case 1:
                h.bits_per_sample = 8;
                break;
            case 2:
                h.bits_per_sample = 12;
                break;
            case 4:
                h.bits_per_sample = 16;
                break;
            case 5:
                h.bits_per_sample = 20;
                break;
            case 6:
                h.bits_per_sample = 24;
                break;
            case 7:
                h.bits_per_sample = 32;
                break;
            default:
                return false;
            }

            // CRC-8 over the header bytes (sync through here, byte-aligned).
            const std::size_t crc_pos = br.aligned_index();
            const std::uint32_t got = br.read(8);
            if (br.error) {
                return false;
            }
            const std::uint8_t want = crc8(br.in.subspan(frame_start, crc_pos - frame_start));
            return got == want;
        }

        // Partitioned-Rice residual (RFC 9639 §9.2.7). The 2^partition_order
        // partitions divide the FULL block_size; only the first partition omits the
        // `predictor_order` warm-up samples. Writing `block_size - order` residuals
        // to `out`.
        [[nodiscard]] bool decode_residual(bit_reader& br, std::int32_t* out, int block_size,
                                           int predictor_order) {
            const std::uint32_t method = br.read(2);
            if (method > 1U) {
                return false;
            }
            const int param_bits = (method == 0U) ? 4 : 5;
            const std::uint32_t partition_order = br.read(4);
            const int n_partitions = 1 << partition_order;
            if (block_size <= 0 || (block_size % n_partitions) != 0) {
                return false;
            }
            const int per_part = block_size >> partition_order;
            if (per_part < predictor_order) {
                return false; // first partition would have a negative sample count
            }
            const std::uint32_t escape = (method == 0U) ? 15U : 31U;
            int idx = 0;
            for (int p = 0; p < n_partitions; ++p) {
                int count = per_part;
                if (p == 0) {
                    count -= predictor_order;
                }
                if (count < 0) {
                    return false;
                }
                const std::uint32_t rice = br.read(static_cast<int>(param_bits));
                if (rice == escape) {
                    const int raw_bits = static_cast<int>(br.read(5));
                    for (int s = 0; s < count; ++s) {
                        out[idx++] = raw_bits > 0 ? br.read_signed(raw_bits) : 0;
                    }
                } else {
                    for (int s = 0; s < count; ++s) {
                        const std::uint32_t q = br.read_unary();
                        const std::uint32_t r = br.read(static_cast<int>(rice));
                        const std::uint32_t u = (q << rice) | r;
                        std::int32_t v = static_cast<std::int32_t>(u >> 1U);
                        if ((u & 1U) != 0U) {
                            v = -v - 1;
                        }
                        out[idx++] = v;
                    }
                }
            }
            return !br.error;
        }

        [[nodiscard]] bool decode_subframe(bit_reader& br, std::int32_t* samples, int block_size,
                                           int bps) {
            if (br.read(1) != 0U) {
                return false; // reserved
            }
            const std::uint32_t type = br.read(6);
            std::uint32_t wasted = 0;
            if (br.read(1) != 0U) {
                wasted = br.read_unary() + 1U;
                bps -= static_cast<int>(wasted);
            }
            if (br.error || bps <= 0 || bps > 32) {
                return false;
            }

            const auto apply_wasted = [&] {
                if (wasted != 0U) {
                    for (int i = 0; i < block_size; ++i) {
                        samples[i] = static_cast<std::int32_t>(
                            static_cast<std::uint32_t>(samples[i]) << wasted);
                    }
                }
                return !br.error;
            };

            if (type == 0U) { // CONSTANT
                const std::int32_t v = br.read_signed(bps);
                for (int i = 0; i < block_size; ++i) {
                    samples[i] = v;
                }
                return apply_wasted();
            }
            if (type == 1U) { // VERBATIM
                for (int i = 0; i < block_size; ++i) {
                    samples[i] = br.read_signed(bps);
                }
                return apply_wasted();
            }

            int order = 0;
            bool is_fixed = false;
            bool is_lpc = false;
            if (type >= 8U && type <= 12U) {
                order = static_cast<int>(type) - 8;
                is_fixed = true;
            } else if (type >= 32U && type <= 63U) {
                order = static_cast<int>(type) - 31;
                is_lpc = true;
            } else {
                return false; // reserved subframe type
            }
            if (order > block_size) {
                return false;
            }

            for (int i = 0; i < order; ++i) {
                samples[i] = br.read_signed(bps);
            }

            std::array<std::int32_t, 32> lpc_coef{};
            int lpc_shift = 0;
            if (is_lpc) {
                const int precision = static_cast<int>(br.read(4)) + 1;
                if (precision > 32) {
                    return false;
                }
                lpc_shift = br.read_signed(5);
                if (lpc_shift < 0) {
                    return false; // negative shift is invalid for FLAC
                }
                for (int i = 0; i < order; ++i) {
                    lpc_coef[static_cast<std::size_t>(i)] = br.read_signed(precision);
                }
            }

            if (!decode_residual(br, samples + order, block_size, order)) {
                return false;
            }

            if (is_fixed) {
                const auto& c = k_fixed_coef[static_cast<std::size_t>(order)];
                for (int i = order; i < block_size; ++i) {
                    std::int64_t pred = 0;
                    for (int k = 0; k < order; ++k) {
                        pred += static_cast<std::int64_t>(c[static_cast<std::size_t>(k)]) *
                                samples[i - 1 - k];
                    }
                    samples[i] += static_cast<std::int32_t>(pred);
                }
            } else if (is_lpc) {
                for (int i = order; i < block_size; ++i) {
                    std::int64_t pred = 0;
                    for (int k = 0; k < order; ++k) {
                        pred += static_cast<std::int64_t>(lpc_coef[static_cast<std::size_t>(k)]) *
                                samples[i - 1 - k];
                    }
                    samples[i] += static_cast<std::int32_t>(pred >> lpc_shift);
                }
            }
            return apply_wasted();
        }

        // Decode one frame into l/r sample buffers; returns the block size or 0.
        [[nodiscard]] int decode_frame(bit_reader& br, std::int32_t* l, std::int32_t* r,
                                       int max_block) {
            const std::size_t frame_start = br.aligned_index();
            frame_header h;
            if (!parse_frame_header(br, h)) {
                return 0;
            }
            if ((h.channels != 1 && h.channels != 2) || h.block_size <= 0 ||
                h.block_size > max_block) {
                return 0;
            }
            if (h.bits_per_sample != 16 && h.bits_per_sample != 0) {
                return 0; // CD audio is 16-bit
            }
            const int bps = h.bits_per_sample != 0 ? h.bits_per_sample : 16;

            if (h.channels == 1) {
                if (!decode_subframe(br, l, h.block_size, bps)) {
                    return 0;
                }
                std::memcpy(r, l, static_cast<std::size_t>(h.block_size) * sizeof(std::int32_t));
            } else {
                int bps_l = bps;
                int bps_r = bps;
                switch (h.channel_assignment) {
                case 8:
                    bps_r = bps + 1;
                    break; // left / side
                case 9:
                    bps_l = bps + 1;
                    break; // side / right
                case 10:
                    bps_r = bps + 1;
                    break; // mid / side
                default:
                    break;
                }
                if (!decode_subframe(br, l, h.block_size, bps_l) ||
                    !decode_subframe(br, r, h.block_size, bps_r)) {
                    return 0;
                }
                if (h.channel_assignment == 8) { // l, side -> r = l - side
                    for (int i = 0; i < h.block_size; ++i) {
                        r[i] = l[i] - r[i];
                    }
                } else if (h.channel_assignment == 9) { // side, r -> l = r + side
                    for (int i = 0; i < h.block_size; ++i) {
                        l[i] = r[i] + l[i];
                    }
                } else if (h.channel_assignment == 10) { // mid, side
                    for (int i = 0; i < h.block_size; ++i) {
                        const std::int32_t side = r[i];
                        std::int32_t mid = l[i];
                        mid = (mid << 1) | (side & 1);
                        l[i] = (mid + side) >> 1;
                        r[i] = (mid - side) >> 1;
                    }
                }
            }

            // Footer: zero-pad to byte boundary, then CRC-16 over the whole frame.
            br.align_byte();
            const std::size_t crc_pos = br.aligned_index();
            const std::uint16_t want = crc16(br.in.subspan(frame_start, crc_pos - frame_start));
            const std::uint32_t got = br.read(16);
            if (br.error || got != want) {
                return 0;
            }
            return h.block_size;
        }

    } // namespace

    std::optional<std::size_t> flac_decode_interleaved(std::span<const std::uint8_t> in,
                                                       std::uint32_t sample_pairs,
                                                       std::span<std::int16_t> out) {
        if (out.size() < static_cast<std::size_t>(sample_pairs) * 2U) {
            return std::nullopt;
        }
        // Per-frame channel scratch; FLAC blocks are bounded (<= 65535 samples).
        constexpr int k_max_block = 65535;
        std::vector<std::int32_t> lbuf(static_cast<std::size_t>(k_max_block));
        std::vector<std::int32_t> rbuf(static_cast<std::size_t>(k_max_block));

        bit_reader br{.in = in};
        std::uint32_t produced = 0;
        while (produced < sample_pairs) {
            const int block = decode_frame(br, lbuf.data(), rbuf.data(), k_max_block);
            if (block <= 0) {
                return std::nullopt;
            }
            const std::uint32_t take =
                std::min<std::uint32_t>(static_cast<std::uint32_t>(block), sample_pairs - produced);
            for (std::uint32_t i = 0; i < take; ++i) {
                out[static_cast<std::size_t>(produced + i) * 2U] =
                    static_cast<std::int16_t>(lbuf[i]);
                out[static_cast<std::size_t>(produced + i) * 2U + 1U] =
                    static_cast<std::int16_t>(rbuf[i]);
            }
            produced += static_cast<std::uint32_t>(block);
        }
        return br.aligned_index();
    }

} // namespace mnemos::disc
