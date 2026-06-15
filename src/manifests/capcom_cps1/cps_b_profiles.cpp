#include "cps_b_profiles.hpp"

#include <array>

// Transcribed from the reference CPS1 CPS-B config + gfx-mapper hardware tables,
// mechanically (to avoid hand-transcription error) into this faithful, uniform
// shape. Keyed by the numeric CPS-B profile id (a board / PAL identity); the PAL
// / board names in comments are documentation only (see THIRD-PARTY-REFERENCES.md),
// never lookup keys.
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

        constexpr std::array<gfx_bank_range, 3> ranges_dm620{{
            {sc3, 0x8000U, 0xBFFFU, 1U},
            {spr, 0x2000U, 0x3FFFU, 2U},
            {all4, 0x0000U, 0x1FFFFU, 0U},
        }};
        constexpr std::array<gfx_bank_range, 4> ranges_tk24b1{{
            {spr, 0x0000U, 0x5FFFU, 0U},
            {sc1, 0x6000U, 0x7FFFU, 0U},
            {sc2, 0x4000U, 0x7FFFU, 1U},
            {sc3, 0x0000U, 0x3FFFU, 1U},
        }};
        constexpr std::array<gfx_bank_range, 4> ranges_wl24b{{
            {spr, 0x0000U, 0x4FFFU, 0U},
            {sc3, 0x5000U, 0x6FFFU, 0U},
            {sc1, 0x7000U, 0x7FFFU, 0U},
            {sc2, 0x0000U, 0x3FFFU, 1U},
        }};
        constexpr std::array<gfx_bank_range, 4> ranges_s224b{{
            {spr, 0x0000U, 0x43FFU, 0U},
            {sc1, 0x4400U, 0x4BFFU, 0U},
            {sc3, 0x4C00U, 0x5FFFU, 0U},
            {sc2, 0x6000U, 0x7FFFU, 0U},
        }};
        constexpr std::array<gfx_bank_range, 4> ranges_yi24b{{
            {spr, 0x0000U, 0x1FFFU, 0U},
            {sc3, 0x2000U, 0x3FFFU, 0U},
            {sc1, 0x4000U, 0x47FFU, 0U},
            {sc2, 0x4800U, 0x7FFFU, 0U},
        }};
        constexpr std::array<gfx_bank_range, 4> ranges_ar24b{{
            {spr, 0x0000U, 0x2FFFU, 0U},
            {sc1, 0x3000U, 0x3FFFU, 0U},
            {sc2, 0x4000U, 0x5FFFU, 0U},
            {sc3, 0x6000U, 0x7FFFU, 0U},
        }};
        constexpr std::array<gfx_bank_range, 7> ranges_o224b{{
            {sc1, 0x0000U, 0x0BFFU, 0U},
            {sc2, 0x0C00U, 0x3BFFU, 0U},
            {sc3, 0x3C00U, 0x4BFFU, 0U},
            {spr, 0x4C00U, 0x7FFFU, 0U},
            {spr, 0x8000U, 0xA7FFU, 1U},
            {sc2, 0xA800U, 0xB7FFU, 1U},
            {sc3, 0xB800U, 0xBFFFU, 1U},
        }};
        constexpr std::array<gfx_bank_range, 4> ranges_ms24b{{
            {spr, 0x0000U, 0x3FFFU, 0U},
            {sc1, 0x4000U, 0x4FFFU, 0U},
            {sc2, 0x5000U, 0x6FFFU, 0U},
            {sc3, 0x7000U, 0x7FFFU, 0U},
        }};
        constexpr std::array<gfx_bank_range, 4> ranges_ck24b{{
            {spr, 0x0000U, 0x2FFFU, 0U},
            {sc1, 0x3000U, 0x3FFFU, 0U},
            {sc2, 0x4000U, 0x6FFFU, 0U},
            {sc3, 0x7000U, 0x7FFFU, 0U},
        }};
        constexpr std::array<gfx_bank_range, 6> ranges_nm24b{{
            {spr, 0x0000U, 0x3FFFU, 0U},
            {sc2, 0x0000U, 0x3FFFU, 0U},
            {sc1, 0x4000U, 0x47FFU, 0U},
            {spr, 0x4800U, 0x67FFU, 0U},
            {sc2, 0x4800U, 0x67FFU, 0U},
            {sc3, 0x6800U, 0x7FFFU, 0U},
        }};
        constexpr std::array<gfx_bank_range, 6> ranges_ca24b{{
            {spr, 0x0000U, 0x2FFFU, 0U},
            {sc2, 0x0000U, 0x2FFFU, 0U},
            {sc3, 0x3000U, 0x4FFFU, 0U},
            {sc1, 0x5000U, 0x57FFU, 0U},
            {spr, 0x5800U, 0x7FFFU, 0U},
            {sc2, 0x5800U, 0x7FFFU, 0U},
        }};
        constexpr std::array<gfx_bank_range, 6> ranges_s9263b{{
            {spr, 0x0000U, 0x7FFFU, 0U},
            {spr, 0x8000U, 0xFFFFU, 1U},
            {spr, 0x10000U, 0x11FFFU, 2U},
            {sc3, 0x2000U, 0x3FFFU, 2U},
            {sc1, 0x4000U, 0x4FFFU, 2U},
            {sc2, 0x5000U, 0x7FFFU, 2U},
        }};
        constexpr std::array<gfx_bank_range, 5> ranges_kd29b{{
            {spr, 0x0000U, 0x7FFFU, 0U},
            {spr, 0x8000U, 0x8FFFU, 1U},
            {sc2, 0x9000U, 0xBFFFU, 1U},
            {sc1, 0xC000U, 0xD7FFU, 1U},
            {sc3, 0xD800U, 0xFFFFU, 1U},
        }};
        constexpr std::array<gfx_bank_range, 6> ranges_cc63b{{
            {spr, 0x0000U, 0x7FFFU, 0U},
            {sc2, 0x0000U, 0x7FFFU, 0U},
            {spr, 0x8000U, 0xFFFFU, 1U},
            {sc1, 0x8000U, 0xFFFFU, 1U},
            {sc2, 0x8000U, 0xFFFFU, 1U},
            {sc3, 0x8000U, 0xFFFFU, 1U},
        }};
        constexpr std::array<gfx_bank_range, 6> ranges_rt24b{{
            {spr, 0x0000U, 0x53FFU, 0U},
            {sc1, 0x5400U, 0x6FFFU, 0U},
            {sc3, 0x7000U, 0x7FFFU, 0U},
            {sc3, 0x0000U, 0x3FFFU, 1U},
            {sc2, 0x2800U, 0x7FFFU, 1U},
            {spr, 0x5400U, 0x7FFFU, 1U},
        }};
        constexpr std::array<gfx_bank_range, 3> ranges_pkb10b{{
            {sc1, 0x0000U, 0x0FFFU, 0U},
            {spr | sc2, 0x1000U, 0x5FFFU, 0U},
            {sc3, 0x6000U, 0x7FFFU, 0U},
        }};
        constexpr std::array<gfx_bank_range, 4> ranges_qd22b{{
            {spr, 0x0000U, 0x3FFFU, 0U},
            {sc1, 0x0000U, 0x3FFFU, 0U},
            {sc2, 0x0000U, 0x3FFFU, 0U},
            {sc3, 0x0000U, 0x3FFFU, 0U},
        }};
        constexpr std::array<gfx_bank_range, 2> ranges_cd63b{{
            {all4, 0x0000U, 0x7FFFU, 0U},
            {all4, 0x8000U, 0xFFFFU, 1U},
        }};
        constexpr std::array<gfx_bank_range, 2> ranges_tk263b{{
            {all4, 0x0000U, 0x7FFFU, 0U},
            {all4, 0x8000U, 0xFFFFU, 1U},
        }};
        constexpr std::array<gfx_bank_range, 6> ranges_kr63b{{
            {spr, 0x0000U, 0x7FFFU, 0U},
            {sc2, 0x0000U, 0x7FFFU, 0U},
            {sc1, 0x8000U, 0x9FFFU, 1U},
            {spr, 0x8000U, 0xCFFFU, 1U},
            {sc2, 0x8000U, 0xCFFFU, 1U},
            {sc3, 0xD000U, 0xFFFFU, 1U},
        }};
        constexpr std::array<gfx_bank_range, 4> ranges_gbpr2{{
            {spr, 0x0000U, 0x3FFFU, 0U},
            {sc1, 0x4000U, 0x7FFFU, 0U},
            {sc2, 0x8000U, 0xBFFFU, 1U},
            {sc3, 0xC000U, 0xFFFFU, 1U},
        }};
        constexpr std::array<gfx_bank_range, 4> ranges_st24m1{{
            {spr, 0x0000U, 0x4FFFU, 0U},
            {sc2, 0x4000U, 0x7FFFU, 0U},
            {sc3, 0x0000U, 0x7FFFU, 1U},
            {sc1, 0x7000U, 0x7FFFU, 1U},
        }};
        constexpr std::array<gfx_bank_range, 3> ranges_lwchr{{
            {spr, 0x0000U, 0x7FFFU, 0U},
            {sc1, 0x0000U, 0x1FFFFU, 0U},
            {sc2 | sc3, 0x0000U, 0x1FFFFU, 1U},
        }};
        constexpr std::array<gfx_bank_range, 4> ranges_va24b{{
            {spr, 0x0000U, 0x7FFFU, 0U},
            {sc1, 0x0000U, 0x7FFFU, 0U},
            {sc2, 0x0000U, 0x7FFFU, 0U},
            {sc3, 0x0000U, 0x7FFFU, 0U},
        }};

        constexpr std::array<cps_b_profile, 25> board_db{{
            // profile 1 (cps_b 01, mapper dm620)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x26U,
                .priority_offset = {0x28U, 0x2AU, 0x2CU, 0x2EU},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x02U, 0x04U, 0x08U, 0x30U, 0x30U},
                .id = 1U,
                .id_offset = reg_none,
                .id_value = 0x0000U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0x2000U, 0x2000U, 0U}, .ranges = ranges_dm620},
            },
            // profile 2 (cps_b 02, mapper tk24b1)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x2CU,
                .priority_offset = {0x2AU, 0x28U, 0x26U, 0x24U},
                .palette_control_offset = 0x22U,
                .layer_enable_mask = {0x02U, 0x04U, 0x08U, 0U, 0U},
                .id = 2U,
                .id_offset = 0x20U,
                .id_value = 0x0002U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0x8000U, 0U, 0U}, .ranges = ranges_tk24b1},
            },
            // profile 3 (cps_b 03, mapper wl24b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x30U,
                .priority_offset = {0x2EU, 0x2CU, 0x2AU, 0x28U},
                .palette_control_offset = 0x26U,
                .layer_enable_mask = {0x20U, 0x10U, 0x08U, 0U, 0U},
                .id = 3U,
                .id_offset = 0x24U,
                .id_value = 0x0003U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0x4000U, 0U, 0U}, .ranges = ranges_wl24b},
            },
            // profile 4 (cps_b 04, mapper s224b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x2EU,
                .priority_offset = {0x26U, 0x30U, 0x28U, 0x32U},
                .palette_control_offset = 0x2AU,
                .layer_enable_mask = {0x02U, 0x04U, 0x08U, 0U, 0U},
                .id = 4U,
                .id_offset = 0x20U,
                .id_value = 0x0004U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0U, 0U, 0U}, .ranges = ranges_s224b},
            },
            // profile 5 (cps_b 05, mapper yi24b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x28U,
                .priority_offset = {0x2AU, 0x2CU, 0x2EU, 0x30U},
                .palette_control_offset = 0x32U,
                .layer_enable_mask = {0x02U, 0x08U, 0x20U, 0x14U, 0x14U},
                .id = 5U,
                .id_offset = 0x20U,
                .id_value = 0x0005U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0U, 0U, 0U}, .ranges = ranges_yi24b},
            },
            // profile 11 (cps_b 11, mapper ar24b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x26U,
                .priority_offset = {0x28U, 0x2AU, 0x2CU, 0x2EU},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x08U, 0x10U, 0x20U, 0U, 0U},
                .id = 11U,
                .id_offset = 0x32U,
                .id_value = 0x0401U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0U, 0U, 0U}, .ranges = ranges_ar24b},
            },
            // profile 12 (cps_b 12, mapper o224b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x2CU,
                .priority_offset = {0x2AU, 0x28U, 0x26U, 0x24U},
                .palette_control_offset = 0x22U,
                .layer_enable_mask = {0x02U, 0x04U, 0x08U, 0U, 0U},
                .id = 12U,
                .id_offset = 0x20U,
                .id_value = 0x0402U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0x4000U, 0U, 0U}, .ranges = ranges_o224b},
            },
            // profile 13 (cps_b 13, mapper ms24b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x22U,
                .priority_offset = {0x24U, 0x26U, 0x28U, 0x2AU},
                .palette_control_offset = 0x2CU,
                .layer_enable_mask = {0x20U, 0x02U, 0x04U, 0U, 0U},
                .id = 13U,
                .id_offset = 0x2EU,
                .id_value = 0x0403U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0U, 0U, 0U}, .ranges = ranges_ms24b},
            },
            // profile 14 (cps_b 14, mapper ck24b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x12U,
                .priority_offset = {0x14U, 0x16U, 0x18U, 0x1AU},
                .palette_control_offset = 0x1CU,
                .layer_enable_mask = {0x08U, 0x20U, 0x10U, 0U, 0U},
                .id = 14U,
                .id_offset = 0x1EU,
                .id_value = 0x0404U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0U, 0U, 0U}, .ranges = ranges_ck24b},
            },
            // profile 15 (cps_b 15, mapper nm24b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x02U,
                .priority_offset = {0x04U, 0x06U, 0x08U, 0x0AU},
                .palette_control_offset = 0x0CU,
                .layer_enable_mask = {0x04U, 0x02U, 0x20U, 0U, 0U},
                .id = 15U,
                .id_offset = 0x0EU,
                .id_value = 0x0405U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0U, 0U, 0U}, .ranges = ranges_nm24b},
            },
            // profile 16 (cps_b 16, mapper ca24b)
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
                .mapper = {.bank_size = {0x8000U, 0U, 0U, 0U}, .ranges = ranges_ca24b},
            },
            // profile 21 (cps_b 21_def, mapper s9263b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x26U,
                .priority_offset = {0x28U, 0x2AU, 0x2CU, 0x2EU},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x02U, 0x04U, 0x08U, 0x30U, 0x30U},
                .id = 21U,
                .id_offset = reg_none,
                .id_value = 0x0000U,
                .mult_offset = {0x00U, 0x02U, 0x04U, 0x06U},
                .mapper = {.bank_size = {0x8000U, 0x8000U, 0x8000U, 0U}, .ranges = ranges_s9263b},
            },
            // profile 22 (cps_b 21_bt2, mapper kd29b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x20U,
                .priority_offset = {0x2EU, 0x2CU, 0x2AU, 0x28U},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x30U, 0x08U, 0x30U, 0U, 0U},
                .id = 22U,
                .id_offset = reg_none,
                .id_value = 0x0000U,
                .mult_offset = {0x1EU, 0x1CU, 0x1AU, 0x18U},
                .mapper = {.bank_size = {0x8000U, 0x8000U, 0U, 0U}, .ranges = ranges_kd29b},
            },
            // profile 23 (cps_b 21_bt3, mapper cc63b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x20U,
                .priority_offset = {0x2EU, 0x2CU, 0x2AU, 0x28U},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x20U, 0x12U, 0x12U, 0U, 0U},
                .id = 23U,
                .id_offset = reg_none,
                .id_value = 0x0000U,
                .mult_offset = {0x06U, 0x04U, 0x02U, 0x00U},
                .mapper = {.bank_size = {0x8000U, 0x8000U, 0U, 0U}, .ranges = ranges_cc63b},
            },
            // profile 24 (cps_b 21_bt1, mapper rt24b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x28U,
                .priority_offset = {0x26U, 0x24U, 0x22U, 0x20U},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x20U, 0x04U, 0x08U, 0x12U, 0x12U},
                .id = 24U,
                .id_offset = 0x32U,
                .id_value = 0x0800U,
                .mult_offset = {0x0EU, 0x0CU, 0x0AU, 0x08U},
                .mapper = {.bank_size = {0x8000U, 0x8000U, 0U, 0U}, .ranges = ranges_rt24b},
            },
            // profile 25 (cps_b 21_bt3, mapper cc63b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x20U,
                .priority_offset = {0x2EU, 0x2CU, 0x2AU, 0x28U},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x20U, 0x12U, 0x12U, 0U, 0U},
                .id = 25U,
                .id_offset = reg_none,
                .id_value = 0x0000U,
                .mult_offset = {0x06U, 0x04U, 0x02U, 0x00U},
                .mapper = {.bank_size = {0x8000U, 0x8000U, 0U, 0U}, .ranges = ranges_cc63b},
            },
            // profile 26 (cps_b 21_pkb10b, mapper pkb10b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x26U,
                .priority_offset = {0x28U, 0x2AU, 0x2CU, 0x2EU},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x02U, 0x04U, 0x08U, 0x30U, 0x30U},
                .id = 26U,
                .id_offset = reg_none,
                .id_value = 0x0000U,
                .mult_offset = {0x00U, 0x02U, 0x04U, 0x06U},
                .mapper = {.bank_size = {0x8000U, 0U, 0U, 0U}, .ranges = ranges_pkb10b},
            },
            // profile 27 (cps_b 21_qd22b, mapper qd22b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x2CU,
                .priority_offset = {reg_none, reg_none, reg_none, reg_none},
                .palette_control_offset = 0x12U,
                .layer_enable_mask = {0x14U, 0x02U, 0x14U, 0U, 0U},
                .id = 27U,
                .id_offset = reg_none,
                .id_value = 0x0000U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x4000U, 0U, 0U, 0U}, .ranges = ranges_qd22b},
            },
            // profile 28 (cps_b 21_cd63b, mapper cd63b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x0AU,
                .priority_offset = {0x0CU, 0x0EU, 0x00U, 0x02U},
                .palette_control_offset = 0x04U,
                .layer_enable_mask = {0x16U, 0x16U, 0x16U, 0U, 0U},
                .id = 28U,
                .id_offset = reg_none,
                .id_value = 0x0000U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0x8000U, 0U, 0U}, .ranges = ranges_cd63b},
            },
            // profile 29 (cps_b 21_def_tk263b, mapper tk263b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x26U,
                .priority_offset = {0x28U, 0x2AU, 0x2CU, 0x2EU},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x02U, 0x04U, 0x08U, 0x30U, 0x30U},
                .id = 29U,
                .id_offset = reg_none,
                .id_value = 0x0000U,
                .mult_offset = {0x00U, 0x02U, 0x04U, 0x06U},
                .mapper = {.bank_size = {0x8000U, 0x8000U, 0U, 0U}, .ranges = ranges_tk263b},
            },
            // profile 30 (cps_b 21_bt4, mapper kr63b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x28U,
                .priority_offset = {0x26U, 0x24U, 0x22U, 0x20U},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x20U, 0x10U, 0x02U, 0U, 0U},
                .id = 30U,
                .id_offset = reg_none,
                .id_value = 0x0000U,
                .mult_offset = {0x06U, 0x04U, 0x02U, 0x00U},
                .mapper = {.bank_size = {0x8000U, 0x8000U, 0U, 0U}, .ranges = ranges_kr63b},
            },
            // profile 31 (cps_b 21_gbpr2, mapper gbpr2)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x26U,
                .priority_offset = {0x28U, 0x2AU, 0x2CU, 0x2EU},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x02U, 0x04U, 0x08U, 0x30U, 0x30U},
                .id = 31U,
                .id_offset = reg_none,
                .id_value = 0x0000U,
                .mult_offset = {0x00U, 0x02U, 0x04U, 0x06U},
                .mapper = {.bank_size = {0x8000U, 0x8000U, 0U, 0U}, .ranges = ranges_gbpr2},
            },
            // profile 101 (cps_b 01_st24m1, mapper st24m1)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x26U,
                .priority_offset = {0x28U, 0x2AU, 0x2CU, 0x2EU},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x02U, 0x04U, 0x08U, 0x30U, 0x30U},
                .id = 101U,
                .id_offset = reg_none,
                .id_value = 0x0000U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0x8000U, 0U, 0U}, .ranges = ranges_st24m1},
            },
            // profile 102 (cps_b 01_lwchr, mapper lwchr)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x26U,
                .priority_offset = {0x28U, 0x2AU, 0x2CU, 0x2EU},
                .palette_control_offset = 0x30U,
                .layer_enable_mask = {0x02U, 0x04U, 0x08U, 0x30U, 0x30U},
                .id = 102U,
                .id_offset = reg_none,
                .id_value = 0x0000U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0x8000U, 0U, 0U}, .ranges = ranges_lwchr},
            },
            // profile 104 (cps_b 04_va24b, mapper va24b)
            cps_b_profile{
                .legacy = false,
                .layer_control_offset = 0x2EU,
                .priority_offset = {0x26U, 0x30U, 0x28U, 0x32U},
                .palette_control_offset = 0x2AU,
                .layer_enable_mask = {0x02U, 0x04U, 0x08U, 0U, 0U},
                .id = 104U,
                .id_offset = 0x20U,
                .id_value = 0x0004U,
                .mult_offset = {reg_none, reg_none, reg_none, reg_none},
                .mapper = {.bank_size = {0x8000U, 0U, 0U, 0U}, .ranges = ranges_va24b},
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
