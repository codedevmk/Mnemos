#include "kabuki.hpp"

#include <algorithm>
#include <cstddef>

namespace mnemos::manifests::capcom_cps1 {
    namespace {

        std::uint8_t bitswap1(std::uint8_t src, std::uint16_t key, std::uint8_t select) noexcept {
            if ((select & (1U << ((key >> 12U) & 7U))) != 0U) {
                src = static_cast<std::uint8_t>((src & 0x3FU) | ((src & 0x40U) << 1U) |
                                                ((src & 0x80U) >> 1U));
            }
            if ((select & (1U << ((key >> 8U) & 7U))) != 0U) {
                src = static_cast<std::uint8_t>((src & 0xCFU) | ((src & 0x10U) << 1U) |
                                                ((src & 0x20U) >> 1U));
            }
            if ((select & (1U << ((key >> 4U) & 7U))) != 0U) {
                src = static_cast<std::uint8_t>((src & 0xF3U) | ((src & 0x04U) << 1U) |
                                                ((src & 0x08U) >> 1U));
            }
            if ((select & (1U << ((key >> 0U) & 7U))) != 0U) {
                src = static_cast<std::uint8_t>((src & 0xFCU) | ((src & 0x01U) << 1U) |
                                                ((src & 0x02U) >> 1U));
            }
            return src;
        }

        std::uint8_t bitswap2(std::uint8_t src, std::uint16_t key, std::uint8_t select) noexcept {
            if ((select & (1U << ((key >> 0U) & 7U))) != 0U) {
                src = static_cast<std::uint8_t>((src & 0x3FU) | ((src & 0x40U) << 1U) |
                                                ((src & 0x80U) >> 1U));
            }
            if ((select & (1U << ((key >> 4U) & 7U))) != 0U) {
                src = static_cast<std::uint8_t>((src & 0xCFU) | ((src & 0x10U) << 1U) |
                                                ((src & 0x20U) >> 1U));
            }
            if ((select & (1U << ((key >> 8U) & 7U))) != 0U) {
                src = static_cast<std::uint8_t>((src & 0xF3U) | ((src & 0x04U) << 1U) |
                                                ((src & 0x08U) >> 1U));
            }
            if ((select & (1U << ((key >> 12U) & 7U))) != 0U) {
                src = static_cast<std::uint8_t>((src & 0xFCU) | ((src & 0x01U) << 1U) |
                                                ((src & 0x02U) >> 1U));
            }
            return src;
        }

        std::uint8_t rol1(std::uint8_t src) noexcept {
            return static_cast<std::uint8_t>(((src & 0x7FU) << 1U) | ((src & 0x80U) >> 7U));
        }

        std::uint8_t bytedecode(std::uint8_t src, std::uint32_t swap_key1, std::uint32_t swap_key2,
                                std::uint8_t xor_key, std::uint16_t select) noexcept {
            src = bitswap1(src, static_cast<std::uint16_t>(swap_key1 & 0xFFFFU),
                           static_cast<std::uint8_t>(select & 0xFFU));
            src = rol1(src);
            src = bitswap2(src, static_cast<std::uint16_t>(swap_key1 >> 16U),
                           static_cast<std::uint8_t>(select & 0xFFU));
            src ^= xor_key;
            src = rol1(src);
            src = bitswap2(src, static_cast<std::uint16_t>(swap_key2 & 0xFFFFU),
                           static_cast<std::uint8_t>(select >> 8U));
            src = rol1(src);
            src = bitswap1(src, static_cast<std::uint16_t>(swap_key2 >> 16U),
                           static_cast<std::uint8_t>(select >> 8U));
            return src;
        }

    } // namespace

    kabuki_keys kabuki_keys_for(kabuki_game game) noexcept {
        switch (game) {
        case kabuki_game::dino:
            return {.swap_key1 = 0x76543210U,
                    .swap_key2 = 0x24601357U,
                    .addr_key = 0x4343U,
                    .xor_key = 0x43U};
        case kabuki_game::punisher:
            return {.swap_key1 = 0x67452103U,
                    .swap_key2 = 0x75316024U,
                    .addr_key = 0x2222U,
                    .xor_key = 0x22U};
        case kabuki_game::wof:
            return {.swap_key1 = 0x01234567U,
                    .swap_key2 = 0x54163072U,
                    .addr_key = 0x5151U,
                    .xor_key = 0x51U};
        }
        return {};
    }

    void kabuki_decode(std::span<const std::uint8_t> encrypted, const kabuki_keys& keys,
                       std::span<std::uint8_t> opcode_out,
                       std::span<std::uint8_t> data_out) noexcept {
        const std::size_t n = std::min({encrypted.size(), opcode_out.size(), data_out.size()});
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint16_t addr = static_cast<std::uint16_t>(i);
            // The opcode and data streams differ only in the per-byte select value.
            const std::uint16_t opcode_select = static_cast<std::uint16_t>(addr + keys.addr_key);
            const std::uint16_t data_select =
                static_cast<std::uint16_t>((addr ^ 0x1FC0U) + keys.addr_key + 1U);
            opcode_out[i] = bytedecode(encrypted[i], keys.swap_key1, keys.swap_key2, keys.xor_key,
                                       opcode_select);
            data_out[i] =
                bytedecode(encrypted[i], keys.swap_key1, keys.swap_key2, keys.xor_key, data_select);
        }
    }

} // namespace mnemos::manifests::capcom_cps1
