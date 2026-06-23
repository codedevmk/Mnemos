#include "cps_a_b.hpp"
#include "cps_b_profiles.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

namespace {
    using cps_a_b = mnemos::chips::video::cps_a_b;
    using mnemos::manifests::capcom_cps1::profile_for_id;
    using gfx = cps_a_b::gfx_type;
} // namespace

// Anchor: three profiles whose register transcription was verified BY HAND
// against the reference source (single-bank, multi-bank-wrap, all-layer). These
// non-circular checks certify the generated census above.
TEST_CASE("capcom_cps1 anchor profiles transcribe the register map exactly") {
    const auto p1 = profile_for_id(1U);
    REQUIRE(p1.has_value());
    CHECK(p1->layer_control_offset == 0x26U);
    CHECK(p1->priority_offset == std::array<std::uint8_t, 4>{0x28U, 0x2AU, 0x2CU, 0x2EU});
    CHECK(p1->palette_control_offset == 0x30U);
    CHECK(p1->layer_enable_mask == std::array<std::uint16_t, 5>{0x02U, 0x04U, 0x08U, 0x30U, 0x30U});
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
