#pragma once

// Composes the runtime determinism substrate -- the scheduler, the frame-tagged
// input log, the rewind ring, and a whole-machine save_target -- into one runnable,
// save-able, rewindable object. This is the missing piece that turns the building
// blocks into actual save / rewind / replay (review B2).
//
// Lifetime: the scheduler and save_target hold NON-OWNING views into the machine
// (chip pointers, RAM spans). The machine MUST outlive the session.

#include "input.hpp"
#include "rewind.hpp"
#include "save_state.hpp"
#include "scheduler.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::runtime {

    class session final {
      public:
        // Applies one recorded input transition to the machine. The mapping from the
        // machine-agnostic input_event to concrete device state is machine-specific,
        // so the caller supplies it. It MUST be a pure function of the event (no
        // hidden state), so that replay after a rewind/load is deterministic.
        using input_apply_fn = std::function<void(const input_event&)>;

        session(scheduler sched, save_target target, input_apply_fn apply = {},
                std::size_t rewind_depth = rewind_ring::default_depth);

        // Pinned: the save components capture `this`, so the session must not move.
        session(const session&) = delete;
        session& operator=(const session&) = delete;
        session(session&&) = delete;
        session& operator=(session&&) = delete;

        // Record a frame-tagged input transition; it is (re)applied on its frame.
        void post_input(const input_event& event) { input_.post(event); }

        // Advance one frame: capture a rewind keyframe for the current frame, apply
        // that frame's recorded input, then run the scheduler one frame. Returns the
        // new session frame index.
        std::uint64_t run_frame();
        void run_frames(std::uint64_t count);

        [[nodiscard]] std::uint64_t frame_index() const noexcept { return frame_; }
        [[nodiscard]] std::uint64_t master_cycle() const noexcept {
            return scheduler_.master_cycle();
        }

        // Whole-machine save / load. The session frame index is prefixed so a load
        // resumes in step. A failed load leaves the machine as-is; the result says why.
        [[nodiscard]] std::vector<std::uint8_t> save();
        [[nodiscard]] load_result load(std::span<const std::uint8_t> data);

        // Restore the most recent keyframe at or before `frame` and leave the session
        // positioned to replay forward (run_frame re-applies the recorded input).
        // Returns false when no keyframe is available or the restore fails.
        [[nodiscard]] bool rewind_to(std::uint64_t frame);

      private:
        [[nodiscard]] std::vector<std::uint8_t> frame_blob(); // [frame | save container]

        scheduler scheduler_;
        save_target target_;
        input_apply_fn apply_;
        input_buffer input_;
        rewind_ring rewind_;
        std::uint64_t frame_{0};
    };

} // namespace mnemos::runtime
