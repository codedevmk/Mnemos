// ricoh_2a03_apu port fidelity. The register-decode + frame-counter behaviour is
// asserted against the reference's defined semantics; the synthesis golden below
// is hand-derived from the chip's documented oscillator math (a single 50%-duty
// pulse at timer=16, full volume), so the first samples are bit-exact.

#include "ricoh_2a03_apu.hpp"

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
    using mnemos::chips::audio::ricoh_2a03_apu;

    // A single 50%-duty pulse on channel 1 at full volume, timer = 16. With the
    // duty pattern {0,1,1,1,1,0,0,0}, the sequencer advances to step 1 (high) on
    // the first native step and dwells there for `timer` steps, so the opening run
    // is a constant high level. Level = (k_channel_peak * 15) / 15 = 1500.
    void configure_pulse1(ricoh_2a03_apu& chip) {
        chip.write_reg(ricoh_2a03_apu::reg_pulse1_0, 0x9F); // duty=2, const vol, vol=15
        chip.write_reg(ricoh_2a03_apu::reg_pulse1_2, 0x10); // timer low = 16
        chip.write_reg(ricoh_2a03_apu::reg_status, 0x01);   // enable pulse 1
        chip.write_reg(ricoh_2a03_apu::reg_pulse1_3, 0x08); // timer high 0 + length idx 1
    }

} // namespace

TEST_CASE("ricoh_2a03_apu registers via the chip registry", "[ricoh_2a03_apu][audio]") {
    auto chip = create_chip("ricoh.2a03_apu");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
}

TEST_CASE("ricoh_2a03_apu reset clears to a defined baseline", "[ricoh_2a03_apu][audio]") {
    ricoh_2a03_apu chip;
    configure_pulse1(chip);
    // Drive some state, then force a fresh power-on reset.
    std::array<std::int16_t, 32> warm{};
    chip.generate(warm);
    chip.reset(reset_kind::power_on);

    // Status reads back zero (no channels enabled, no IRQs latched).
    REQUIRE(chip.read_reg(ricoh_2a03_apu::reg_status) == 0x00);
    REQUIRE_FALSE(chip.irq_asserted());
    // A reset chip is silent.
    chip.step();
    REQUIRE(chip.last_sample() == 0);
    REQUIRE(chip.last_left() == 0);
    REQUIRE(chip.last_right() == 0);
}

TEST_CASE("ricoh_2a03_apu status write enables channels and reads back",
          "[ricoh_2a03_apu][audio]") {
    ricoh_2a03_apu chip;
    // Enabling a channel and loading its length counter makes the status bit read 1.
    chip.write_reg(ricoh_2a03_apu::reg_status, 0x01);   // enable pulse 1
    chip.write_reg(ricoh_2a03_apu::reg_pulse1_3, 0x08); // length idx 1 -> counter = 254
    const std::uint8_t status = chip.read_reg(ricoh_2a03_apu::reg_status);
    REQUIRE((status & ricoh_2a03_apu::status_pulse1) != 0U);
    REQUIRE((status & ricoh_2a03_apu::status_pulse2) == 0U);

    // A non-status APU address reads back the open-bus latch (last byte written).
    chip.write_reg(ricoh_2a03_apu::reg_pulse2_0, 0x55);
    REQUIRE(chip.read_reg(ricoh_2a03_apu::reg_pulse2_0) == 0x55);
}

TEST_CASE("ricoh_2a03_apu frame IRQ fires in 4-step mode and clears on read",
          "[ricoh_2a03_apu][audio]") {
    ricoh_2a03_apu chip;
    chip.write_reg(ricoh_2a03_apu::reg_frame_counter, 0x00); // 4-step, IRQ enabled
    REQUIRE_FALSE(chip.irq_asserted());
    chip.tick(30000); // past the 4-step last-step cycle (29829)
    REQUIRE(chip.irq_asserted());
    const std::uint8_t status = chip.read_reg(ricoh_2a03_apu::reg_status);
    REQUIRE((status & ricoh_2a03_apu::status_frame_irq) != 0U);
    // Reading the status port clears the frame IRQ.
    REQUIRE_FALSE(chip.irq_asserted());

    // 5-step mode never raises the frame IRQ.
    ricoh_2a03_apu chip5;
    chip5.write_reg(ricoh_2a03_apu::reg_frame_counter, 0x80); // 5-step
    chip5.tick(40000);
    REQUIRE_FALSE(chip5.irq_asserted());
}

TEST_CASE("ricoh_2a03_apu synthesises a deterministic pulse waveform", "[ricoh_2a03_apu][audio]") {
    ricoh_2a03_apu chip;
    configure_pulse1(chip);

    std::array<std::int16_t, 64> buf{}; // 32 stereo pairs
    chip.generate(buf);

    // The mono mix is duplicated to both lanes.
    for (std::size_t i = 0; i < buf.size(); i += 2) {
        REQUIRE(buf[i] == buf[i + 1]);
    }
    // Opening run is the constant high level (1500), bit-exact per the math above.
    for (std::size_t pair = 0; pair < 8; ++pair) {
        INFO("pair " << pair);
        REQUIRE(buf[pair * 2] == 1500);
    }
    // The waveform is non-trivial: at least one sample is silent later (the duty
    // sequence eventually swings low), so it is a real square, not a DC level.
    std::array<std::int16_t, 1024> tail{};
    chip.generate(tail);
    bool saw_low = false;
    for (const auto s : tail) {
        if (s == 0) {
            saw_low = true;
            break;
        }
    }
    REQUIRE(saw_low);
}

TEST_CASE("ricoh_2a03_apu DMC streams delta-PCM samples from the bus reader",
          "[ricoh_2a03_apu][audio]") {
    ricoh_2a03_apu chip;
    // A bus reader that always yields $FF: every shifted bit is 1, so the delta
    // decoder ramps the 7-bit DAC upward toward its maximum.
    chip.set_dmc_reader([](std::uint16_t) -> std::uint8_t { return 0xFFU; });
    chip.write_reg(ricoh_2a03_apu::reg_dmc_0, 0x0FU);  // rate index 15 (fastest); no loop/IRQ
    chip.write_reg(ricoh_2a03_apu::reg_dmc_2, 0x00U);  // sample address $C000
    chip.write_reg(ricoh_2a03_apu::reg_dmc_3, 0xFFU);  // length 4081 bytes (won't drain here)
    chip.write_reg(ricoh_2a03_apu::reg_status, 0x10U); // enable DMC -> start the sample

    // With only the DMC enabled the mono mix tracks the DAC level (centred on 64),
    // so a rising DAC is directly observable in the output.
    chip.step();
    const std::int16_t before = chip.last_sample(); // DAC = 0 -> strongly negative
    chip.tick(8000);
    const std::int16_t after = chip.last_sample();
    REQUIRE(after > before);
    REQUIRE(after > 0); // an all-ones stream ramps the DAC above mid-scale
}

TEST_CASE("ricoh_2a03_apu DMC raises its IRQ at the end of a non-looping sample",
          "[ricoh_2a03_apu][audio]") {
    ricoh_2a03_apu chip;
    chip.set_dmc_reader([](std::uint16_t) -> std::uint8_t { return 0x00U; });
    chip.write_reg(ricoh_2a03_apu::reg_dmc_0, 0x8FU);  // IRQ enable, no loop, rate 15
    chip.write_reg(ricoh_2a03_apu::reg_dmc_2, 0x00U);  // address $C000
    chip.write_reg(ricoh_2a03_apu::reg_dmc_3, 0x00U);  // length = 1 byte
    chip.write_reg(ricoh_2a03_apu::reg_status, 0x10U); // start
    REQUIRE_FALSE(chip.irq_asserted());

    chip.tick(2000); // drains the 1-byte sample -> end-of-sample IRQ
    REQUIRE(chip.irq_asserted());
    const std::uint8_t status = chip.read_reg(ricoh_2a03_apu::reg_status);
    REQUIRE((status & ricoh_2a03_apu::status_dmc_irq) != 0U);

    // Writing $4015 clears the DMC IRQ.
    chip.write_reg(ricoh_2a03_apu::reg_status, 0x10U);
    REQUIRE_FALSE(chip.irq_asserted());
}

TEST_CASE("ricoh_2a03_apu delivers the /IRQ level through the callback",
          "[ricoh_2a03_apu][audio]") {
    ricoh_2a03_apu chip;
    bool irq = false;
    int edges = 0;
    chip.set_irq_callback([&](bool a) {
        irq = a;
        ++edges;
    });

    // Frame IRQ (4-step mode, not inhibited) -> callback asserts on the edge.
    chip.write_reg(ricoh_2a03_apu::reg_frame_counter, 0x00);
    REQUIRE_FALSE(irq);
    chip.tick(30000); // past the 4-step last-step cycle (29829)
    CHECK(irq);
    CHECK(edges == 1); // a single rising edge, not one per cycle

    // Reading the status port acknowledges it -> callback deasserts.
    (void)chip.read_reg(ricoh_2a03_apu::reg_status);
    CHECK_FALSE(irq);
    CHECK(edges == 2);
}

TEST_CASE("ricoh_2a03_apu save_state/load_state round-trips bit-identically",
          "[ricoh_2a03_apu][audio]") {
    ricoh_2a03_apu a;
    configure_pulse1(a);
    // Also exercise the other channels so every serialised field is non-trivial.
    a.write_reg(ricoh_2a03_apu::reg_tri_0, 0xFF);
    a.write_reg(ricoh_2a03_apu::reg_tri_3, 0x10);
    a.write_reg(ricoh_2a03_apu::reg_noise_0, 0x1F);
    a.write_reg(ricoh_2a03_apu::reg_noise_3, 0x10);
    a.write_reg(ricoh_2a03_apu::reg_dmc_1, 0x40);
    a.write_reg(ricoh_2a03_apu::reg_status, 0x1F); // enable all channels
    // Advance so runtime accumulator state is non-trivial.
    std::array<std::int16_t, 50> warm{};
    a.generate(warm);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    ricoh_2a03_apu b;
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    // From the restored state both chips must produce identical output.
    std::array<std::int16_t, 256> from_a{};
    std::array<std::int16_t, 256> from_b{};
    a.generate(from_a);
    b.generate(from_b);
    REQUIRE(from_a == from_b);
}

TEST_CASE("ricoh_2a03_apu audio capture counts and drains in stereo frames",
          "[ricoh_2a03_apu][audio]") {
    ricoh_2a03_apu chip;
    configure_pulse1(chip); // an enabled, audible voice
    chip.enable_audio_capture(true);
    constexpr std::size_t frames = 10U;
    chip.tick(frames); // clock_divider defaults to 1 -> one (L,R) frame per cycle

    // pending_samples() is in stereo frames (pairs), NOT raw int16.
    REQUIRE(chip.pending_samples() == frames);

    std::array<std::int16_t, 16> buf{};
    const std::size_t got = chip.drain_samples(buf.data(), 3U);
    REQUIRE(got == 3U);
    REQUIRE(chip.pending_samples() == frames - 3U);
    bool any_nonzero = false; // an active voice produces non-silent capture
    for (std::size_t i = 0; i < got * 2U; ++i) {
        any_nonzero = any_nonzero || (buf[i] != 0);
    }
    REQUIRE(any_nonzero);

    std::array<std::int16_t, 64> rest{};
    const std::size_t left = chip.drain_samples(rest.data(), 100U);
    REQUIRE(left == frames - 3U);
    REQUIRE(chip.pending_samples() == 0U);
}
