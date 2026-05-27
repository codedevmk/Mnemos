#pragma once

// Named boolean predicates the manifest builder uses to gate chips on /
// off per scheduler tick. Separate from callback_table because predicates
// have a fixed signature (bool()) and a different role (control flow,
// not data path).
//
// Typical use: the manifest declares
//
//   [[gate]]
//   chip = "z80"
//   predicate = "genesis.z80_running"
//
// and the host supplies a predicate_table with "genesis.z80_running"
// mapped to a closure over the Genesis system's z80_running flag. The
// builder wraps the z80 chip with a gated_chip that consults the
// predicate each tick.
//
// Tier 2 placement so chips, the manifests layer, and the host all share
// the same type. Same rationale as callbacks.hpp / config.hpp.

#include <functional>
#include <string>
#include <unordered_map>

namespace mnemos::chips {

    // Per-tick gating predicate. true = run the chip this tick; false = skip.
    using predicate_fn = std::function<bool()>;

    using predicate_table = std::unordered_map<std::string, predicate_fn>;

} // namespace mnemos::chips
