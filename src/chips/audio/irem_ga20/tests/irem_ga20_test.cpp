#include "irem_ga20.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::irem_ga20;

    std::array<std::uint8_t, 0x300> make_sample_rom() {
        std::array<std::uint8_t, 0x300> rom{};
        rom[0x100] = 0x90U;
        rom[0x101] = 0xA0U;
        rom[0x102] = 0x70U;
        rom[0x103] = 0x00U;
        rom[0x200] = 0x60U;
        rom[0x201] = 0x50U;
        rom[0x202] = 0x00U;
        return rom;
    }

    void configure_channel0(irem_ga20& chip) {
        chip.write_register(irem_ga20::reg_start_low, 0x10U);  // 0x0010 << 4 = 0x100
        chip.write_register(irem_ga20::reg_start_high, 0x00U);
        chip.write_register(irem_ga20::reg_end_low, 0x11U);    // 0x0011 << 4 = 0x110
        chip.write_register(irem_ga20::reg_end_high, 0x00U);
        chip.write_register(irem_ga20::reg_rate, 0xFFU);       // advance every GA20 sample
        chip.write_register(irem_ga20::reg_volume, 0xF6U);     // volume = 246*256/(246+10)
        chip.write_register(irem_ga20::reg_control, irem_ga20::control_key_on);
    }

} // namespace

TEST_CASE("irem_ga20 registers in the chip factory as an audio synth", "[irem_ga20][audio]") {
    auto chip = mnemos::chips::create_chip("irem.ga20");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == mnemos::chips::chip_class::audio_synth);
    REQUIRE(chip->metadata().part_number == "GA20");
}

TEST_CASE("irem_ga20 plays unsigned PCM until the zero terminator", "[irem_ga20][audio]") {
    const auto rom = make_sample_rom();
    irem_ga20 chip;
    chip.set_sample_rom(rom);
    configure_channel0(chip);

    REQUIRE(chip.read_register(irem_ga20::reg_status) == irem_ga20::status_active);
    CHECK(chip.step() == 3936);
    CHECK(chip.step() == 7872);
    CHECK(chip.step() == -3936);
    CHECK(chip.step() == 0);
    REQUIRE(chip.read_register(irem_ga20::reg_status) == 0U);
}

TEST_CASE("irem_ga20 mixes active channels and clamps output", "[irem_ga20][audio]") {
    const auto rom = make_sample_rom();
    irem_ga20 chip;
    chip.set_sample_rom(rom);

    configure_channel0(chip);
    constexpr std::uint8_t ch1 = 8U;
    chip.write_register(ch1 + irem_ga20::reg_start_low, 0x20U);
    chip.write_register(ch1 + irem_ga20::reg_start_high, 0x00U);
    chip.write_register(ch1 + irem_ga20::reg_end_low, 0x21U);
    chip.write_register(ch1 + irem_ga20::reg_end_high, 0x00U);
    chip.write_register(ch1 + irem_ga20::reg_rate, 0xFFU);
    chip.write_register(ch1 + irem_ga20::reg_volume, 0xFFU);
    chip.write_register(ch1 + irem_ga20::reg_control, irem_ga20::control_key_on);

    const std::int16_t first = chip.step();
    REQUIRE(first < 0);
    REQUIRE(chip.read_register(irem_ga20::reg_status) == irem_ga20::status_active);
    REQUIRE(chip.read_register(ch1 + irem_ga20::reg_status) == irem_ga20::status_active);
}

TEST_CASE("irem_ga20 control bit clears active status", "[irem_ga20][audio]") {
    const auto rom = make_sample_rom();
    irem_ga20 chip;
    chip.set_sample_rom(rom);
    configure_channel0(chip);
    REQUIRE(chip.channel_active(0U));

    chip.write_register(irem_ga20::reg_control, 0x00U);
    REQUIRE_FALSE(chip.channel_active(0U));
    REQUIRE(chip.read_register(irem_ga20::reg_status) == 0U);
    REQUIRE(chip.step() == 0);
}

TEST_CASE("irem_ga20 capture sink queues stereo frames at clock divided by four",
          "[irem_ga20][audio]") {
    const auto rom = make_sample_rom();
    irem_ga20 chip;
    chip.set_sample_rom(rom);
    chip.enable_audio_capture(true);
    configure_channel0(chip);

    chip.tick(12U);
    REQUIRE(chip.pending_samples() == 3U);

    std::array<std::int16_t, 6> out{};
    const std::size_t got = chip.drain_samples(out.data(), 3U);
    REQUIRE(got == 3U);
    REQUIRE(chip.pending_samples() == 0U);
    REQUIRE(out[0] == out[1]);
    REQUIRE(out[0] == 3936);
    REQUIRE(out[2] == out[3]);
    REQUIRE(out[2] == 7872);
    REQUIRE(out[4] == out[5]);
    REQUIRE(out[4] == -3936);
}

TEST_CASE("irem_ga20 capture divider decimates native PCM samples", "[irem_ga20][audio]") {
    const auto rom = make_sample_rom();
    irem_ga20 chip;
    chip.set_sample_rom(rom);
    chip.enable_audio_capture(true);
    chip.set_capture_divider(2U);
    configure_channel0(chip);

    chip.tick(12U); // three native GA20 samples, one captured after decimation
    REQUIRE(chip.pending_samples() == 1U);

    std::array<std::int16_t, 2> out{};
    REQUIRE(chip.drain_samples(out.data(), 1U) == 1U);
    REQUIRE(out[0] == out[1]);
    REQUIRE(out[0] == 7872);
}

TEST_CASE("irem_ga20 save_state/load_state round-trips playback state", "[irem_ga20][audio]") {
    const auto rom = make_sample_rom();
    irem_ga20 a;
    a.set_sample_rom(rom);
    configure_channel0(a);
    REQUIRE(a.step() == 3936);

    std::vector<std::uint8_t> state;
    state_writer writer(state);
    a.save_state(writer);

    irem_ga20 b;
    b.set_sample_rom(rom);
    state_reader reader(state);
    b.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(b.read_register(irem_ga20::reg_status) == irem_ga20::status_active);
    REQUIRE(a.step() == b.step());
    REQUIRE(a.step() == b.step());
}
