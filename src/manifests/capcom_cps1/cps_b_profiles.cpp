#include "cps_b_profiles.hpp"

#include <array>

namespace mnemos::manifests::capcom_cps1 {
    namespace {
        using gfx_bank_range = chips::video::cps_a_b::gfx_bank_range;
        constexpr std::uint8_t reg_none = chips::video::cps_a_b::reg_none;

        // gfx-mapper layer bits (match cps_a_b::gfx_type_bit).
        constexpr std::uint8_t spr = 0x01U;
        constexpr std::uint8_t sc1 = 0x02U;
        constexpr std::uint8_t sc2 = 0x04U;
        constexpr std::uint8_t sc3 = 0x08U;
        constexpr std::uint8_t all4 = spr | sc1 | sc2 | sc3;

        // Profile 1 -- CPS-B-01 default register block, "dm620" gfx mapper. Three
        // banks (0x8000 / 0x2000 / 0x2000): the two small banks fold higher codes
        // back on themselves, so this entry exercises non-identity remapping.
        constexpr std::array<gfx_bank_range, 3> ranges_01{{
            {sc3, 0x8000U, 0xBFFFU, 1U},
            {spr, 0x2000U, 0x3FFFU, 2U},
            {all4, 0x0000U, 0x1FFFFU, 0U},
        }};

        // Profile 16 -- CPS-B-16, self-identifying (port 0x00 reads 0x0406),
        // "ca24b" gfx mapper. One 0x8000 bank, six type-gated ranges with a gap.
        constexpr std::array<gfx_bank_range, 6> ranges_16{{
            {spr, 0x0000U, 0x2FFFU, 0U},
            {sc2, 0x0000U, 0x2FFFU, 0U},
            {sc3, 0x3000U, 0x4FFFU, 0U},
            {sc1, 0x5000U, 0x57FFU, 0U},
            {spr, 0x5800U, 0x7FFFU, 0U},
            {sc2, 0x5800U, 0x7FFFU, 0U},
        }};

        // Profile 28 -- CPS-B-21 "CD63B", "cd63b" gfx mapper. Two 0x8000 banks,
        // both serving all four layers (a clean high/low split).
        constexpr std::array<gfx_bank_range, 2> ranges_28{{
            {all4, 0x0000U, 0x7FFFU, 0U},
            {all4, 0x8000U, 0xFFFFU, 1U},
        }};

        // The board census. Keyed by the numeric profile id only; the PAL / board
        // names in the comments above are documentation (see THIRD-PARTY-
        // REFERENCES.md), never lookup keys.
        constexpr std::array<cps_b_profile, 3> board_db{{
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x26U,
                .priority_offset = {0x28U, 0x2AU, 0x2CU, 0x2EU},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x02U, 0x04U, 0x08U, 0x30U, 0x30U},
                .id = 1U,
                .id_offset = reg_none,
                .id_value = 0U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0x2000U, 0x2000U, 0U}, .ranges = ranges_01},
            },
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x0CU,
                .priority_offset = {0x0AU, 0x08U, 0x06U, 0x04U},
                .palette_control_offset = 0x02U,
                .layer_enable_mask = {0x10U, 0x0AU, 0x0AU, 0U, 0U},
                .id = 16U,
                .id_offset = 0x00U,
                .id_value = 0x0406U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0U, 0U, 0U}, .ranges = ranges_16},
            },
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x0AU,
                .priority_offset = {0x0CU, 0x0EU, 0x00U, 0x02U},
                .palette_control_offset = 0x04U,
                .layer_enable_mask = {0x16U, 0x16U, 0x16U, 0U, 0U},
                .id = 28U,
                .id_offset = reg_none,
                .id_value = 0U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0x8000U, 0U, 0U}, .ranges = ranges_28},
            },
        }};
    } // namespace

    std::optional<cps_b_profile> profile_for_id(std::uint16_t profile_id) noexcept {
        for (const cps_b_profile& profile : board_db) {
            if (profile.id == profile_id) {
                return profile;
            }
        }
        return std::nullopt;
    }

    std::size_t profile_count() noexcept { return board_db.size(); }

} // namespace mnemos::manifests::capcom_cps1
