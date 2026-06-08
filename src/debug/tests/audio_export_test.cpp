// Verifies export_audio walks player_system through the audio_source contract
// only -- writing PCM samples as WAV and emitting a JSON manifest -- with no
// knowledge of which system produced the samples.

#include "audio_export.hpp"
#include "audio_views.hpp"
#include "player_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::chip_metadata;
    using mnemos::chips::frame_buffer_view;
    using mnemos::chips::ichip;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::frontend_sdk::audio_chunk;
    using mnemos::frontend_sdk::controller_state;
    using mnemos::frontend_sdk::player_system;
    using mnemos::frontend_sdk::spec_field;
    using mnemos::frontend_sdk::video_region;
    using mnemos::instrumentation::audio_source;
    using mnemos::instrumentation::ichip_introspection;
    using mnemos::instrumentation::sample_view;

    class fake_audio final : public audio_source {
      public:
        [[nodiscard]] std::span<const sample_view> samples() const override { return table_; }

      private:
        std::array<std::int16_t, 3> pcm_{0, 1000, -1000};
        std::array<sample_view, 1> table_{sample_view{.name = "sample_0100",
                                                      .frames = pcm_,
                                                      .sample_rate = 32550U,
                                                      .channels = 1,
                                                      .loop_start = 2,
                                                      .source_addr = 0x0100U}};
    };

    class audio_intro final : public ichip_introspection {
      public:
        [[nodiscard]] audio_source* audio() override { return &audio_; }

      private:
        fake_audio audio_;
    };

    class pcm_chip final : public ichip {
      public:
        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "rf5c68",
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
        audio_intro intro_;
    };

    // A chip with no audio_source -- export must skip it.
    class plain_chip final : public ichip {
      public:
        [[nodiscard]] chip_metadata metadata() const noexcept override {
            return {.manufacturer = "test",
                    .part_number = "cpu",
                    .family = "test",
                    .klass = chip_class::cpu,
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

    class audio_system final : public player_system {
      public:
        audio_system() {
            chip_list_[0] = &plain_;
            chip_list_[1] = &pcm_;
        }
        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override { return {}; }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }
        [[nodiscard]] std::span<ichip* const> chips() const noexcept override { return chip_list_; }

      private:
        plain_chip plain_;
        pcm_chip pcm_;
        std::array<ichip*, 2> chip_list_{};
        std::vector<spec_field> spec_{};
    };

    [[nodiscard]] std::filesystem::path make_scratch_dir(const std::string& tag) {
        const auto base =
            std::filesystem::temp_directory_path() / ("mnemos_audio_export_test_" + tag);
        std::filesystem::remove_all(base);
        std::filesystem::create_directories(base);
        return base;
    }

    [[nodiscard]] std::vector<std::uint8_t> read_file(const std::filesystem::path& p) {
        std::ifstream in(p, std::ios::binary);
        std::vector<std::uint8_t> out;
        if (!in) {
            return out;
        }
        in.seekg(0, std::ios::end);
        out.resize(static_cast<std::size_t>(in.tellg()));
        in.seekg(0, std::ios::beg);
        in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
        return out;
    }

    [[nodiscard]] bool is_riff_wave(const std::vector<std::uint8_t>& b) {
        return b.size() >= 12U && b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F' &&
               b[8] == 'W' && b[9] == 'A' && b[10] == 'V' && b[11] == 'E';
    }

    [[nodiscard]] std::string read_text(const std::filesystem::path& p) {
        const auto bytes = read_file(p);
        return std::string(bytes.begin(), bytes.end());
    }

} // namespace

TEST_CASE("export_audio writes a WAV per sample and a JSON manifest", "[audio_export]") {
    const auto scratch = make_scratch_dir("basic");
    const auto base = (scratch / "out").string();

    audio_system sys;
    CHECK(mnemos::debug::export_audio(sys, base) == 1U);

    const auto wav = scratch / "out.rf5c68.sample.sample_0100.wav";
    REQUIRE(std::filesystem::exists(wav));
    CHECK(is_riff_wave(read_file(wav)));

    // The asset-free CPU chip produces no files.
    for (const auto& entry : std::filesystem::directory_iterator(scratch)) {
        CHECK(entry.path().filename().string().find("cpu") == std::string::npos);
    }
}

TEST_CASE("export_audio manifest describes each sample", "[audio_export]") {
    const auto scratch = make_scratch_dir("manifest");
    const auto base = (scratch / "out").string();

    audio_system sys;
    (void)mnemos::debug::export_audio(sys, base);

    const auto manifest = scratch / "out.audio.json";
    REQUIRE(std::filesystem::exists(manifest));
    const std::string json = read_text(manifest);

    CHECK(json.find("\"id\": \"rf5c68\"") != std::string::npos);
    CHECK(json.find("\"name\": \"sample_0100\"") != std::string::npos);
    CHECK(json.find("\"sample_rate\": 32550") != std::string::npos);
    CHECK(json.find("\"channels\": 1") != std::string::npos);
    CHECK(json.find("\"frames\": 3") != std::string::npos);
    CHECK(json.find("\"loop_start\": 2") != std::string::npos);
    CHECK(json.find("\"file\": \"out.rf5c68.sample.sample_0100.wav\"") != std::string::npos);
}

TEST_CASE("export_audio writes an empty manifest for a system with no samples", "[audio_export]") {
    class empty_system final : public player_system {
      public:
        [[nodiscard]] video_region region() const noexcept override { return {60000U}; }
        [[nodiscard]] const std::vector<spec_field>& system_spec() const noexcept override {
            return spec_;
        }
        [[nodiscard]] frame_buffer_view current_frame() const noexcept override { return {}; }
        void step_one_frame() override {}
        void apply_input(int, const controller_state&) noexcept override {}
        [[nodiscard]] audio_chunk drain_audio() noexcept override { return {}; }

      private:
        std::vector<spec_field> spec_{};
    };

    const auto scratch = make_scratch_dir("empty");
    const auto base = (scratch / "out").string();

    empty_system sys;
    CHECK(mnemos::debug::export_audio(sys, base) == 0U);

    const auto manifest = scratch / "out.audio.json";
    REQUIRE(std::filesystem::exists(manifest));
    CHECK(read_text(manifest).find("\"chips\": []") != std::string::npos);
}
