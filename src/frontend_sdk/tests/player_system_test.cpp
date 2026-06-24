// Compile-time + behavioural smoke for the player_system interface. A trivial
// mock that returns a 2x2 framebuffer and a fixed audio chunk verifies the
// shape is implementable and the borrowed views observe the right values.

#include "player_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace {

    using mnemos::frontend_sdk::audio_chunk;
    using mnemos::frontend_sdk::controller_state;
    using mnemos::frontend_sdk::player_system;
    using mnemos::frontend_sdk::spec_field;
    using mnemos::frontend_sdk::video_region;

    class stub_player final : public player_system {
      public:
        stub_player() {
            spec_.push_back({.label = "System", .value = "Stub"});
            spec_.push_back({.label = "Region", .value = "NTSC"});
        }

        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }

        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }

        [[nodiscard]] mnemos::chips::frame_buffer_view current_frame() const noexcept override {
            return {.pixels = pixels_.data(), .width = 2U, .height = 2U};
        }

        void step_one_frame() override { ++frames_; }

        void apply_input(int port, const controller_state& state) noexcept override {
            last_port_ = port;
            last_input_ = state;
        }

        [[nodiscard]] audio_chunk drain_audio() noexcept override {
            return {.samples = audio_.data(), .frame_count = 1U, .sample_rate = 44100U};
        }

        std::uint64_t frames() const noexcept { return frames_; }
        int last_port() const noexcept { return last_port_; }
        const controller_state& last_input() const noexcept { return last_input_; }

      private:
        std::array<std::uint32_t, 4U> pixels_{0xFF0000U, 0x00FF00U, 0x0000FFU, 0xFFFFFFU};
        std::array<std::int16_t, 2U> audio_{1234, -1234};
        std::uint64_t frames_{};
        int last_port_{-1};
        controller_state last_input_{};
        std::vector<spec_field> spec_{};
    };

} // namespace

TEST_CASE("player_system stub fulfils the interface contract") {
    stub_player p;

    CHECK(p.region().frames_per_second_x1000 == 60000U);

    const auto fb = p.current_frame();
    REQUIRE(fb.pixels != nullptr);
    CHECK(fb.width == 2U);
    CHECK(fb.height == 2U);
    CHECK(fb.pixels[0] == 0xFF0000U);

    p.step_one_frame();
    p.step_one_frame();
    CHECK(p.frames() == 2U);

    controller_state st{};
    st.start = true;
    st.a = true;
    st.set_key(0x04U, true);
    p.apply_input(0, st);
    CHECK(p.last_port() == 0);
    CHECK(p.last_input().start);
    CHECK(p.last_input().a);
    CHECK_FALSE(p.last_input().up);
    CHECK(p.last_input().key_down(0x04U));
    st.set_key(0x04U, false);
    CHECK_FALSE(st.key_down(0x04U));
    st.set_key(static_cast<std::uint16_t>(mnemos::peripheral::keyboard_usage_count), true);
    CHECK_FALSE(st.key_down(static_cast<std::uint16_t>(
        mnemos::peripheral::keyboard_usage_count)));

    const auto audio = p.drain_audio();
    REQUIRE(audio.samples != nullptr);
    CHECK(audio.frame_count == 1U);
    CHECK(audio.sample_rate == 44100U);
    CHECK(audio.samples[0] == 1234);
    CHECK(audio.samples[1] == -1234);

    CHECK(p.save_state().empty());
    CHECK(p.load_state(std::span<const std::uint8_t>{}).status ==
          mnemos::runtime::load_status::unsupported_version);

    // The spec publisher hands back a borrowed view of the cached vector;
    // its contents stay stable across calls.
    const auto& spec_a = p.system_spec();
    const auto& spec_b = p.system_spec();
    REQUIRE(spec_a.size() == 2U);
    CHECK(spec_a[0].label == "System");
    CHECK(spec_a[0].value == "Stub");
    CHECK(spec_a[1].label == "Region");
    CHECK(spec_a[1].value == "NTSC");
    CHECK(&spec_a == &spec_b);
}
