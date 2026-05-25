#include <mnemos/runtime/rewind.hpp>

#include <utility>

namespace mnemos::runtime {

    void rewind_ring::push(std::uint64_t frame, std::vector<std::uint8_t> state) {
        entries_.push_back({frame, std::move(state)});
        while (entries_.size() > depth_) {
            entries_.pop_front();
        }
    }

    const std::vector<std::uint8_t>* rewind_ring::at_or_before(std::uint64_t frame) const {
        // Entries are ordered by frame; scan from the newest for the first match.
        for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
            if (it->frame <= frame) {
                return &it->state;
            }
        }
        return nullptr;
    }

} // namespace mnemos::runtime
