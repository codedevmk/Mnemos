// paula port fidelity vs the Emu reference. The golden PCM block was hand-traced
// from the reference DMA state machine through the identical scenario built in
// configure_channel0() below (one channel, unity volume, period 1).

#include "chip_registry.hpp"
#include "paula.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::create_chip;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::paula;

    // Channel 0 fetching a 2-word (4-sample) buffer at chip-RAM 0x0100. Bytes are
    // signed 8-bit: +16, -16, +32, -32. Volume 64 (unity) -> output x64. Period 1
    // advances one sample per step. DMA on for channel 0 keys the state machine.
    void configure_channel0(paula& chip) {
        const auto ram = chip.chipram();
        ram[0x0100] = 0x10; // +16
        ram[0x0101] = 0xF0; // -16
        ram[0x0102] = 0x20; // +32
        ram[0x0103] = 0xE0; // -32

        // AUDxLC = 0x0100 (high word 0, low word 0x0100).
        chip.write_reg(0, paula::reg_lch, 0x0000);
        chip.write_reg(0, paula::reg_lcl, 0x0100);
        chip.write_reg(0, paula::reg_len, 0x0002); // 2 words
        chip.write_reg(0, paula::reg_per, 0x0001); // one color clock per sample
        chip.write_reg(0, paula::reg_vol, 0x0040); // 64 -> unity
        chip.set_dma(true, 0x01);                  // master + channel 0 enable
    }

    // First five stereo pairs (10 int16). Channel 0 pans to LEFT; right stays 0.
    constexpr std::array<std::int16_t, 10> kPcmGolden = {1024, 0,     -1024, 0,    2048,
                                                         0,    -2048, 0,     1024, 0};

} // namespace

TEST_CASE("paula registers in the chip registry as an audio synth", "[paula][audio]") {
    auto chip = create_chip("commodore.paula");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
}

TEST_CASE("paula reset clears state to a defined baseline", "[paula][audio]") {
    paula chip;
    configure_channel0(chip);
    chip.step(); // produce some live state + a non-zero output
    REQUIRE(chip.channel_active(0));

    chip.reset(reset_kind::power_on);
    for (int ci = 0; ci < paula::channel_count; ++ci) {
        REQUIRE_FALSE(chip.channel_active(ci));
        REQUIRE(chip.channel_output(ci) == 0);
        REQUIRE(chip.read_reg(ci, paula::reg_len) == 0);
        REQUIRE(chip.read_reg(ci, paula::reg_vol) == 0);
    }
    REQUIRE(chip.interrupts() == 0);
    chip.step();
    REQUIRE(chip.last_left() == 0);
    REQUIRE(chip.last_right() == 0);
}

TEST_CASE("paula DMA-armed channel produces the reference PCM stream", "[paula][audio]") {
    paula chip;
    configure_channel0(chip);
    std::array<std::int16_t, 10> buf{};
    chip.generate(buf);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        INFO("sample " << i << (i % 2 == 0 ? " (L)" : " (R)"));
        REQUIRE(buf[i] == kPcmGolden[i]);
    }
}

TEST_CASE("paula register writes read back and volume clamps to 64", "[paula][audio]") {
    paula chip;
    chip.write_reg(0, paula::reg_lcl, 0x1234); // LSB forced off -> 0x1234
    REQUIRE(chip.read_reg(0, paula::reg_lcl) == 0x1234);
    chip.write_reg(0, paula::reg_lcl, 0x1235); // odd address rounds down
    REQUIRE(chip.read_reg(0, paula::reg_lcl) == 0x1234);
    chip.write_reg(0, paula::reg_per, 0x00AB);
    REQUIRE(chip.read_reg(0, paula::reg_per) == 0x00AB);
    // Volume clamps to the 0..64 DAC range.
    chip.write_reg(0, paula::reg_vol, 0x00FF);
    REQUIRE(chip.read_reg(0, paula::reg_vol) == paula::volume_max);
    chip.write_reg(0, paula::reg_vol, 0x0020);
    REQUIRE(chip.read_reg(0, paula::reg_vol) == 0x0020);
    // Out-of-range channel writes are dropped.
    chip.write_reg(9, paula::reg_len, 0x0001);
    REQUIRE(chip.read_reg(9, paula::reg_len) == 0);
}

TEST_CASE("paula AUDxDAT manual write produces one word without DMA", "[paula][audio]") {
    paula chip;
    std::uint8_t callback_sources = 0U;
    chip.set_interrupt_callback([&](std::uint8_t sources) {
        callback_sources = static_cast<std::uint8_t>(callback_sources | sources);
    });
    chip.write_reg(0, paula::reg_per, 0x0001);
    chip.write_reg(0, paula::reg_vol, 0x0040);
    chip.write_reg(0, paula::reg_dat, 0x10F0);

    std::array<std::int16_t, 8> buf{};
    chip.generate(buf);

    CHECK(buf[0] == 1024);
    CHECK(buf[1] == 0);
    CHECK(buf[2] == -1024);
    CHECK(buf[3] == 0);
    CHECK(buf[4] == 0);
    CHECK(buf[5] == 0);
    CHECK((callback_sources & 0x01U) != 0U);
    CHECK((chip.interrupts() & 0x01U) != 0U);
    CHECK_FALSE(chip.channel_active(0));
}

TEST_CASE("paula DMA arm/disarm gates the state machine", "[paula][audio]") {
    paula chip;
    configure_channel0(chip);
    REQUIRE(chip.channel_active(0));

    // Buffer wrap raises the matching AUDxINT and reloads from LC/LEN.
    std::array<std::int16_t, 12> buf{}; // > one full 4-sample buffer
    chip.generate(buf);
    REQUIRE((chip.interrupts() & 0x01U) != 0U);
    chip.clear_interrupts(0x01);
    REQUIRE(chip.interrupts() == 0);

    // Disarming the channel parks it to idle and silences it.
    chip.set_dma(true, 0x00);
    REQUIRE_FALSE(chip.channel_active(0));
    chip.step();
    REQUIRE(chip.last_left() == 0);
    REQUIRE(chip.last_right() == 0);
}

TEST_CASE("paula buffer wrap emits an audio interrupt callback", "[paula][audio]") {
    paula chip;
    std::uint8_t callback_sources = 0U;
    chip.set_interrupt_callback([&](std::uint8_t sources) {
        callback_sources = static_cast<std::uint8_t>(callback_sources | sources);
    });
    configure_channel0(chip);

    std::array<std::int16_t, 12> buf{};
    chip.generate(buf);

    CHECK((callback_sources & 0x01U) != 0U);
    CHECK((chip.interrupts() & 0x01U) != 0U);
}

TEST_CASE("paula save_state/load_state round-trips bit-identically", "[paula][audio]") {
    paula a;
    configure_channel0(a);
    // Advance a few samples so runtime accumulator state is non-trivial.
    std::array<std::int16_t, 14> warm{};
    a.generate(warm);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    paula b;
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    // From the restored state both chips must produce identical output.
    std::array<std::int16_t, 64> from_a{};
    std::array<std::int16_t, 64> from_b{};
    a.generate(from_a);
    b.generate(from_b);
    REQUIRE(from_a == from_b);
}

TEST_CASE("paula audio capture counts and drains in stereo frames", "[paula][audio]") {
    paula chip;
    configure_channel0(chip); // an armed, audible channel
    chip.enable_audio_capture(true);
    constexpr std::size_t frames = 10U;
    chip.tick(frames); // clock_divider defaults to 1 -> one (L,R) frame per cycle

    // pending_samples() is in stereo frames (pairs), matching the player's
    // add_source() contract -- NOT raw int16.
    REQUIRE(chip.pending_samples() == frames);

    // Drain fewer pairs than queued: returns pairs, fills 2*pairs int16.
    std::array<std::int16_t, 16> buf{};
    const std::size_t got = chip.drain_samples(buf.data(), 3U);
    REQUIRE(got == 3U);
    REQUIRE(chip.pending_samples() == frames - 3U);
    bool any_nonzero = false; // an active channel must produce non-silent capture
    for (std::size_t i = 0; i < got * 2U; ++i) {
        any_nonzero = any_nonzero || (buf[i] != 0);
    }
    REQUIRE(any_nonzero);

    // Draining past the end returns only what remains.
    std::array<std::int16_t, 64> rest{};
    const std::size_t left = chip.drain_samples(rest.data(), 100U);
    REQUIRE(left == frames - 3U);
    REQUIRE(chip.pending_samples() == 0U);
}
