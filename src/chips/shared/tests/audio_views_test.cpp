// Verifies the audio sample-extraction contract (tier 2): a chip can expose an
// `audio_source` of PCM samples through `ichip_introspection::audio()`, and a
// generic consumer reads the frames + metadata WITHOUT downcasting to a concrete
// chip type. This is the abstraction the system-agnostic audio exporter depends
// on.

#include "audio_views.hpp"
#include "introspection_views.hpp"
#include "shared.hpp" // chip.hpp + ibus.hpp + chip_registry

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::chip_metadata;
    using mnemos::chips::ichip;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::instrumentation::audio_source;
    using mnemos::instrumentation::ichip_introspection;
    using mnemos::instrumentation::sample_view;

    // A minimal audio_source: a mono one-shot and a stereo looping sample. The
    // backing arrays live in the source so the borrowed spans stay valid.
    class fake_audio final : public audio_source {
      public:
        [[nodiscard]] std::span<const sample_view> samples() const override { return table_; }

      private:
        std::array<std::int16_t, 4> mono_{0, 1000, -1000, 32767};
        std::array<std::int16_t, 6> stereo_{1, -1, 2, -2, 3, -3}; // 3 L/R frames
        std::array<sample_view, 2> table_{sample_view{.name = "kick",
                                                      .frames = mono_,
                                                      .sample_rate = 8000U,
                                                      .channels = 1,
                                                      .loop_start = -1,
                                                      .source_addr = 0x0000U},
                                          sample_view{.name = "pad",
                                                      .frames = stereo_,
                                                      .sample_rate = 32000U,
                                                      .channels = 2,
                                                      .loop_start = 1,
                                                      .source_addr = 0x1000U}};
    };

    class audio_introspection final : public ichip_introspection {
      public:
        [[nodiscard]] audio_source* audio() override { return &audio_; }

      private:
        fake_audio audio_;
    };

    class audio_chip final : public ichip {
      public:
        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "pcm",
                    .family = "test",
                    .klass = chip_class::audio_synth,
                    .revision = 1U};
        }
        void tick(std::uint64_t) override {}
        void reset(reset_kind) override {}
        void save_state(state_writer&) const override {}
        void load_state(state_reader&) override {}
        [[nodiscard]] ichip_introspection& introspection() noexcept override { return intro_; }

      private:
        audio_introspection intro_;
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

TEST_CASE("ichip_introspection exposes no audio_source by default", "[audio]") {
    plain_chip chip;
    CHECK(chip.introspection().audio() == nullptr);
}

TEST_CASE("audio_source surfaces a mono one-shot sample", "[audio]") {
    audio_chip chip;
    auto* src = chip.introspection().audio();
    REQUIRE(src != nullptr);
    auto samples = src->samples();
    REQUIRE(samples.size() == 2U);

    const sample_view& mono = samples[0];
    CHECK(mono.name == "kick");
    CHECK(mono.sample_rate == 8000U);
    CHECK(mono.channels == 1);
    CHECK(mono.loop_start == -1);
    CHECK(mono.source_addr == 0x0000U);
    REQUIRE(mono.well_formed());
    CHECK(mono.frame_count() == 4U);
    REQUIRE(mono.frames.size() == 4U);
    CHECK(mono.frames[3] == 32767);
}

TEST_CASE("audio_source surfaces a stereo looping sample", "[audio]") {
    audio_chip chip;
    auto samples = chip.introspection().audio()->samples();
    REQUIRE(samples.size() == 2U);

    const sample_view& stereo = samples[1];
    CHECK(stereo.name == "pad");
    CHECK(stereo.sample_rate == 32000U);
    CHECK(stereo.channels == 2);
    CHECK(stereo.loop_start == 1);
    CHECK(stereo.source_addr == 0x1000U);
    REQUIRE(stereo.well_formed());
    CHECK(stereo.frame_count() == 3U); // 6 interleaved values / 2 channels
}

TEST_CASE("sample_view well_formed rejects a ragged interleave", "[audio]") {
    std::array<std::int16_t, 3> ragged{0, 1, 2};
    const sample_view bad{.name = "x", .frames = ragged, .sample_rate = 8000U, .channels = 2};
    CHECK_FALSE(bad.well_formed()); // 3 values is not a whole number of stereo frames
    CHECK(bad.frame_count() == 1U); // floor(3 / 2)
}
