#include "deflate.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>

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

        // Optimal canonical Huffman code lengths for `freq[0..n)` capped at
        // `max_bits`. Unused symbols get length 0. The length multiset comes from
        // a Huffman tree; over-long codes are pulled in by demoting shallower
        // leaves (each step preserves the leaf count and reduces the Kraft sum by
        // one), then lengths are handed to symbols longest-first by ascending
        // frequency -- the canonical optimal assignment.
        void build_huffman_lengths(const std::uint32_t* freq, int n, int max_bits,
                                   std::uint8_t* len_out) {
            for (int i = 0; i < n; ++i) {
                len_out[i] = 0;
            }

            struct leaf_t final {
                std::uint32_t freq;
                int sym;
            };
            std::vector<leaf_t> used;
            for (int i = 0; i < n; ++i) {
                if (freq[i] > 0) {
                    used.push_back({freq[i], i});
                }
            }
            if (used.empty()) {
                return;
            }
            if (used.size() == 1) {
                len_out[used[0].sym] = 1; // a lone symbol still needs one bit
                return;
            }

            // Build the tree: leaves are node ids 0..leaves-1, internal nodes grow
            // the arrays. A min-heap keeps the two lowest-frequency subtrees.
            const int leaves = static_cast<int>(used.size());
            std::vector<std::uint32_t> nfreq;
            std::vector<int> nleft;
            std::vector<int> nright;
            nfreq.reserve(static_cast<std::size_t>(leaves) * 2U);
            for (const leaf_t& u : used) {
                nfreq.push_back(u.freq);
                nleft.push_back(-1);
                nright.push_back(-1);
            }

            const auto worse = [&](int a, int b) {
                if (nfreq[static_cast<std::size_t>(a)] != nfreq[static_cast<std::size_t>(b)]) {
                    return nfreq[static_cast<std::size_t>(a)] > nfreq[static_cast<std::size_t>(b)];
                }
                return a > b; // deterministic tie-break
            };
            std::priority_queue<int, std::vector<int>, decltype(worse)> heap(worse);
            for (int i = 0; i < leaves; ++i) {
                heap.push(i);
            }
            while (heap.size() > 1) {
                const int a = heap.top();
                heap.pop();
                const int b = heap.top();
                heap.pop();
                const int id = static_cast<int>(nfreq.size());
                nfreq.push_back(nfreq[static_cast<std::size_t>(a)] +
                                nfreq[static_cast<std::size_t>(b)]);
                nleft.push_back(a);
                nright.push_back(b);
                heap.push(id);
            }
            const int root = heap.top();

            std::vector<int> depth(nfreq.size(), 0);
            std::vector<std::pair<int, int>> stack;
            stack.push_back({root, 0});
            int maxlen = 0;
            while (!stack.empty()) {
                const auto [id, d] = stack.back();
                stack.pop_back();
                if (nleft[static_cast<std::size_t>(id)] == -1) {
                    depth[static_cast<std::size_t>(id)] = d;
                    maxlen = std::max(maxlen, d);
                } else {
                    stack.push_back({nleft[static_cast<std::size_t>(id)], d + 1});
                    stack.push_back({nright[static_cast<std::size_t>(id)], d + 1});
                }
            }

            std::vector<int> bl_count(static_cast<std::size_t>(std::max(maxlen, max_bits)) + 1U, 0);
            for (int i = 0; i < leaves; ++i) {
                ++bl_count[static_cast<std::size_t>(depth[static_cast<std::size_t>(i)])];
            }

            if (maxlen > max_bits) {
                for (int b = maxlen; b > max_bits; --b) {
                    bl_count[static_cast<std::size_t>(max_bits)] +=
                        bl_count[static_cast<std::size_t>(b)];
                    bl_count[static_cast<std::size_t>(b)] = 0;
                }
                const auto kraft = [&]() {
                    long long k = 0;
                    for (int b = 1; b <= max_bits; ++b) {
                        k += static_cast<long long>(bl_count[static_cast<std::size_t>(b)])
                             << (max_bits - b);
                    }
                    return k;
                };
                const long long target = 1LL << max_bits;
                while (kraft() > target) {
                    int b = max_bits - 1;
                    while (b >= 1 && bl_count[static_cast<std::size_t>(b)] == 0) {
                        --b;
                    }
                    --bl_count[static_cast<std::size_t>(b)];
                    bl_count[static_cast<std::size_t>(b + 1)] += 2;
                    --bl_count[static_cast<std::size_t>(max_bits)];
                }
            }

            std::sort(used.begin(), used.end(), [](const leaf_t& a, const leaf_t& b) {
                return a.freq != b.freq ? a.freq < b.freq : a.sym < b.sym;
            });
            const int top = std::min(maxlen, max_bits);
            std::size_t idx = 0;
            for (int b = top; b >= 1; --b) {
                for (int k = 0; k < bl_count[static_cast<std::size_t>(b)]; ++k) {
                    len_out[used[idx++].sym] = static_cast<std::uint8_t>(b);
                }
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

        // A literal (dist == 0, length holds the byte) or a back-reference
        // (dist > 0, length is the match length 3..258).
        struct token final {
            std::uint16_t length;
            std::uint16_t dist;
        };

        struct lz_result final {
            std::vector<token> tokens;
            std::array<std::uint32_t, 288> litlen_freq{};
            std::array<std::uint32_t, 30> dist_freq{};
        };

        // Greedy LZ77 over the whole input, producing the token stream plus the
        // symbol histograms a dynamic block needs.
        lz_result lz77_tokenize(std::span<const std::uint8_t> src) {
            lz_result r;
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
                    r.tokens.push_back({static_cast<std::uint16_t>(best_len),
                                        static_cast<std::uint16_t>(best_dist)});
                    ++r.litlen_freq[static_cast<std::size_t>(encode_length(best_len).symbol)];
                    ++r.dist_freq[static_cast<std::size_t>(encode_distance(best_dist).symbol)];
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
                    r.tokens.push_back({s[pos], 0});
                    ++r.litlen_freq[s[pos]];
                    ++pos;
                }
            }
            ++r.litlen_freq[256]; // end-of-block, emitted once
            return r;
        }

        // Emit the token stream with the given canonical codes, terminated by the
        // end-of-block symbol (256).
        void emit_tokens(bit_writer& bw, const std::vector<token>& tokens, const huff_code* litlen,
                         const huff_code* dist) {
            for (const token& t : tokens) {
                if (t.dist == 0) {
                    bw.put_bits(litlen[t.length].code, litlen[t.length].len);
                    continue;
                }
                const symbol_extra ls = encode_length(t.length);
                bw.put_bits(litlen[ls.symbol].code, litlen[ls.symbol].len);
                bw.put_bits(static_cast<std::uint32_t>(ls.extra_value), ls.extra_bits);
                const symbol_extra ds = encode_distance(t.dist);
                bw.put_bits(dist[ds.symbol].code, dist[ds.symbol].len);
                bw.put_bits(static_cast<std::uint32_t>(ds.extra_value), ds.extra_bits);
            }
            bw.put_bits(litlen[256].code, litlen[256].len);
        }

        std::vector<std::uint8_t> emit_fixed(const lz_result& lz) {
            std::vector<std::uint8_t> out;
            bit_writer bw{out};
            bw.put_bits(1, 1); // BFINAL
            bw.put_bits(1, 2); // BTYPE = 01 (fixed Huffman)
            emit_tokens(bw, lz.tokens, fixed_litlen_codes().data(), fixed_dist_codes().data());
            bw.align_to_byte();
            return out;
        }

        // The code-length alphabet RLE (RFC 1951 §3.2.7): a literal length 0..15,
        // 16 = repeat previous 3-6x, 17 = repeat zero 3-10x, 18 = repeat zero
        // 11-138x.
        struct cl_item final {
            std::uint8_t symbol;
            std::uint8_t extra;
        };

        void rle_code_lengths(const std::uint8_t* lengths, int count, std::vector<cl_item>& out,
                              std::array<std::uint32_t, 19>& freq) {
            int i = 0;
            while (i < count) {
                const int cur = lengths[i];
                int run = 1;
                while (i + run < count && lengths[i + run] == cur) {
                    ++run;
                }
                i += run;
                if (cur != 0) {
                    out.push_back({static_cast<std::uint8_t>(cur), 0});
                    ++freq[static_cast<std::size_t>(cur)];
                    int rep = run - 1;
                    while (rep >= 3) {
                        const int r = std::min(rep, 6);
                        out.push_back({16, static_cast<std::uint8_t>(r - 3)});
                        ++freq[16];
                        rep -= r;
                    }
                    while (rep-- > 0) {
                        out.push_back({static_cast<std::uint8_t>(cur), 0});
                        ++freq[static_cast<std::size_t>(cur)];
                    }
                } else {
                    int rep = run;
                    while (rep >= 11) {
                        const int r = std::min(rep, 138);
                        out.push_back({18, static_cast<std::uint8_t>(r - 11)});
                        ++freq[18];
                        rep -= r;
                    }
                    while (rep >= 3) {
                        const int r = std::min(rep, 10);
                        out.push_back({17, static_cast<std::uint8_t>(r - 3)});
                        ++freq[17];
                        rep -= r;
                    }
                    while (rep-- > 0) {
                        out.push_back({0, 0});
                        ++freq[0];
                    }
                }
            }
        }

        std::vector<std::uint8_t> emit_dynamic(const lz_result& lz) {
            std::array<std::uint8_t, 286> ll_len{};
            build_huffman_lengths(lz.litlen_freq.data(), 286, 15, ll_len.data());

            // A dynamic block must carry at least one distance code even when the
            // data has no matches; force symbol 0 so the tree is well-formed.
            std::array<std::uint32_t, 30> dist_freq = lz.dist_freq;
            if (std::none_of(dist_freq.begin(), dist_freq.end(),
                             [](std::uint32_t f) { return f > 0; })) {
                dist_freq[0] = 1;
            }
            std::array<std::uint8_t, 30> dl_len{};
            build_huffman_lengths(dist_freq.data(), 30, 15, dl_len.data());

            std::array<huff_code, 286> ll_codes{};
            build_codes(ll_len.data(), 286, ll_codes.data());
            std::array<huff_code, 30> dl_codes{};
            build_codes(dl_len.data(), 30, dl_codes.data());

            int hlit = 286;
            while (hlit > 257 && ll_len[static_cast<std::size_t>(hlit - 1)] == 0) {
                --hlit;
            }
            int hdist = 30;
            while (hdist > 1 && dl_len[static_cast<std::size_t>(hdist - 1)] == 0) {
                --hdist;
            }

            std::vector<std::uint8_t> seq;
            seq.reserve(static_cast<std::size_t>(hlit + hdist));
            for (int i = 0; i < hlit; ++i) {
                seq.push_back(ll_len[static_cast<std::size_t>(i)]);
            }
            for (int i = 0; i < hdist; ++i) {
                seq.push_back(dl_len[static_cast<std::size_t>(i)]);
            }

            std::vector<cl_item> rle;
            std::array<std::uint32_t, 19> cl_freq{};
            rle_code_lengths(seq.data(), static_cast<int>(seq.size()), rle, cl_freq);

            std::array<std::uint8_t, 19> cl_len{};
            build_huffman_lengths(cl_freq.data(), 19, 7, cl_len.data());
            std::array<huff_code, 19> cl_codes{};
            build_codes(cl_len.data(), 19, cl_codes.data());

            static constexpr std::array<int, 19> order = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                          11, 4,  12, 3, 13, 2, 14, 1, 15};
            int hclen = 19;
            while (hclen > 4 &&
                   cl_len[static_cast<std::size_t>(order[static_cast<std::size_t>(hclen - 1)])] ==
                       0) {
                --hclen;
            }

            std::vector<std::uint8_t> out;
            bit_writer bw{out};
            bw.put_bits(1, 1); // BFINAL
            bw.put_bits(2, 2); // BTYPE = 10 (dynamic Huffman)
            bw.put_bits(static_cast<std::uint32_t>(hlit - 257), 5);
            bw.put_bits(static_cast<std::uint32_t>(hdist - 1), 5);
            bw.put_bits(static_cast<std::uint32_t>(hclen - 4), 4);
            for (int i = 0; i < hclen; ++i) {
                bw.put_bits(cl_len[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])],
                            3);
            }
            for (const cl_item& it : rle) {
                bw.put_bits(cl_codes[it.symbol].code, cl_codes[it.symbol].len);
                if (it.symbol == 16) {
                    bw.put_bits(it.extra, 2);
                } else if (it.symbol == 17) {
                    bw.put_bits(it.extra, 3);
                } else if (it.symbol == 18) {
                    bw.put_bits(it.extra, 7);
                }
            }
            emit_tokens(bw, lz.tokens, ll_codes.data(), dl_codes.data());
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
        const lz_result lz = lz77_tokenize(src);
        std::vector<std::uint8_t> best = deflate_stored(src);
        if (std::vector<std::uint8_t> fixed = emit_fixed(lz); fixed.size() < best.size()) {
            best = std::move(fixed);
        }
        if (std::vector<std::uint8_t> dynamic = emit_dynamic(lz); dynamic.size() < best.size()) {
            best = std::move(dynamic);
        }
        return best;
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
