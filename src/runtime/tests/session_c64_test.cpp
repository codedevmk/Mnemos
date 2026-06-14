// runtime::session end-to-end on a real C64: frame-tagged input replay, rewind, and
// whole-machine save/load through the composed object (review B2). No ROM required.

#include "session.hpp"

#include "c64_input.hpp"
#include "c64_runtime.hpp"
#include "machine_save.hpp"

#include "chip.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using namespace mnemos;
    namespace c64 = manifests::c64;

    std::unique_ptr<c64::c64_runtime> make_c64() {
        return c64::build_c64_runtime(std::vector<std::uint8_t>(0x2000U),
                                      std::vector<std::uint8_t>(0x2000U),
                                      std::vector<std::uint8_t>(0x1000U));
    }

    std::vector<runtime::scheduled_chip> slots_for(c64::c64_runtime& rt) {
        std::vector<runtime::scheduled_chip> slots;
        for (const c64::c64_scheduled_entry& entry : rt.schedule()) {
            slots.push_back(runtime::scheduled_chip{
                .chip = entry.chip, .divider = entry.weight == 0U ? 1U : entry.weight});
        }
        return slots;
    }

    // The machine save target WITHOUT the scheduler component -- the session owns that.
    runtime::save_target make_target(c64::c64_runtime& rt) {
        const std::array<std::string_view, 2> regions{"ram", "color_ram"};
        runtime::save_target target =
            runtime::build_save_target(rt.graph, std::string("commodore.c64.pal"), 1U, 0U, regions);
        target.chips.push_back(runtime::save_chip{.id = "drive8", .chip = &rt.drive8});
        target.chips.push_back(runtime::save_chip{.id = "tape", .chip = &rt.tape});
        target.components.push_back(runtime::save_component{
            .id = "input",
            .save = [&rt](chips::state_writer& w) { rt.input.save_state(w); },
            .load = [&rt](chips::state_reader& r) { rt.input.load_state(r); }});
        return target;
    }

    // Machine-agnostic input -> C64: device 0 = joystick port 2, code = full mask.
    runtime::session::input_apply_fn apply_for(c64::c64_runtime& rt) {
        return [&rt](const runtime::input_event& event) {
            if (event.device == 0U) {
                rt.input.set_joystick(2U, event.code);
            }
        };
    }

    constexpr std::uint8_t k_joy_lf =
        static_cast<std::uint8_t>(c64::c64_input::joy_left | c64::c64_input::joy_fire);

} // namespace

TEST_CASE("runtime::session replays frame-tagged input deterministically after rewind") {
    const std::unique_ptr<c64::c64_runtime> rt = make_c64();
    runtime::session s(runtime::scheduler(slots_for(*rt), rt->video()), make_target(*rt),
                       apply_for(*rt));

    s.post_input(
        runtime::input_event{.frame = 3U, .device = 0U, .code = k_joy_lf, .pressed = true});
    s.post_input(runtime::input_event{.frame = 6U, .device = 0U, .code = 0U, .pressed = false});

    s.run_frames(10U);
    const std::vector<std::uint8_t> reference = s.save();
    REQUIRE(s.frame_index() == 10U);

    REQUIRE(s.rewind_to(3U));
    REQUIRE(s.frame_index() == 3U);
    s.run_frames(7U); // replay frames 3..9 -- input is re-applied from the log
    CHECK(s.frame_index() == 10U);
    CHECK(s.save() == reference);
}

TEST_CASE("runtime::session save/load round-trips the whole machine + frame index") {
    const std::unique_ptr<c64::c64_runtime> rt = make_c64();
    runtime::session s(runtime::scheduler(slots_for(*rt), rt->video()), make_target(*rt),
                       apply_for(*rt));

    s.post_input(runtime::input_event{
        .frame = 2U, .device = 0U, .code = c64::c64_input::joy_right, .pressed = true});
    s.run_frames(5U);
    const std::vector<std::uint8_t> snapshot = s.save();
    const std::uint64_t frame_at_save = s.frame_index();

    s.run_frames(5U);
    const std::vector<std::uint8_t> reference = s.save();
    REQUIRE(reference != snapshot);

    REQUIRE(s.load(snapshot).ok());
    CHECK(s.frame_index() == frame_at_save);
    CHECK(s.save() == snapshot); // idempotent
    s.run_frames(5U);
    CHECK(s.save() == reference); // transparency through the session
}
