// Verifies the registry maps family IDs to factories, returns nullptr for
// unknown families, lists registered families, and supports re-registration
// (the path tests take to install mock factories without polluting a shared
// process-wide registry).

#include "adapter_registry.hpp"
#include "player_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <span>
#include <vector>

namespace {

    using mnemos::chips::frame_buffer_view;
    using mnemos::chips::ichip;
    using mnemos::frontend_sdk::adapter_options;
    using mnemos::frontend_sdk::adapter_registry;
    using mnemos::frontend_sdk::audio_chunk;
    using mnemos::frontend_sdk::controller_state;
    using mnemos::frontend_sdk::player_system;
    using mnemos::frontend_sdk::spec_field;
    using mnemos::frontend_sdk::video_region;

    class stub_system final : public player_system {
      public:
        explicit stub_system(std::string tag) : tag_(std::move(tag)) {}

        [[nodiscard]] const std::string& tag() const noexcept { return tag_; }
        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override { return {}; }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }

      private:
        std::string tag_;
        std::vector<spec_field> spec_{};
    };

} // namespace

TEST_CASE("adapter_registry maps family -> factory invocation", "[registry]") {
    auto& r = adapter_registry::instance();

    // Register a temporary family for the test. The test name is unique so
    // it doesn't collide with any real adapter the same process linked in.
    r.register_family("test.alpha", [](adapter_options) {
        return std::unique_ptr<player_system>(std::make_unique<stub_system>("alpha"));
    });

    auto system = r.create("test.alpha", {});
    REQUIRE(system != nullptr);

    auto* concrete = dynamic_cast<stub_system*>(system.get());
    REQUIRE(concrete != nullptr);
    CHECK(concrete->tag() == "alpha");
}

TEST_CASE("adapter_registry returns nullptr for unknown family", "[registry]") {
    auto& r = adapter_registry::instance();
    auto missing = r.create("test.does_not_exist_zzz", {});
    CHECK(missing == nullptr);
}

TEST_CASE("adapter_registry supports re-registration", "[registry]") {
    auto& r = adapter_registry::instance();

    r.register_family("test.beta", [](adapter_options) {
        return std::unique_ptr<player_system>(std::make_unique<stub_system>("v1"));
    });
    auto first = r.create("test.beta", {});
    REQUIRE(first != nullptr);
    CHECK(dynamic_cast<stub_system*>(first.get())->tag() == "v1");

    r.register_family("test.beta", [](adapter_options) {
        return std::unique_ptr<player_system>(std::make_unique<stub_system>("v2"));
    });
    auto second = r.create("test.beta", {});
    REQUIRE(second != nullptr);
    CHECK(dynamic_cast<stub_system*>(second.get())->tag() == "v2");
}

TEST_CASE("adapter_registry families() lists registered ids in sorted order",
          "[registry]") {
    auto& r = adapter_registry::instance();

    r.register_family("test.gamma", [](adapter_options) {
        return std::unique_ptr<player_system>(std::make_unique<stub_system>("g"));
    });
    r.register_family("test.delta", [](adapter_options) {
        return std::unique_ptr<player_system>(std::make_unique<stub_system>("d"));
    });

    const auto fams = r.families();
    REQUIRE(std::find(fams.begin(), fams.end(), "test.gamma") != fams.end());
    REQUIRE(std::find(fams.begin(), fams.end(), "test.delta") != fams.end());

    // sorted property: the gamma+delta entries appear in alphabetical order
    // when both are present.
    const auto gi = std::find(fams.begin(), fams.end(), "test.gamma");
    const auto di = std::find(fams.begin(), fams.end(), "test.delta");
    CHECK(di < gi); // delta < gamma alphabetically
}
