#include "session.hpp"

#include "chip.hpp"
#include "state.hpp"

#include <utility>

namespace mnemos::runtime {

    namespace {
        constexpr std::size_t frame_prefix_size = sizeof(std::uint64_t);
    } // namespace

    session::session(scheduler sched, save_target target, input_apply_fn apply,
                     std::size_t rewind_depth)
        : scheduler_(std::move(sched)), target_(std::move(target)), apply_(std::move(apply)),
          rewind_(rewind_depth) {
        // The session owns the scheduler, so it serialises the scheduler's pacing
        // state itself (master cycle + accumulators); callers supply machine state
        // only and must NOT add a scheduler component to the target.
        target_.components.push_back(save_component{
            .id = "_session.scheduler",
            .save = [this](chips::state_writer& writer) { scheduler_.save_state(writer); },
            .load = [this](chips::state_reader& reader) { scheduler_.load_state(reader); }});
    }

    std::vector<std::uint8_t> session::frame_blob() {
        std::vector<std::uint8_t> out;
        chips::state_writer writer(out);
        writer.u64(frame_);
        target_.master_cycle = scheduler_.master_cycle();
        const std::vector<std::uint8_t> body = write_save_state(target_);
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }

    std::uint64_t session::run_frame() {
        // Keyframe first: the state at the START of this frame, before its input.
        rewind_.push(frame_, frame_blob());
        if (apply_) {
            for (const input_event& event : input_.events_for_frame(frame_)) {
                apply_(event);
            }
        }
        scheduler_.run_frame();
        return ++frame_;
    }

    void session::run_frames(std::uint64_t count) {
        for (std::uint64_t i = 0; i < count; ++i) {
            run_frame();
        }
    }

    std::vector<std::uint8_t> session::save() { return frame_blob(); }

    load_result session::load(std::span<const std::uint8_t> data) {
        chips::state_reader reader(data);
        const std::uint64_t frame = reader.u64();
        if (!reader.ok()) {
            return {.status = load_status::truncated};
        }
        load_result result = read_save_state(data.subspan(frame_prefix_size), target_);
        if (result.ok()) {
            frame_ = frame;
        }
        return result;
    }

    bool session::rewind_to(std::uint64_t frame) {
        const std::vector<std::uint8_t>* const keyframe = rewind_.at_or_before(frame);
        if (keyframe == nullptr) {
            return false;
        }
        return load(std::span<const std::uint8_t>(*keyframe)).ok();
    }

} // namespace mnemos::runtime
