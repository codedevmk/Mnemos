#include "ym2612.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::audio::ym2612;
    using reset_kind = mnemos::chips::reset_kind;

    // Program a register through the two-step address/data port protocol.
    void poke(ym2612& ym, int port, std::uint8_t reg, std::uint8_t value) {
        ym.write(port, false, reg);  // latch address
        ym.write(port, true, value); // write data
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::iaudio_synth, ym2612>);
static_assert(ym2612::static_class == mnemos::chips::chip_class::audio_synth);

TEST_CASE("ym2612 reports identity and registers under yamaha.ym2612") {
    const ym2612 fm;
    const auto md = fm.metadata();
    CHECK(md.manufacturer == "Yamaha");
    CHECK(md.part_number == "YM2612");
    CHECK(md.family == "OPN2");
    CHECK(md.klass == mnemos::chips::chip_class::audio_synth);

    auto chip = mnemos::chips::create_chip("yamaha.ym2612");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == "YM2612");
}

TEST_CASE("ym2612 powers on silent") {
    ym2612 fm;
    const auto s = fm.step();
    CHECK(s.left == 0);
    CHECK(s.right == 0);
    CHECK(fm.read_status() == 0U);
    CHECK_FALSE(fm.dac_enabled());
}

TEST_CASE("ym2612 latches channel frequency atomically on the A0 write") {
    ym2612 fm;
    // A4 latches fnum-high (5) and block (3); A0 commits with the low byte.
    poke(fm, 0, 0xA4U, 0x1DU);       // fnum_hi=5, block=3
    CHECK(fm.channel_fnum(0) == 0U); // A4 alone does not commit
    poke(fm, 0, 0xA0U, 0x55U);
    CHECK(fm.channel_fnum(0) == 0x555U);
    CHECK(fm.channel_block(0) == 3U);
}

TEST_CASE("ym2612 decodes feedback and algorithm") {
    ym2612 fm;
    poke(fm, 0, 0xB0U, 0x3AU); // algorithm = 2, feedback = 7
    CHECK(fm.channel_algorithm(0) == 2U);
    poke(fm, 1, 0xB2U, 0x05U); // port 1 -> channel 6 (index 5), algorithm 5
    CHECK(fm.channel_algorithm(5) == 5U);
}

TEST_CASE("ym2612 remaps per-operator registers through the slot table") {
    ym2612 fm;
    // $44 is operator slot 1, which the chip wires to internal operator 2.
    poke(fm, 0, 0x44U, 0x40U); // TL = 0x40
    CHECK(fm.operator_total_level(0, 2) == 0x40U);
    CHECK(fm.operator_total_level(0, 1) == 0U); // untouched
}

TEST_CASE("ym2612 keys operators on and off with the slot remap") {
    ym2612 fm;
    poke(fm, 0, 0x28U, 0xF0U); // key on all 4 operators of channel 1 (index 0)
    for (int op = 0; op < ym2612::operator_count; ++op) {
        CHECK(fm.operator_key_on(0, op));
    }

    poke(fm, 0, 0x28U, 0x20U); // key on only slot 1 -> internal operator 2
    CHECK(fm.operator_key_on(0, 2));
    CHECK_FALSE(fm.operator_key_on(0, 0));
    CHECK_FALSE(fm.operator_key_on(0, 1));
    CHECK_FALSE(fm.operator_key_on(0, 3));
}

TEST_CASE("ym2612 Timer A overflows and raises its IRQ") {
    ym2612 fm;
    poke(fm, 0, 0x24U, 0xFFU); // Timer A load MSB
    poke(fm, 0, 0x25U, 0x03U); // Timer A load LSB -> load = 1023 (period 1)
    CHECK(fm.timer_a_load() == 1023U);
    poke(fm, 0, 0x27U, 0x05U); // run + IRQ enable for Timer A

    const bool irq = fm.tick_timers_master(ym2612::timer_a_master_period);
    CHECK(irq);
    CHECK((fm.read_status() & 0x01U) != 0U);

    poke(fm, 0, 0x27U, 0x10U); // reset Timer A overflow flag
    CHECK((fm.read_status() & 0x01U) == 0U);
}

TEST_CASE("ym2612 Timer B overflows on its slower cadence") {
    ym2612 fm;
    poke(fm, 0, 0x26U, 0xFFU); // Timer B load -> period 1
    poke(fm, 0, 0x27U, 0x0AU); // run + IRQ enable for Timer B
    CHECK(fm.timer_b_load() == 0xFFU);

    // One Timer B tick takes 16128 master clocks; Timer A is off so it stays idle.
    const bool irq = fm.tick_timers_master(ym2612::timer_b_master_period);
    CHECK(irq);
    CHECK((fm.read_status() & 0x02U) != 0U);
}

TEST_CASE("ym2612 decodes the channel-6 DAC") {
    ym2612 fm;
    poke(fm, 0, 0x2BU, 0x80U); // DAC enable
    poke(fm, 0, 0x2AU, 0x7FU); // DAC data
    CHECK(fm.dac_enabled());
    CHECK(fm.dac_data() == 0x7FU);
    poke(fm, 0, 0x2BU, 0x00U); // DAC disable
    CHECK_FALSE(fm.dac_enabled());
}

TEST_CASE("ym2612 decodes the LFO register") {
    ym2612 fm;
    poke(fm, 0, 0x22U, 0x0BU); // LFO enable (bit 3) + freq 3
    CHECK(fm.lfo_enabled());
    poke(fm, 0, 0x22U, 0x00U);
    CHECK_FALSE(fm.lfo_enabled());
}

TEST_CASE("ym2612 reset clears the digital state") {
    ym2612 fm;
    poke(fm, 0, 0x28U, 0xF0U); // key on channel 1
    poke(fm, 0, 0x2BU, 0x80U); // DAC enable
    poke(fm, 0, 0xB0U, 0x07U); // feedback 0, algorithm 7
    fm.reset(reset_kind::soft);
    CHECK_FALSE(fm.operator_key_on(0, 0));
    CHECK_FALSE(fm.dac_enabled());
    CHECK(fm.channel_algorithm(0) == 0U);
}

TEST_CASE("ym2612 round-trips its state") {
    ym2612 fm;
    poke(fm, 0, 0xA4U, 0x1DU);
    poke(fm, 0, 0xA0U, 0x55U); // ch1 fnum = 0x555, block 3
    poke(fm, 0, 0x44U, 0x40U); // operator TL
    poke(fm, 0, 0x28U, 0xF0U); // key on channel 1
    poke(fm, 0, 0x2BU, 0x80U); // DAC enable
    poke(fm, 0, 0x2AU, 0x42U); // DAC data
    poke(fm, 0, 0x24U, 0xFFU);
    poke(fm, 0, 0x25U, 0x03U); // Timer A load 1023

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    fm.save_state(writer);

    ym2612 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.channel_fnum(0) == 0x555U);
    CHECK(restored.channel_block(0) == 3U);
    CHECK(restored.operator_total_level(0, 2) == 0x40U);
    CHECK(restored.operator_key_on(0, 0));
    CHECK(restored.dac_enabled());
    CHECK(restored.dac_data() == 0x42U);
    CHECK(restored.timer_a_load() == 1023U);
}
