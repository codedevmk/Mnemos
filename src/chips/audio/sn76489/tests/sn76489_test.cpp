#include "sn76489.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::audio::sn76489;
    using reset_kind = mnemos::chips::reset_kind;
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::iaudio_synth, sn76489>);

TEST_CASE("sn76489 reports identity and registers under ti.sn76489") {
    const sn76489 psg;
    const auto md = psg.metadata();
    CHECK(md.manufacturer == "Texas Instruments");
    CHECK(md.part_number == "SN76489");
    CHECK(md.klass == mnemos::chips::chip_class::audio_synth);

    auto chip = mnemos::chips::create_chip("ti.sn76489");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == "SN76489");
}

TEST_CASE("sn76489 powers on silent") {
    sn76489 psg;
    for (int ch = 0; ch < sn76489::channel_count; ++ch) {
        CHECK(psg.volume(ch) == 0x0FU); // all channels attenuated to silence
    }
    CHECK(psg.step() == 0); // nothing audible
}

TEST_CASE("sn76489 latches tone and volume registers") {
    sn76489 psg;
    psg.write(0x82U); // latch ch0 tone, low nibble = 2
    psg.write(0x01U); // data byte: high 6 bits = 1 -> tone = (1 << 4) | 2 = 0x12
    CHECK(psg.tone(0) == 0x012U);

    psg.write(0x90U); // latch ch0 volume = 0 (loud)
    CHECK(psg.volume(0) == 0x00U);
}

TEST_CASE("sn76489 produces a square wave at the channel volume") {
    sn76489 psg;
    (void)psg.step(); // reset sample (silent)
    psg.write(0x82U); // tone0 low nibble = 2
    psg.write(0x00U); // tone0 high bits = 0 -> period 2
    psg.write(0x90U); // ch0 volume 0 (loudest)

    bool saw_pos = false;
    bool saw_neg = false;
    for (int i = 0; i < 8; ++i) {
        const std::int16_t s = psg.step();
        CHECK((s == 8191 || s == -8191)); // only ch0 audible, at peak amplitude
        saw_pos = saw_pos || s == 8191;
        saw_neg = saw_neg || s == -8191;
    }
    CHECK(saw_pos);
    CHECK(saw_neg);
}

TEST_CASE("sn76489 attenuation halves toward silence") {
    sn76489 psg;
    psg.write(0x82U); // tone0 period 2
    psg.write(0x00U);
    psg.write(0x92U); // ch0 volume = 2 (two -2 dB steps)
    bool saw_level = false;
    for (int i = 0; i < 8; ++i) {
        const std::int16_t s = psg.step();
        if (s == 5170 || s == -5170) { // vol_table[2]
            saw_level = true;
        }
    }
    CHECK(saw_level);
}

TEST_CASE("sn76489 resets the LFSR on a noise write") {
    sn76489 psg;
    psg.write(0xE7U); // latch ch3 noise: white (bit2) + rate 3
    CHECK(psg.lfsr() == 0x8000U);
}

TEST_CASE("sn76489 tick steps through the internal divider") {
    sn76489 psg;
    psg.write(0x82U); // tone0 period 2, loud
    psg.write(0x00U);
    psg.write(0x90U);
    psg.set_clock_divider(16);
    psg.tick(16U * 8U); // 8 PSG steps
    // After several steps a loud square wave is at peak amplitude.
    CHECK((psg.last_sample() == 8191 || psg.last_sample() == -8191));
}

TEST_CASE("sn76489 round-trips its state") {
    sn76489 psg;
    psg.write(0x82U);
    psg.write(0x01U);        // tone0 = 0x12
    psg.write(0x90U);        // vol0 = 0
    psg.write(0xE5U);        // noise mode
    psg.write_stereo(0x3CU); // GG stereo register
    (void)psg.step();

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    psg.save_state(writer);

    sn76489 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.tone(0) == 0x012U);
    CHECK(restored.volume(0) == 0x00U);
    CHECK(restored.lfsr() == psg.lfsr());
    CHECK(restored.stereo_register() == 0x3CU); // appended field round-trips
}

TEST_CASE("sn76489 Game Gear stereo register routes channels to L/R") {
    sn76489 psg;
    // ch0 held high at peak (+8191): tone period 0 holds the output high.
    psg.write(0x80U); // latch ch0 tone, low nibble 0
    psg.write(0x00U); // tone0 high bits 0 -> period 0
    psg.write(0x90U); // ch0 volume 0 (loud)
    psg.write(0xBFU); // ch1 volume 0xF (mute)
    psg.write(0xDFU); // ch2 volume 0xF (mute)
    psg.write(0xFFU); // ch3 volume 0xF (mute)

    psg.set_stereo_capture(true);
    psg.enable_audio_capture(true);
    std::array<std::int16_t, 2> lr{};

    // Route ch0 to the LEFT channel only (bit 4).
    psg.write_stereo(0x10U);
    CHECK(psg.stereo_register() == 0x10U);
    psg.tick(static_cast<std::uint64_t>(sn76489::default_clock_divider)); // one step
    REQUIRE(psg.pending_samples() == 2U);
    psg.drain_samples(lr.data(), 2U);
    CHECK(lr[0] == 8191); // left = ch0
    CHECK(lr[1] == 0);    // right silent

    // Route ch0 to the RIGHT channel only (bit 0).
    psg.write_stereo(0x01U);
    psg.tick(static_cast<std::uint64_t>(sn76489::default_clock_divider));
    REQUIRE(psg.pending_samples() == 2U);
    psg.drain_samples(lr.data(), 2U);
    CHECK(lr[0] == 0);    // left silent
    CHECK(lr[1] == 8191); // right = ch0
}

TEST_CASE("sn76489 exposes its register file via introspection") {
    sn76489 psg;
    auto* rv = psg.introspection().registers();
    REQUIRE(rv != nullptr); // register_view backed by register_snapshot()
    CHECK_FALSE(rv->registers().empty());
}
