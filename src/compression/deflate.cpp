#include "deflate.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace mnemos::compression {

    namespace {

        // ---- LSB-first bit writer (RFC 1951 §3.1.1 bit ordering) ----
        // Data elements pack LSB-first; Huffman codes are pre-reversed (see
        // build_codes) so that emitting them LSB-first puts them MSB-first on the
        // wire, matching how inflate_raw reads them back.
        struct bit_writer final {
            std::vector<std::uint8_t>& out;
            std::uint32_t bit_buf{0};
            int bit_count{0};

            void put_bits(std::uint32_t value, int count) {
                if (count == 0) {
                    return;
                }
                bit_buf |= (value & ((1U << count) - 1U)) << bit_count;
                bit_count += count;
                while (bit_count >= 8) {
                    out.push_back(static_cast<std::uint8_t>(bit_buf & 0xFFU));
                    bit_buf >>= 8U;
                    bit_count -= 8;
                }
            }

            void align_to_byte() {
                if (bit_count > 0) {
                    out.push_back(static_cast<std::uint8_t>(bit_buf & 0xFFU));
                    bit_buf = 0;
                    bit_count = 0;
                }
            }
        };

        struct huff_code final {
            std::uint16_t code{}; // bit-reversed canonical code, ready for put_bits
            std::uint8_t len{};
        };

        // Canonical Huffman code assignment (RFC 1951 §3.2.2), returning each
        // code bit-reversed over its length for LSB-first emission.
        void build_codes(const std::uint8_t* lengths, int n, huff_code* out_codes) {
            std::array<int, 16> bl_count{};
            for (int i = 0; i < n; ++i) {
                ++bl_count[lengths[i]];
            }
            bl_count[0] = 0;

            std::array<int, 16> next_code{};
            int code = 0;
            for (int bits = 1; bits <= 15; ++bits) {
                code = (code + bl_count[bits - 1]) << 1;
                next_code[bits] = code;
            }

            for (int i = 0; i < n; ++i) {
                const int len = lengths[i];
                if (len == 0) {
                    out_codes[i] = {0, 0};
                    continue;
                }
                const int c = next_code[len]++;
                std::uint16_t rev = 0;
                for (int b = 0; b < len; ++b) {
                    rev = static_cast<std::uint16_t>((rev << 1) | ((c >> b) & 1));
                }
                out_codes[i] = {rev, static_cast<std::uint8_t>(len)};
            }
        }

        // ---- length / distance code tables (RFC 1951 §3.2.5) ----
        constexpr std::array<std::uint16_t, 29> kLenBase = {
            3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
            31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        constexpr std::array<std::uint8_t, 29> kLenExtra = {
            0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        constexpr std::array<std::uint16_t, 30> kDistBase = {
            1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
            193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        constexpr std::array<std::uint8_t, 30> kDistExtra = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                                             4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                                             9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

        struct symbol_extra final {
            int symbol{};
            int extra_bits{};
            int extra_value{};
        };

        // Map a match length [3,258] to its literal/length symbol (257..285).
        symbol_extra encode_length(int len) {
            for (int i = 28; i >= 0; --i) {
                if (len >= kLenBase[static_cast<std::size_t>(i)]) {
                    return {257 + i, kLenExtra[static_cast<std::size_t>(i)],
                            len - kLenBase[static_cast<std::size_t>(i)]};
                }
            }
            return {257, 0, 0};
        }

        // Map a back-reference distance [1,32768] to its distance symbol (0..29).
        symbol_extra encode_distance(int dist) {
            for (int i = 29; i >= 0; --i) {
                if (dist >= kDistBase[static_cast<std::size_t>(i)]) {
                    return {i, kDistExtra[static_cast<std::size_t>(i)],
                            dist - kDistBase[static_cast<std::size_t>(i)]};
                }
            }
            return {0, 0, 0};
        }

        // Fixed Huffman code lengths (RFC 1951 §3.2.6).
        const std::array<huff_code, 288>& fixed_litlen_codes() {
            static const std::array<huff_code, 288> codes = [] {
                std::array<std::uint8_t, 288> lengths{};
                for (int i = 0; i <= 143; ++i) {
                    lengths[static_cast<std::size_t>(i)] = 8;
                }
                for (int i = 144; i <= 255; ++i) {
                    lengths[static_cast<std::size_t>(i)] = 9;
                }
                for (int i = 256; i <= 279; ++i) {
                    lengths[static_cast<std::size_t>(i)] = 7;
                }
                for (int i = 280; i <= 287; ++i) {
                    lengths[static_cast<std::size_t>(i)] = 8;
                }
                std::array<huff_code, 288> c{};
                build_codes(lengths.data(), 288, c.data());
                return c;
            }();
            return codes;
        }

        const std::array<huff_code, 30>& fixed_dist_codes() {
            static const std::array<huff_code, 30> codes = [] {
                std::array<std::uint8_t, 30> lengths{};
                lengths.fill(5);
                std::array<huff_code, 30> c{};
                build_codes(lengths.data(), 30, c.data());
                return c;
            }();
            return codes;
        }

        // ---- LZ77 match finding ----
        constexpr int kMinMatch = 3;
        constexpr int kMaxMatch = 258;
        constexpr int kWindow = 32768;
        constexpr int kHashBits = 15;
        constexpr int kHashSize = 1 << kHashBits;
        constexpr int kMaxChain = 256;

        inline std::uint32_t hash3(const std::uint8_t* p) {
            const std::uint32_t v = (static_cast<std::uint32_t>(p[0]) << 16) |
                                    (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
            return (v * 2654435761U) >> (32 - kHashBits);
        }

        // Emit `src` as a single fixed-Huffman block (BFINAL=1, BTYPE=01).
        std::vector<std::uint8_t> deflate_fixed(std::span<const std::uint8_t> src) {
            std::vector<std::uint8_t> out;
            out.reserve(src.size() / 2 + 16);
            bit_writer bw{out};

            const auto& litlen = fixed_litlen_codes();
            const auto& dist = fixed_dist_codes();

            bw.put_bits(1, 1); // BFINAL
            bw.put_bits(1, 2); // BTYPE = 01 (fixed Huffman)

            const std::size_t n = src.size();
            const std::uint8_t* s = src.data();
            std::vector<int> head(static_cast<std::size_t>(kHashSize), -1);
            std::vector<int> prev(n, -1);

            std::size_t pos = 0;
            while (pos < n) {
                int best_len = 0;
                int best_dist = 0;
                if (pos + kMinMatch <= n) {
                    const std::uint32_t h = hash3(s + pos);
                    int cand = head[h];
                    int chain = kMaxChain;
                    const int max_len = static_cast<int>(std::min<std::size_t>(kMaxMatch, n - pos));
                    while (cand >= 0 &&
                           static_cast<int>(pos - static_cast<std::size_t>(cand)) <= kWindow &&
                           chain-- > 0) {
                        if (best_len > 0 && s[static_cast<std::size_t>(cand) +
                                              static_cast<std::size_t>(best_len)] !=
                                                s[pos + static_cast<std::size_t>(best_len)]) {
                            cand = prev[static_cast<std::size_t>(cand)];
                            continue;
                        }
                        int len = 0;
                        while (len < max_len &&
                               s[static_cast<std::size_t>(cand) + static_cast<std::size_t>(len)] ==
                                   s[pos + static_cast<std::size_t>(len)]) {
                            ++len;
                        }
                        if (len > best_len) {
                            best_len = len;
                            best_dist = static_cast<int>(pos - static_cast<std::size_t>(cand));
                            if (len >= max_len) {
                                break;
                            }
                        }
                        cand = prev[static_cast<std::size_t>(cand)];
                    }
                    prev[pos] = head[h];
                    head[h] = static_cast<int>(pos);
                }

                if (best_len >= kMinMatch) {
                    const symbol_extra len_sym = encode_length(best_len);
                    bw.put_bits(litlen[static_cast<std::size_t>(len_sym.symbol)].code,
                                litlen[static_cast<std::size_t>(len_sym.symbol)].len);
                    bw.put_bits(static_cast<std::uint32_t>(len_sym.extra_value),
                                len_sym.extra_bits);
                    const symbol_extra dist_sym = encode_distance(best_dist);
                    bw.put_bits(dist[static_cast<std::size_t>(dist_sym.symbol)].code,
                                dist[static_cast<std::size_t>(dist_sym.symbol)].len);
                    bw.put_bits(static_cast<std::uint32_t>(dist_sym.extra_value),
                                dist_sym.extra_bits);

                    for (int k = 1; k < best_len; ++k) {
                        const std::size_t q = pos + static_cast<std::size_t>(k);
                        if (q + kMinMatch <= n) {
                            const std::uint32_t hh = hash3(s + q);
                            prev[q] = head[hh];
                            head[hh] = static_cast<int>(q);
                        }
                    }
                    pos += static_cast<std::size_t>(best_len);
                } else {
                    bw.put_bits(litlen[s[pos]].code, litlen[s[pos]].len);
                    ++pos;
                }
            }

            bw.put_bits(litlen[256].code, litlen[256].len); // end of block
            bw.align_to_byte();
            return out;
        }

        // Emit `src` as stored (BTYPE=00) blocks -- the guaranteed-no-expansion
        // fallback for incompressible or tiny input.
        std::vector<std::uint8_t> deflate_stored(std::span<const std::uint8_t> src) {
            constexpr std::size_t kMaxBlock = 65535;
            std::vector<std::uint8_t> out;
            const std::size_t blocks = src.empty() ? 1 : (src.size() + kMaxBlock - 1) / kMaxBlock;
            out.reserve(src.size() + blocks * 5);

            std::size_t off = 0;
            for (std::size_t b = 0; b < blocks; ++b) {
                const std::size_t len = std::min(kMaxBlock, src.size() - off);
                const bool final_block = (b + 1 == blocks);
                out.push_back(static_cast<std::uint8_t>(final_block ? 1 : 0)); // BFINAL, BTYPE=00
                const auto len16 = static_cast<std::uint16_t>(len);
                const auto nlen16 = static_cast<std::uint16_t>(~len16);
                out.push_back(static_cast<std::uint8_t>(len16 & 0xFFU));
                out.push_back(static_cast<std::uint8_t>(len16 >> 8U));
                out.push_back(static_cast<std::uint8_t>(nlen16 & 0xFFU));
                out.push_back(static_cast<std::uint8_t>(nlen16 >> 8U));
                out.insert(out.end(), src.begin() + static_cast<std::ptrdiff_t>(off),
                           src.begin() + static_cast<std::ptrdiff_t>(off + len));
                off += len;
            }
            return out;
        }

        std::uint32_t adler32(std::span<const std::uint8_t> data) {
            constexpr std::uint32_t kMod = 65521;
            std::uint32_t a = 1;
            std::uint32_t b = 0;
            for (const std::uint8_t byte : data) {
                a = (a + byte) % kMod;
                b = (b + a) % kMod;
            }
            return (b << 16) | a;
        }

    } // namespace

    std::vector<std::uint8_t> deflate_raw(std::span<const std::uint8_t> src) {
        std::vector<std::uint8_t> fixed = deflate_fixed(src);
        std::vector<std::uint8_t> stored = deflate_stored(src);
        return fixed.size() <= stored.size() ? fixed : stored;
    }

    std::vector<std::uint8_t> deflate_zlib(std::span<const std::uint8_t> src) {
        std::vector<std::uint8_t> out;
        out.push_back(0x78); // CMF: deflate, 32K window
        out.push_back(0x9C); // FLG: default level, (CMF*256+FLG) % 31 == 0

        const std::vector<std::uint8_t> body = deflate_raw(src);
        out.insert(out.end(), body.begin(), body.end());

        const std::uint32_t adler = adler32(src);
        out.push_back(static_cast<std::uint8_t>((adler >> 24) & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((adler >> 16) & 0xFFU));
        out.push_back(static_cast<std::uint8_t>((adler >> 8) & 0xFFU));
        out.push_back(static_cast<std::uint8_t>(adler & 0xFFU));
        return out;
    }

} // namespace mnemos::compression
