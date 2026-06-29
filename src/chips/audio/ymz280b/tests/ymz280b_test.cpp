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
    ymz.write_register(ymz280b::reg_control, ymz280b::control_key_on);
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
