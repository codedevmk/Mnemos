#include "ymz280b.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

static_assert(std::is_base_of_v<mnemos::chips::iaudio_synth, mnemos::chips::audio::ymz280b>);

TEST_CASE("ymz280b reports identity and factory id") {
    const mnemos::chips::audio::ymz280b ymz;
    const auto md = ymz.metadata();
    CHECK(md.manufacturer == "Yamaha");
    CHECK(md.part_number == "YMZ280B");
    CHECK(md.family == "YMZ280B");
    CHECK(md.klass == mnemos::chips::chip_class::audio_synth);

    auto chip = mnemos::chips::create_chip("yamaha.ymz280b");
    REQUIRE(chip != nullptr);
    CHECK(chip->metadata().part_number == std::string("YMZ280B"));
}

TEST_CASE("ymz280b key-on plays attached sample ROM into capture queue") {
    using ymz280b = mnemos::chips::audio::ymz280b;

    ymz280b ymz;
    std::vector<std::uint8_t> rom(0x40U, 0x90U);
    ymz.set_sample_rom(rom);
    ymz.enable_audio_capture(true);
    ymz.set_capture_divider(1U);
    ymz.key_channel(0U, 0x00U, 0x20U, 0xFFU);

    ymz.tick(ymz280b::clocks_per_sample * 4U);
    CHECK(ymz.channel_active(0U));
    CHECK(ymz.last_sample() != 0);
    CHECK(ymz.pending_samples() > 0U);

    std::vector<std::int16_t> out(ymz.pending_samples() * 2U);
    CHECK(ymz.drain_samples(out.data(), out.size() / 2U) > 0U);
}

TEST_CASE("ymz280b register key-on and state round-trip preserve playback") {
    using ymz280b = mnemos::chips::audio::ymz280b;

    ymz280b ymz;
    std::vector<std::uint8_t> rom(0x100U, 0xA0U);
    ymz.set_sample_rom(rom);
    ymz.write_register(ymz280b::reg_start_low, 0x10U);
    ymz.write_register(ymz280b::reg_start_mid, 0x00U);
    ymz.write_register(ymz280b::reg_start_high, 0x00U);
    ymz.write_register(ymz280b::reg_end_low, 0x40U);
    ymz.write_register(ymz280b::reg_end_mid, 0x00U);
    ymz.write_register(ymz280b::reg_end_high, 0x00U);
    ymz.write_register(ymz280b::reg_rate, 0x20U);
    ymz.write_register(ymz280b::reg_volume, 0x80U);
    ymz.write_register(ymz280b::reg_pan, 0x08U);
    ymz.write_register(ymz280b::reg_control,
                       ymz280b::control_key_on | ymz280b::control_mode_pcm8 |
                           ymz280b::control_fn8);
    CHECK(ymz.channel_active(0U));
    CHECK((ymz.read_register(ymz280b::global_status_register) & 0x01U) != 0U);
    CHECK(ymz.step() != 0);

    std::vector<std::uint8_t> state;
    mnemos::chips::state_writer writer(state);
    ymz.save_state(writer);

    ymz280b restored;
    restored.set_sample_rom(rom);
    mnemos::chips::state_reader reader(state);
    restored.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(restored.channel_active(0U));
    CHECK(restored.step() != 0);
}

TEST_CASE("ymz280b datasheet register map keys PCM8 from 24-bit addresses") {
    using ymz280b = mnemos::chips::audio::ymz280b;

    ymz280b ymz;
    std::vector<std::uint8_t> rom(0x200000U, 0x00U);
    rom[0x123456U] = 0x40U;
    rom[0x123457U] = 0x41U;
    ymz.set_sample_rom(rom);

    ymz.write_register(ymz280b::reg_start_high, 0x12U);
    ymz.write_register(ymz280b::reg_start_mid, 0x34U);
    ymz.write_register(ymz280b::reg_start_low, 0x56U);
    ymz.write_register(ymz280b::reg_end_high, 0x12U);
    ymz.write_register(ymz280b::reg_end_mid, 0x34U);
    ymz.write_register(ymz280b::reg_end_low, 0x58U);
    ymz.write_register(ymz280b::reg_pitch, 0x00U);
    ymz.write_register(ymz280b::reg_volume, 0xFFU);
    ymz.write_register(ymz280b::reg_pan, 0x08U);
    ymz.write_register(ymz280b::reg_control,
                       ymz280b::control_key_on | ymz280b::control_mode_pcm8 |
                           ymz280b::control_fn8);

    REQUIRE(ymz.channel_active(0U));
    CHECK(ymz.step() == 0x4000);
    const auto regs = ymz.register_snapshot();
    CHECK(regs[1].value == 0x123457U);
}

TEST_CASE("ymz280b loops between loop-start and loop-end addresses") {
    using ymz280b = mnemos::chips::audio::ymz280b;

    ymz280b ymz;
    std::vector<std::uint8_t> rom(0x40U, 0x00U);
    rom[0x10U] = 0x20U;
    rom[0x11U] = 0x30U;
    rom[0x12U] = 0x40U;
    ymz.set_sample_rom(rom);

    ymz.write_register(ymz280b::reg_start_low, 0x10U);
    ymz.write_register(ymz280b::reg_loop_start_low, 0x11U);
    ymz.write_register(ymz280b::reg_loop_end_low, 0x13U);
    ymz.write_register(ymz280b::reg_end_low, 0x20U);
    ymz.write_register(ymz280b::reg_volume, 0xFFU);
    ymz.write_register(ymz280b::reg_pan, 0x08U);
    ymz.write_register(ymz280b::reg_control,
                       ymz280b::control_key_on | ymz280b::control_mode_pcm8 |
                           ymz280b::control_loop | ymz280b::control_fn8);

    CHECK(ymz.step() == 0x2000);
    CHECK(ymz.step() == 0x3000);
    CHECK(ymz.step() == 0x4000);
    CHECK(ymz.step() == 0x3000);
    CHECK(ymz.channel_active(0U));
}

TEST_CASE("ymz280b decodes signed MSB-first PCM16") {
    using ymz280b = mnemos::chips::audio::ymz280b;

    ymz280b ymz;
    std::vector<std::uint8_t> rom(0x20U, 0x00U);
    rom[0x04U] = 0x7FU;
    rom[0x05U] = 0x00U;
    ymz.set_sample_rom(rom);

    ymz.write_register(ymz280b::reg_start_low, 0x04U);
    ymz.write_register(ymz280b::reg_end_low, 0x08U);
    ymz.write_register(ymz280b::reg_volume, 0xFFU);
    ymz.write_register(ymz280b::reg_pan, 0x08U);
    ymz.write_register(ymz280b::reg_control,
                       ymz280b::control_key_on | ymz280b::control_mode_pcm16 |
                           ymz280b::control_fn8);

    CHECK(ymz.step() == 0x7F00);
}

TEST_CASE("ymz280b decodes ADPCM nibbles and latches enabled end status") {
    using ymz280b = mnemos::chips::audio::ymz280b;

    ymz280b ymz;
    std::vector<std::uint8_t> rom(0x20U, 0x00U);
    rom[0x04U] = 0x70U;
    ymz.set_sample_rom(rom);

    ymz.write_register(ymz280b::reg_start_low, 0x04U);
    ymz.write_register(ymz280b::reg_end_low, 0x05U);
    ymz.write_register(ymz280b::reg_volume, 0xFFU);
    ymz.write_register(ymz280b::reg_pan, 0x08U);
    ymz.write_register(ymz280b::irq_enable_register, 0x01U);
    ymz.write_register(ymz280b::reg_control,
                       ymz280b::control_key_on | ymz280b::control_mode_adpcm4 |
                           ymz280b::control_fn8);

    CHECK(ymz.step() > 0);
    CHECK(ymz.step() > 0);
    CHECK_FALSE(ymz.channel_active(0U));
    CHECK((ymz.read_register(ymz280b::global_status_register) & 0x01U) != 0U);
    CHECK((ymz.read_register(ymz280b::global_status_register) & 0x01U) == 0U);
}
