// ym2610 (OPNB) top-level wrapper: registry round-trip, reset baseline, a
// register write driving the synthesis path into audible output, and a bit-exact
// save/load round-trip (which must restore the FM core plus every sub-chip).

#include "ym2610.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {

    using mnemos::chips::chip_class;
    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::adpcm_a;
    using mnemos::chips::audio::adpcm_b;
    using mnemos::chips::audio::ym2610;

    // Drive the SSG block through the YM2610 bank-A address/data ports: a tone on
    // channel A at full volume. The SSG mix is the simplest path to a guaranteed
    // non-silent output from a register write.
    void configure_ssg_tone(ym2610& chip) {
        // Bank A $00..$0F is the SSG. Address then data.
        const auto write_a = [&chip](std::uint8_t reg, std::uint8_t value) {
            chip.write_address_a(reg);
            chip.write_data_a(value);
        };
        write_a(0x00, 0x40); // channel A tone period low
        write_a(0x01, 0x00); // channel A tone period high
        write_a(0x07, 0x3E); // mixer: enable channel-A tone (active-low bit0 = 0)
        write_a(0x08, 0x0F); // channel A amplitude = full fixed volume
    }

    // Drive an FM channel (bank A, channel 1) into a sounding state: algorithm 7
    // (four parallel carriers), all four operators loud with a fast attack, a
    // mid-range pitch, then key all four slots on.
    void configure_fm_channel(ym2610& chip) {
        const auto write_a = [&chip](std::uint8_t reg, std::uint8_t value) {
            chip.write_address_a(reg);
            chip.write_data_a(value);
        };
        // Per-operator blocks for channel 1 (reg low nibble 0 selects ch within
        // the pair; slot is bits 3:2). Set DT/MUL, TL=0, fast AR, and a release.
        for (std::uint8_t slot = 0; slot < 4; ++slot) {
            const std::uint8_t base = static_cast<std::uint8_t>(slot << 2U);
            write_a(static_cast<std::uint8_t>(0x30 + base), 0x01); // DT=0, MUL=1
            write_a(static_cast<std::uint8_t>(0x40 + base), 0x00); // TL=0 (loud)
            write_a(static_cast<std::uint8_t>(0x50 + base), 0x1F); // KS=0, AR=31 (instant)
            write_a(static_cast<std::uint8_t>(0x60 + base), 0x00); // D1R=0
            write_a(static_cast<std::uint8_t>(0x70 + base), 0x00); // D2R=0
            write_a(static_cast<std::uint8_t>(0x80 + base), 0x0F); // D1L=0, RR=15
        }
        write_a(0xB0, 0x07); // feedback 0, algorithm 7 (four parallel carriers)
        write_a(0xB4, 0xC0); // L+R enable
        write_a(0xA4, 0x22); // block 4, F-number high bits
        write_a(0xA0, 0x80); // F-number low byte
        write_a(0x28, 0xF0); // key on all four slots of channel 1 (sel=0)
    }

    std::vector<std::uint8_t> make_adpcm_a_rom() {
        std::vector<std::uint8_t> rom(0x200, 0U);
        rom[0x100] = 0x12U;
        rom[0x101] = 0x34U;
        return rom;
    }

    void configure_adpcm_a_channel0(ym2610& chip) {
        const auto write_b = [&chip](std::uint8_t reg, std::uint8_t value) {
            chip.write_address_b(reg);
            chip.write_data_b(value);
        };
        write_b(adpcm_a::reg_tl, 0x3FU);
        write_b(adpcm_a::reg_ch_pan_level, 0xDFU);
        write_b(adpcm_a::reg_ch_start_lo, 0x01U);
        write_b(adpcm_a::reg_ch_start_hi, 0x00U);
        write_b(adpcm_a::reg_ch_end_lo, 0x01U);
        write_b(adpcm_a::reg_ch_end_hi, 0x00U);
        write_b(adpcm_a::reg_key, 0x01U);
    }

    void configure_adpcm_a_channel5(ym2610& chip) {
        const auto write_b = [&chip](std::uint8_t reg, std::uint8_t value) {
            chip.write_address_b(reg);
            chip.write_data_b(value);
        };
        write_b(adpcm_a::reg_tl, 0x3FU);
        write_b(adpcm_a::reg_ch_pan_level + 5, 0xDFU);
        write_b(adpcm_a::reg_ch_start_lo + 5, 0x01U);
        write_b(adpcm_a::reg_ch_start_hi + 5, 0x00U);
        write_b(adpcm_a::reg_ch_end_lo + 5, 0x01U);
        write_b(adpcm_a::reg_ch_end_hi + 5, 0x00U);
        write_b(adpcm_a::reg_key, 0x20U);
    }

    std::uint64_t register_value(std::span<const mnemos::chips::register_descriptor> regs,
                                 std::string_view name) {
        for (const auto& reg : regs) {
            if (reg.name == name) {
                return reg.value;
            }
        }
        FAIL("missing register " << name);
        return 0U;
    }

} // namespace

TEST_CASE("ym2610 registers in the chip factory as an audio synth", "[ym2610][audio]") {
    const auto chip = mnemos::chips::create_chip("yamaha.ym2610");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
    REQUIRE(chip->metadata().part_number == "ym2610");
}

TEST_CASE("ym2610 reset clears state to a silent baseline", "[ym2610][audio]") {
    ym2610 chip;
    configure_fm_channel(chip);
    configure_ssg_tone(chip);
    std::array<std::int16_t, 64> warm{};
    chip.generate(warm);

    chip.reset(reset_kind::power_on);

    // After reset nothing is keyed/enabled, so the mix is silent.
    chip.step();
    REQUIRE(chip.last_left() == 0);
    REQUIRE(chip.last_right() == 0);
}

TEST_CASE("ym2610 an SSG register write produces output", "[ym2610][audio]") {
    ym2610 chip;
    // A silent chip with nothing programmed produces no output.
    chip.step();
    REQUIRE(chip.last_left() == 0);
    REQUIRE(chip.last_right() == 0);

    configure_ssg_tone(chip);
    bool any_nonzero = false;
    for (int i = 0; i < 256; ++i) {
        chip.step();
        any_nonzero = any_nonzero || (chip.last_left() != 0) || (chip.last_right() != 0);
    }
    REQUIRE(any_nonzero);
}

TEST_CASE("ym2610 an FM register write produces output", "[ym2610][audio]") {
    ym2610 chip;
    configure_fm_channel(chip);
    bool any_nonzero = false;
    for (int i = 0; i < 512; ++i) {
        chip.step();
        any_nonzero = any_nonzero || (chip.last_left() != 0) || (chip.last_right() != 0);
    }
    REQUIRE(any_nonzero);
}

TEST_CASE("ym2610 ignores invalid four-channel FM selectors", "[ym2610][audio]") {
    ym2610 chip;

    // Low selector 2 is a valid six-channel OPN selector, but the YM2610 model
    // represented here has four FM channels. Taito sound programs can touch
    // these holes while probing/resetting the OPNB register file; that must not
    // decode into channel index 4.
    chip.write_address_b(0xA2);
    chip.write_data_b(0x7F);
    chip.write_address_b(0xB2);
    chip.write_data_b(0x07);
    chip.write_address_a(0x28);
    chip.write_data_a(0xF2);

    std::array<std::int16_t, 16> out{};
    chip.generate(out);

    REQUIRE(chip.fm_register(0x1A2) == 0x7F);
    REQUIRE(chip.fm_register(0x1B2) == 0x07);
}

TEST_CASE("ym2610 routes the full bank-B ADPCM-A register window", "[ym2610][audio]") {
    ym2610 chip;

    chip.write_address_b(adpcm_a::reg_ch_end_lo);
    chip.write_data_b(0x34U);
    chip.write_address_b(adpcm_a::reg_ch_end_hi);
    chip.write_data_b(0x12U);

    REQUIRE(chip.adpcm_a_block().read_reg(adpcm_a::reg_ch_end_lo) == 0x34U);
    REQUIRE(chip.adpcm_a_block().read_reg(adpcm_a::reg_ch_end_hi) == 0x12U);
}

TEST_CASE("ym2610 routes bank-B zero to the ADPCM-A key register", "[ym2610][audio]") {
    ym2610 chip;
    const std::vector<std::uint8_t> rom = make_adpcm_a_rom();
    chip.adpcm_a_block().set_sample_rom(rom);

    configure_adpcm_a_channel0(chip);

    bool any_nonzero = false;
    for (int i = 0; i < 32; ++i) {
        chip.step();
        any_nonzero = any_nonzero || chip.last_left() != 0 || chip.last_right() != 0;
    }

    REQUIRE(chip.adpcm_a_block().read_reg(adpcm_a::reg_key) == 0x01U);
    REQUIRE(any_nonzero);
}

TEST_CASE("ym2610 clocks ADPCM-A at one third of the YM output rate", "[ym2610][audio]") {
    ym2610 chip;
    const std::vector<std::uint8_t> rom = make_adpcm_a_rom();
    chip.adpcm_a_block().set_sample_rom(rom);
    configure_adpcm_a_channel0(chip);

    std::vector<std::int16_t> left;
    for (int i = 0; i < 10; ++i) {
        chip.step();
        left.push_back(chip.last_left());
    }

    REQUIRE(left == std::vector<std::int16_t>{96, 96, 96, 256, 256, 256, 480, 480, 480, 768});
}

TEST_CASE("ym2610 exposes CPU-facing register writes to introspection",
          "[ym2610][audio][introspection]") {
    ym2610 chip;
    std::vector<std::uint16_t> ports;
    std::vector<std::uint8_t> values;

    auto* trace = chip.introspection().reg_writes();
    REQUIRE(trace != nullptr);
    trace->install([&](const mnemos::instrumentation::reg_write_event& event) {
        ports.push_back(event.port);
        values.push_back(event.value);
    });

    chip.write_address_a(0x08U);
    chip.write_data_a(0x0FU);
    chip.write_address_b(0x10U);
    chip.write_data_b(0x80U);
    trace->install({});

    REQUIRE(ports == std::vector<std::uint16_t>{0x0008U, 0x0110U});
    REQUIRE(values == std::vector<std::uint8_t>{0x0FU, 0x80U});
}

TEST_CASE("ym2610 exposes ADPCM-A per-channel diagnostics", "[ym2610][audio]") {
    ym2610 chip;
    const std::vector<std::uint8_t> rom = make_adpcm_a_rom();
    chip.adpcm_a_block().set_sample_rom(rom);
    configure_adpcm_a_channel5(chip);

    chip.step();
    const auto regs = chip.register_snapshot();

    REQUIRE(register_value(regs, "ADPCMA_KEY") == 0x20U);
    REQUIRE(register_value(regs, "ADPCMA_ACTIVE") == 0x20U);
    REQUIRE(register_value(regs, "ADPCMA_CH5_START") == 0x000100U);
    REQUIRE(register_value(regs, "ADPCMA_CH5_END") == 0x0001FFU);
    REQUIRE(register_value(regs, "ADPCMA_CH5_ACTIVE") == 1U);
    REQUIRE(register_value(regs, "ADPCMA_CH5_ACC") != 0U);
    REQUIRE(register_value(regs, "ADPCMA_CH5_END_EVENTS") == 0U);
    REQUIRE(register_value(regs, "ADPCMA_CH5_ROM_UNDERRUNS") == 0U);
    REQUIRE(register_value(regs, "ADPCMA_CH0_ACTIVE") == 0U);
}

TEST_CASE("ym2610 exposes ADPCM-B stream diagnostics", "[ym2610][audio]") {
    ym2610 chip;
    const std::vector<std::uint8_t> rom(0x100, 0x11U);
    chip.adpcm_b_block().set_sample_rom(rom);

    const auto write_a = [&chip](std::uint8_t reg, std::uint8_t value) {
        chip.write_address_a(static_cast<std::uint8_t>(0x10U + reg));
        chip.write_data_a(value);
    };
    write_a(adpcm_b::reg_start_lo, 0x00U);
    write_a(adpcm_b::reg_start_hi, 0x00U);
    write_a(adpcm_b::reg_end_lo, 0x00U);
    write_a(adpcm_b::reg_end_hi, 0x00U);
    write_a(adpcm_b::reg_delta_lo, 0x00U);
    write_a(adpcm_b::reg_delta_hi, 0x01U);
    write_a(adpcm_b::reg_pan_tl, adpcm_b::pan_left | adpcm_b::pan_right | 0x05U);
    write_a(adpcm_b::reg_ctrl, adpcm_b::ctrl_start | adpcm_b::ctrl_repeat);

    chip.step();
    const auto regs = chip.register_snapshot();

    REQUIRE(register_value(regs, "ADPCMB_CTRL") ==
            (adpcm_b::ctrl_start | adpcm_b::ctrl_repeat));
    REQUIRE((register_value(regs, "ADPCMB_STATUS") & adpcm_b::status_busy) != 0U);
    REQUIRE(register_value(regs, "ADPCMB_TL") == 0x05U);
    REQUIRE(register_value(regs, "ADPCMB_START") == 0x0000U);
    REQUIRE(register_value(regs, "ADPCMB_END") == 0x0000U);
    REQUIRE(register_value(regs, "ADPCMB_DELTA_N") == 0x0100U);
    REQUIRE(register_value(regs, "ADPCMB_CURSOR") > 0U);
    REQUIRE(register_value(regs, "ADPCMB_ACTIVE") == 1U);
    REQUIRE(register_value(regs, "ADPCMB_REPEAT") == 1U);
    REQUIRE(register_value(regs, "ADPCMB_END_EVENTS") == 0U);
    REQUIRE(register_value(regs, "ADPCMB_LOOP_EVENTS") == 0U);
    REQUIRE(register_value(regs, "ADPCMB_ROM_UNDERRUNS") == 0U);

    std::array<std::int16_t, 1100> loop_buf{};
    chip.generate(loop_buf);
    const auto loop_regs = chip.register_snapshot();

    REQUIRE(register_value(loop_regs, "ADPCMB_ACTIVE") == 1U);
    REQUIRE(register_value(loop_regs, "ADPCMB_END_EVENTS") >= 1U);
    REQUIRE(register_value(loop_regs, "ADPCMB_LOOP_EVENTS") >= 1U);
    REQUIRE(register_value(loop_regs, "ADPCMB_ROM_UNDERRUNS") == 0U);
}

TEST_CASE("ym2610 timer A overflow raises and clears the IRQ line", "[ym2610][audio]") {
    ym2610 chip;
    std::vector<bool> irq_edges;
    chip.set_irq([&](bool asserted) { irq_edges.push_back(asserted); });

    chip.write_address_a(0x24U);
    chip.write_data_a(0xFFU);
    chip.write_address_a(0x25U);
    chip.write_data_a(0x02U);
    chip.write_address_a(0x27U);
    chip.write_data_a(0x05U); // run timer A + enable timer A IRQ

    chip.tick((2U * ym2610::default_clock_divider) - 1U);
    CHECK_FALSE(chip.irq_asserted());
    CHECK((chip.read_status() & ym2610::status_timer_a) == 0U);

    chip.tick(1U);
    REQUIRE(chip.irq_asserted());
    CHECK((chip.read_status() & ym2610::status_timer_a) != 0U);
    REQUIRE(irq_edges == std::vector<bool>{true});

    chip.write_address_a(0x27U);
    chip.write_data_a(0x15U); // keep timer A running/enabled, clear flag A

    CHECK_FALSE(chip.irq_asserted());
    CHECK((chip.read_status() & ym2610::status_timer_a) == 0U);
    CHECK(irq_edges == std::vector<bool>{true, false});
}

TEST_CASE("ym2610 reset drops a pending timer IRQ", "[ym2610][audio]") {
    ym2610 chip;
    std::vector<bool> irq_edges;
    chip.set_irq([&](bool asserted) { irq_edges.push_back(asserted); });

    chip.write_address_a(0x24U);
    chip.write_data_a(0xFFU);
    chip.write_address_a(0x25U);
    chip.write_data_a(0x02U);
    chip.write_address_a(0x27U);
    chip.write_data_a(0x05U);
    chip.tick(2U * ym2610::default_clock_divider);
    REQUIRE(chip.irq_asserted());

    chip.reset(reset_kind::power_on);

    CHECK_FALSE(chip.irq_asserted());
    CHECK(irq_edges == std::vector<bool>{true, false});
}

TEST_CASE("ym2610 save_state/load_state round-trips bit-identically", "[ym2610][audio]") {
    ym2610 a;
    configure_fm_channel(a);
    configure_ssg_tone(a);
    // Advance a few samples so the FM phase/envelope + SSG accumulators are
    // non-trivial.
    std::array<std::int16_t, 80> warm{};
    a.generate(warm);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    ym2610 b;
    state_reader reader(blob);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);

    // From the restored state both chips (FM core + every sub-chip) must produce
    // identical output.
    std::array<std::int16_t, 256> from_a{};
    std::array<std::int16_t, 256> from_b{};
    a.generate(from_a);
    b.generate(from_b);
    REQUIRE(from_a == from_b);
}

TEST_CASE("ym2610 audio capture counts and drains in stereo frames", "[ym2610][audio]") {
    ym2610 chip;
    configure_ssg_tone(chip);  // an audible source
    chip.set_clock_divider(1); // one (L,R) frame per ticked cycle
    chip.enable_audio_capture(true);
    constexpr std::size_t frames = 10U;
    chip.tick(frames);

    // pending_samples() is in stereo frames (pairs), matching the player's
    // add_source() contract -- NOT raw int16.
    REQUIRE(chip.pending_samples() == frames);

    std::array<std::int16_t, 16> buf{};
    const std::size_t got = chip.drain_samples(buf.data(), 3U);
    REQUIRE(got == 3U);
    REQUIRE(chip.pending_samples() == frames - 3U);

    std::array<std::int16_t, 64> rest{};
    const std::size_t left = chip.drain_samples(rest.data(), 100U);
    REQUIRE(left == frames - 3U);
    REQUIRE(chip.pending_samples() == 0U);
}
