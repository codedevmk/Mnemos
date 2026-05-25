#include "scheduler.hpp"

#include "chip.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {

    using namespace mnemos;

    struct introspection_stub final : instrumentation::ichip_introspection {};

    // Counts the cycles it is ticked with; everything else is a no-op stub.
    struct counting_chip : chips::ichip {
        std::uint64_t ticks{};

        [[nodiscard]] chips::chip_metadata metadata() const noexcept override { return {}; }
        void tick(std::uint64_t cycles) override { ticks += cycles; }
        void reset(chips::reset_kind) override {}
        void save_state(chips::state_writer&) const override {}
        void load_state(chips::state_reader&) override {}
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return intro_;
        }

      private:
        introspection_stub intro_;
    };

    // A video chip that completes a frame every `period` ticks.
    struct fake_video final : chips::ivideo {
        std::uint32_t period{1U};
        std::uint64_t frames{};
        std::uint32_t counter{};

        [[nodiscard]] chips::chip_metadata metadata() const noexcept override { return {}; }
        void tick(std::uint64_t cycles) override {
            counter += static_cast<std::uint32_t>(cycles);
            while (counter >= period) {
                counter -= period;
                ++frames;
            }
        }
        void reset(chips::reset_kind) override {}
        void save_state(chips::state_writer&) const override {}
        void load_state(chips::state_reader&) override {}
        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override {
            return intro_;
        }
        [[nodiscard]] std::uint64_t frame_index() const noexcept override { return frames; }
        [[nodiscard]] chips::frame_buffer_view framebuffer() const noexcept override {
            return {nullptr, 0U, 0U};
        }

      private:
        introspection_stub intro_;
    };

} // namespace

TEST_CASE("scheduler ticks every chip each cycle in the lockstep fast path") {
    counting_chip a;
    counting_chip b;
    runtime::scheduler sched({{&a, 1U}, {&b, 1U}}, nullptr);

    sched.run_master_cycles(7U);

    CHECK(sched.master_cycle() == 7U);
    CHECK(a.ticks == 7U);
    CHECK(b.ticks == 7U);
}

TEST_CASE("scheduler honours per-chip dividers") {
    counting_chip fast; // every cycle
    counting_chip slow; // every third cycle
    runtime::scheduler sched({{&fast, 1U}, {&slow, 3U}}, nullptr);

    sched.run_master_cycles(12U);

    CHECK(sched.master_cycle() == 12U);
    CHECK(fast.ticks == 12U);
    CHECK(slow.ticks == 4U);
}

TEST_CASE("scheduler advances exactly one frame at a time") {
    fake_video video;
    video.period = 5U;
    counting_chip cpu;
    runtime::scheduler sched({{&video, 1U}, {&cpu, 1U}}, &video);

    const std::uint64_t reached = sched.run_frame();

    CHECK(reached == 1U);
    CHECK(sched.frame_index() == 1U);
    CHECK(sched.master_cycle() == 5U);
    CHECK(cpu.ticks == 5U); // CPU advanced in lockstep with the beam

    sched.run_frames(3U);
    CHECK(sched.frame_index() == 4U);
    CHECK(sched.master_cycle() == 20U);
}

TEST_CASE("scheduler with no frame source treats run_frame as a no-op") {
    counting_chip a;
    runtime::scheduler sched({{&a, 1U}}, nullptr);

    CHECK(sched.run_frame() == 0U);
    CHECK(a.ticks == 0U);
    CHECK(sched.frame_index() == 0U);
}
