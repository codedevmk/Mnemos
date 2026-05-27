// Verifies scheduler_factory is substitutable -- a custom factory's create()
// is called with the exact inputs the adapter would have passed to the
// default path, and the resulting scheduler runs the supplied chips.

#include "scheduler_factory.hpp"

#include "chip.hpp"
#include "scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::chip_metadata;
    using mnemos::chips::ichip;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::instrumentation::ichip_introspection;
    using mnemos::frontend_sdk::default_scheduler_factory;
    using mnemos::frontend_sdk::scheduler_factory;
    using mnemos::runtime::scheduled_chip;
    using mnemos::runtime::scheduler;

    class tick_counter_chip final : public ichip {
      public:
        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "counter",
                    .family = "test",
                    .klass = chip_class::peripheral,
                    .revision = 1U};
        }
        void tick(std::uint64_t c) override { ticks += c; }
        void reset(reset_kind) override {}
        void save_state(state_writer&) const override {}
        void load_state(state_reader&) override {}
        [[nodiscard]] ichip_introspection& introspection() noexcept override {
            return intro_;
        }

        std::uint64_t ticks{};

      private:
        ichip_introspection intro_{};
    };

} // namespace

TEST_CASE("default_scheduler_factory builds a runtime::scheduler that ticks chips",
          "[scheduler_factory]") {
    default_scheduler_factory factory;
    tick_counter_chip chip;
    auto sched = factory.create({{&chip, 1U}}, nullptr);
    sched.run_master_cycles(5U);
    CHECK(chip.ticks == 5U);
}

TEST_CASE("scheduler_factory can be substituted by a custom implementation",
          "[scheduler_factory]") {
    class recording_factory final : public scheduler_factory {
      public:
        [[nodiscard]] scheduler create(std::vector<scheduled_chip> chips,
                                       mnemos::chips::ivideo* fs) override {
            captured_count = chips.size();
            captured_frame_source = fs;
            return scheduler(std::move(chips), fs);
        }
        std::size_t captured_count{};
        mnemos::chips::ivideo* captured_frame_source{};
    };

    recording_factory rec;
    tick_counter_chip a;
    tick_counter_chip b;
    auto sched = rec.create({{&a, 1U}, {&b, 2U}}, nullptr);
    CHECK(rec.captured_count == 2U);
    CHECK(rec.captured_frame_source == nullptr);

    // The returned scheduler is fully functional -- the factory just gets to
    // intercept construction; it doesn't change the runtime contract.
    sched.run_master_cycles(2U);
    CHECK(a.ticks == 2U);
    CHECK(b.ticks == 1U);
}
