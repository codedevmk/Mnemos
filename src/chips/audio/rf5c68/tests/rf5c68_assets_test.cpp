// Verifies the RF5C68 audio_source: it decodes a voice's wave-RAM region into a
// PCM sample_view (8-bit sign-magnitude -> s16, 0xFF terminator, loop point),
// read through the system-agnostic contract only (no downcast to rf5c68).

#include "audio_views.hpp"
#include "rf5c68.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace {
    using mnemos::chips::audio::rf5c68;

    [[nodiscard]] const mnemos::instrumentation::sample_view*
    find(std::span<const mnemos::instrumentation::sample_view> samples, std::string_view name) {
        for (const auto& s : samples) {
            if (s.name == name) {
                return &s;
            }
        }
        return nullptr;
    }
} // namespace

TEST_CASE("rf5c68 exposes an audio_source", "[rf5c68][audio]") {
    rf5c68 chip;
    CHECK(chip.introspection().audio() != nullptr);
}

TEST_CASE("rf5c68 decodes a voice's wave-RAM region into a PCM sample", "[rf5c68][audio]") {
    rf5c68 chip;
    const auto ram = chip.waveram();
    // A 0xFF at address 0 makes the default (start=0) voices zero-length, so only
    // the voice we configure produces a sample.
    ram[0x0000U] = 0xFFU;
    // Sample at 0x0100: 8-bit sign-magnitude, then the 0xFF loop/stop sentinel.
    const std::array<std::uint8_t, 5> wave = {0x10U, 0x7FU, 0x90U, 0xA0U, 0xFFU};
    for (std::size_t i = 0; i < wave.size(); ++i) {
        ram[0x0100U + i] = wave[i];
    }
    chip.write_reg(rf5c68::reg_ctrl, 0x00); // select voice 0
    chip.write_reg(rf5c68::reg_lsl, 0x02);  // loop-start low
    chip.write_reg(rf5c68::reg_lsh, 0x01);  // loop_start = 0x0102
    chip.write_reg(rf5c68::reg_st, 0x01);   // start = 0x0100

    auto samples = chip.introspection().audio()->samples();
    const auto* s = find(samples, "sample_0100");
    REQUIRE(s != nullptr);
    CHECK(s->sample_rate == 32550U);
    CHECK(s->channels == 1);
    CHECK(s->source_addr == 0x0100U);
    CHECK(s->loop_start == 2); // 0x0102 - 0x0100
    REQUIRE(s->well_formed());
    REQUIRE(s->frames.size() == 4U); // four data bytes before the 0xFF sentinel
    // sign-magnitude -> s16, scaled x256.
    CHECK(s->frames[0] == 16 * 256);  // 0x10 -> +16
    CHECK(s->frames[1] == 127 * 256); // 0x7F -> +127
    CHECK(s->frames[2] == -16 * 256); // 0x90 -> sign + mag 0x10
    CHECK(s->frames[3] == -32 * 256); // 0xA0 -> sign + mag 0x20
}

TEST_CASE("rf5c68 emits no samples on a blank chip", "[rf5c68][audio]") {
    rf5c68 chip; // wave RAM all zero -> no 0xFF sentinel anywhere
    CHECK(chip.introspection().audio()->samples().empty());
}
