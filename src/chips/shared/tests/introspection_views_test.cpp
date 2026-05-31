// Verifies the capability sub-interfaces of `ichip_introspection` (tier 2) can
// be implemented and consumed without downcasting to concrete chip types -- the
// abstraction the player frontend's `--screenshot` debug path depends on.

#include "introspection_views.hpp"
#include "shared.hpp" // chip.hpp + ibus.hpp + chip_registry

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::chip_metadata;
    using mnemos::chips::frame_buffer_view;
    using mnemos::chips::ichip;
    using mnemos::chips::register_descriptor;
    using mnemos::chips::register_value_format;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::instrumentation::debug_layer;
    using mnemos::instrumentation::ichip_introspection;
    using mnemos::instrumentation::memory_view;
    using mnemos::instrumentation::register_view;
    using mnemos::instrumentation::trace_event;
    using mnemos::instrumentation::trace_target;

    class ram_view final : public memory_view {
      public:
        explicit ram_view(std::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}
        [[nodiscard]] std::string_view name() const noexcept override { return "ram"; }
        [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept override {
            return bytes_;
        }

      private:
        std::span<const std::uint8_t> bytes_;
    };

    class single_reg_view final : public register_view {
      public:
        explicit single_reg_view(const register_descriptor& reg) noexcept : reg_(reg) {}
        [[nodiscard]] std::span<const register_descriptor> registers() override {
            return std::span<const register_descriptor>(&reg_, 1U);
        }

      private:
        register_descriptor reg_;
    };

    class noop_trace final : public trace_target {
      public:
        void install(callback cb) override {
            installed_ = static_cast<bool>(cb);
            last_ = std::move(cb);
        }
        // Test helper: fire a synthetic event.
        void fire(trace_event ev) {
            if (last_) {
                last_(ev);
            }
        }
        [[nodiscard]] bool installed() const noexcept { return installed_; }

      private:
        callback last_{};
        bool installed_{};
    };

    class fake_layer final : public debug_layer {
      public:
        [[nodiscard]] std::string_view name() const noexcept override { return "plane_a"; }
        [[nodiscard]] frame_buffer_view view() const override {
            return {.pixels = pixels_.data(), .width = 2U, .height = 2U, .stride = 0U};
        }

      private:
        std::array<std::uint32_t, 4> pixels_{0x010203U, 0x040506U, 0x070809U, 0x0A0B0CU};
    };

    class fancy_introspection final : public ichip_introspection {
      public:
        fancy_introspection(std::span<const std::uint8_t> ram, const register_descriptor& reg)
            : ram_(ram), reg_(reg) {
            mem_table_[0] = &ram_;
            layer_table_[0] = &layer_;
        }

        [[nodiscard]] std::span<memory_view* const> memory_views() override { return mem_table_; }
        [[nodiscard]] register_view* registers() override { return &reg_; }
        [[nodiscard]] trace_target* trace() override { return &trace_; }
        [[nodiscard]] std::span<debug_layer* const> debug_layers() override { return layer_table_; }

        [[nodiscard]] noop_trace& trace_impl() noexcept { return trace_; }

      private:
        ram_view ram_;
        single_reg_view reg_;
        noop_trace trace_;
        fake_layer layer_;
        std::array<memory_view*, 1> mem_table_{};
        std::array<debug_layer*, 1> layer_table_{};
    };

    class fancy_chip final : public ichip {
      public:
        fancy_chip()
            : ram_{0xAAU, 0xBBU, 0xCCU}, reg_{.name = "pc",
                                              .value = 0x1234U,
                                              .bit_width = 16U,
                                              .format = register_value_format::unsigned_integer},
              intro_(ram_, reg_) {}

        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "fancy",
                    .family = "test",
                    .klass = chip_class::cpu,
                    .revision = 1U};
        }
        void tick(std::uint64_t) override {}
        void reset(reset_kind) override {}
        void save_state(state_writer&) const override {}
        void load_state(state_reader&) override {}
        [[nodiscard]] ichip_introspection& introspection() noexcept override { return intro_; }

        [[nodiscard]] fancy_introspection& intro_impl() noexcept { return intro_; }

      private:
        std::array<std::uint8_t, 3> ram_;
        register_descriptor reg_;
        fancy_introspection intro_;
    };

    class plain_chip final : public ichip {
      public:
        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "plain",
                    .family = "test",
                    .klass = chip_class::peripheral,
                    .revision = 1U};
        }
        void tick(std::uint64_t) override {}
        void reset(reset_kind) override {}
        void save_state(state_writer&) const override {}
        void load_state(state_reader&) override {}
        [[nodiscard]] ichip_introspection& introspection() noexcept override { return intro_; }

      private:
        ichip_introspection intro_;
    };

} // namespace

TEST_CASE("ichip_introspection default exposes no capabilities", "[introspection]") {
    plain_chip chip;
    auto& intro = chip.introspection();
    CHECK(intro.memory_views().empty());
    CHECK(intro.registers() == nullptr);
    CHECK(intro.trace() == nullptr);
    CHECK(intro.debug_layers().empty());
}

TEST_CASE("ichip_introspection memory_view advertises live bytes", "[introspection]") {
    fancy_chip chip;
    auto& intro = chip.introspection();
    auto views = intro.memory_views();
    REQUIRE(views.size() == 1U);
    auto* mv = views[0];
    REQUIRE(mv != nullptr);
    CHECK(mv->name() == "ram");
    auto bytes = mv->bytes();
    REQUIRE(bytes.size() == 3U);
    CHECK(bytes[0] == 0xAAU);
    CHECK(bytes[1] == 0xBBU);
    CHECK(bytes[2] == 0xCCU);
}

TEST_CASE("ichip_introspection register_view returns descriptor span", "[introspection]") {
    fancy_chip chip;
    auto* regs = chip.introspection().registers();
    REQUIRE(regs != nullptr);
    auto descriptors = regs->registers();
    REQUIRE(descriptors.size() == 1U);
    CHECK(descriptors[0].name == "pc");
    CHECK(descriptors[0].value == 0x1234U);
    CHECK(descriptors[0].bit_width == 16U);
}

TEST_CASE("ichip_introspection trace_target install + fire round-trip", "[introspection]") {
    fancy_chip chip;
    auto* tt = chip.introspection().trace();
    REQUIRE(tt != nullptr);

    trace_event observed{};
    bool fired = false;
    tt->install([&](const trace_event& ev) {
        observed = ev;
        fired = true;
    });
    chip.intro_impl().trace_impl().fire({.pc = 0xDEADU, .cycles = 42U});

    CHECK(fired);
    CHECK(observed.pc == 0xDEADU);
    CHECK(observed.cycles == 42U);

    // Empty callback clears.
    tt->install({});
    fired = false;
    chip.intro_impl().trace_impl().fire({.pc = 0xBEEFU, .cycles = 7U});
    CHECK_FALSE(fired);
}

TEST_CASE("ichip_introspection debug_layer view exposes pixels", "[introspection]") {
    fancy_chip chip;
    auto layers = chip.introspection().debug_layers();
    REQUIRE(layers.size() == 1U);
    auto* layer = layers[0];
    REQUIRE(layer != nullptr);
    CHECK(layer->name() == "plane_a");
    auto fb = layer->view();
    CHECK(fb.width == 2U);
    CHECK(fb.height == 2U);
    REQUIRE(fb.pixels != nullptr);
    CHECK(fb.pixels[0] == 0x010203U);
    CHECK(fb.pixels[3] == 0x0A0B0CU);
}
