#include "scheduler.hpp"

#include "chip.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <utility>
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

    struct recorded_tick final {
        char chip{};
        std::uint64_t cycles{};
    };

    struct recording_chip : chips::ichip {
        char name{};
        std::vector<recorded_tick>* log{};
        std::uint64_t ticks{};

        [[nodiscard]] chips::chip_metadata metadata() const noexcept override { return {}; }
        void tick(std::uint64_t cycles) override {
            ticks += cycles;
            log->push_back({name, cycles});
        }
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

TEST_CASE("scheduler batches fixed dividers at dispatch boundaries") {
    std::vector<recorded_tick> log;
    recording_chip beam;
    recording_chip cpu;
    recording_chip audio;
    beam.name = 'v';
    cpu.name = 'c';
    audio.name = 'a';
    beam.log = &log;
    cpu.log = &log;
    audio.log = &log;
    runtime::scheduler sched({{&beam, 1U}, {&cpu, 3U}, {&audio, 5U}}, nullptr);

    sched.run_master_cycles(6U);

    REQUIRE(log.size() == 6U);
    CHECK(log[0].chip == 'v');
    CHECK(log[0].cycles == 3U);
    CHECK(log[1].chip == 'c');
    CHECK(log[1].cycles == 1U);
    CHECK(log[2].chip == 'v');
    CHECK(log[2].cycles == 2U);
    CHECK(log[3].chip == 'a');
    CHECK(log[3].cycles == 1U);
    CHECK(log[4].chip == 'v');
    CHECK(log[4].cycles == 1U);
    CHECK(log[5].chip == 'c');
    CHECK(log[5].cycles == 1U);
    CHECK(beam.ticks == 6U);
    CHECK(cpu.ticks == 2U);
    CHECK(audio.ticks == 1U);
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

TEST_CASE("scheduler validation rejects invalid chip rates") {
    counting_chip chip;

    const auto null_status = runtime::validate_scheduled_chip({nullptr, 1U});
    CHECK_FALSE(null_status.has_value());
    CHECK(null_status.error() == runtime::schedule_error::null_chip);

    const auto zero_divider = runtime::validate_scheduled_chip({&chip, 0U});
    CHECK_FALSE(zero_divider.has_value());
    CHECK(zero_divider.error() == runtime::schedule_error::zero_divider);

    const auto zero_rational_den =
        runtime::validate_scheduled_chip({.chip = &chip, .divider = 1U, .rate_num = 3U});
    CHECK_FALSE(zero_rational_den.has_value());
    CHECK(zero_rational_den.error() == runtime::schedule_error::zero_rational_denominator);

    const auto overflowing_rational =
        runtime::validate_scheduled_chip({.chip = &chip,
                                          .divider = 1U,
                                          .rate_num = std::numeric_limits<std::uint32_t>::max(),
                                          .rate_den = 1U});
    CHECK_FALSE(overflowing_rational.has_value());
    CHECK(overflowing_rational.error() == runtime::schedule_error::overflowing_rational_rate);

    const auto valid_rational = runtime::validate_scheduled_chip(
        {.chip = &chip, .divider = 1U, .rate_num = 8U, .rate_den = 3U});
    CHECK(valid_rational.has_value());
}

TEST_CASE("scheduler factory rejects a frame source that is not scheduled") {
    counting_chip cpu;
    fake_video video;
    std::vector<runtime::scheduled_chip> chips = {{&cpu, 1U}};

    const auto result = runtime::make_scheduler(std::move(chips), &video);

    CHECK_FALSE(result.has_value());
    CHECK(result.error() == runtime::schedule_error::frame_source_not_scheduled);
}

TEST_CASE("scheduler constructor filters invalid entries instead of hanging") {
    counting_chip valid;
    counting_chip invalid;
    runtime::scheduler sched({{&valid, 1U}, {&invalid, 0U}}, nullptr);

    CHECK_FALSE(sched.config_valid());
    CHECK(sched.config_error() == runtime::schedule_error::zero_divider);

    sched.run_master_cycles(5U);

    CHECK(sched.master_cycle() == 5U);
    CHECK(valid.ticks == 5U);
    CHECK(invalid.ticks == 0U);
}

TEST_CASE("scheduler constructor disables an unscheduled frame source") {
    counting_chip cpu;
    fake_video video;
    runtime::scheduler sched({{&cpu, 1U}}, &video);

    CHECK_FALSE(sched.config_valid());
    CHECK(sched.config_error() == runtime::schedule_error::frame_source_not_scheduled);
    CHECK(sched.frame_source() == nullptr);
    CHECK(sched.run_frame() == 0U);
    CHECK(cpu.ticks == 0U);
}

TEST_CASE("scheduler spreads rational-rate chips evenly across master cycles") {
    counting_chip rational; // 3 chip cycles per 8 master cycles
    counting_chip divided;  // every fourth master cycle
    mnemos::runtime::scheduler s(
        {{.chip = &rational, .divider = 1U, .rate_num = 8U, .rate_den = 3U},
         {.chip = &divided, .divider = 4U}},
        nullptr);

    s.run_master_cycles(8U);
    CHECK(rational.ticks == 3U);
    CHECK(divided.ticks == 2U);

    // The accumulator carries the remainder exactly: over any span the tick
    // count is floor((cycles * den) / num) with no drift.
    s.run_master_cycles(8000U - 8U);
    CHECK(rational.ticks == 3000U);
    CHECK(divided.ticks == 2000U);
}

TEST_CASE("scheduler rational rate models the M72 YM2151 crystal exactly") {
    // 3.579545 MHz off a 32 MHz master: 715909 chip cycles per 6400000
    // master cycles. One emulated second must yield exactly 3579545 ticks.
    counting_chip opm;
    mnemos::runtime::scheduler s(
        {{.chip = &opm, .divider = 1U, .rate_num = 6400000U, .rate_den = 715909U}}, nullptr);
    s.run_master_cycles(32000000U);
    CHECK(opm.ticks == 3579545U);
}
