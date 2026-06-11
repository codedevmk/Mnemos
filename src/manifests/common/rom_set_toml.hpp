#pragma once

// TOML form of an arcade ROM-set declaration (schema mnemos-romset/1) -- the
// data a per-game manifest carries: the set/board identity and the region/
// file placement the rom_set loader consumes. Validation follows the system
// manifest loader's strictness: unknown keys, missing required fields, and
// out-of-range values are diagnostics with source position.
//
//   [set]
//   schema = "mnemos-romset/1"
//   name   = "rtype"
//   board  = "irem_m72"
//
//   [[region]]
//   name = "maincpu"
//   size = 0x100000
//   fill = 0xFF            # optional, default 0xFF
//
//   [[region.file]]
//   name   = "rt_r-l0-b.1b"
//   offset = 0x00000
//   stride = 2             # optional, default 1 (2 = even/odd interleave)
//   size   = 0x10000       # optional expected byte count, default any
//   crc32  = 0x1234ABCD    # optional; integer or "0x..." string

#include "manifest.hpp" // diagnostic
#include "rom_set.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace mnemos::manifests::common {

    struct rom_set_load_result final {
        std::optional<rom_set_decl> value;
        std::vector<diagnostic> errors;

        [[nodiscard]] bool ok() const noexcept { return value.has_value() && errors.empty(); }
    };

    // Parse + strictly validate a ROM-set declaration from TOML text. On any
    // error, `errors` is non-empty and `value` is reset.
    [[nodiscard]] rom_set_load_result
    parse_rom_set_decl(std::string_view text, std::string_view source_name = "<rom_set>");

} // namespace mnemos::manifests::common
