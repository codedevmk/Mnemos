#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace mnemos::runtime {

    // A frame-tagged input transition: at frame `frame`, input `code` on `device`
    // changed to `pressed`. Device/code semantics are machine-specific (e.g. the
    // C64 keyboard-matrix row/column, or a joystick direction bit).
    struct input_event final {
        std::uint64_t frame{};
        std::uint8_t device{};
        std::uint8_t code{};
        bool pressed{};
    };

    // An ordered, frame-tagged input log (TDS §11.1). Events are kept sorted by
    // frame so replay is deterministic regardless of post order; the per-frame
    // slice is contiguous and returned as a view. Wiring this to the CIA1 keyboard
    // matrix / joystick lines is follow-up work.
    class input_buffer final {
      public:
        void post(const input_event& event);

        // The events tagged with `frame`, in insertion order within that frame.
        [[nodiscard]] std::span<const input_event>
        events_for_frame(std::uint64_t frame) const noexcept;

        [[nodiscard]] std::span<const input_event> all() const noexcept { return events_; }
        [[nodiscard]] std::size_t size() const noexcept { return events_.size(); }
        [[nodiscard]] bool empty() const noexcept { return events_.empty(); }
        void clear() noexcept { events_.clear(); }

      private:
        std::vector<input_event> events_; // sorted by frame, stable within a frame
    };

} // namespace mnemos::runtime
