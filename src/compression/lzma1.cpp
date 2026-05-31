#include "lzma1.hpp"

#include <algorithm>
#include <array>
#include <vector>

namespace mnemos::compression {

    namespace {

        constexpr std::uint32_t kNumStates = 12U;
        constexpr std::uint32_t kNumLitStates = 7U;     // states 0..6 follow literals
        constexpr std::uint32_t kNumPosStatesMax = 16U; // 1 << pb_max, pb_max = 4

        constexpr std::uint32_t kMatchMinLen = 2U;
        constexpr std::uint32_t kNumLowLenBits = 3U;
        constexpr std::uint32_t kNumMidLenBits = 3U;
        constexpr std::uint32_t kNumHighLenBits = 8U;
        constexpr std::uint32_t kNumLowLenSyms = 1U << kNumLowLenBits;   // 8
        constexpr std::uint32_t kNumMidLenSyms = 1U << kNumMidLenBits;   // 8
        constexpr std::uint32_t kNumHighLenSyms = 1U << kNumHighLenBits; // 256

        constexpr std::uint32_t kNumLenToPosStates = 4U;
        constexpr std::uint32_t kNumPosSlotBits = 6U;
        constexpr std::uint32_t kNumPosSlots = 1U << kNumPosSlotBits;
        constexpr std::uint32_t kNumAlignBits = 4U;
        constexpr std::uint32_t kNumAlignSyms = 1U << kNumAlignBits;
        constexpr std::uint32_t kStartPosModelIdx = 4U;
        constexpr std::uint32_t kEndPosModelIdx = 14U;
        constexpr std::uint32_t kNumFullDistances = 1U << (kEndPosModelIdx / 2U);

        constexpr std::uint32_t kProbBits = 11U;
        constexpr std::uint16_t kProbInit = 1U << (kProbBits - 1U); // 1024
        constexpr std::uint32_t kMoveBits = 5U;

        // Range decoder keeps >= kTopValue units of range for 11-bit precision.
        constexpr std::uint32_t kTopValue = 0x01000000U;

        // The range coder flushes 5 trailing bytes; a complete decode of an
        // exactly-sized stream leaves at most this many bytes unconsumed.
        constexpr std::size_t kRcFlushBytes = 5U;

        // Three depth-8 bit-trees per literal context (sub-decoders).
        constexpr std::uint32_t kLitStateSize = 0x300U;

        // Post-op state transitions; steer subsequent probability contexts.
        constexpr std::array<std::uint8_t, kNumStates> kStateAfterLiteral = {0, 0, 0, 0, 1, 2,
                                                                             3, 4, 5, 6, 4, 5};
        constexpr std::array<std::uint8_t, kNumStates> kStateAfterMatch = {7, 7,  7,  7,  7,  7,
                                                                           7, 10, 10, 10, 10, 10};
        constexpr std::array<std::uint8_t, kNumStates> kStateAfterRep = {8, 8,  8,  8,  8,  8,
                                                                         8, 11, 11, 11, 11, 11};
        constexpr std::array<std::uint8_t, kNumStates> kStateAfterShortRep = {
            9, 9, 9, 9, 9, 9, 9, 11, 11, 11, 11, 11};

        struct len_decoder final {
            std::uint16_t choice{kProbInit};  // 0 -> low tree, 1 -> mid/high
            std::uint16_t choice2{kProbInit}; // 0 -> mid, 1 -> high
            std::array<std::array<std::uint16_t, kNumLowLenSyms>, kNumPosStatesMax> low{};
            std::array<std::array<std::uint16_t, kNumMidLenSyms>, kNumPosStatesMax> mid{};
            std::array<std::uint16_t, kNumHighLenSyms> high{};

            void reset() noexcept {
                choice = kProbInit;
                choice2 = kProbInit;
                for (auto& row : low) {
                    row.fill(kProbInit);
                }
                for (auto& row : mid) {
                    row.fill(kProbInit);
                }
                high.fill(kProbInit);
            }
        };

        struct decoder final {
            // Range coder.
            std::uint32_t range{};
            std::uint32_t code{};
            bool err{};

            // Input.
            std::span<const std::uint8_t> src;
            std::size_t src_pos{};

            // Output.
            std::span<std::uint8_t> dst;
            std::size_t out_pos{};

            // Config.
            std::uint8_t lc{}, lp{}, pb{};
            std::uint32_t lit_pos_mask{};
            std::uint32_t pb_mask{};

            // State machine.
            std::uint32_t state{};
            std::uint32_t rep0{}, rep1{}, rep2{}, rep3{};

            // Probability tables (all init to kProbInit).
            std::array<std::uint16_t, kNumStates * kNumPosStatesMax> is_match{};
            std::array<std::uint16_t, kNumStates> is_rep{};
            std::array<std::uint16_t, kNumStates> is_rep_g0{};
            std::array<std::uint16_t, kNumStates> is_rep_g1{};
            std::array<std::uint16_t, kNumStates> is_rep_g2{};
            std::array<std::uint16_t, kNumStates * kNumPosStatesMax> is_rep0_long{};

            std::array<std::array<std::uint16_t, kNumPosSlots>, kNumLenToPosStates> pos_slot{};
            // Sized to kNumFullDistances so the highest small-distance slot's
            // reverse bit-tree can't run past the end.
            std::array<std::uint16_t, kNumFullDistances> pos_encoders{};
            std::array<std::uint16_t, kNumAlignSyms> pos_align{};

            len_decoder len;
            len_decoder rep_len;

            // Literal probs, sized (1 << (lc + lp)) * kLitStateSize.
            std::vector<std::uint16_t> literal;
        };

        std::uint8_t rc_next_byte(decoder& s) noexcept {
            if (s.src_pos >= s.src.size()) {
                s.err = true;
                return 0;
            }
            return s.src[s.src_pos++];
        }

        void rc_normalize(decoder& s) noexcept {
            if (s.range < kTopValue) {
                s.range <<= 8U;
                s.code = (s.code << 8U) | rc_next_byte(s);
            }
        }

        // Decode one bit against `prob`, adapting it toward the result.
        std::uint32_t rc_decode_bit(decoder& s, std::uint16_t& prob) noexcept {
            const std::uint32_t p = prob;
            const std::uint32_t new_bound = (s.range >> kProbBits) * p;
            std::uint32_t bit;
            if (s.code < new_bound) {
                s.range = new_bound;
                prob = static_cast<std::uint16_t>(p + (((1U << kProbBits) - p) >> kMoveBits));
                bit = 0;
            } else {
                s.range -= new_bound;
                s.code -= new_bound;
                prob = static_cast<std::uint16_t>(p - (p >> kMoveBits));
                bit = 1;
            }
            rc_normalize(s);
            return bit;
        }

        // Forward bit-tree (high bit first).
        std::uint32_t rc_decode_bit_tree(decoder& s, std::uint16_t* probs,
                                         unsigned num_bits) noexcept {
            std::uint32_t index = 1;
            for (unsigned i = 0; i < num_bits; ++i) {
                index = (index << 1U) | rc_decode_bit(s, probs[index]);
            }
            return index - (1U << num_bits);
        }

        // Reverse bit-tree (low bit first).
        std::uint32_t rc_decode_reverse_bit_tree(decoder& s, std::uint16_t* probs,
                                                 unsigned num_bits) noexcept {
            std::uint32_t index = 1;
            std::uint32_t result = 0;
            for (unsigned i = 0; i < num_bits; ++i) {
                const std::uint32_t bit = rc_decode_bit(s, probs[index]);
                index = (index << 1U) | bit;
                result |= bit << i;
            }
            return result;
        }

        // Uncompressed direct bits (no probability).
        std::uint32_t rc_decode_direct_bits(decoder& s, unsigned num_bits) noexcept {
            std::uint32_t result = 0;
            for (unsigned i = 0; i < num_bits; ++i) {
                s.range >>= 1U;
                s.code -= s.range;
                const std::uint32_t t = 0U - (s.code >> 31U); // 0 if MSB clear, else all-ones
                s.code += s.range & t;
                result = (result << 1U) | (t + 1U);
                rc_normalize(s);
            }
            return result;
        }

        void init_probs(decoder& s) noexcept {
            s.is_match.fill(kProbInit);
            s.is_rep.fill(kProbInit);
            s.is_rep_g0.fill(kProbInit);
            s.is_rep_g1.fill(kProbInit);
            s.is_rep_g2.fill(kProbInit);
            s.is_rep0_long.fill(kProbInit);
            for (auto& row : s.pos_slot) {
                row.fill(kProbInit);
            }
            s.pos_encoders.fill(kProbInit);
            s.pos_align.fill(kProbInit);
            s.len.reset();
            s.rep_len.reset();
            std::fill(s.literal.begin(), s.literal.end(), kProbInit);
        }

        std::uint32_t decode_length(decoder& s, len_decoder& ld, std::uint32_t pos_state) noexcept {
            if (rc_decode_bit(s, ld.choice) == 0) {
                const std::uint32_t v =
                    rc_decode_bit_tree(s, ld.low[pos_state].data(), kNumLowLenBits);
                return kMatchMinLen + v; // 2..9
            }
            if (rc_decode_bit(s, ld.choice2) == 0) {
                const std::uint32_t v =
                    rc_decode_bit_tree(s, ld.mid[pos_state].data(), kNumMidLenBits);
                return kMatchMinLen + kNumLowLenSyms + v; // 10..17
            }
            const std::uint32_t v = rc_decode_bit_tree(s, ld.high.data(), kNumHighLenBits);
            return kMatchMinLen + kNumLowLenSyms + kNumMidLenSyms + v; // 18..273
        }

        // Literal context: (out_pos low bits) and (previous byte high bits).
        std::uint32_t lit_context(const decoder& s) noexcept {
            const std::uint32_t prev_byte = (s.out_pos == 0) ? 0U : s.dst[s.out_pos - 1];
            std::uint32_t ctx = (static_cast<std::uint32_t>(s.out_pos) & s.lit_pos_mask) << s.lc;
            ctx |= prev_byte >> (8U - s.lc);
            return ctx;
        }

        void decode_literal(decoder& s) noexcept {
            const std::uint32_t ctx = lit_context(s);
            std::uint16_t* probs = &s.literal[static_cast<std::size_t>(ctx) * kLitStateSize];
            std::uint32_t symbol = 1;
            if (s.state >= kNumLitStates) {
                // Matched-literal decode: steer bits by the rep0 match byte.
                const std::uint32_t rep_pos = s.rep0 + 1U;
                std::uint32_t match_byte = (s.out_pos >= rep_pos) ? s.dst[s.out_pos - rep_pos] : 0U;
                while (symbol < 0x100U) {
                    const std::uint32_t match_bit = (match_byte >> 7U) & 1U;
                    match_byte <<= 1U;
                    const std::uint32_t bit =
                        rc_decode_bit(s, probs[((1U + match_bit) << 8U) | symbol]);
                    symbol = (symbol << 1U) | bit;
                    if (match_bit != bit) {
                        while (symbol < 0x100U) {
                            symbol = (symbol << 1U) | rc_decode_bit(s, probs[symbol]);
                        }
                        break;
                    }
                }
            } else {
                while (symbol < 0x100U) {
                    symbol = (symbol << 1U) | rc_decode_bit(s, probs[symbol]);
                }
            }
            if (s.out_pos >= s.dst.size()) {
                s.err = true;
                return;
            }
            s.dst[s.out_pos++] = static_cast<std::uint8_t>(symbol & 0xFFU);
            s.state = kStateAfterLiteral[s.state];
        }

        std::uint32_t get_len_to_pos_state(std::uint32_t len) noexcept {
            len -= kMatchMinLen;
            if (len >= kNumLenToPosStates) {
                len = kNumLenToPosStates - 1U;
            }
            return len;
        }

        std::uint32_t decode_distance(decoder& s, std::uint32_t len) noexcept {
            const std::uint32_t len_pos_state = get_len_to_pos_state(len);
            const std::uint32_t pos_slot =
                rc_decode_bit_tree(s, s.pos_slot[len_pos_state].data(), kNumPosSlotBits);
            if (pos_slot < kStartPosModelIdx) {
                return pos_slot;
            }
            const std::uint32_t num_direct_bits = (pos_slot >> 1U) - 1U;
            const std::uint32_t base = (2U | (pos_slot & 1U)) << num_direct_bits;
            if (pos_slot < kEndPosModelIdx) {
                // Small-distance: probs at pos_encoders + (base - pos_slot).
                std::uint16_t* probs = &s.pos_encoders[base - pos_slot];
                return base + rc_decode_reverse_bit_tree(s, probs, num_direct_bits);
            }
            const std::uint32_t hi = rc_decode_direct_bits(s, num_direct_bits - kNumAlignBits);
            const std::uint32_t lo =
                rc_decode_reverse_bit_tree(s, s.pos_align.data(), kNumAlignBits);
            return base + (hi << kNumAlignBits) + lo;
        }

        // Copy a (distance, length) match; distance is 0-based (0 = previous byte).
        // Overlapping copies must go byte-by-byte. Matches running past out_size are
        // truncated, not errored (the main loop exits at out_size).
        void copy_match(decoder& s, std::uint32_t distance, std::uint32_t len) noexcept {
            const std::uint32_t rep_pos = distance + 1U;
            if (rep_pos > s.out_pos) {
                s.err = true;
                return;
            }
            for (std::uint32_t k = 0; k < len; ++k) {
                if (s.out_pos >= s.dst.size()) {
                    return; // truncate, not error
                }
                s.dst[s.out_pos] = s.dst[s.out_pos - rep_pos];
                ++s.out_pos;
            }
        }

        bool main_loop(decoder& s) noexcept {
            while (s.out_pos < s.dst.size()) {
                if (s.err) {
                    return false;
                }
                const std::uint32_t pos_state = static_cast<std::uint32_t>(s.out_pos) & s.pb_mask;
                const std::uint32_t is_match_idx = s.state * kNumPosStatesMax + pos_state;
                if (rc_decode_bit(s, s.is_match[is_match_idx]) == 0) {
                    decode_literal(s);
                    continue;
                }
                std::uint32_t length;
                if (rc_decode_bit(s, s.is_rep[s.state]) != 0) {
                    if (rc_decode_bit(s, s.is_rep_g0[s.state]) == 0) {
                        const std::uint32_t is_rep0_long_idx =
                            s.state * kNumPosStatesMax + pos_state;
                        if (rc_decode_bit(s, s.is_rep0_long[is_rep0_long_idx]) == 0) {
                            // Short rep: single-byte repeat of rep0.
                            if (s.out_pos >= s.dst.size() || s.out_pos < s.rep0 + 1U) {
                                return false;
                            }
                            s.dst[s.out_pos] = s.dst[s.out_pos - s.rep0 - 1U];
                            ++s.out_pos;
                            s.state = kStateAfterShortRep[s.state];
                            continue;
                        }
                        length = decode_length(s, s.rep_len, pos_state);
                    } else {
                        std::uint32_t distance;
                        if (rc_decode_bit(s, s.is_rep_g1[s.state]) == 0) {
                            distance = s.rep1;
                        } else {
                            if (rc_decode_bit(s, s.is_rep_g2[s.state]) == 0) {
                                distance = s.rep2;
                            } else {
                                distance = s.rep3;
                                s.rep3 = s.rep2;
                            }
                            s.rep2 = s.rep1;
                        }
                        s.rep1 = s.rep0;
                        s.rep0 = distance;
                        length = decode_length(s, s.rep_len, pos_state);
                    }
                    s.state = kStateAfterRep[s.state];
                } else {
                    s.rep3 = s.rep2;
                    s.rep2 = s.rep1;
                    s.rep1 = s.rep0;
                    length = decode_length(s, s.len, pos_state);
                    s.rep0 = decode_distance(s, length);
                    s.state = kStateAfterMatch[s.state];
                    if (s.rep0 == 0xFFFFFFFFU) {
                        return false; // end-of-stream marker, not valid with a known size
                    }
                }
                copy_match(s, s.rep0, length);
                if (s.err) {
                    return false;
                }
            }
            return true;
        }

        // Initialize the range coder and probability tables. false on any error.
        bool init_state(decoder& s, std::uint8_t lc, std::uint8_t lp, std::uint8_t pb,
                        std::span<const std::uint8_t> src, std::span<std::uint8_t> dst) {
            if (lc > 8 || lp > 4 || pb > 4) {
                return false;
            }
            if (src.size() < 5) {
                return false; // need 5 bytes for range init
            }

            s.lc = lc;
            s.lp = lp;
            s.pb = pb;
            s.lit_pos_mask = (1U << lp) - 1U;
            s.pb_mask = (1U << pb) - 1U;

            s.src = src;
            s.dst = dst;

            s.literal.resize((static_cast<std::size_t>(1) << (lc + lp)) * kLitStateSize);
            init_probs(s);

            // Range-coder init: first byte must be 0, then 4 big-endian bytes into
            // `code`; `range` starts at 0xFFFFFFFF. Part of the bitstream contract.
            if (rc_next_byte(s) != 0) {
                return false;
            }
            s.code = 0;
            for (int i = 0; i < 4; ++i) {
                s.code = (s.code << 8U) | rc_next_byte(s);
            }
            s.range = 0xFFFFFFFFU;
            return !s.err;
        }

    } // namespace

    std::optional<std::size_t> lzma1_decode(std::uint8_t lc, std::uint8_t lp, std::uint8_t pb,
                                            std::span<const std::uint8_t> src,
                                            std::span<std::uint8_t> dst) {
        decoder s;
        if (!init_state(s, lc, lp, pb, src, dst)) {
            return std::nullopt;
        }
        if (!main_loop(s) || s.out_pos != dst.size()) {
            return std::nullopt;
        }
        // A correctly-sized stream is consumed up to the range coder's 5-byte
        // flush; a leftover larger than that means dst was too small and we
        // stopped on a valid prefix. (The CHD caller slices src to the exact
        // base-stream length, so this leftover is purely the flush.)
        if (s.src.size() - s.src_pos > kRcFlushBytes) {
            return std::nullopt;
        }
        return s.src_pos;
    }

} // namespace mnemos::compression
