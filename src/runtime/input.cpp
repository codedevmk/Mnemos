#include "input.hpp"

#include <algorithm>

namespace mnemos::runtime {

    void input_buffer::post(const input_event& event) {
        // Insert after all events with frame <= event.frame so order within a frame
        // is the post order (stable replay).
        const auto at = std::upper_bound(
            events_.begin(), events_.end(), event.frame,
            [](std::uint64_t frame, const input_event& e) noexcept { return frame < e.frame; });
        events_.insert(at, event);
    }

    std::span<const input_event>
    input_buffer::events_for_frame(std::uint64_t frame) const noexcept {
        const auto lo = std::lower_bound(
            events_.begin(), events_.end(), frame,
            [](const input_event& e, std::uint64_t f) noexcept { return e.frame < f; });
        const auto hi = std::upper_bound(
            events_.begin(), events_.end(), frame,
            [](std::uint64_t f, const input_event& e) noexcept { return f < e.frame; });
        return std::span<const input_event>(events_.data() + (lo - events_.begin()),
                                            static_cast<std::size_t>(hi - lo));
    }

} // namespace mnemos::runtime
