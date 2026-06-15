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
    const std::uint8_t none = cps_a_b::reg_none;

    // One hand-computed (layer, code) -> mapped golden, derived directly from the
    // Emu range tables: boundary-spanning, non-identity bank-wrap, and reject cases.
    struct gfx_golden {
        gfx type;
        std::uint32_t code;
        std::uint32_t mapped;
    };

    // The expected transcription of one board profile plus its golden mappings.
    struct profile_expect {
        std::uint16_t id;
        std::uint8_t layer_control_offset;
        std::array<std::uint8_t, 4> priority_offset;
        std::uint8_t palette_control_offset;
        std::array<std::uint16_t, 5> layer_enable_mask;
        std::uint8_t id_offset;
        std::uint16_t id_value;
        std::array<std::uint8_t, 4> mult_offset;
        std::array<std::uint32_t, 4> bank_size;
        std::size_t range_count;
        std::vector<gfx_golden> goldens;
    };

    std::vector<profile_expect> expectations() {
        return {
            // Profile 1 (dm620): three banks (0x8000 / 0x2000 / 0x2000); the small
            // banks fold higher codes, so this proves non-identity remapping.
            {1U,
             0x26U,
             {0x28U, 0x2AU, 0x2CU, 0x2EU},
             0x30U,
             {0x02U, 0x04U, 0x08U, 0x30U, 0x30U},
             none,
             0U,
             {none, none, none, none},
             {0x8000U, 0x2000U, 0x2000U, 0U},
             3U,
             {{gfx::scroll3, 0x0400U, 0x0400U},   // bank0 identity
              {gfx::scroll3, 0x1400U, 0x1000U},   // bank1 (0x2000) wrap -> non-identity
              {gfx::sprites, 0x1000U, 0x5000U},   // bank2 base 0xA000 -> non-identity
              {gfx::sprites, 0x0800U, 0x0800U},   // bank0 identity
              {gfx::scroll1, 0x4000U, 0x4000U},   // bank0 identity
              {gfx::scroll1, 0x20000U, absent}}}, // beyond the all-layer range
            // Profile 16 (ca24b): one 0x8000 bank; type gating + gap reject + a
            // self-identifying id port.
            {16U,
             0x0CU,
             {0x0AU, 0x08U, 0x06U, 0x04U},
             0x02U,
             {0x10U, 0x0AU, 0x0AU, 0U, 0U},
             0x00U,
             0x0406U,
             {none, none, none, none},
             {0x8000U, 0U, 0U, 0U},
             6U,
             {{gfx::scroll1, 0x5000U, 0x5000U},   // sc1 range
              {gfx::scroll1, 0x5800U, absent},    // just past the sc1 range end
              {gfx::scroll2, 0x1000U, 0x1000U},   // sc2 low range
              {gfx::scroll2, 0x2C00U, 0x2C00U},   // sc2 high range
              {gfx::scroll2, 0x2000U, absent},    // the gap between the sc2 ranges
              {gfx::sprites, 0x1000U, 0x1000U},   // spr low range
              {gfx::scroll3, 0x0800U, 0x0800U}}}, // sc3 range
            // Profile 28 (cd63b): two 0x8000 banks, all four layers, clean split.
            {28U,
             0x0AU,
             {0x0CU, 0x0EU, 0x00U, 0x02U},
             0x04U,
             {0x16U, 0x16U, 0x16U, 0U, 0U},
             none,
             0U,
             {none, none, none, none},
             {0x8000U, 0x8000U, 0U, 0U},
             2U,
             {{gfx::sprites, 0x1000U, 0x1000U},  // bank0
              {gfx::scroll3, 0x1000U, 0x1000U},  // bank1
              {gfx::scroll1, 0x8000U, 0x8000U},  // bank1 (shift 0)
              {gfx::scroll3, 0x2000U, absent}}}, // beyond bank1
        };
    }
} // namespace

TEST_CASE("capcom_cps1 board census resolves each profile by hardware id") {
    for (const profile_expect& e : expectations()) {
        const auto profile = profile_for_id(e.id);
        REQUIRE(profile.has_value());
        CHECK_FALSE(profile->legacy);
        CHECK(profile->layer_control_offset == e.layer_control_offset);
        CHECK(profile->priority_offset == e.priority_offset);
        CHECK(profile->palette_control_offset == e.palette_control_offset);
        CHECK(profile->layer_enable_mask == e.layer_enable_mask);
        CHECK(profile->id_offset == e.id_offset);
        CHECK(profile->id_value == e.id_value);
        CHECK(profile->mult_offset == e.mult_offset);
        CHECK(profile->mapper.bank_size == e.bank_size);
        CHECK(profile->mapper.ranges.size() == e.range_count);
    }
}

TEST_CASE("capcom_cps1 gfx mapper maps codes per profile (golden tuples)") {
    for (const profile_expect& e : expectations()) {
        const auto profile = profile_for_id(e.id);
        REQUIRE(profile.has_value());
        cps_a_b chip;
        chip.set_cps_b_profile(*profile);
        for (const gfx_golden& g : e.goldens) {
            INFO("profile " << e.id << " layer " << static_cast<int>(g.type) << " code " << g.code);
            CHECK(chip.mapped_gfx_code(g.type, g.code) == g.mapped);
        }
    }
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
    CHECK(profile_count() == expectations().size());
}
