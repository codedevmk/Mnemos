#include "sn76489.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

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
    psg.write(0x01U); // tone0 = 0x12
    psg.write(0x90U); // vol0 = 0
    psg.write(0xE5U); // noise mode
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
}
