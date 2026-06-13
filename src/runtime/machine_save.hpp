#pragma once

// Generic save-target construction from a manifest-built machine (tier 5).
//
// build_save_target turns the chips and RAM a `manifests::system_graph` owns into
// a runtime::save_target, system-agnostically: every chip becomes a chunk keyed by
// its manifest id, and each named writable region becomes a memory chunk. Chips are
// emitted in id order so the serialised byte stream is deterministic regardless of
// the graph's unordered_map iteration order (ARCH-004 §16 -- the save-state byte
// stream must be reproducible).
//
// Machine state that is NOT a graph chip or a flat RAM region -- the scheduler's
// pacing state, or a non-chip peripheral such as the C64 keyboard matrix -- is added
// by the caller via save_target::components / chips / memory after this returns.

#include "builder.hpp" // manifests::system_graph
#include "save_state.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace mnemos::runtime {

    [[nodiscard]] save_target build_save_target(const manifests::system_graph& graph,
                                                std::string manifest_id,
                                                std::uint32_t manifest_rev,
                                                std::uint64_t master_cycle,
                                                std::span<const std::string_view> ram_regions);

} // namespace mnemos::runtime
