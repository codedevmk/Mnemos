#include "qsound.hpp"

#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace {

    using mnemos::chips::reset_kind;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::qsound;
    constexpr std::int16_t mixed_0x40 = 16383;
    constexpr std::int16_t mixed_0x20 = 8192;

    // Program one DSP register through the 3-port window the sound CPU uses:
    // data high byte, data low byte, then the register-select that commits.
    void program(qsound& q, std::uint8_t reg, std::uint16_t data) {
        q.write_port(0, static_cast<std::uint8_t>(data >> 8U));
        q.write_port(1, static_cast<std::uint8_t>(data & 0xFFU));
        q.write_port(2, reg);
    }

    void program_at(qsound& q, std::uint8_t reg, std::uint16_t data, std::uint16_t pc) {
        q.write_port_with_pc(0, static_cast<std::uint8_t>(data >> 8U), pc);
        q.write_port_with_pc(1, static_cast<std::uint8_t>(data & 0xFFU), pc);
        q.write_port_with_pc(2, reg, pc);
    }

    void program_zero_dry_delay(qsound& q) {
        program(q, 0xDFU, 0x0000U);
        program(q, 0xE1U, 0x0000U);
        program(q, 0xE2U, 0x0001U);
    }

    // A sample ROM with one known PCM byte at bank 0, address `addr`.
    std::vector<std::uint8_t> rom_with(std::uint32_t addr, std::uint8_t byte) {
        std::vector<std::uint8_t> rom(0x20000, 0U);
        rom[addr] = byte;
        return rom;
    }

    [[nodiscard]] bool all_zero(std::span<const std::int16_t> samples) noexcept {
        for (const std::int16_t sample : samples) {
            if (sample != 0) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool any_nonzero(std::span<const std::int16_t> samples) noexcept {
        for (const std::int16_t sample : samples) {
            if (sample != 0) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::uint64_t introspection_value(qsound& q, std::string_view name) {
        auto* view = q.introspection().registers();
        REQUIRE(view != nullptr);
        for (const auto& reg : view->registers()) {
            if (reg.name == name) {
                return reg.value;
            }
        }
        FAIL("missing QSound register descriptor");
        return 0U;
    }

} // namespace

TEST_CASE("qsound mixes one centered voice at the expected level", "[qsound]") {
    qsound q;
    const auto rom = rom_with(0x10U, 0x40U); // sample 0x4000 = 16384
    q.set_sample_rom(rom);

    // Voice 0: addr 0x10, rate 0x100, end 0x1000, volume 0x4000, pan center (0x20).
    program(q, 1U, 0x0010U);  // addr
    program(q, 2U, 0x0100U);  // rate
    program(q, 5U, 0x1000U);  // end_addr
    program(q, 6U, 0x4000U);  // volume
    program(q, 0x80U, 0x20U); // pan (centered)

    q.step();
    // sample = (0x4000 * 0x4000) >> 14 = 16384; DL-1425 center pan
    // routes the dry signal at full scale into both lanes.
    CHECK(q.last_left() == mixed_0x40);
    CHECK(q.last_right() == mixed_0x40);
}

TEST_CASE("qsound pans hard left, center, and hard right", "[qsound]") {
    const auto rom = rom_with(0x10U, 0x40U);

    SECTION("hard left (pan 0x10)") {
        qsound q;
        q.set_sample_rom(rom);
        program(q, 1U, 0x0010U);
        program(q, 2U, 0x0100U);
        program(q, 5U, 0x1000U);
        program(q, 6U, 0x4000U);
        program(q, 0x80U, 0x10U); // all to the left
        q.step();
        CHECK(q.last_left() == mixed_0x40);
        CHECK(q.last_right() == 0);
    }
    SECTION("center (pan 0x20)") {
        qsound q;
        q.set_sample_rom(rom);
        program(q, 1U, 0x0010U);
        program(q, 2U, 0x0100U);
        program(q, 5U, 0x1000U);
        program(q, 6U, 0x4000U);
        program(q, 0x80U, 0x20U); // centered
        q.step();
        CHECK(q.last_left() == mixed_0x40);
        CHECK(q.last_right() == mixed_0x40);
    }
    SECTION("hard right (pan 0x30)") {
        qsound q;
        q.set_sample_rom(rom);
        program(q, 1U, 0x0010U);
        program(q, 2U, 0x0100U);
        program(q, 5U, 0x1000U);
        program(q, 6U, 0x4000U);
        program(q, 0x80U, 0x30U); // all to the right
        q.step();
        CHECK(q.last_left() == 0);
        CHECK(q.last_right() == mixed_0x40);
    }
}

TEST_CASE("qsound direct pan mixer matches the CPS2 bring-up attenuation", "[qsound]") {
    qsound q;
    q.set_mixer_mode(qsound::mixer_mode::direct_pan);
    const auto rom = rom_with(0x10U, 0x40U);
    q.set_sample_rom(rom);

    program(q, 1U, 0x0010U);
    program(q, 2U, 0x0100U);
    program(q, 5U, 0x1000U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x20U);

    q.step();

    CHECK(q.last_left() == 2048);
    CHECK(q.last_right() == 2048);
    CHECK(q.current_mixer_mode() == qsound::mixer_mode::direct_pan);
}

TEST_CASE("qsound power-on pan defaults to the DL-1425 center address", "[qsound]") {
    qsound q;
    const auto rom = rom_with(0x10U, 0x40U);
    q.set_sample_rom(rom);
    program(q, 1U, 0x0010U);
    program(q, 2U, 0x0100U);
    program(q, 5U, 0x1000U);
    program(q, 6U, 0x4000U);

    q.step();

    CHECK(q.last_left() == mixed_0x40);
    CHECK(q.last_right() == mixed_0x40);
}

TEST_CASE("qsound skips voices with zero volume", "[qsound]") {
    qsound q;
    const auto rom = rom_with(0x10U, 0x40U);
    q.set_sample_rom(rom);
    program(q, 1U, 0x0010U);
    program(q, 5U, 0x1000U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x20U);
    program(q, 6U, 0x0000U);
    q.step();
    CHECK(q.last_left() == 0);
    CHECK(q.last_right() == 0);
}

TEST_CASE("qsound direct pan keeps muted PCM voices parked", "[qsound]") {
    qsound q;
    q.set_mixer_mode(qsound::mixer_mode::direct_pan);
    auto rom = std::vector<std::uint8_t>(0x20000, 0U);
    rom[0x10U] = 0x40U;
    rom[0x11U] = 0x20U;
    q.set_sample_rom(rom);

    program(q, 1U, 0x0010U);
    program(q, 2U, 0x1000U);
    program(q, 5U, 0x0020U);
    program(q, 0x80U, 0x20U);

    q.step();
    CHECK(q.last_left() == 0);
    CHECK(q.last_right() == 0);

    program(q, 6U, 0x4000U);
    q.step();
    CHECK(q.last_left() == 2048);
    CHECK(q.last_right() == 2048);
}

TEST_CASE("qsound emits stationary PCM voices when rate is zero", "[qsound]") {
    qsound q;
    const auto rom = rom_with(0x10U, 0x40U);
    q.set_sample_rom(rom);
    program(q, 1U, 0x0010U);
    program(q, 5U, 0x1000U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x20U);

    q.step();

    CHECK(q.last_left() == mixed_0x40);
    CHECK(q.last_right() == mixed_0x40);
}

TEST_CASE("qsound keeps advancing a PCM voice past end when loop length is zero",
          "[qsound]") {
    qsound q;
    auto rom = std::vector<std::uint8_t>(0x20000, 0U);
    rom[0x10U] = 0x40U;
    rom[0x12U] = 0x20U;
    q.set_sample_rom(rom);
    program(q, 1U, 0x0010U);
    program(q, 2U, 0x2000U);
    program(q, 5U, 0x0011U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x20U);
    program_zero_dry_delay(q);

    q.step();
    CHECK(q.last_left() == mixed_0x40);
    CHECK(q.last_right() == mixed_0x40);

    q.step();
    CHECK(q.last_left() == mixed_0x20);
    CHECK(q.last_right() == mixed_0x20);
}

TEST_CASE("qsound plays PCM voices at high (>=0x8000) sample addresses",
          "[qsound][pcm]") {
    qsound q;
    q.set_mixer_mode(qsound::mixer_mode::direct_pan);
    auto rom = std::vector<std::uint8_t>(0x10000, 0U);
    rom[0x8000U] = 0x40U;
    rom[0x8001U] = 0x40U;
    rom[0x8002U] = 0x40U;
    q.set_sample_rom(rom);

    program(q, 1U, 0x8000U);  // high 16-bit sample address
    program(q, 2U, 0x1000U);  // advance one byte per QSound sample
    program(q, 5U, 0x8004U);  // high 16-bit end address
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x20U);

    // A voice address is a full 16-bit index, so it must advance through the
    // upper half of the bank (0x8000, 0x8001, 0x8002) instead of being pinned
    // at the old 0x7FFF phase cap that silenced every high-address (melodic)
    // sample while low-half percussion/voice samples kept playing.
    q.step();
    CHECK(q.last_left() == 2048);
    CHECK(q.last_right() == 2048);
    CHECK(introspection_value(q, "PCM00_ADDR") == 0x8001U);

    q.step();
    CHECK(q.last_left() == 2048);
    CHECK(q.last_right() == 2048);
    CHECK(introspection_value(q, "PCM00_ADDR") == 0x8002U);
}

TEST_CASE("qsound treats high PCM end registers as unsigned loop boundaries",
          "[qsound][pcm]") {
    qsound q;
    q.set_mixer_mode(qsound::mixer_mode::direct_pan);
    auto rom = std::vector<std::uint8_t>(0x10000, 0U);
    rom[0x0010U] = 0x40U;
    rom[0x0011U] = 0x20U;
    q.set_sample_rom(rom);

    program(q, 1U, 0x0010U);
    program(q, 2U, 0x1000U);
    program(q, 4U, 0x0010U);
    program(q, 5U, 0x8000U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x20U);

    q.step();
    CHECK(q.last_left() == 2048);
    CHECK(q.last_right() == 2048);
    CHECK(introspection_value(q, "PCM00_ADDR") == 0x0011U);

    q.step();
    CHECK(q.last_left() == 1024);
    CHECK(q.last_right() == 1024);
    CHECK(introspection_value(q, "PCM00_ADDR") == 0x0012U);
}

TEST_CASE("qsound advances PCM voices across the full 16-bit address space",
          "[qsound][pcm]") {
    qsound q;
    q.set_mixer_mode(qsound::mixer_mode::direct_pan);
    auto rom = std::vector<std::uint8_t>(0x10000, 0U);
    rom[0xFFF0U] = 0x40U;
    q.set_sample_rom(rom);

    program(q, 1U, 0xFFF0U);
    program(q, 2U, 0xFFFFU);
    program(q, 5U, 0x0001U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x20U);

    q.step();

    CHECK(q.last_left() == 2048);
    CHECK(q.last_right() == 2048);
    // 0xFFF0 + 0xFFFF saturates at the 28-bit phase ceiling -> address 0xFFFF,
    // the true top of the 16-bit space (not the old 0x7FFF clamp).
    CHECK(introspection_value(q, "PCM00_ADDR") == 0xFFFFU);
    CHECK(introspection_value(q, "PCM00_PHASE") == 0xFFF0U);
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
    CHECK(q.last_left() == mixed_0x40); // found the sample at bank 1
}

TEST_CASE("qsound masks the bank high bit instead of treating it as an enable",
          "[qsound]") {
    qsound q;
    auto rom = std::vector<std::uint8_t>(0x20000, 0U);
    rom[(1U << 16U) | 0x20U] = 0x40U;
    q.set_sample_rom(rom);

    // Current QSound references address `(bank & 0x7fff) << 16 | addr`; the high
    // bit is not a playback enable gate.
    program(q, (15U << 3U) | 0U, 0x0001U); // voice 0 bank = low bank 1
    program(q, 1U, 0x0020U);
    program(q, 2U, 0x0100U);
    program(q, 5U, 0x1000U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x00U);

    q.step();

    CHECK(q.last_left() == mixed_0x40);
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
    CHECK(q.last_left() == mixed_0x40); // voice 4 found the sample in bank 2
}

TEST_CASE("qsound records CPS2 sound-driver register diagnostics", "[qsound][trace]") {
    qsound q;

    program_at(q, 0x06U, 0x4000U, 0x1234U); // voice 0 volume
    program_at(q, 0xCDU, 0x2222U, 0x2345U); // ADPCM voice 0 volume
    program_at(q, 0xD6U, 0x0001U, 0x3456U); // ADPCM voice 0 trigger

    CHECK(q.port_write_count() == 9U);
    CHECK(q.register_write_count() == 3U);
    CHECK(q.register_write_histogram(0x06U) == 1U);
    CHECK(q.register_write_histogram(0xCDU) == 1U);
    CHECK(q.nonzero_pcm_volume_write_count() == 1U);
    CHECK(q.nonzero_adpcm_volume_write_count() == 1U);
    CHECK(q.adpcm_trigger_count() == 1U);
    CHECK(q.last_register() == 0xD6U);
    CHECK(q.last_register_data() == 0x0001U);
    CHECK(q.last_register_pc() == 0x3456U);
    CHECK(q.register_trace_count() == 3U);

    const auto first = q.register_trace(0U);
    CHECK(first.sequence == 0U);
    CHECK(first.reg == 0x06U);
    CHECK(first.data == 0x4000U);
    CHECK(first.pc == 0x1234U);
    const auto last = q.register_trace(2U);
    CHECK(last.sequence == 2U);
    CHECK(last.reg == 0xD6U);
    CHECK(last.data == 0x0001U);
    CHECK(last.pc == 0x3456U);

    CHECK(introspection_value(q, "REGWR") == 3U);
    CHECK(introspection_value(q, "TRACECOUNT") == 3U);
    CHECK(introspection_value(q, "LASTREG") == 0xD6U);
    CHECK(introspection_value(q, "LASTDATA") == 0x0001U);
    CHECK(introspection_value(q, "LASTPC") == 0x3456U);
    CHECK(introspection_value(q, "PCM_VOLWR") == 1U);
    CHECK(introspection_value(q, "ADPCM_VOLWR") == 1U);
    CHECK(introspection_value(q, "ADPCM_TRIG") == 1U);
    CHECK(introspection_value(q, "ADPCM0_START") == 0U);
    CHECK(introspection_value(q, "ADPCM0_END") == 0U);
    CHECK(introspection_value(q, "ADPCM0_BANK") == 0x8000U);
    CHECK(introspection_value(q, "ADPCM0_VOL") == 0x2222U);
    CHECK(introspection_value(q, "ADPCM0_PLAY") == 0U);
    CHECK(introspection_value(q, "ADPCM0_FLAG") == 1U);
    CHECK(introspection_value(q, "REG06_WR") == 1U);
    CHECK(introspection_value(q, "REG06_DATA") == 0x4000U);
    CHECK(introspection_value(q, "REG06_PC") == 0x1234U);
    CHECK(introspection_value(q, "REGCD_WR") == 1U);
    CHECK(introspection_value(q, "REGCD_DATA") == 0x2222U);
    CHECK(introspection_value(q, "REGCD_PC") == 0x2345U);
    CHECK(introspection_value(q, "TRACE000_SEQ") == 0U);
    CHECK(introspection_value(q, "TRACE000_REG") == 0x06U);
    CHECK(introspection_value(q, "TRACE000_DATA") == 0x4000U);
    CHECK(introspection_value(q, "TRACE000_PC") == 0x1234U);
    CHECK(introspection_value(q, "TRACE002_SEQ") == 2U);
    CHECK(introspection_value(q, "TRACE002_REG") == 0xD6U);
    CHECK(introspection_value(q, "TRACE002_DATA") == 0x0001U);
    CHECK(introspection_value(q, "TRACE002_PC") == 0x3456U);
}

TEST_CASE("qsound captures PCM echo registers and keeps output deterministic",
          "[qsound][echo]") {
    qsound q;
    const auto rom = rom_with(0x10U, 0x40U);
    q.set_sample_rom(rom);
    program(q, 1U, 0x0010U);
    program(q, 2U, 0x0000U);
    program(q, 5U, 0x1000U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x20U);
    program(q, 0xBAU, 0x4000U); // voice 0 echo send
    program(q, 0x93U, 0x4000U); // echo feedback
    program(q, 0xD9U, static_cast<std::uint16_t>(qsound::echo_delay_base + 1U));

    std::array<std::int16_t, 6> first{};
    std::array<std::int16_t, 6> second{};
    q.generate(first);
    q.reset(reset_kind::power_on);
    q.set_sample_rom(rom);
    program(q, 1U, 0x0010U);
    program(q, 2U, 0x0000U);
    program(q, 5U, 0x1000U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x20U);
    program(q, 0xBAU, 0x4000U);
    program(q, 0x93U, 0x4000U);
    program(q, 0xD9U, static_cast<std::uint16_t>(qsound::echo_delay_base + 1U));
    q.generate(second);
    CHECK(second == first);
    CHECK(any_nonzero(second));
    CHECK(introspection_value(q, "ECHOFB") == 0x4000U);
    CHECK(introspection_value(q, "ECHOLEN") == 1U);
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
    CHECK(q2.last_left() == mixed_0x40); // restored voice plays identically
}

TEST_CASE("qsound save/load preserves register diagnostics", "[qsound][trace][save]") {
    qsound q;
    program_at(q, 0x06U, 0x4000U, 0x4567U);
    program_at(q, 0xD6U, 0x0001U, 0x5678U);

    std::vector<std::uint8_t> blob;
    state_writer w(blob);
    q.save_state(w);

    qsound restored;
    state_reader r(blob);
    restored.load_state(r);
    REQUIRE(r.ok());

    CHECK(restored.port_write_count() == q.port_write_count());
    CHECK(restored.register_write_count() == q.register_write_count());
    CHECK(restored.nonzero_pcm_volume_write_count() == 1U);
    CHECK(restored.adpcm_trigger_count() == 1U);
    CHECK(restored.last_register() == 0xD6U);
    CHECK(restored.last_register_data() == 0x0001U);
    CHECK(restored.last_register_pc() == 0x5678U);
    REQUIRE(restored.register_trace_count() == 2U);
    const auto entry = restored.register_trace(1U);
    CHECK(entry.sequence == 1U);
    CHECK(entry.reg == 0xD6U);
    CHECK(entry.data == 0x0001U);
    CHECK(entry.pc == 0x5678U);
}

TEST_CASE("qsound save/load preserves echo delay state", "[qsound][echo][save]") {
    qsound q;
    const auto rom = rom_with(0x10U, 0x40U);
    q.set_sample_rom(rom);
    program(q, 1U, 0x0010U);
    program(q, 2U, 0x0000U);
    program(q, 5U, 0x1000U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x20U);
    program(q, 0xBAU, 0x4000U);
    program(q, 0x93U, 0x4000U);
    program(q, 0xD9U, static_cast<std::uint16_t>(qsound::echo_delay_base + 1U));
    q.step();

    std::vector<std::uint8_t> blob;
    state_writer w(blob);
    q.save_state(w);

    qsound restored;
    restored.set_sample_rom(rom);
    state_reader r(blob);
    restored.load_state(r);
    REQUIRE(r.ok());

    std::array<std::int16_t, 6> reference{};
    std::array<std::int16_t, 6> actual{};
    q.generate(reference);
    restored.generate(actual);
    CHECK(actual == reference);
}

TEST_CASE("qsound decodes triggered ADPCM voices into both lanes", "[qsound][adpcm]") {
    qsound q;
    auto rom = std::vector<std::uint8_t>(0x20000, 0U);
    rom[0x20U] = 0x70U; // high nibble +7, low nibble 0
    q.set_sample_rom(rom);

    program(q, 0xCAU, 0x0020U); // voice 0 start
    program(q, 0xCBU, 0x0022U); // voice 0 end
    program(q, 0xCCU, 0x8000U); // voice 0 bank
    program(q, 0xCDU, 0x4000U); // voice 0 volume
    program(q, 0xD6U, 0x0001U); // trigger voice 0

    q.step();

    // predictor = 75 from the +7 nibble and initial step size 10; volume 0x4000
    // gives sample 18, mixed mono into L/R.
    CHECK(q.last_left() == 18);
    CHECK(q.last_right() == 18);
}

TEST_CASE("qsound ADPCM trigger latches a late nonzero volume", "[qsound][adpcm]") {
    qsound q;
    auto rom = std::vector<std::uint8_t>(0x20000, 0U);
    rom[0x20U] = 0x70U;
    q.set_sample_rom(rom);

    program(q, 0xCAU, 0x0020U);
    program(q, 0xCBU, 0x0022U);
    program(q, 0xCCU, 0x8000U);
    program(q, 0xD6U, 0x0001U);
    program(q, 0xCDU, 0x4000U);

    std::array<std::int16_t, 32> samples{};
    q.generate(samples);

    CHECK(any_nonzero(samples));
}

TEST_CASE("qsound ADPCM trigger survives audio ticks before volume",
          "[qsound][adpcm]") {
    qsound q;
    auto rom = std::vector<std::uint8_t>(0x20000, 0U);
    rom[0x20U] = 0x70U;
    q.set_sample_rom(rom);

    program(q, 0xCAU, 0x0020U);
    program(q, 0xCBU, 0x0022U);
    program(q, 0xCCU, 0x8000U);
    program(q, 0xD6U, 0x0001U);

    std::array<std::int16_t, 24> silent{};
    q.generate(silent);
    CHECK(all_zero(silent));

    program(q, 0xCDU, 0x4000U);
    std::array<std::int16_t, 32> audible{};
    q.generate(audible);
    CHECK(any_nonzero(audible));
}

TEST_CASE("qsound ADPCM consumes the top nibble before the low nibble",
          "[qsound][adpcm]") {
    qsound q;
    auto rom = std::vector<std::uint8_t>(0x20000, 0U);
    rom[0x20U] = 0x07U; // top nibble 0 first, then low nibble +7.
    q.set_sample_rom(rom);

    program(q, 0xCAU, 0x0020U);
    program(q, 0xCBU, 0x0022U);
    program(q, 0xCCU, 0x8000U);
    program(q, 0xCDU, 0x4000U);
    program(q, 0xD6U, 0x0001U);

    q.step();

    CHECK(q.last_left() < 0);
    CHECK(q.last_right() < 0);
}

TEST_CASE("qsound save/load preserves ADPCM decoder phase", "[qsound][adpcm][save]") {
    qsound q;
    auto rom = std::vector<std::uint8_t>(0x20000, 0U);
    rom[0x20U] = 0x70U;
    q.set_sample_rom(rom);

    program(q, 0xCAU, 0x0020U);
    program(q, 0xCBU, 0x0022U);
    program(q, 0xCCU, 0x8000U);
    program(q, 0xCDU, 0x4000U);
    program(q, 0xD6U, 0x0001U);
    q.step();

    std::vector<std::uint8_t> blob;
    state_writer w(blob);
    q.save_state(w);

    qsound restored;
    restored.set_sample_rom(rom);
    state_reader r(blob);
    restored.load_state(r);
    REQUIRE(r.ok());

    std::array<std::int16_t, 12> reference{};
    std::array<std::int16_t, 12> actual{};
    q.generate(reference);
    restored.generate(actual);
    CHECK(actual == reference);
}

TEST_CASE("qsound generate fills interleaved stereo pairs", "[qsound]") {
    qsound q;
    const auto rom = rom_with(0x10U, 0x40U);
    q.set_sample_rom(rom);
    program(q, 1U, 0x0010U);
    program(q, 2U, 0x0100U);
    program(q, 5U, 0x1000U);
    program(q, 6U, 0x4000U);
    program(q, 0x80U, 0x20U);
    std::array<std::int16_t, 8> buf{};
    q.generate(buf);
    CHECK(buf[0] == mixed_0x40); // L
    CHECK(buf[1] == mixed_0x40); // R
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
    q.tick(qsound::command_ready_cycles);
    CHECK(q.read_status() == qsound::ready_flag);
}

TEST_CASE("qsound immediate ready mode keeps register scripts unthrottled", "[qsound]") {
    qsound q;
    q.set_ready_mode(qsound::ready_mode::immediate);
    CHECK(q.current_ready_mode() == qsound::ready_mode::immediate);
    CHECK(q.read_status() == qsound::ready_flag);

    q.write_port(0, 0x12U);
    q.write_port(1, 0x34U);
    q.write_port(2, 0x93U);
    CHECK(q.read_status() == qsound::ready_flag);

    q.tick(qsound::command_ready_cycles - 1U);
    CHECK(q.read_status() == qsound::ready_flag);
}

TEST_CASE("qsound register commit holds ready low for one sample interval", "[qsound]") {
    qsound q;
    CHECK(q.read_status() == qsound::ready_flag);

    q.write_port(0, 0x12U);
    q.write_port(1, 0x34U);
    CHECK(q.read_status() == qsound::ready_flag);

    q.write_port(2, 0x93U);
    CHECK(q.read_status() == 0U);

    q.tick(qsound::command_ready_cycles - 1U);
    CHECK(q.read_status() == 0U);

    q.tick(1U);
    CHECK(q.read_status() == qsound::ready_flag);
}
