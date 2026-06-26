#include "msm5205.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::msm5205;

    constexpr std::array<std::int16_t, 16> kRampGolden = {32,   128,   288,   512,   800,  1200,
                                                          1920, 3408,  3200,  2624,  1744, 624,
                                                          -688, -2624, -6000, -12720};
    constexpr std::array<std::int16_t, 20> kPositiveSaturationGolden = {
        480,   1488,  3664,  8352,  18240, 32752, 32752, 32752, 32752, 32752,
        32752, 32752, 32752, 32752, 32752, 32752, 32752, 32752, 32752, 32752};
} // namespace

TEST_CASE("msm5205 registers in the chip factory as an audio synth", "[msm5205][audio]") {
    auto chip = mnemos::chips::create_chip("oki.msm5205");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == mnemos::chips::chip_class::audio_synth);
    REQUIRE(chip->metadata().part_number == "MSM5205");
}

TEST_CASE("msm5205 consumes one latched ADPCM nibble per VCLK", "[msm5205][audio]") {
    msm5205 chip;
    for (std::size_t i = 0; i < kRampGolden.size(); ++i) {
        chip.data_w(static_cast<std::uint8_t>(i));
        chip.vclk_tick();
        INFO("nibble " << i);
        REQUIRE(chip.last_sample() == kRampGolden[i]);
    }
    REQUIRE(chip.vclk_count() == kRampGolden.size());
}

TEST_CASE("msm5205 clamps the 12-bit predictor at positive saturation", "[msm5205][audio]") {
    msm5205 chip;
    for (std::size_t i = 0; i < kPositiveSaturationGolden.size(); ++i) {
        chip.data_w(0x07U);
        chip.vclk_tick();
        INFO("sample " << i);
        REQUIRE(chip.last_sample() == kPositiveSaturationGolden[i]);
    }
    REQUIRE(chip.predictor() == 2047);
    REQUIRE(chip.step_index() == msm5205::max_step_index);
}

TEST_CASE("msm5205 reset pin silences and reinitializes the decoder", "[msm5205][audio]") {
    msm5205 chip;
    chip.data_w(0x07U);
    chip.vclk_tick();
    REQUIRE(chip.last_sample() != 0);
    chip.reset_w(true);
    REQUIRE(chip.reset_asserted());
    REQUIRE(chip.last_sample() == 0);
    REQUIRE(chip.predictor() == 0);

    chip.data_w(0x07U);
    chip.vclk_tick();
    REQUIRE(chip.last_sample() == 0);
    chip.reset_w(false);
    chip.vclk_tick();
    REQUIRE(chip.last_sample() == 480);
}

TEST_CASE("msm5205 generate duplicates mono output to both lanes", "[msm5205][audio]") {
    msm5205 chip;
    chip.data_w(0x07U);
    std::array<std::int16_t, 8> buf{};
    chip.generate(buf);
    for (std::size_t pair = 0; pair < buf.size() / 2U; ++pair) {
        REQUIRE(buf[pair * 2U] == buf[pair * 2U + 1U]);
        REQUIRE(buf[pair * 2U] == kPositiveSaturationGolden[pair]);
    }
}

TEST_CASE("msm5205 capture sink queues stereo frames behind the divider", "[msm5205][audio]") {
    msm5205 chip;
    chip.set_clock_divider(4);
    chip.enable_audio_capture(true);
    chip.data_w(0x07U);
    chip.tick(10);
    REQUIRE(chip.pending_samples() == 2U);

    std::array<std::int16_t, 8> buf{};
    const std::size_t got = chip.drain_samples(buf.data(), 8U);
    REQUIRE(got == 2U);
    REQUIRE(buf[0] == 480);
    REQUIRE(buf[1] == 480);
    REQUIRE(buf[2] == 1488);
    REQUIRE(buf[3] == 1488);
    REQUIRE(chip.pending_samples() == 0U);
}

TEST_CASE("msm5205 save_state/load_state preserves decoder phase", "[msm5205][audio]") {
    msm5205 source;
    source.set_clock_divider(3);
    for (std::uint8_t nibble : std::array<std::uint8_t, 4>{0x01U, 0x02U, 0x07U, 0x0FU}) {
        source.data_w(nibble);
        source.vclk_tick();
    }

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    source.save_state(writer);

    msm5205 restored;
    state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    source.data_w(0x07U);
    restored.data_w(0x07U);
    source.vclk_tick();
    restored.vclk_tick();
    REQUIRE(restored.last_sample() == source.last_sample());
    REQUIRE(restored.step_index() == source.step_index());
}
