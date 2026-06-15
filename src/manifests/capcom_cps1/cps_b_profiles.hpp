#pragma once

#include "cps_a_b.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace mnemos::manifests::capcom_cps1 {

    // The CPS-B per-board profile type (register scramble + layer-enable masks +
    // 68k-protection ports + graphics-code mapper) is owned by the cps_a_b chip;
    // this library is the hardware-keyed census of concrete board profiles. A
    // profile is looked up by its numeric CPS-B profile id -- a board / PAL
    // identity, never a game name -- which the romset TOML selects in a later
    // increment. Hardware sources behind each entry are listed in
    // THIRD-PARTY-REFERENCES.md.
    using cps_b_profile = chips::video::cps_a_b::cps_b_profile;

    // Resolve a board profile by its numeric id; std::nullopt when the id is not
    // in the census (the caller then keeps the chip's legacy default profile).
    [[nodiscard]] std::optional<cps_b_profile> profile_for_id(std::uint16_t profile_id) noexcept;

    // The number of encoded board profiles (for completeness assertions: every
    // profile in the census must have a matching conformance test row).
    [[nodiscard]] std::size_t profile_count() noexcept;

} // namespace mnemos::manifests::capcom_cps1
