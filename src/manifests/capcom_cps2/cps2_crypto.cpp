#include "cps2_crypto.hpp"

#include <cstdlib>
#include <vector>

// CPS-2 68000 opcode cipher (two 4-round Feistel networks keyed by the 16-bit
// address + the 64-bit master key). The cipher is D = FN2(E, K xor EX(FN1(A,K))):
// the address A is run through the first network to seed a per-address subkey,
// which (xored with the master key) keys the second network that transforms the
// opcode word E. The s-box constants below are the documented hardware tables;
// see THIRD-PARTY-REFERENCES.md for the algorithm's provenance and license.

namespace mnemos::manifests::capcom_cps2 {
    namespace {

        constexpr int bit(std::uint32_t value, int n) noexcept {
            return static_cast<int>((value >> n) & 1U);
        }

        // Gather 8 bits of `value` selected by positions[0..7] into a byte.
        constexpr std::uint8_t gather8(std::uint32_t value,
                                       const std::array<int, 8>& positions) noexcept {
            std::uint8_t out = 0U;
            for (int i = 0; i < 8; ++i) {
                out = static_cast<std::uint8_t>(
                    out | (bit(value, positions[static_cast<std::size_t>(i)]) << i));
            }
            return out;
        }

        // The two Feistel networks split the 16-bit word into two 8-bit halves by
        // these bit selections (A = right half, B = left half).
        constexpr std::array<int, 8> fn1_group_a{10, 4, 6, 7, 2, 13, 15, 14};
        constexpr std::array<int, 8> fn1_group_b{0, 1, 3, 5, 8, 9, 11, 12};
        constexpr std::array<int, 8> fn2_group_a{6, 0, 2, 13, 1, 4, 14, 7};
        constexpr std::array<int, 8> fn2_group_b{3, 5, 9, 10, 8, 15, 12, 11};

        // One s-box: a 64-entry 2-bit table, the 6 input-bit positions (-1 = no
        // data input, key only), and the 2 output-bit positions. Each round of a
        // network is four of these.
        struct sbox {
            std::array<std::uint8_t, 64> table;
            std::array<int, 6> inputs;
            std::array<int, 2> outputs;

            [[nodiscard]] int extract_inputs(std::uint32_t value) const noexcept {
                int res = 0;
                for (int i = 0; i < 6; ++i) {
                    if (inputs[static_cast<std::size_t>(i)] >= 0) {
                        res |= bit(value, inputs[static_cast<std::size_t>(i)]) << i;
                    }
                }
                return res;
            }
        };

        // The hardware s-box flattened into a runtime lookup: input byte -> packed
        // input index, and index ^ key6 -> output mask at the box's output bits.
        class optimised_sbox {
          public:
            void optimise(const sbox& in) noexcept {
                for (int i = 0; i < 256; ++i) {
                    input_lookup_[static_cast<std::size_t>(i)] =
                        static_cast<std::uint8_t>(in.extract_inputs(static_cast<std::uint32_t>(i)));
                }
                for (int i = 0; i < 64; ++i) {
                    const int o = in.table[static_cast<std::size_t>(i)];
                    std::uint8_t mask = 0U;
                    if ((o & 1) != 0) {
                        mask = static_cast<std::uint8_t>(mask | (1U << in.outputs[0]));
                    }
                    if ((o & 2) != 0) {
                        mask = static_cast<std::uint8_t>(mask | (1U << in.outputs[1]));
                    }
                    output_[static_cast<std::size_t>(i)] = mask;
                }
            }

            [[nodiscard]] std::uint8_t apply(std::uint8_t in, std::uint32_t key) const noexcept {
                return output_[static_cast<std::size_t>(input_lookup_[in] ^ (key & 0x3FU))];
            }

          private:
            std::array<std::uint8_t, 256> input_lookup_{};
            std::array<std::uint8_t, 64> output_{};
        };

        // ---- s-box data: first Feistel network, rounds 1-4 (four boxes each) ----
        constexpr std::array<sbox, 4> fn1_r1_boxes{{
            {{0, 2, 2, 0, 1, 0, 1, 1, 3, 2, 0, 3, 0, 3, 1, 2, 1, 1, 1, 2, 1, 3,
              2, 2, 2, 3, 3, 2, 1, 1, 1, 2, 2, 2, 0, 0, 3, 1, 3, 1, 1, 1, 3, 0,
              0, 1, 0, 0, 1, 2, 2, 1, 2, 3, 2, 2, 2, 3, 1, 3, 2, 0, 1, 3},
             {3, 4, 5, 6, -1, -1},
             {3, 6}},
            {{3, 0, 2, 2, 2, 1, 1, 1, 1, 2, 1, 0, 0, 0, 2, 3, 2, 3, 1, 3, 0, 0,
              0, 2, 1, 2, 2, 3, 0, 3, 3, 3, 0, 1, 3, 2, 3, 3, 3, 1, 1, 1, 1, 2,
              0, 1, 2, 1, 3, 2, 3, 1, 1, 3, 2, 2, 2, 3, 1, 3, 2, 3, 0, 0},
             {0, 1, 2, 4, 7, -1},
             {2, 7}},
            {{3, 0, 3, 1, 1, 0, 2, 2, 3, 1, 2, 0, 3, 3, 2, 3, 0, 1, 0, 1, 2, 3,
              0, 2, 0, 2, 0, 1, 0, 0, 1, 0, 2, 3, 1, 2, 1, 0, 2, 0, 2, 1, 0, 1,
              0, 2, 1, 0, 3, 1, 2, 3, 1, 3, 1, 1, 1, 2, 0, 2, 2, 0, 0, 0},
             {0, 1, 2, 3, 6, 7},
             {0, 1}},
            {{3, 2, 0, 3, 0, 2, 2, 1, 1, 2, 3, 2, 1, 3, 2, 1, 2, 2, 1, 3, 3, 2,
              1, 0, 1, 0, 1, 3, 0, 0, 0, 2, 2, 1, 0, 1, 0, 1, 0, 1, 3, 1, 1, 2,
              2, 3, 2, 0, 3, 3, 2, 0, 2, 1, 3, 3, 0, 0, 3, 0, 1, 1, 3, 3},
             {0, 1, 3, 5, 6, 7},
             {4, 5}},
        }};

        constexpr std::array<sbox, 4> fn1_r2_boxes{{
            {{3, 3, 2, 0, 3, 0, 3, 1, 0, 3, 0, 1, 0, 2, 1, 3, 1, 3, 0, 3, 3, 1,
              3, 3, 3, 2, 3, 2, 2, 3, 1, 2, 0, 2, 2, 1, 0, 1, 2, 0, 3, 3, 0, 1,
              3, 2, 1, 2, 3, 0, 1, 3, 0, 1, 2, 2, 1, 2, 1, 2, 0, 1, 3, 0},
             {0, 1, 2, 3, 6, -1},
             {1, 6}},
            {{1, 2, 3, 2, 1, 3, 0, 1, 1, 0, 2, 0, 0, 2, 3, 2, 3, 3, 0, 1, 2, 2,
              1, 0, 1, 0, 1, 2, 3, 2, 1, 3, 2, 2, 2, 0, 1, 0, 2, 3, 2, 1, 2, 1,
              2, 1, 0, 3, 0, 1, 2, 3, 1, 2, 1, 3, 2, 0, 3, 2, 3, 0, 2, 0},
             {2, 4, 5, 6, 7, -1},
             {5, 7}},
            {{0, 1, 0, 2, 1, 1, 0, 1, 0, 2, 2, 2, 1, 3, 0, 0, 1, 1, 3, 1, 2, 2,
              2, 3, 1, 0, 3, 3, 3, 2, 2, 2, 1, 1, 3, 0, 3, 1, 3, 0, 1, 3, 3, 2,
              1, 1, 0, 0, 1, 2, 2, 2, 1, 1, 1, 2, 2, 0, 0, 3, 2, 3, 1, 3},
             {1, 2, 3, 4, 5, 7},
             {0, 3}},
            {{2, 1, 0, 3, 3, 3, 2, 0, 1, 2, 1, 1, 1, 0, 3, 1, 1, 3, 3, 0, 1, 2,
              1, 0, 0, 0, 3, 0, 3, 0, 3, 0, 1, 3, 3, 3, 0, 3, 2, 0, 2, 1, 2, 2,
              2, 1, 1, 3, 0, 1, 0, 1, 0, 1, 1, 1, 1, 3, 1, 0, 1, 2, 3, 3},
             {0, 1, 3, 4, 6, 7},
             {2, 4}},
        }};

        constexpr std::array<sbox, 4> fn1_r3_boxes{{
            {{0, 0, 0, 3, 3, 1, 1, 0, 2, 0, 2, 0, 0, 0, 3, 2, 0, 1, 2, 3, 2, 2,
              1, 0, 3, 0, 0, 0, 0, 0, 2, 3, 3, 0, 0, 1, 1, 2, 3, 3, 0, 1, 3, 2,
              0, 1, 3, 3, 2, 0, 0, 1, 0, 2, 0, 0, 0, 3, 1, 3, 3, 3, 3, 3},
             {0, 1, 5, 6, 7, -1},
             {0, 5}},
            {{2, 3, 2, 3, 0, 2, 3, 0, 2, 2, 3, 0, 3, 2, 0, 2, 1, 0, 2, 3, 1, 1,
              1, 0, 0, 1, 0, 2, 1, 2, 2, 1, 3, 0, 2, 1, 2, 3, 3, 0, 3, 2, 3, 1,
              0, 2, 1, 0, 1, 2, 2, 3, 0, 2, 1, 3, 1, 3, 0, 2, 1, 1, 1, 3},
             {2, 3, 4, 6, 7, -1},
             {6, 7}},
            {{3, 0, 2, 1, 1, 3, 1, 2, 2, 1, 2, 2, 2, 0, 0, 1, 2, 3, 1, 0, 2, 0,
              0, 2, 3, 1, 2, 0, 0, 0, 3, 0, 2, 1, 1, 2, 0, 0, 1, 2, 3, 1, 1, 2,
              0, 1, 3, 0, 3, 1, 1, 0, 0, 2, 3, 0, 0, 0, 0, 3, 2, 0, 0, 0},
             {0, 2, 3, 4, 5, 6},
             {1, 4}},
            {{0, 1, 0, 0, 2, 1, 3, 2, 3, 3, 2, 1, 0, 1, 1, 1, 1, 1, 0, 3, 3, 1,
              1, 0, 0, 2, 2, 1, 0, 3, 3, 2, 1, 3, 3, 0, 3, 0, 2, 1, 1, 2, 3, 2,
              2, 2, 1, 0, 0, 3, 3, 3, 2, 2, 3, 1, 0, 2, 3, 0, 3, 1, 1, 0},
             {0, 1, 2, 3, 5, 7},
             {2, 3}},
        }};

        constexpr std::array<sbox, 4> fn1_r4_boxes{{
            {{1, 1, 1, 1, 1, 0, 1, 3, 3, 2, 3, 0, 1, 2, 0, 2, 3, 3, 0, 1, 2, 1,
              2, 3, 0, 3, 2, 3, 2, 0, 1, 2, 0, 1, 0, 3, 2, 1, 3, 2, 3, 1, 2, 3,
              2, 0, 1, 2, 2, 0, 0, 0, 2, 1, 3, 0, 3, 1, 3, 0, 1, 3, 3, 0},
             {1, 2, 3, 4, 5, 7},
             {0, 4}},
            {{3, 0, 0, 0, 0, 1, 0, 2, 3, 3, 1, 3, 0, 3, 1, 2, 2, 2, 3, 1, 0, 0,
              2, 0, 1, 0, 2, 2, 3, 3, 0, 0, 1, 1, 3, 0, 2, 3, 0, 3, 0, 3, 0, 2,
              0, 2, 0, 1, 0, 3, 0, 1, 3, 1, 1, 0, 0, 1, 3, 3, 2, 2, 1, 0},
             {0, 1, 2, 3, 5, 6},
             {1, 3}},
            {{0, 1, 1, 2, 0, 1, 3, 1, 2, 0, 3, 2, 0, 0, 3, 0, 3, 0, 1, 2, 2, 3,
              3, 2, 3, 2, 0, 1, 0, 0, 1, 0, 3, 0, 2, 3, 0, 2, 2, 2, 1, 1, 0, 2,
              2, 0, 0, 1, 2, 1, 1, 1, 2, 3, 0, 3, 1, 2, 3, 3, 1, 1, 3, 0},
             {0, 2, 4, 5, 6, 7},
             {2, 6}},
            {{0, 1, 2, 2, 0, 1, 0, 3, 2, 2, 1, 1, 3, 2, 0, 2, 0, 1, 3, 3, 0, 2,
              2, 3, 3, 2, 0, 0, 2, 1, 3, 3, 1, 1, 1, 3, 1, 2, 1, 1, 0, 3, 3, 2,
              3, 2, 3, 0, 3, 1, 0, 0, 3, 0, 0, 0, 2, 2, 2, 1, 2, 3, 0, 0},
             {0, 1, 3, 4, 6, 7},
             {5, 7}},
        }};

        // ---- s-box data: second Feistel network, rounds 1-4 ----
        constexpr std::array<sbox, 4> fn2_r1_boxes{{
            {{2, 0, 2, 0, 3, 0, 0, 3, 1, 1, 0, 1, 3, 2, 0, 1, 2, 0, 1, 2, 0, 2,
              0, 2, 2, 2, 3, 0, 2, 1, 3, 0, 0, 1, 0, 1, 2, 2, 3, 3, 0, 3, 0, 2,
              3, 0, 1, 2, 1, 1, 0, 2, 0, 3, 1, 1, 2, 2, 1, 3, 1, 1, 3, 1},
             {0, 3, 4, 5, 7, -1},
             {6, 7}},
            {{1, 1, 0, 3, 0, 2, 0, 1, 3, 0, 2, 0, 1, 1, 0, 0, 1, 3, 2, 2, 0, 2,
              2, 2, 2, 0, 1, 3, 3, 3, 1, 1, 1, 3, 1, 3, 2, 2, 2, 2, 2, 2, 0, 1,
              0, 1, 1, 2, 3, 1, 1, 2, 0, 3, 3, 3, 2, 2, 3, 1, 1, 1, 3, 0},
             {1, 2, 3, 4, 6, -1},
             {3, 5}},
            {{1, 0, 2, 2, 3, 3, 3, 3, 1, 2, 2, 1, 0, 1, 2, 1, 1, 2, 3, 1, 2, 0,
              0, 1, 2, 3, 1, 2, 0, 0, 0, 2, 2, 0, 1, 1, 0, 0, 2, 0, 0, 0, 2, 3,
              2, 3, 0, 1, 3, 0, 0, 0, 2, 3, 2, 0, 1, 3, 2, 1, 3, 1, 1, 3},
             {1, 2, 4, 5, 6, 7},
             {1, 4}},
            {{1, 3, 3, 0, 3, 2, 3, 1, 3, 2, 1, 1, 3, 3, 2, 1, 2, 3, 0, 3, 1, 0,
              0, 2, 3, 0, 0, 0, 3, 3, 0, 1, 2, 3, 0, 0, 0, 1, 2, 1, 3, 0, 0, 1,
              0, 2, 2, 2, 3, 3, 1, 2, 1, 3, 0, 0, 0, 3, 0, 1, 3, 2, 2, 0},
             {0, 2, 3, 5, 6, 7},
             {0, 2}},
        }};

        constexpr std::array<sbox, 4> fn2_r2_boxes{{
            {{3, 1, 3, 0, 3, 0, 3, 1, 3, 0, 0, 1, 1, 3, 0, 3, 1, 1, 0, 1, 2, 3,
              2, 3, 3, 1, 2, 2, 2, 0, 2, 3, 2, 2, 2, 1, 1, 3, 3, 0, 3, 1, 2, 1,
              1, 1, 0, 2, 0, 3, 3, 0, 0, 2, 0, 0, 1, 1, 2, 1, 2, 1, 1, 0},
             {0, 2, 4, 6, -1, -1},
             {4, 6}},
            {{0, 3, 0, 3, 3, 2, 1, 2, 3, 1, 1, 1, 2, 0, 2, 3, 0, 3, 1, 2, 2, 1,
              3, 3, 3, 2, 1, 2, 2, 0, 1, 0, 2, 3, 0, 1, 2, 0, 1, 1, 2, 0, 2, 1,
              2, 0, 2, 3, 3, 1, 0, 2, 3, 3, 0, 3, 1, 1, 3, 0, 0, 1, 2, 0},
             {1, 3, 4, 5, 6, 7},
             {0, 3}},
            {{0, 0, 2, 1, 3, 2, 1, 0, 1, 2, 2, 2, 1, 1, 0, 3, 1, 2, 2, 3, 2, 1,
              1, 0, 3, 0, 0, 1, 1, 2, 3, 1, 3, 3, 2, 2, 1, 0, 1, 1, 1, 2, 0, 1,
              2, 3, 0, 3, 3, 0, 3, 2, 2, 0, 2, 2, 1, 2, 3, 2, 1, 0, 2, 1},
             {0, 1, 3, 4, 5, 7},
             {1, 7}},
            {{0, 2, 1, 2, 0, 2, 2, 0, 1, 3, 2, 0, 3, 2, 3, 0, 3, 3, 2, 3, 1, 2,
              3, 1, 2, 2, 0, 0, 2, 2, 1, 2, 2, 3, 3, 3, 1, 1, 0, 0, 0, 3, 2, 0,
              3, 2, 3, 1, 1, 1, 1, 0, 1, 0, 1, 3, 0, 0, 1, 2, 2, 3, 2, 0},
             {1, 2, 3, 5, 6, 7},
             {2, 5}},
        }};

        constexpr std::array<sbox, 4> fn2_r3_boxes{{
            {{2, 1, 2, 1, 2, 3, 1, 3, 2, 2, 1, 3, 3, 0, 0, 1, 0, 2, 0, 3, 3, 1,
              0, 0, 1, 1, 0, 2, 3, 2, 1, 2, 1, 1, 2, 1, 1, 3, 2, 2, 0, 2, 2, 3,
              3, 3, 2, 0, 0, 0, 0, 0, 3, 3, 3, 0, 1, 2, 1, 0, 2, 3, 3, 1},
             {2, 3, 4, 6, -1, -1},
             {3, 5}},
            {{3, 2, 3, 3, 1, 0, 3, 0, 2, 0, 1, 1, 1, 0, 3, 0, 3, 1, 3, 1, 0, 1,
              2, 3, 2, 2, 3, 2, 0, 1, 1, 2, 3, 0, 0, 2, 1, 0, 0, 2, 2, 0, 1, 0,
              0, 2, 0, 0, 1, 3, 1, 3, 2, 0, 3, 3, 1, 0, 2, 2, 2, 3, 0, 0},
             {0, 1, 3, 5, 7, -1},
             {0, 2}},
            {{2, 2, 1, 0, 2, 3, 3, 0, 0, 0, 1, 3, 1, 2, 3, 2, 2, 3, 1, 3, 0, 3,
              0, 3, 3, 2, 2, 1, 0, 0, 0, 2, 1, 2, 2, 2, 0, 0, 1, 2, 0, 1, 3, 0,
              2, 3, 2, 1, 3, 2, 2, 2, 3, 1, 3, 0, 2, 0, 2, 1, 0, 3, 3, 1},
             {0, 1, 2, 3, 5, 7},
             {1, 6}},
            {{1, 2, 3, 2, 0, 2, 1, 3, 3, 1, 0, 1, 1, 2, 2, 0, 0, 1, 1, 1, 2, 1,
              1, 2, 0, 1, 3, 3, 1, 1, 1, 2, 3, 3, 1, 0, 2, 1, 1, 1, 2, 1, 0, 0,
              2, 2, 3, 2, 3, 2, 2, 0, 2, 2, 3, 3, 0, 2, 3, 0, 2, 2, 1, 1},
             {0, 2, 4, 5, 6, 7},
             {4, 7}},
        }};

        constexpr std::array<sbox, 4> fn2_r4_boxes{{
            {{2, 0, 1, 1, 2, 1, 3, 3, 1, 1, 1, 2, 0, 1, 0, 2, 0, 1, 2, 0, 2, 3,
              0, 2, 3, 3, 2, 2, 3, 2, 0, 1, 3, 0, 2, 0, 2, 3, 1, 3, 2, 0, 0, 1,
              1, 2, 3, 1, 1, 1, 0, 1, 2, 0, 3, 3, 1, 1, 1, 3, 3, 1, 1, 0},
             {0, 1, 3, 6, 7, -1},
             {0, 3}},
            {{1, 2, 2, 1, 0, 3, 3, 1, 0, 2, 2, 2, 1, 0, 1, 0, 1, 1, 0, 1, 0, 2,
              1, 0, 2, 1, 0, 2, 3, 2, 3, 3, 2, 2, 1, 2, 2, 3, 1, 3, 3, 3, 0, 1,
              0, 1, 3, 0, 0, 0, 1, 2, 0, 3, 3, 2, 3, 2, 1, 3, 2, 1, 0, 2},
             {0, 1, 2, 4, 5, 6},
             {4, 7}},
            {{2, 3, 2, 1, 3, 2, 3, 0, 0, 2, 1, 1, 0, 0, 3, 2, 3, 1, 0, 1, 2, 2,
              2, 1, 3, 2, 2, 1, 0, 2, 1, 2, 0, 3, 1, 0, 0, 3, 1, 1, 3, 3, 2, 0,
              1, 0, 1, 3, 0, 0, 1, 2, 1, 2, 3, 2, 1, 0, 0, 3, 2, 1, 1, 3},
             {0, 2, 3, 4, 5, 7},
             {1, 2}},
            {{2, 0, 0, 3, 2, 2, 2, 1, 3, 3, 1, 1, 2, 0, 0, 3, 1, 0, 3, 2, 1, 0,
              2, 0, 3, 2, 2, 3, 2, 0, 3, 0, 1, 3, 0, 2, 2, 1, 3, 3, 0, 1, 0, 3,
              1, 1, 3, 2, 0, 3, 0, 2, 3, 2, 1, 3, 2, 3, 0, 0, 1, 3, 2, 1},
             {2, 3, 4, 5, 6, 7},
             {5, 6}},
        }};

        // Combine the four boxes of one round (each keyed by 6 bits of `key`).
        std::uint8_t round_fn(std::uint8_t in, const optimised_sbox* boxes,
                              std::uint32_t key) noexcept {
            return static_cast<std::uint8_t>(
                boxes[0].apply(in, key >> 0U) | boxes[1].apply(in, key >> 6U) |
                boxes[2].apply(in, key >> 12U) | boxes[3].apply(in, key >> 18U));
        }

        // Expand the 64-bit master key into the 96-bit (4x24) key for the 1st FN.
        void expand_1st_key(std::array<std::uint32_t, 4>& dst,
                            const std::array<std::uint32_t, 2>& src) noexcept {
            static constexpr std::array<int, 96> bits{
                33, 58, 49, 36, 0,  31, 22, 30, 3,  16, 5,  53, 10, 41, 23, 19, 27, 39, 43, 6,
                34, 12, 61, 21, 48, 13, 32, 35, 6,  42, 43, 14, 21, 41, 52, 25, 18, 47, 46, 37,
                57, 53, 20, 8,  55, 54, 59, 60, 27, 33, 35, 18, 8,  15, 63, 1,  50, 44, 16, 46,
                5,  4,  45, 51, 38, 25, 13, 11, 62, 29, 48, 2,  59, 61, 62, 56, 51, 57, 54, 9,
                24, 63, 22, 7,  26, 42, 45, 40, 23, 14, 2,  31, 52, 28, 44, 17};
            dst = {0U, 0U, 0U, 0U};
            for (int i = 0; i < 96; ++i) {
                const int b = bits[static_cast<std::size_t>(i)];
                dst[static_cast<std::size_t>(i / 24)] |=
                    static_cast<std::uint32_t>(bit(src[static_cast<std::size_t>(b / 32)], b % 32))
                    << (i % 24);
            }
        }

        // Expand (master key xor subkey) into the 96-bit key for the 2nd FN.
        void expand_2nd_key(std::array<std::uint32_t, 4>& dst,
                            const std::array<std::uint32_t, 2>& src) noexcept {
            static constexpr std::array<int, 96> bits{
                34, 9,  32, 24, 44, 54, 38, 61, 47, 13, 28, 7,  29, 58, 18, 1,  20, 60, 15, 6,
                11, 43, 39, 19, 63, 23, 16, 62, 54, 40, 31, 3,  56, 61, 17, 25, 47, 38, 55, 57,
                5,  4,  15, 42, 22, 7,  2,  19, 46, 37, 29, 39, 12, 30, 49, 57, 31, 41, 26, 27,
                24, 36, 11, 63, 33, 16, 56, 62, 48, 60, 59, 32, 12, 30, 53, 48, 10, 0,  50, 35,
                3,  59, 14, 49, 51, 45, 44, 2,  21, 33, 55, 52, 23, 28, 8,  26};
            dst = {0U, 0U, 0U, 0U};
            for (int i = 0; i < 96; ++i) {
                const int b = bits[static_cast<std::size_t>(i)];
                dst[static_cast<std::size_t>(i / 24)] |=
                    static_cast<std::uint32_t>(bit(src[static_cast<std::size_t>(b / 32)], b % 32))
                    << (i % 24);
            }
        }

        // Expand the 16-bit FN1 seed into the 64-bit subkey (each row a seed-bit
        // permutation).
        void expand_subkey(std::array<std::uint32_t, 2>& subkey, std::uint16_t seed) noexcept {
            static constexpr std::array<int, 64> bits{
                5,  10, 14, 9, 4,  0,  15, 6, 1,  8, 3, 2,  12, 7,  13, 11, 5,  12, 7,  2, 13, 11,
                9,  14, 4,  1, 6,  10, 8,  0, 15, 3, 4, 10, 2,  0,  6,  9,  12, 1,  11, 7, 15, 8,
                13, 5,  14, 3, 14, 11, 12, 7, 4,  5, 2, 10, 1,  15, 0,  9,  8,  6,  13, 3};
            subkey = {0U, 0U};
            for (int i = 0; i < 64; ++i) {
                subkey[static_cast<std::size_t>(i / 32)] |=
                    static_cast<std::uint32_t>(bit(seed, bits[static_cast<std::size_t>(i)]))
                    << (i % 32);
            }
        }

        // One Feistel network pass over a 16-bit word.
        std::uint16_t feistel(std::uint16_t val, const std::array<int, 8>& bits_a,
                              const std::array<int, 8>& bits_b, const optimised_sbox* boxes1,
                              const optimised_sbox* boxes2, const optimised_sbox* boxes3,
                              const optimised_sbox* boxes4, std::uint32_t key1, std::uint32_t key2,
                              std::uint32_t key3, std::uint32_t key4) noexcept {
            std::uint8_t l = gather8(val, bits_b);
            std::uint8_t r = gather8(val, bits_a);
            l = static_cast<std::uint8_t>(l ^ round_fn(r, boxes1, key1));
            r = static_cast<std::uint8_t>(r ^ round_fn(l, boxes2, key2));
            l = static_cast<std::uint8_t>(l ^ round_fn(r, boxes3, key3));
            r = static_cast<std::uint8_t>(r ^ round_fn(l, boxes4, key4));
            std::uint16_t out = 0U;
            for (int i = 0; i < 8; ++i) {
                out = static_cast<std::uint16_t>(
                    out | (bit(l, i) << bits_a[static_cast<std::size_t>(i)]));
                out = static_cast<std::uint16_t>(
                    out | (bit(r, i) << bits_b[static_cast<std::size_t>(i)]));
            }
            return out;
        }

        void optimise_round(optimised_sbox* out, const std::array<sbox, 4>& in) noexcept {
            for (int box = 0; box < 4; ++box) {
                out[box].optimise(in[static_cast<std::size_t>(box)]);
            }
        }

        // Transform `count` 16-bit words in place: each word at address `a` in
        // [lower, upper] is run through the cipher (decrypt or encrypt); others pass
        // through unchanged.
        void cps2_crypt(bool encrypt, std::span<std::uint16_t> words,
                        const std::array<std::uint32_t, 2>& master_key, std::uint32_t lower_limit,
                        std::uint32_t upper_limit) noexcept {
            std::array<optimised_sbox, 16> sboxes1{};
            optimise_round(&sboxes1[0], fn1_r1_boxes);
            optimise_round(&sboxes1[4], fn1_r2_boxes);
            optimise_round(&sboxes1[8], fn1_r3_boxes);
            optimise_round(&sboxes1[12], fn1_r4_boxes);
            std::array<optimised_sbox, 16> sboxes2{};
            optimise_round(&sboxes2[0], fn2_r1_boxes);
            optimise_round(&sboxes2[4], fn2_r2_boxes);
            optimise_round(&sboxes2[8], fn2_r3_boxes);
            optimise_round(&sboxes2[12], fn2_r4_boxes);

            std::array<std::uint32_t, 4> key1{};
            expand_1st_key(key1, master_key);
            // extra bits for s-boxes with fewer than 6 inputs
            key1[0] ^= static_cast<std::uint32_t>(bit(key1[0], 1)) << 4U;
            key1[0] ^= static_cast<std::uint32_t>(bit(key1[0], 2)) << 5U;
            key1[0] ^= static_cast<std::uint32_t>(bit(key1[0], 8)) << 11U;
            key1[1] ^= static_cast<std::uint32_t>(bit(key1[1], 0)) << 5U;
            key1[1] ^= static_cast<std::uint32_t>(bit(key1[1], 8)) << 11U;
            key1[2] ^= static_cast<std::uint32_t>(bit(key1[2], 1)) << 5U;
            key1[2] ^= static_cast<std::uint32_t>(bit(key1[2], 8)) << 11U;

            const auto count = static_cast<std::uint32_t>(words.size());
            for (std::uint32_t i = 0; i < 0x10000U; ++i) {
                const std::uint16_t seed = feistel(
                    static_cast<std::uint16_t>(i), fn1_group_a, fn1_group_b, &sboxes1[0],
                    &sboxes1[4], &sboxes1[8], &sboxes1[12], key1[0], key1[1], key1[2], key1[3]);
                std::array<std::uint32_t, 2> subkey{};
                expand_subkey(subkey, seed);
                subkey[0] ^= master_key[0];
                subkey[1] ^= master_key[1];
                std::array<std::uint32_t, 4> key2{};
                expand_2nd_key(key2, subkey);
                key2[0] ^= static_cast<std::uint32_t>(bit(key2[0], 0)) << 5U;
                key2[0] ^= static_cast<std::uint32_t>(bit(key2[0], 6)) << 11U;
                key2[1] ^= static_cast<std::uint32_t>(bit(key2[1], 0)) << 5U;
                key2[1] ^= static_cast<std::uint32_t>(bit(key2[1], 1)) << 4U;
                key2[2] ^= static_cast<std::uint32_t>(bit(key2[2], 2)) << 5U;
                key2[2] ^= static_cast<std::uint32_t>(bit(key2[2], 3)) << 4U;
                key2[2] ^= static_cast<std::uint32_t>(bit(key2[2], 7)) << 11U;
                key2[3] ^= static_cast<std::uint32_t>(bit(key2[3], 1)) << 5U;

                for (std::uint32_t a = i; a < count; a += 0x10000U) {
                    if (a < lower_limit || a > upper_limit) {
                        continue;
                    }
                    auto& word = words[a];
                    word = encrypt ? feistel(word, fn2_group_a, fn2_group_b, &sboxes2[12],
                                             &sboxes2[8], &sboxes2[4], &sboxes2[0], key2[3],
                                             key2[2], key2[1], key2[0])
                                   : feistel(word, fn2_group_a, fn2_group_b, &sboxes2[0],
                                             &sboxes2[4], &sboxes2[8], &sboxes2[12], key2[0],
                                             key2[1], key2[2], key2[3]);
                }
            }
        }

        bool crypt_be(bool encrypt, std::span<const std::uint8_t> in_be,
                      std::span<std::uint8_t> out_be, const cps2_crypto_key& key) noexcept {
            if (in_be.size() != out_be.size() || in_be.empty() || (in_be.size() & 1U) != 0U ||
                in_be.size() > 0x7FFFFFFEU) {
                return false;
            }
            const std::size_t word_count = in_be.size() / 2U;
            std::vector<std::uint16_t> words(word_count);
            for (std::size_t i = 0; i < word_count; ++i) {
                words[i] = static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(in_be[i * 2U]) << 8U) | in_be[i * 2U + 1U]);
            }
            cps2_crypt(encrypt, words, key.master_key, key.lower_limit, key.upper_limit);
            for (std::size_t i = 0; i < word_count; ++i) {
                out_be[i * 2U] = static_cast<std::uint8_t>(words[i] >> 8U);
                out_be[i * 2U + 1U] = static_cast<std::uint8_t>(words[i]);
            }
            return true;
        }

    } // namespace

    bool decode_key(std::span<const std::uint8_t> key_data, cps2_crypto_key& out) noexcept {
        if (key_data.size() != crypto_key_size) {
            return false;
        }
        std::array<std::uint16_t, 10> decoded{};
        for (int b = 0; b < 10 * 16; ++b) {
            const int src = (317 - b) % 160;
            if (((key_data[static_cast<std::size_t>(src / 8)] >> ((src ^ 7) % 8)) & 1U) != 0U) {
                decoded[static_cast<std::size_t>(b / 16)] |=
                    static_cast<std::uint16_t>(0x8000U >> (b % 16));
            }
        }
        out.decoded = decoded;
        out.master_key[0] = (static_cast<std::uint32_t>(decoded[0]) << 16U) | decoded[1];
        out.master_key[1] = (static_cast<std::uint32_t>(decoded[2]) << 16U) | decoded[3];
        if (decoded[9] == 0xFFFFU) {
            out.lower_limit = 0xFF0000U;
            out.upper_limit = 0xFFFFFFU;
        } else {
            out.lower_limit = 0U;
            out.upper_limit =
                ((static_cast<std::uint32_t>(~decoded[9] & 0x03FFU) << 14U) | 0x3FFFU) + 1U;
        }
        return true;
    }

    bool decrypt_opcodes(std::span<const std::uint8_t> encrypted_be,
                         std::span<std::uint8_t> decrypted_be,
                         const cps2_crypto_key& key) noexcept {
        return crypt_be(false, encrypted_be, decrypted_be, key);
    }

    bool encrypt_opcodes(std::span<const std::uint8_t> plain_be,
                         std::span<std::uint8_t> encrypted_be,
                         const cps2_crypto_key& key) noexcept {
        return crypt_be(true, plain_be, encrypted_be, key);
    }

} // namespace mnemos::manifests::capcom_cps2
