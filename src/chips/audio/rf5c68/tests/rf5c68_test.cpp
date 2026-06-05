// rf5c68 port fidelity vs the Emu reference. The golden PCM block was generated
// by compiling+running the reference chip through the identical scenario built
// in configure_voice0() below.

#include "rf5c68.hpp"

#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::rf5c68;

    // Reproduce the golden generator's scenario: a sign-magnitude waveform at
    // 0x0100 with a 0xFF loop terminator, one voice with envelope/pan/fractional
    // pitch, enabled and keyed on.
    void configure_voice0(rf5c68& chip) {
        constexpr std::array<std::uint8_t, 11> wave = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60,
                                                       0x70, 0x7F, 0x90, 0xA0, 0xFF};
        const auto ram = chip.waveram();
        for (std::size_t i = 0; i < wave.size(); ++i) {
            ram[0x0100 + i] = wave[i];
        }
        chip.write_reg(rf5c68::reg_ctrl, 0x00); // select voice 0
        chip.write_reg(rf5c68::reg_env, 0xC0);
        chip.write_reg(rf5c68::reg_pan, 0x8F); // right=8, left=15
        chip.write_reg(rf5c68::reg_fdl, 0x00);
        chip.write_reg(rf5c68::reg_fdh, 0x05); // freq_divider = 0x0500
        chip.write_reg(rf5c68::reg_lsl, 0x00);
        chip.write_reg(rf5c68::reg_lsh, 0x01);  // loop_start = 0x0100
        chip.write_reg(rf5c68::reg_st, 0x01);   // start = 0x0100
        chip.write_reg(rf5c68::reg_ctrl, 0x80); // enable, voice 0
        chip.write_reg(rf5c68::reg_chan, 0xFE); // unmute channel 0 only
        chip.key_on(0);
    }

    constexpr std::array<std::int16_t, 128> kPcmGolden = {
        720,  384,  720,  384,   1440,  768,  1440, 768,  2160, 1152,  2880,  1536, 2880,
        1536, 3600, 1920, 4320,  2304,  4320, 2304, 5040, 2688, 5040,  2688,  5715, 3048,
        -720, -384, -720, -384,  -1440, -768, 720,  384,  720,  384,   1440,  768,  1440,
        768,  2160, 1152, 2880,  1536,  2880, 1536, 3600, 1920, 4320,  2304,  4320, 2304,
        5040, 2688, 5040, 2688,  5715,  3048, -720, -384, -720, -384,  -1440, -768, 720,
        384,  720,  384,  1440,  768,   1440, 768,  2160, 1152, 2880,  1536,  2880, 1536,
        3600, 1920, 4320, 2304,  4320,  2304, 5040, 2688, 5040, 2688,  5715,  3048, -720,
        -384, -720, -384, -1440, -768,  720,  384,  720,  384,  1440,  768,   1440, 768,
        2160, 1152, 2880, 1536,  2880,  1536, 3600, 1920, 4320, 2304,  4320,  2304, 5040,
        2688, 5040, 2688, 5715,  3048,  -720, -384, -720, -384, -1440, -768};

} // namespace

TEST_CASE("rf5c68 generates PCM sample-for-sample like the reference", "[rf5c68][audio]") {
    rf5c68 chip;
    configure_voice0(chip);
    std::array<std::int16_t, 128> buf{};
    chip.generate(buf);
    for (std::size_t i = 0; i < buf.size(); ++i) {
        INFO("sample " << i << (i % 2 == 0 ? " (L)" : " (R)"));
        REQUIRE(buf[i] == kPcmGolden[i]);
    }
}

TEST_CASE("rf5c68 register writes read back and decode CTRL/CHAN", "[rf5c68][audio]") {
    rf5c68 chip;
    chip.write_reg(rf5c68::reg_ctrl, 0x83); // enable, bank 3, voice 0
    REQUIRE(chip.read_reg(rf5c68::reg_ctrl) == 0x83);
    chip.write_reg(rf5c68::reg_env, 0x55);
    REQUIRE(chip.read_reg(rf5c68::reg_env) == 0x55);
    // Reserved index returns open bus.
    REQUIRE(chip.read_reg(0x0A) == 0xFF);
    // A disabled chip is silent.
    chip.write_reg(rf5c68::reg_ctrl, 0x00);
    chip.step();
    REQUIRE(chip.last_left() == 0);
    REQUIRE(chip.last_right() == 0);
}

TEST_CASE("rf5c68 save_state/load_state round-trips bit-identically", "[rf5c68][audio]") {
    rf5c68 a;
    configure_voice0(a);
    // Advance a few samples so runtime accumulator state is non-trivial.
    std::array<std::int16_t, 20> warm{};
    a.generate(warm);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    rf5c68 b;
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
