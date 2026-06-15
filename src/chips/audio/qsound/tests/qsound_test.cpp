#include "qsound.hpp"

#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::qsound;

    // Program one DSP register through the 3-port window the sound CPU uses:
    // data high byte, data low byte, then the register-select that commits.
    void program(qsound& q, std::uint8_t reg, std::uint16_t data) {
        q.write_port(0, static_cast<std::uint8_t>(data >> 8U));
        q.write_port(1, static_cast<std::uint8_t>(data & 0xFFU));
        q.write_port(2, reg);
    }

    // A sample ROM with one known PCM byte at bank 0, address `addr`.
    std::vector<std::uint8_t> rom_with(std::uint32_t addr, std::uint8_t byte) {
        std::vector<std::uint8_t> rom(0x20000, 0U);
        rom[addr] = byte;
        return rom;
    }

} // namespace

TEST_CASE("qsound mixes one centered voice at the expected level", "[qsound]") {
    qsound q;
    const auto rom = rom_with(0x10U, 0x40U); // sample 0x4000 = 16384
    q.set_sample_rom(rom);

    // Voice 0: addr 0x10, rate 0x100, end 0x1000, volume 0x4000, pan center (0x10).
    program(q, 1U, 0x0010U);  // addr
    program(q, 2U, 0x0100U);  // rate
    program(q, 5U, 0x1000U);  // end_addr
    program(q, 6U, 0x4000U);  // volume
    program(q, 0x80U, 0x10U); // pan (centered)

    q.step();
    // sample = (0x4000 * 16384) >> 14 = 16384; pan center -> each lane
    // (16384 * 16) >> 5 = 8192; output >> mix_shift(2) = 2048.
    CHECK(q.last_left() == 2048);
    CHECK(q.last_right() == 2048);
}

TEST_CASE("qsound pans hard left and hard right", "[qsound]") {
    const auto rom = rom_with(0x10U, 0x40U);

    SECTION("hard left (pan 0)") {
        qsound q;
        q.set_sample_rom(rom);
        program(q, 1U, 0x0010U);
        program(q, 2U, 0x0100U);
        program(q, 5U, 0x1000U);
        program(q, 6U, 0x4000U);
        program(q, 0x80U, 0x00U); // all to the left
        q.step();
        // left = (16384 * 0x20) >> 5 = 16384 -> >> 2 = 4096; right = 0.
        CHECK(q.last_left() == 4096);
        CHECK(q.last_right() == 0);
    }
    SECTION("hard right (pan 0x20)") {
        qsound q;
        q.set_sample_rom(rom);
        program(q, 1U, 0x0010U);
        program(q, 2U, 0x0100U);
        program(q, 5U, 0x1000U);
        program(q, 6U, 0x4000U);
        program(q, 0x80U, 0x20U); // all to the right
        q.step();
        CHECK(q.last_left() == 0);
        CHECK(q.last_right() == 4096);
    }
}

TEST_CASE("qsound skips voices with zero volume or rate", "[qsound]") {
    qsound q;
    const auto rom = rom_with(0x10U, 0x40U);
    q.set_sample_rom(rom);
    program(q, 1U, 0x0010U);
    program(q, 5U, 0x1000U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x10U);
    // rate left at 0 -> the voice is idle.
    q.step();
    CHECK(q.last_left() == 0);
    CHECK(q.last_right() == 0);
}

TEST_CASE("qsound read_sample addresses the ROM by bank:addr", "[qsound]") {
    qsound q;
    // bank 1 (0x8001 -> bank bits 1) addr 0x20 -> rom[(1<<16)|0x20].
    auto rom = std::vector<std::uint8_t>(0x20000, 0U);
    rom[(1U << 16U) | 0x20U] = 0x40U;
    q.set_sample_rom(rom);
    // Voice 0 bank is programmed via voice 15's reg 0 (bank writes the NEXT voice).
    program(q, (15U << 3U) | 0U, 0x8001U); // voice 0 bank = 0x8001
    program(q, 1U, 0x0020U);               // voice 0 addr
    program(q, 2U, 0x0100U);
    program(q, 5U, 0x1000U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x00U);
    q.step();
    CHECK(q.last_left() == 4096); // found the sample at bank 1
}

TEST_CASE("qsound bank register programs the next voice", "[qsound]") {
    qsound q;
    auto rom = std::vector<std::uint8_t>(0x30000, 0U);
    rom[(2U << 16U) | 0x10U] = 0x40U; // a sample in bank 2
    q.set_sample_rom(rom);
    // Writing voice 3's bank register (reg 0) sets voice 4's bank to 0x8002.
    program(q, (3U << 3U) | 0U, 0x8002U);
    program(q, (4U << 3U) | 1U, 0x0010U); // voice 4 addr
    program(q, (4U << 3U) | 2U, 0x0100U); // voice 4 rate
    program(q, (4U << 3U) | 5U, 0x1000U); // voice 4 end
    program(q, (4U << 3U) | 6U, 0x4000U); // voice 4 volume
    program(q, 0x80U + 4U, 0x00U);        // voice 4 pan (left)
    q.step();
    CHECK(q.last_left() == 4096); // voice 4 found the sample in bank 2
}

TEST_CASE("qsound save/load round-trips voice state", "[qsound]") {
    qsound q;
    const auto rom = rom_with(0x10U, 0x40U);
    q.set_sample_rom(rom);
    program(q, 1U, 0x0010U);
    program(q, 2U, 0x0100U);
    program(q, 5U, 0x1000U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x00U);

    std::vector<std::uint8_t> blob;
    state_writer w(blob);
    q.save_state(w);

    qsound q2;
    q2.set_sample_rom(rom);
    state_reader r(blob);
    q2.load_state(r);
    REQUIRE(r.ok());
    q2.step();
    CHECK(q2.last_left() == 4096); // restored voice plays identically
}

TEST_CASE("qsound generate fills interleaved stereo pairs", "[qsound]") {
    qsound q;
    const auto rom = rom_with(0x10U, 0x40U);
    q.set_sample_rom(rom);
    program(q, 1U, 0x0010U);
    program(q, 2U, 0x0100U);
    program(q, 5U, 0x1000U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x10U);
    std::array<std::int16_t, 8> buf{};
    q.generate(buf);
    CHECK(buf[0] == 2048); // L
    CHECK(buf[1] == 2048); // R
}

TEST_CASE("qsound reset restores power-on voice defaults", "[qsound]") {
    qsound q;
    program(q, 6U, 0x4000U); // voice 0 volume
    q.reset(reset_kind::power_on);
    const auto rom = rom_with(0x10U, 0x40U);
    q.set_sample_rom(rom);
    program(q, 1U, 0x0010U);
    program(q, 2U, 0x0100U);
    program(q, 5U, 0x1000U);
    program(q, 0x80U, 0x10U);
    // volume was cleared by reset and not re-programmed -> idle.
    q.step();
    CHECK(q.last_left() == 0);
    CHECK(q.read_status() == qsound::ready_flag);
}
