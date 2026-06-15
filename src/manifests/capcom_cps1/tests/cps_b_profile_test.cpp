#include "cps_a_b.hpp"
#include "cps_b_profiles.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
    using cps_a_b = mnemos::chips::video::cps_a_b;
    using mnemos::manifests::capcom_cps1::profile_count;
    using mnemos::manifests::capcom_cps1::profile_for_id;
    using gfx = cps_a_b::gfx_type;

    constexpr std::uint32_t absent = cps_a_b::gfx_code_absent;

    // One (layer, code) -> mapped golden. The expected values below are computed
    // by an INDEPENDENT reimplementation of the mapper algorithm over the same
    // reference tables, so matching them cross-checks the C++ map_gfx_code rather
    // than echoing the census data back at itself.
    struct gfx_golden {
        gfx type;
        std::uint32_t code;
        std::uint32_t mapped;
    };

    struct golden_row {
        std::uint16_t id;
        std::size_t range_count;
        std::vector<gfx_golden> goldens;
    };

    // Generated sweep: every census profile, two goldens per gfx range (range
    // start + end, so identity, bank-wrap, and bank concatenation are all hit)
    // plus an out-of-range reject. Regenerate from the reference tables; do not
    // hand-edit individual rows.
    std::vector<golden_row> golden_rows() {
        return {
            {1U,
             3U,
             {{gfx::scroll3, 0x1000U, 0x1000U},
              {gfx::scroll3, 0x17FFU, 0x13FFU},
              {gfx::sprites, 0x1000U, 0x5000U},
              {gfx::sprites, 0x1FFFU, 0x5FFFU},
              {gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0xFFFFU, 0x3FFFU},
              {gfx::sprites, 0x10FFFU, absent}}},
            {2U,
             4U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x2FFFU, 0x2FFFU},
              {gfx::scroll1, 0x6000U, 0x6000U},
              {gfx::scroll1, 0x7FFFU, 0x7FFFU},
              {gfx::scroll2, 0x2000U, 0x6000U},
              {gfx::scroll2, 0x3FFFU, 0x7FFFU},
              {gfx::scroll3, 0x0U, 0x1000U},
              {gfx::scroll3, 0x7FFU, 0x17FFU},
              {gfx::scroll3, 0x1FFFU, absent}}},
            {3U,
             4U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x27FFU, 0x27FFU},
              {gfx::scroll3, 0xA00U, 0xA00U},
              {gfx::scroll3, 0xDFFU, 0xDFFU},
              {gfx::scroll1, 0x7000U, 0x7000U},
              {gfx::scroll1, 0x7FFFU, 0x7FFFU},
              {gfx::scroll2, 0x0U, 0x4000U},
              {gfx::scroll2, 0x1FFFU, 0x5FFFU},
              {gfx::scroll2, 0x4FFFU, absent}}},
            {4U,
             4U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x21FFU, 0x21FFU},
              {gfx::scroll1, 0x4400U, 0x4400U},
              {gfx::scroll1, 0x4BFFU, 0x4BFFU},
              {gfx::scroll3, 0x980U, 0x980U},
              {gfx::scroll3, 0xBFFU, 0xBFFU},
              {gfx::scroll2, 0x3000U, 0x3000U},
              {gfx::scroll2, 0x3FFFU, 0x3FFFU},
              {gfx::scroll2, 0x4FFFU, absent}}},
            {5U,
             4U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0xFFFU, 0xFFFU},
              {gfx::scroll3, 0x400U, 0x400U},
              {gfx::scroll3, 0x7FFU, 0x7FFU},
              {gfx::scroll1, 0x4000U, 0x4000U},
              {gfx::scroll1, 0x47FFU, 0x47FFU},
              {gfx::scroll2, 0x2400U, 0x2400U},
              {gfx::scroll2, 0x3FFFU, 0x3FFFU},
              {gfx::scroll2, 0x4FFFU, absent}}},
            {11U,
             4U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x17FFU, 0x17FFU},
              {gfx::scroll1, 0x3000U, 0x3000U},
              {gfx::scroll1, 0x3FFFU, 0x3FFFU},
              {gfx::scroll2, 0x2000U, 0x2000U},
              {gfx::scroll2, 0x2FFFU, 0x2FFFU},
              {gfx::scroll3, 0xC00U, 0xC00U},
              {gfx::scroll3, 0xFFFU, 0xFFFU},
              {gfx::scroll3, 0x1FFFU, absent}}},
            {12U,
             7U,
             {{gfx::scroll1, 0x0U, 0x0U},
              {gfx::scroll1, 0xBFFU, 0xBFFU},
              {gfx::scroll2, 0x600U, 0x600U},
              {gfx::scroll2, 0x1DFFU, 0x1DFFU},
              {gfx::scroll3, 0x780U, 0x780U},
              {gfx::scroll3, 0x97FU, 0x97FU},
              {gfx::sprites, 0x2600U, 0x2600U},
              {gfx::sprites, 0x3FFFU, 0x3FFFU},
              {gfx::sprites, 0x4000U, 0x4000U},
              {gfx::sprites, 0x53FFU, 0x53FFU},
              {gfx::scroll2, 0x5400U, 0x5400U},
              {gfx::scroll2, 0x5BFFU, 0x5BFFU},
              {gfx::scroll3, 0x1700U, 0x1700U},
              {gfx::scroll3, 0x17FFU, 0x17FFU},
              {gfx::scroll3, 0x27FFU, absent}}},
            {13U,
             4U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x1FFFU, 0x1FFFU},
              {gfx::scroll1, 0x4000U, 0x4000U},
              {gfx::scroll1, 0x4FFFU, 0x4FFFU},
              {gfx::scroll2, 0x2800U, 0x2800U},
              {gfx::scroll2, 0x37FFU, 0x37FFU},
              {gfx::scroll3, 0xE00U, 0xE00U},
              {gfx::scroll3, 0xFFFU, 0xFFFU},
              {gfx::scroll3, 0x1FFFU, absent}}},
            {14U,
             4U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x17FFU, 0x17FFU},
              {gfx::scroll1, 0x3000U, 0x3000U},
              {gfx::scroll1, 0x3FFFU, 0x3FFFU},
              {gfx::scroll2, 0x2000U, 0x2000U},
              {gfx::scroll2, 0x37FFU, 0x37FFU},
              {gfx::scroll3, 0xE00U, 0xE00U},
              {gfx::scroll3, 0xFFFU, 0xFFFU},
              {gfx::scroll3, 0x1FFFU, absent}}},
            {15U,
             6U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x1FFFU, 0x1FFFU},
              {gfx::scroll2, 0x0U, 0x0U},
              {gfx::scroll2, 0x1FFFU, 0x1FFFU},
              {gfx::scroll1, 0x4000U, 0x4000U},
              {gfx::scroll1, 0x47FFU, 0x47FFU},
              {gfx::sprites, 0x2400U, 0x2400U},
              {gfx::sprites, 0x33FFU, 0x33FFU},
              {gfx::scroll2, 0x2400U, 0x2400U},
              {gfx::scroll2, 0x33FFU, 0x33FFU},
              {gfx::scroll3, 0xD00U, 0xD00U},
              {gfx::scroll3, 0xFFFU, 0xFFFU},
              {gfx::scroll3, 0x1FFFU, absent}}},
            {16U,
             6U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x17FFU, 0x17FFU},
              {gfx::scroll2, 0x0U, 0x0U},
              {gfx::scroll2, 0x17FFU, 0x17FFU},
              {gfx::scroll3, 0x600U, 0x600U},
              {gfx::scroll3, 0x9FFU, 0x9FFU},
              {gfx::scroll1, 0x5000U, 0x5000U},
              {gfx::scroll1, 0x57FFU, 0x57FFU},
              {gfx::sprites, 0x2C00U, 0x2C00U},
              {gfx::sprites, 0x3FFFU, 0x3FFFU},
              {gfx::scroll2, 0x2C00U, 0x2C00U},
              {gfx::scroll2, 0x3FFFU, 0x3FFFU},
              {gfx::scroll2, 0x4FFFU, absent}}},
            {21U,
             6U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x3FFFU, 0x3FFFU},
              {gfx::sprites, 0x4000U, 0x4000U},
              {gfx::sprites, 0x7FFFU, 0x7FFFU},
              {gfx::sprites, 0x8000U, 0x8000U},
              {gfx::sprites, 0x8FFFU, 0x8FFFU},
              {gfx::scroll3, 0x400U, 0x2400U},
              {gfx::scroll3, 0x7FFU, 0x27FFU},
              {gfx::scroll1, 0x4000U, 0x14000U},
              {gfx::scroll1, 0x4FFFU, 0x14FFFU},
              {gfx::scroll2, 0x2800U, 0xA800U},
              {gfx::scroll2, 0x3FFFU, 0xBFFFU},
              {gfx::scroll2, 0x9FFFU, absent}}},
            {22U,
             5U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x3FFFU, 0x3FFFU},
              {gfx::sprites, 0x4000U, 0x4000U},
              {gfx::sprites, 0x47FFU, 0x47FFU},
              {gfx::scroll2, 0x4800U, 0x4800U},
              {gfx::scroll2, 0x5FFFU, 0x5FFFU},
              {gfx::scroll1, 0xC000U, 0xC000U},
              {gfx::scroll1, 0xD7FFU, 0xD7FFU},
              {gfx::scroll3, 0x1B00U, 0x1B00U},
              {gfx::scroll3, 0x1FFFU, 0x1FFFU},
              {gfx::scroll3, 0x2FFFU, absent}}},
            {23U,
             6U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x3FFFU, 0x3FFFU},
              {gfx::scroll2, 0x0U, 0x0U},
              {gfx::scroll2, 0x3FFFU, 0x3FFFU},
              {gfx::sprites, 0x4000U, 0x4000U},
              {gfx::sprites, 0x7FFFU, 0x7FFFU},
              {gfx::scroll1, 0x8000U, 0x8000U},
              {gfx::scroll1, 0xFFFFU, 0xFFFFU},
              {gfx::scroll2, 0x4000U, 0x4000U},
              {gfx::scroll2, 0x7FFFU, 0x7FFFU},
              {gfx::scroll3, 0x1000U, 0x1000U},
              {gfx::scroll3, 0x1FFFU, 0x1FFFU},
              {gfx::scroll3, 0x2FFFU, absent}}},
            {24U,
             6U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x29FFU, 0x29FFU},
              {gfx::scroll1, 0x5400U, 0x5400U},
              {gfx::scroll1, 0x6FFFU, 0x6FFFU},
              {gfx::scroll3, 0xE00U, 0xE00U},
              {gfx::scroll3, 0xFFFU, 0xFFFU},
              {gfx::scroll3, 0x0U, 0x1000U},
              {gfx::scroll3, 0x7FFU, 0x17FFU},
              {gfx::scroll2, 0x1400U, 0x5400U},
              {gfx::scroll2, 0x3FFFU, 0x7FFFU},
              {gfx::sprites, 0x2A00U, 0x6A00U},
              {gfx::sprites, 0x3FFFU, 0x7FFFU},
              {gfx::sprites, 0x4FFFU, absent}}},
            {25U,
             6U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x3FFFU, 0x3FFFU},
              {gfx::scroll2, 0x0U, 0x0U},
              {gfx::scroll2, 0x3FFFU, 0x3FFFU},
              {gfx::sprites, 0x4000U, 0x4000U},
              {gfx::sprites, 0x7FFFU, 0x7FFFU},
              {gfx::scroll1, 0x8000U, 0x8000U},
              {gfx::scroll1, 0xFFFFU, 0xFFFFU},
              {gfx::scroll2, 0x4000U, 0x4000U},
              {gfx::scroll2, 0x7FFFU, 0x7FFFU},
              {gfx::scroll3, 0x1000U, 0x1000U},
              {gfx::scroll3, 0x1FFFU, 0x1FFFU},
              {gfx::scroll3, 0x2FFFU, absent}}},
            {26U,
             3U,
             {{gfx::scroll1, 0x0U, 0x0U},
              {gfx::scroll1, 0xFFFU, 0xFFFU},
              {gfx::sprites, 0x800U, 0x800U},
              {gfx::sprites, 0x2FFFU, 0x2FFFU},
              {gfx::scroll3, 0xC00U, 0xC00U},
              {gfx::scroll3, 0xFFFU, 0xFFFU},
              {gfx::scroll3, 0x1FFFU, absent}}},
            {27U,
             4U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x1FFFU, 0x1FFFU},
              {gfx::scroll1, 0x0U, 0x0U},
              {gfx::scroll1, 0x3FFFU, 0x3FFFU},
              {gfx::scroll2, 0x0U, 0x0U},
              {gfx::scroll2, 0x1FFFU, 0x1FFFU},
              {gfx::scroll3, 0x0U, 0x0U},
              {gfx::scroll3, 0x7FFU, 0x7FFU},
              {gfx::scroll3, 0x17FFU, absent}}},
            {28U,
             2U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x3FFFU, 0x3FFFU},
              {gfx::sprites, 0x4000U, 0x4000U},
              {gfx::sprites, 0x7FFFU, 0x7FFFU},
              {gfx::sprites, 0x8FFFU, absent}}},
            {29U,
             2U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x3FFFU, 0x3FFFU},
              {gfx::sprites, 0x4000U, 0x4000U},
              {gfx::sprites, 0x7FFFU, 0x7FFFU},
              {gfx::sprites, 0x8FFFU, absent}}},
            {30U,
             6U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x3FFFU, 0x3FFFU},
              {gfx::scroll2, 0x0U, 0x0U},
              {gfx::scroll2, 0x3FFFU, 0x3FFFU},
              {gfx::scroll1, 0x8000U, 0x8000U},
              {gfx::scroll1, 0x9FFFU, 0x9FFFU},
              {gfx::sprites, 0x4000U, 0x4000U},
              {gfx::sprites, 0x67FFU, 0x67FFU},
              {gfx::scroll2, 0x4000U, 0x4000U},
              {gfx::scroll2, 0x67FFU, 0x67FFU},
              {gfx::scroll3, 0x1A00U, 0x1A00U},
              {gfx::scroll3, 0x1FFFU, 0x1FFFU},
              {gfx::scroll3, 0x2FFFU, absent}}},
            {31U,
             4U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x1FFFU, 0x1FFFU},
              {gfx::scroll1, 0x4000U, 0x4000U},
              {gfx::scroll1, 0x7FFFU, 0x7FFFU},
              {gfx::scroll2, 0x4000U, 0x4000U},
              {gfx::scroll2, 0x5FFFU, 0x5FFFU},
              {gfx::scroll3, 0x1800U, 0x1800U},
              {gfx::scroll3, 0x1FFFU, 0x1FFFU},
              {gfx::scroll3, 0x2FFFU, absent}}},
            {101U,
             4U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x27FFU, 0x27FFU},
              {gfx::scroll2, 0x2000U, 0x2000U},
              {gfx::scroll2, 0x3FFFU, 0x3FFFU},
              {gfx::scroll3, 0x0U, 0x1000U},
              {gfx::scroll3, 0xFFFU, 0x1FFFU},
              {gfx::scroll1, 0x7000U, 0xF000U},
              {gfx::scroll1, 0x7FFFU, 0xFFFFU},
              {gfx::scroll1, 0x8FFFU, absent}}},
            {102U,
             3U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x3FFFU, 0x3FFFU},
              {gfx::scroll1, 0x0U, 0x0U},
              {gfx::scroll1, 0x1FFFFU, 0x7FFFU},
              {gfx::scroll2, 0x0U, 0x4000U},
              {gfx::scroll2, 0xFFFFU, 0x7FFFU},
              {gfx::scroll2, 0x10FFFU, absent}}},
            {104U,
             4U,
             {{gfx::sprites, 0x0U, 0x0U},
              {gfx::sprites, 0x3FFFU, 0x3FFFU},
              {gfx::scroll1, 0x0U, 0x0U},
              {gfx::scroll1, 0x7FFFU, 0x7FFFU},
              {gfx::scroll2, 0x0U, 0x0U},
              {gfx::scroll2, 0x3FFFU, 0x3FFFU},
              {gfx::scroll3, 0x0U, 0x0U},
              {gfx::scroll3, 0xFFFU, 0xFFFU},
              {gfx::scroll3, 0x1FFFU, absent}}},
        };
    }
} // namespace

TEST_CASE("capcom_cps1 census maps gfx codes per profile (generated golden sweep)") {
    for (const golden_row& row : golden_rows()) {
        const auto profile = profile_for_id(row.id);
        REQUIRE(profile.has_value());
        CHECK_FALSE(profile->legacy);
        CHECK(profile->id == row.id);
        CHECK(profile->mapper.ranges.size() == row.range_count);
        cps_a_b chip;
        chip.set_cps_b_profile(*profile);
        for (const gfx_golden& g : row.goldens) {
            INFO("profile " << row.id << " layer " << static_cast<int>(g.type) << " code "
                            << g.code);
            CHECK(chip.mapped_gfx_code(g.type, g.code) == g.mapped);
        }
    }
}

// Anchor: three profiles whose register transcription was verified BY HAND
// against the reference source (one single-bank, one multi-bank-wrap, one
// all-layer). These non-circular checks certify the generated census.
TEST_CASE("capcom_cps1 anchor profiles transcribe the register map exactly") {
    const std::uint8_t none = cps_a_b::reg_none;

    const auto p1 = profile_for_id(1U);
    REQUIRE(p1.has_value());
    CHECK(p1->layer_control_offset == 0x26U);
    CHECK(p1->priority_offset == std::array<std::uint8_t, 4>{0x28U, 0x2AU, 0x2CU, 0x2EU});
    CHECK(p1->palette_control_offset == 0x30U);
    CHECK(p1->layer_enable_mask == std::array<std::uint16_t, 5>{0x02U, 0x04U, 0x08U, 0x30U, 0x30U});
    CHECK(p1->id_offset == none);
    CHECK(p1->mult_offset == std::array<std::uint8_t, 4>{none, none, none, none});
    CHECK(p1->mapper.bank_size == std::array<std::uint32_t, 4>{0x8000U, 0x2000U, 0x2000U, 0U});

    const auto p16 = profile_for_id(16U);
    REQUIRE(p16.has_value());
    CHECK(p16->layer_control_offset == 0x0CU);
    CHECK(p16->priority_offset == std::array<std::uint8_t, 4>{0x0AU, 0x08U, 0x06U, 0x04U});
    CHECK(p16->palette_control_offset == 0x02U);
    CHECK(p16->layer_enable_mask == std::array<std::uint16_t, 5>{0x10U, 0x0AU, 0x0AU, 0U, 0U});
    CHECK(p16->id_offset == 0x00U); // CPS-B-16 self-identifies
    CHECK(p16->id_value == 0x0406U);

    const auto p28 = profile_for_id(28U);
    REQUIRE(p28.has_value());
    CHECK(p28->layer_control_offset == 0x0AU);
    CHECK(p28->priority_offset == std::array<std::uint8_t, 4>{0x0CU, 0x0EU, 0x00U, 0x02U});
    CHECK(p28->palette_control_offset == 0x04U);
    CHECK(p28->layer_enable_mask == std::array<std::uint16_t, 5>{0x16U, 0x16U, 0x16U, 0U, 0U});
    CHECK(p28->mapper.bank_size == std::array<std::uint32_t, 4>{0x8000U, 0x8000U, 0U, 0U});
}

TEST_CASE("capcom_cps1 unknown id resolves to nothing") {
    CHECK_FALSE(profile_for_id(0U).has_value()); // legacy is the chip default, not a census row
    CHECK_FALSE(profile_for_id(999U).has_value());
}

TEST_CASE("capcom_cps1 chip default profile maps codes identically") {
    cps_a_b chip; // legacy default carries no mapper -> identity passthrough
    CHECK(chip.mapped_gfx_code(gfx::scroll1, 0x1234U) == 0x1234U);
    CHECK(chip.mapped_gfx_code(gfx::sprites, 0x0ABCU) == 0x0ABCU);
}

TEST_CASE("capcom_cps1 every census profile has a conformance row") {
    CHECK(profile_count() == golden_rows().size());
}
