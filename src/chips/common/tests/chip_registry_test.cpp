#include <mnemos/chips/common.hpp>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string_view>
#include <type_traits>

namespace {

    class test_introspection final : public mnemos::instrumentation::i_chip_introspection {};

    class test_cpu final : public mnemos::chips::i_cpu {
      public:
        [[nodiscard]] mnemos::chips::chip_metadata metadata() const noexcept override {
            return {
                .manufacturer = "Test Vendor",
                .part_number = "cpu",
                .family = "test",
                .klass = mnemos::chips::chip_class::cpu,
                .revision = 1U,
            };
        }

        void tick(std::uint64_t cycles) override { elapsed_cycles += cycles; }

        void reset(mnemos::chips::reset_kind kind) override { last_reset = kind; }

        void save_state(mnemos::chips::state_writer&) const override {}

        void load_state(mnemos::chips::state_reader&) override {}

        void attach_bus(mnemos::chips::i_bus& bus) noexcept override { bus_ = &bus; }

        [[nodiscard]] mnemos::instrumentation::i_chip_introspection&
        introspection() noexcept override {
            return introspection_;
        }

        std::uint64_t elapsed_cycles{};
        mnemos::chips::reset_kind last_reset{mnemos::chips::reset_kind::power_on};

      private:
        test_introspection introspection_;
        mnemos::chips::i_bus* bus_{};
    };

    [[nodiscard]] std::unique_ptr<mnemos::chips::i_chip> make_test_cpu() {
        return std::make_unique<test_cpu>();
    }

} // namespace

static_assert(std::is_trivially_copyable_v<mnemos::chips::chip_metadata>);
static_assert(std::is_trivially_copyable_v<mnemos::chips::register_descriptor>);
static_assert(std::is_base_of_v<mnemos::chips::i_chip, mnemos::chips::i_cpu>);
static_assert(mnemos::chips::i_cpu::static_class == mnemos::chips::chip_class::cpu);

TEST_CASE("chip taxonomy names are stable") {
    using mnemos::chips::chip_class;

    CHECK(mnemos::chips::chip_class_name(chip_class::cpu) == "cpu");
    CHECK(mnemos::chips::chip_class_name(chip_class::audio_synth) == "audio_synth");
    CHECK(mnemos::chips::chip_class_name(chip_class::video) == "video");
    CHECK(mnemos::chips::chip_class_name(chip_class::bus_controller) == "bus_controller");
    CHECK(mnemos::chips::chip_class_name(chip_class::storage) == "storage");
    CHECK(mnemos::chips::chip_class_name(chip_class::mapper) == "mapper");
    CHECK(mnemos::chips::chip_class_name(chip_class::peripheral) == "peripheral");
}

TEST_CASE("chip ids require lowercase dotted components") {
    CHECK(mnemos::chips::is_canonical_chip_id("mos.6510"));
    CHECK(mnemos::chips::is_canonical_chip_id("yamaha.ym2612"));
    CHECK_FALSE(mnemos::chips::is_canonical_chip_id(""));
    CHECK_FALSE(mnemos::chips::is_canonical_chip_id("mos"));
    CHECK_FALSE(mnemos::chips::is_canonical_chip_id(".mos.6510"));
    CHECK_FALSE(mnemos::chips::is_canonical_chip_id("mos.6510."));
    CHECK_FALSE(mnemos::chips::is_canonical_chip_id("mos..6510"));
    CHECK_FALSE(mnemos::chips::is_canonical_chip_id("MOS.6510"));
    CHECK_FALSE(mnemos::chips::is_canonical_chip_id("mos-technology.6510"));
}

TEST_CASE("chip factory registry rejects invalid registrations") {
    const auto invalid_id =
        mnemos::chips::register_factory("invalid", mnemos::chips::chip_class::cpu, make_test_cpu);
    const auto null_factory = mnemos::chips::register_factory(
        "test.null_factory", mnemos::chips::chip_class::cpu, nullptr);

    CHECK_FALSE(invalid_id.registered());
    CHECK(invalid_id.error() == mnemos::chips::chip_registry_error::invalid_id);
    CHECK_FALSE(null_factory.registered());
    CHECK(null_factory.error() == mnemos::chips::chip_registry_error::null_factory);
}

TEST_CASE("chip factory registry registers and creates chips by canonical id") {
    const auto registration =
        mnemos::chips::register_factory("test.cpu", mnemos::chips::chip_class::cpu, make_test_cpu);

    REQUIRE(registration.registered());
    REQUIRE(registration.descriptor() != nullptr);
    CHECK(registration.descriptor()->canonical_id == "test.cpu");
    CHECK(registration.descriptor()->id == mnemos::foundation::chip_id{"test.cpu"});
    CHECK(registration.descriptor()->klass == mnemos::chips::chip_class::cpu);

    const mnemos::chips::chip_factory_descriptor* descriptor =
        mnemos::chips::find_factory("test.cpu");
    REQUIRE(descriptor != nullptr);
    CHECK(descriptor->canonical_id == "test.cpu");

    std::unique_ptr<mnemos::chips::i_chip> chip = mnemos::chips::create_chip("test.cpu");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::cpu);
}

TEST_CASE("chip factory registry rejects duplicate ids") {
    const auto first = mnemos::chips::register_factory(
        "test.duplicate", mnemos::chips::chip_class::cpu, make_test_cpu);
    const auto second = mnemos::chips::register_factory(
        "test.duplicate", mnemos::chips::chip_class::cpu, make_test_cpu);

    REQUIRE(first.registered());
    CHECK_FALSE(second.registered());
    CHECK(second.error() == mnemos::chips::chip_registry_error::duplicate_id);
    CHECK(second.descriptor() == first.descriptor());
}

TEST_CASE("missing chip factories return null") {
    CHECK(mnemos::chips::find_factory("test.missing") == nullptr);
    CHECK(mnemos::chips::create_chip("test.missing") == nullptr);
    CHECK(mnemos::chips::create_chip("not_canonical") == nullptr);
}
