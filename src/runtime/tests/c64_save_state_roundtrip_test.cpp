// End-to-end whole-machine save/load/rewind/input determinism on a real C64.
//
// The machine is built through the manifest path with zero-filled system ROM images
// (no copyrighted ROM is required or committed -- ARCH-005 / repo policy). From
// reset it evolves deterministically, which is all these invariants need:
//
//   * the serialised save-state byte stream is identical across independent runs
//     (ARCH-004 §16: determinism of the save-state byte stream);
//   * loading a save reproduces the machine's forward evolution exactly
//     (save/load transparency -- the property rewind and netplay are built on);
//   * a rewind-ring seek + replay lands on the identical state;
//   * corrupt / mismatched / truncated states are rejected, and unknown chunks are
//     skipped (forward compatibility).
//
// The captured state is the whole machine: every scheduled chip plus the banking
// PLA, the 64K RAM and colour RAM, the non-chip keyboard/joystick state, and the
// scheduler's pacing state -- so the transparency comparison is over all of it.

#include "c64_input.hpp"
#include "c64_runtime.hpp"
#include "machine_save.hpp"
#include "rewind.hpp"
#include "save_state.hpp"
#include "scheduler.hpp"

#include "chip.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

    using namespace mnemos;

    constexpr std::string_view k_manifest_id = "commodore.c64.pal";
    constexpr std::uint32_t k_manifest_rev = 1U;

    std::unique_ptr<manifests::c64::c64_runtime> make_c64() {
        return manifests::c64::build_c64_runtime(std::vector<std::uint8_t>(0x2000U),  // BASIC 8K
                                                 std::vector<std::uint8_t>(0x2000U),  // KERNAL 8K
                                                 std::vector<std::uint8_t>(0x1000U)); // CHARGEN 4K
    }

    runtime::scheduler make_scheduler(manifests::c64::c64_runtime& rt) {
        std::vector<runtime::scheduled_chip> slots;
        for (const manifests::c64::c64_scheduled_entry& entry : rt.schedule()) {
            slots.push_back(
                runtime::scheduled_chip{.chip = entry.chip,
                                        .divider = entry.weight == 0U ? 1U : entry.weight,
                                        .rate_num = 0U,
                                        .rate_den = 0U});
        }
        return runtime::scheduler(std::move(slots), rt.video());
    }

    // A deterministic, distinctive starting state so a save has real content to
    // preserve (rather than all-zero RAM that would round-trip trivially).
    void seed(manifests::c64::c64_runtime& rt) {
        const std::span<std::uint8_t> ram = rt.graph.region_span("ram");
        for (std::size_t i = 0; i < ram.size(); ++i) {
            ram[i] = static_cast<std::uint8_t>((i * 31U + 7U) & 0xFFU);
        }
        const std::span<std::uint8_t> color = rt.graph.region_span("color_ram");
        for (std::size_t i = 0; i < color.size(); ++i) {
            color[i] = static_cast<std::uint8_t>((i * 13U + 1U) & 0x0FU);
        }
        rt.input.set_joystick(2U, static_cast<std::uint8_t>(manifests::c64::c64_input::joy_left |
                                                            manifests::c64::c64_input::joy_fire));
        rt.input.press(manifests::c64::c64_input::key::a);
    }

    // The whole-machine save target: the generic builder supplies the manifest chips
    // (cpu/video/audio/cia1/cia2/pla) and the writable RAM regions; the drive and
    // tape are scheduled but not manifest chips, and the keyboard state and scheduler
    // pacing are not chips at all, so they are added here.
    runtime::save_target full_target(manifests::c64::c64_runtime& rt, runtime::scheduler& sched) {
        const std::array<std::string_view, 2> regions{"ram", "color_ram"};
        runtime::save_target target = runtime::build_save_target(
            rt.graph, std::string(k_manifest_id), k_manifest_rev, sched.master_cycle(), regions);

        target.chips.push_back(runtime::save_chip{.id = "drive8", .chip = &rt.drive8});
        target.chips.push_back(runtime::save_chip{.id = "tape", .chip = &rt.tape});

        target.components.push_back(runtime::save_component{
            .id = "input",
            .save = [&rt](chips::state_writer& writer) { rt.input.save_state(writer); },
            .load = [&rt](chips::state_reader& reader) { rt.input.load_state(reader); }});
        target.components.push_back(runtime::save_component{
            .id = "scheduler",
            .save = [&sched](chips::state_writer& writer) { sched.save_state(writer); },
            .load = [&sched](chips::state_reader& reader) { sched.load_state(reader); }});
        return target;
    }

    std::vector<std::uint8_t> snapshot(manifests::c64::c64_runtime& rt, runtime::scheduler& sched) {
        runtime::save_target target = full_target(rt, sched);
        target.master_cycle = sched.master_cycle();
        return runtime::write_save_state(target);
    }

} // namespace

TEST_CASE("C64 machine exposes the writable regions the save target needs") {
    const std::unique_ptr<manifests::c64::c64_runtime> rt = make_c64();
    REQUIRE(rt != nullptr);
    CHECK(rt->graph.region_span("ram").size() == 0x10000U);
    CHECK(rt->graph.region_span("color_ram").size() == 0x0400U);
}

TEST_CASE("C64 save-state byte stream is deterministic across independent runs") {
    const std::unique_ptr<manifests::c64::c64_runtime> a = make_c64();
    const std::unique_ptr<manifests::c64::c64_runtime> b = make_c64();
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);

    runtime::scheduler sa = make_scheduler(*a);
    runtime::scheduler sb = make_scheduler(*b);
    seed(*a);
    seed(*b);
    sa.run_frames(8U);
    sb.run_frames(8U);

    const std::vector<std::uint8_t> da = snapshot(*a, sa);
    const std::vector<std::uint8_t> db = snapshot(*b, sb);

    REQUIRE(!da.empty());
    CHECK(sa.master_cycle() == sb.master_cycle());
    CHECK(da == db);
}

TEST_CASE("C64 save/load reproduces forward evolution exactly") {
    const std::unique_ptr<manifests::c64::c64_runtime> a = make_c64();
    REQUIRE(a != nullptr);
    runtime::scheduler sa = make_scheduler(*a);
    seed(*a);

    sa.run_frames(8U);
    const std::uint64_t cycle_at_save = sa.master_cycle();
    const std::vector<std::uint8_t> state_at_8 = snapshot(*a, sa);

    // Reference trajectory: continue eight more frames from the live machine.
    sa.run_frames(8U);
    const std::vector<std::uint8_t> reference_at_16 = snapshot(*a, sa);
    CHECK(sa.master_cycle() != cycle_at_save);

    // Restore the frame-8 snapshot into the same machine and replay eight frames.
    runtime::save_target target = full_target(*a, sa);
    const runtime::load_result loaded = runtime::read_save_state(state_at_8, target);
    REQUIRE(loaded.ok());
    CHECK(loaded.master_cycle == cycle_at_save);
    CHECK(sa.master_cycle() == cycle_at_save); // scheduler pacing was restored
    sa.run_frames(8U);

    const std::vector<std::uint8_t> replayed_at_16 = snapshot(*a, sa);
    CHECK(replayed_at_16 == reference_at_16);
}

TEST_CASE("C64 rewind-ring seek and replay is deterministic") {
    const std::unique_ptr<manifests::c64::c64_runtime> a = make_c64();
    REQUIRE(a != nullptr);
    runtime::scheduler sa = make_scheduler(*a);
    seed(*a);

    runtime::rewind_ring ring(32U);
    std::vector<std::uint64_t> cycle_at_frame(1U, 0U); // 1-based index by frame
    for (std::uint64_t frame = 1U; frame <= 12U; ++frame) {
        sa.run_frame();
        ring.push(frame, snapshot(*a, sa));
        cycle_at_frame.push_back(sa.master_cycle());
    }
    const std::vector<std::uint8_t> final_at_12 = snapshot(*a, sa);

    const std::vector<std::uint8_t>* const seek = ring.at_or_before(4U);
    REQUIRE(seek != nullptr);

    runtime::save_target target = full_target(*a, sa);
    REQUIRE(runtime::read_save_state(*seek, target).ok());
    REQUIRE(sa.master_cycle() == cycle_at_frame[4]);

    sa.run_frames(12U - 4U);
    const std::vector<std::uint8_t> replayed_at_12 = snapshot(*a, sa);
    CHECK(sa.master_cycle() == cycle_at_frame[12]);
    CHECK(replayed_at_12 == final_at_12);
}

TEST_CASE("C64 save-state load rejects corruption and mismatch") {
    const std::unique_ptr<manifests::c64::c64_runtime> a = make_c64();
    REQUIRE(a != nullptr);
    runtime::scheduler sa = make_scheduler(*a);
    seed(*a);
    sa.run_frames(4U);
    const std::vector<std::uint8_t> good = snapshot(*a, sa);
    REQUIRE(good.size() > 16U);

    runtime::save_target target = full_target(*a, sa);

    SECTION("bad magic") {
        std::vector<std::uint8_t> bad = good;
        bad[0] = static_cast<std::uint8_t>(bad[0] ^ 0xFFU);
        CHECK(runtime::read_save_state(bad, target).status == runtime::load_status::bad_magic);
    }
    SECTION("too short to be a container") {
        const std::vector<std::uint8_t> bad(good.begin(), good.begin() + 6);
        CHECK(runtime::read_save_state(bad, target).status == runtime::load_status::bad_magic);
    }
    SECTION("truncated header") {
        // Long enough to pass the magic/size guard, but the header underruns.
        const std::vector<std::uint8_t> bad(good.begin(), good.begin() + 16);
        CHECK(runtime::read_save_state(bad, target).status == runtime::load_status::truncated);
    }
    SECTION("corrupted body fails the CRC") {
        std::vector<std::uint8_t> bad = good;
        bad[bad.size() - 6U] = static_cast<std::uint8_t>(bad[bad.size() - 6U] ^ 0xFFU);
        CHECK(runtime::read_save_state(bad, target).status == runtime::load_status::bad_crc);
    }
    SECTION("manifest id mismatch") {
        runtime::save_target other = full_target(*a, sa);
        other.manifest_id = "commodore.c64.ntsc";
        CHECK(runtime::read_save_state(good, other).status ==
              runtime::load_status::manifest_mismatch);
    }
    SECTION("a partial target skips unknown chunks and still loads") {
        const std::array<std::string_view, 2> regions{"ram", "color_ram"};
        runtime::save_target partial = runtime::build_save_target(
            a->graph, std::string(k_manifest_id), k_manifest_rev, sa.master_cycle(), regions);
        CHECK(runtime::read_save_state(good, partial).ok());
    }
    SECTION("a valid load still succeeds") { CHECK(runtime::read_save_state(good, target).ok()); }
}
