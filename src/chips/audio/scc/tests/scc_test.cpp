#include "scc.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::chip_class;
    using mnemos::chips::create_chip;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::scc;

    void configure_channel_a(scc& chip) {
        chip.write(0x9800U, 0x40U);
        chip.write(0x9801U, 0xC0U);
        chip.write(0x9880U, 0x00U);
        chip.write(0x9881U, 0x00U);
        chip.write(0x988AU, 0x0FU);
        chip.write(0x988FU, 0x01U);
    }
} // namespace

TEST_CASE("scc registers in the chip registry as an audio synth", "[scc][audio]") {
    auto chip = create_chip("konami.scc");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
}

TEST_CASE("scc wave RAM and mirrored register window decode", "[scc][audio]") {
    scc chip;
    chip.write(0x9800U, 0x12U);
    chip.write(0x9820U, 0x34U);
    chip.write(0x9840U, 0x56U);
    chip.write(0x9860U, 0x78U);

    CHECK(chip.read(0x9800U) == 0x12U);
    CHECK(chip.read(0x9820U) == 0x34U);
    CHECK(chip.read(0x9840U) == 0x56U);
    CHECK(chip.read(0x9860U) == 0x78U);

    chip.write(0x9980U, 0xABU); // mirrors $9880-$988F through $9FFF
    chip.write(0x9981U, 0x0CU);
    CHECK(chip.period(0) == 0x0CABU);
}

TEST_CASE("scc channel registers control frequency volume and enable", "[scc][audio]") {
    scc chip;
    chip.write(0x9880U, 0xCDU);
    chip.write(0x9881U, 0xABU);
    chip.write(0x988AU, 0x3FU);
    chip.write(0x988FU, 0x21U);

    CHECK(chip.period(0) == 0x0BCDU);
    CHECK(chip.volume(0) == 0x0FU);
    CHECK(chip.enable_mask() == 0x01U);
}

TEST_CASE("scc generates signed wavetable output and captures stereo frames", "[scc][audio]") {
    scc chip;
    configure_channel_a(chip);
    chip.set_clock_divider(1);
    chip.enable_audio_capture(true);
    chip.tick(4);

    CHECK(chip.pending_samples() == 4U);
    CHECK(chip.last_left() == chip.last_right());

    std::array<std::int16_t, 8> frames{};
    const std::size_t got = chip.drain_samples(frames.data(), 4U);
    CHECK(got == 4U);
    CHECK(chip.pending_samples() == 0U);
    bool any_nonzero = false;
    for (const std::int16_t sample : frames) {
        any_nonzero = any_nonzero || sample != 0;
    }
    CHECK(any_nonzero);
}

TEST_CASE("scc channels 4 and 5 share waveform RAM in compatibility mode", "[scc][audio]") {
    scc chip;
    chip.write(0x9860U, 0x7FU);
    chip.write(0x9886U, 0x00U);
    chip.write(0x9888U, 0x00U);
    chip.write(0x988DU, 0x0FU);
    chip.write(0x988EU, 0x0FU);
    chip.write(0x988FU, 0x18U);
    chip.set_clock_divider(1);
    chip.tick(1);

    // Both voices read waveform bank 3; equal phase and volume sum to 2x one voice.
    CHECK(chip.last_left() == static_cast<std::int16_t>(0x7F * 15 * 2 * 2));
}

TEST_CASE("scc save_state/load_state round-trips generated output", "[scc][audio]") {
    scc a;
    configure_channel_a(a);
    a.set_clock_divider(1);
    a.tick(8);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    scc b;
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    a.tick(32);
    b.tick(32);
    CHECK(a.last_left() == b.last_left());
    CHECK(a.last_right() == b.last_right());
}

TEST_CASE("scc reset clears registers and output", "[scc][audio]") {
    scc chip;
    configure_channel_a(chip);
    chip.tick(8);
    chip.reset(reset_kind::hard);

    CHECK(chip.period(0) == 0U);
    CHECK(chip.volume(0) == 0U);
    CHECK(chip.enable_mask() == 0U);
    CHECK(chip.deformation() == 0U);
    CHECK(chip.last_left() == 0);
}
