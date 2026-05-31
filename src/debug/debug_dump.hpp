#pragma once

// System-agnostic debug-dump helpers used by the player's --screenshot path.
// Both helpers walk a `frontend_sdk::player_system` only through its public
// debug enumerator (`chips()` + per-chip introspection); neither knows what
// system is loaded. Adding a new system means implementing the SDK contract
// in its adapter -- the player doesn't grow Genesis/SMS/C64-shaped code paths.

#include "player_system.hpp"

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>

namespace mnemos::debug {

    // Writes the system's primary framebuffer to <base_path> as a PPM, then
    // for every chip in `sys.chips()`:
    //   - each `memory_view` is written as <base_path>.<chip_id>.<view_name>.bin
    //   - each `debug_layer` is written as <base_path>.<chip_id>.<layer_name>.ppm
    // <chip_id> is the chip's metadata.part_number, lowercase, with non-alnum
    // chars replaced by '_'. Returns true if the primary framebuffer wrote;
    // sidecar failures are reported on stderr but don't fail the call (any
    // dump that opened still gets the partial file flushed at scope-exit).
    bool dump_screenshot_artifacts(const frontend_sdk::player_system& sys,
                                   const std::string& base_path);

    // RAII handle for a CSV trace session. Construction installs a trace_target
    // callback on the first chip in `sys.chips()` that advertises one (the
    // adapter publishes chips in scheduler order, so this is typically the
    // primary CPU). Destruction clears the callback.
    //
    // The CSV format is `frame,inst,pc,cycles` -- columns supported by every
    // CPU through the generic `trace_target` capability. m68000-specific cycle-
    // sources (refresh / Z80 access / IRQ entry) that the prior Genesis-only
    // path emitted are NOT in this CSV; tooling that wants them can still
    // query `m68000_diagnostics::last_cycle_sources()` directly.
    class trace_csv_session final {
      public:
        // Tries to open <csv_path> and install a trace hook. If the file can't
        // open or no chip advertises a trace_target, the session is inactive
        // (`active() == false`); the dtor is a no-op. The caller's frame
        // counter is read live on each fire -- it should be incremented on
        // step_one_frame().
        trace_csv_session(frontend_sdk::player_system& sys, const std::string& csv_path,
                          const std::uint64_t& frame_counter);

        trace_csv_session(const trace_csv_session&) = delete;
        trace_csv_session& operator=(const trace_csv_session&) = delete;
        trace_csv_session(trace_csv_session&&) = delete;
        trace_csv_session& operator=(trace_csv_session&&) = delete;
        ~trace_csv_session();

        [[nodiscard]] bool active() const noexcept { return target_ != nullptr; }

      private:
        chips::ichip* target_{};
        std::ofstream out_{};
        // Heap-allocated state shared with the installed callback so the
        // session can outlive the construction stack without lifetime games.
        struct state;
        std::unique_ptr<state> state_{};
    };

} // namespace mnemos::debug
