#include "ym2612.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
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

    std::uint8_t op_reg(std::uint8_t base, std::uint8_t slot) {
        return static_cast<std::uint8_t>(base + slot * 4U);
    }

    // Program channel 1 (index 0) as a loud algorithm-7 voice (all four operators are
    // carriers) at a mid frequency, then key it on. Used by the synthesis tests.
    void program_loud_voice(ym2612& ym) {
        for (std::uint8_t slot = 0; slot < 4; ++slot) {
            poke(ym, 0, op_reg(0x30U, slot), 0x01U); // DT=0, MUL=1
            poke(ym, 0, op_reg(0x40U, slot), 0x00U); // TL=0 (loudest)
            poke(ym, 0, op_reg(0x50U, slot), 0x1FU); // RS=0, AR=31 (fast attack)
            poke(ym, 0, op_reg(0x60U, slot), 0x00U); // AM=0, D1R=0
            poke(ym, 0, op_reg(0x70U, slot), 0x00U); // D2R=0
            poke(ym, 0, op_reg(0x80U, slot), 0x00U); // SL=0, RR=0
            poke(ym, 0, op_reg(0x90U, slot), 0x00U); // SSG-EG off
        }
        poke(ym, 0, 0xB0U, 0x07U); // feedback 0, algorithm 7
        poke(ym, 0, 0xA4U, 0x24U); // block 4, fnum-hi 4
        poke(ym, 0, 0xA0U, 0x00U); // fnum-lo -> fnum 0x400
        poke(ym, 0, 0x28U, 0xF0U); // key on all four operators
    }

    int peak_abs_left(ym2612& ym, int samples) {
        int peak = 0;
        for (int i = 0; i < samples; ++i) {
            const auto s = ym.step();
            peak = std::max(peak, std::abs(static_cast<int>(s.left)));
        }
        return peak;
    }
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::iaudio_synth, ym2612>);

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

TEST_CASE("ym2612 generates FM output for a keyed voice") {
    ym2612 fm;
    program_loud_voice(fm);
    CHECK(peak_abs_left(fm, 512) > 0); // a loud algorithm-7 voice is audible
}

TEST_CASE("ym2612 stays silent until a voice is keyed on") {
    ym2612 fm;
    // Program the same loud voice but never send the $28 key-on.
    for (std::uint8_t slot = 0; slot < 4; ++slot) {
        poke(fm, 0, op_reg(0x40U, slot), 0x00U); // TL loud
        poke(fm, 0, op_reg(0x50U, slot), 0x1FU); // fast attack
    }
    poke(fm, 0, 0xB0U, 0x07U);
    poke(fm, 0, 0xA4U, 0x24U);
    poke(fm, 0, 0xA0U, 0x00U);
    CHECK(peak_abs_left(fm, 256) == 0); // operators remain in the OFF phase
}

TEST_CASE("ym2612 release silences a keyed voice") {
    ym2612 fm;
    program_loud_voice(fm);
    REQUIRE(peak_abs_left(fm, 64) > 0); // audible while keyed
    for (std::uint8_t slot = 0; slot < 4; ++slot) {
        poke(fm, 0, op_reg(0x80U, slot), 0x0FU); // SL=0, RR=15 (fastest release)
    }
    poke(fm, 0, 0x28U, 0x00U); // key off -> enter release
    for (int i = 0; i < 8192; ++i) {
        (void)fm.step(); // let the envelope ramp to the floor
    }
    CHECK(peak_abs_left(fm, 64) == 0); // fully released -> silent
}

TEST_CASE("ym2612 routes the channel-6 DAC to the output") {
    ym2612 fm;
    poke(fm, 0, 0x2BU, 0x80U); // DAC enable
    poke(fm, 0, 0x2AU, 0xFFU); // full positive
    const auto hi = fm.step();
    CHECK(hi.left > 0);
    CHECK(hi.right > 0);
    poke(fm, 0, 0x2AU, 0x00U); // full negative
    const auto lo = fm.step();
    CHECK(lo.left < 0);
}

TEST_CASE("ym2612 update fills an interleaved stereo buffer") {
    ym2612 fm;
    poke(fm, 0, 0x2BU, 0x80U);
    poke(fm, 0, 0x2AU, 0xFFU);
    std::array<std::int16_t, 8> buf{};
    fm.update(buf);
    CHECK(buf[0] > 0); // left
    CHECK(buf[1] > 0); // right
    CHECK(buf[6] > 0); // a later frame still carries the DAC level
}

TEST_CASE("ym2612 round-trips a playing voice bit-exactly") {
    ym2612 fm;
    program_loud_voice(fm);
    for (int i = 0; i < 100; ++i) {
        (void)fm.step();
    }

    std::vector<std::uint8_t> blob;
    mnemos::chips::state_writer writer(blob);
    fm.save_state(writer);

    ym2612 restored;
    mnemos::chips::state_reader reader(blob);
    restored.load_state(reader);
    REQUIRE(reader.ok());

    // The full synthesis state (phase accumulators, envelope levels, feedback
    // history, LFO + EG clocks) must reproduce the next sample identically.
    const auto a = fm.step();
    const auto b = restored.step();
    CHECK(a.left == b.left);
    CHECK(a.right == b.right);
}

TEST_CASE("ym2612 exposes its register file via introspection") {
    ym2612 fm;
    auto* rv = fm.introspection().registers();
    REQUIRE(rv != nullptr); // register_view backed by register_snapshot()
    CHECK_FALSE(rv->registers().empty());
}
