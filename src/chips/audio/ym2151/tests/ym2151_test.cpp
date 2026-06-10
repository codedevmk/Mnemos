#include "ym2151.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

namespace {
    using mnemos::chips::audio::ym2151;
} // namespace

TEST_CASE("ym2151 registers through the chip registry", "[ym2151]") {
    auto chip = mnemos::chips::create_chip("yamaha.ym2151");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == "ym2151");
    CHECK(chip->metadata().klass == mnemos::chips::chip_class::audio_synth);
}

TEST_CASE("ym2151 latches an address and stores the data byte", "[ym2151]") {
    ym2151 opm;
    opm.write_address(0x28U); // channel 0 KC
    opm.write_data(0x4AU);
    CHECK(opm.register_value(0x28U) == 0x4AU);

    // BUSY holds for one sample period after the data write.
    CHECK((opm.read_status() & ym2151::status_busy) != 0U);
    opm.tick(ym2151::busy_clocks);
    CHECK((opm.read_status() & ym2151::status_busy) == 0U);
}

TEST_CASE("ym2151 timer A overflows on its documented cadence and raises IRQ", "[ym2151]") {
    ym2151 opm;
    std::vector<bool> irq_edges;
    opm.set_irq([&](bool asserted) { irq_edges.push_back(asserted); });

    // CLKA = 1022: overflow every 64 * (1024 - 1022) = 128 chip clocks.
    opm.write_address(0x10U);
    opm.write_data(0xFFU); // CLKA high 8 = 0xFF
    opm.write_address(0x11U);
    opm.write_data(0x02U); // CLKA low 2 = 0b10 -> 1022
    opm.write_address(0x14U);
    opm.write_data(0x05U); // run A + IRQ enable A

    opm.tick(127U);
    CHECK((opm.read_status() & ym2151::status_timer_a) == 0U);
    opm.tick(1U);
    CHECK((opm.read_status() & ym2151::status_timer_a) != 0U);
    REQUIRE(irq_edges.size() == 1U);
    CHECK(irq_edges[0] == true);

    // Write-one-to-clear drops the flag and the line.
    opm.write_address(0x14U);
    opm.write_data(0x15U); // keep running + enabled, reset flag A
    CHECK((opm.read_status() & ym2151::status_timer_a) == 0U);
    REQUIRE(irq_edges.size() == 2U);
    CHECK(irq_edges[1] == false);

    // It keeps overflowing on the same cadence after the reload.
    opm.tick(128U);
    CHECK((opm.read_status() & ym2151::status_timer_a) != 0U);
}

TEST_CASE("ym2151 timer B overflows on its slower cadence", "[ym2151]") {
    ym2151 opm;
    // CLKB = 254: overflow every 1024 * (256 - 254) = 2048 chip clocks.
    opm.write_address(0x12U);
    opm.write_data(0xFEU);
    opm.write_address(0x14U);
    opm.write_data(0x0AU); // run B + IRQ enable B

    opm.tick(2047U);
    CHECK((opm.read_status() & ym2151::status_timer_b) == 0U);
    CHECK_FALSE(opm.irq_asserted());
    opm.tick(1U);
    CHECK((opm.read_status() & ym2151::status_timer_b) != 0U);
    CHECK(opm.irq_asserted());
}

TEST_CASE("ym2151 flag without enable does not assert the IRQ line", "[ym2151]") {
    ym2151 opm;
    opm.write_address(0x10U);
    opm.write_data(0xFFU);
    opm.write_address(0x11U);
    opm.write_data(0x03U); // CLKA = 1023: overflow every 64 clocks
    opm.write_address(0x14U);
    opm.write_data(0x01U); // run A, IRQ disabled

    opm.tick(64U);
    CHECK((opm.read_status() & ym2151::status_timer_a) != 0U);
    CHECK_FALSE(opm.irq_asserted());
}

TEST_CASE("ym2151 emits silence with nothing keyed on", "[ym2151]") {
    ym2151 opm;
    std::vector<std::int16_t> buffer(64U, 0x7FFF);
    opm.update(buffer);
    for (const std::int16_t sample : buffer) {
        CHECK(sample == 0);
    }
}

TEST_CASE("ym2151 save and load round-trips the timer state", "[ym2151]") {
    ym2151 opm;
    opm.write_address(0x12U);
    opm.write_data(0xF0U);
    opm.write_address(0x14U);
    opm.write_data(0x0AU);
    opm.tick(5000U);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    opm.save_state(writer);

    ym2151 restored;
    mnemos::chips::state_reader reader(snapshot);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.register_value(0x12U) == 0xF0U);
    CHECK(restored.read_status() == opm.read_status());
    CHECK(restored.elapsed_clocks() == opm.elapsed_clocks());
}

namespace {

    void write_reg(ym2151& opm, std::uint8_t address, std::uint8_t value) {
        opm.write_address(address);
        opm.write_data(value);
    }

    // A single audible sine: connection 7 (parallel carriers) with three
    // operators muted at TL 127 and one carrier at TL 0, instant attack,
    // sustain at full level.
    void program_single_carrier(ym2151& opm, std::uint8_t channel, std::uint8_t kc,
                                std::uint8_t rl_bits) {
        write_reg(opm, static_cast<std::uint8_t>(0x20U + channel),
                  static_cast<std::uint8_t>(rl_bits | 0x07U)); // RL | FB 0 | CON 7
        write_reg(opm, static_cast<std::uint8_t>(0x28U + channel), kc);
        write_reg(opm, static_cast<std::uint8_t>(0x30U + channel), 0U); // KF 0
        for (std::uint8_t slot = 0; slot < 4U; ++slot) {
            const auto op_base = static_cast<std::uint8_t>(slot * 8U + channel);
            write_reg(opm, static_cast<std::uint8_t>(0x40U + op_base), 0x01U); // DT1 0, MUL 1
            write_reg(opm, static_cast<std::uint8_t>(0x60U + op_base),
                      slot == 3U ? 0x00U : 0x7FU);                             // only C2 audible
            write_reg(opm, static_cast<std::uint8_t>(0x80U + op_base), 0x1FU); // AR max
            write_reg(opm, static_cast<std::uint8_t>(0xA0U + op_base), 0x00U); // D1R 0
            write_reg(opm, static_cast<std::uint8_t>(0xC0U + op_base), 0x00U); // DT2 0, D2R 0
            write_reg(opm, static_cast<std::uint8_t>(0xE0U + op_base), 0x0FU); // D1L 0, RR max
        }
        write_reg(opm, 0x08U, static_cast<std::uint8_t>(0x78U | channel)); // all slots on
    }

    [[nodiscard]] std::size_t count_zero_crossings(const std::vector<std::int16_t>& interleaved) {
        std::size_t crossings = 0;
        std::int16_t previous = 0;
        for (std::size_t i = 0; i < interleaved.size(); i += 2U) { // left channel
            const std::int16_t sample = interleaved[i];
            if (sample != 0 && previous != 0 && (sample < 0) != (previous < 0)) {
                ++crossings;
            }
            if (sample != 0) {
                previous = sample;
            }
        }
        return crossings;
    }

} // namespace

TEST_CASE("ym2151 keyed-on carrier produces audio and key-off releases to silence", "[ym2151]") {
    ym2151 opm;
    program_single_carrier(opm, 0U, 0x4AU, 0xC0U); // both L and R

    std::vector<std::int16_t> buffer(2048U, 0);
    opm.update(buffer);
    bool left_active = false;
    bool right_active = false;
    for (std::size_t i = 0; i < buffer.size(); i += 2U) {
        left_active = left_active || buffer[i] != 0;
        right_active = right_active || buffer[i + 1U] != 0;
    }
    CHECK(left_active);
    CHECK(right_active);

    write_reg(opm, 0x08U, 0x00U); // key off channel 0
    std::vector<std::int16_t> tail(16384U, 0);
    opm.update(tail); // release runs out
    std::vector<std::int16_t> silence(256U, 0x7FFF);
    opm.update(silence);
    for (const std::int16_t sample : silence) {
        CHECK(sample == 0);
    }
}

TEST_CASE("ym2151 pan bits route a channel to one side", "[ym2151]") {
    ym2151 opm;
    program_single_carrier(opm, 0U, 0x4AU, 0x40U); // left only

    std::vector<std::int16_t> buffer(1024U, 0);
    opm.update(buffer);
    bool left_active = false;
    for (std::size_t i = 0; i < buffer.size(); i += 2U) {
        left_active = left_active || buffer[i] != 0;
        CHECK(buffer[i + 1U] == 0); // right stays silent
    }
    CHECK(left_active);
}

TEST_CASE("ym2151 an octave up doubles the output frequency", "[ym2151]") {
    ym2151 low;
    ym2151 high;
    program_single_carrier(low, 0U, 0x4AU, 0xC0U);  // octave 4
    program_single_carrier(high, 0U, 0x5AU, 0xC0U); // octave 5, same note

    std::vector<std::int16_t> low_buf(16384U, 0);
    std::vector<std::int16_t> high_buf(16384U, 0);
    low.update(low_buf);
    high.update(high_buf);
    const auto low_crossings = count_zero_crossings(low_buf);
    const auto high_crossings = count_zero_crossings(high_buf);
    REQUIRE(low_crossings > 50U);
    const double ratio = static_cast<double>(high_crossings) / static_cast<double>(low_crossings);
    CHECK(ratio > 1.9);
    CHECK(ratio < 2.1);
}

TEST_CASE("ym2151 rendering is deterministic across instances", "[ym2151]") {
    ym2151 a;
    ym2151 b;
    program_single_carrier(a, 2U, 0x3CU, 0xC0U);
    program_single_carrier(b, 2U, 0x3CU, 0xC0U);

    std::vector<std::int16_t> buf_a(512U, 0);
    std::vector<std::int16_t> buf_b(512U, 0);
    a.update(buf_a);
    b.update(buf_b);
    CHECK(buf_a == buf_b);
}

TEST_CASE("ym2151 synthesis state survives save/load mid-note", "[ym2151]") {
    ym2151 opm;
    program_single_carrier(opm, 0U, 0x4AU, 0xC0U);
    std::vector<std::int16_t> warmup(512U, 0);
    opm.update(warmup);

    std::vector<std::uint8_t> snapshot;
    mnemos::chips::state_writer writer(snapshot);
    opm.save_state(writer);

    std::vector<std::int16_t> expected(256U, 0);
    opm.update(expected);

    ym2151 restored;
    mnemos::chips::state_reader reader(snapshot);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    std::vector<std::int16_t> resumed(256U, 0);
    restored.update(resumed);
    CHECK(resumed == expected);
}
