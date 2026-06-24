#include "scc.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

namespace {
    using mnemos::chips::chip_class;
    using mnemos::chips::state_reader;
    using mnemos::chips::state_writer;
    using mnemos::chips::audio::scc;

    void program_square_tone(scc& chip) {
        for (int i = 0; i < scc::waveform_size; ++i) {
            chip.write(static_cast<std::uint16_t>(i), i < (scc::waveform_size / 2) ? 0x60U : 0xA0U);
        }
        chip.write(0x80U, 0x02U); // channel 1 period low
        chip.write(0x81U, 0x00U); // channel 1 period high
        chip.write(0x8AU, 0x0FU); // channel 1 volume
        chip.write(0x8FU, 0x01U); // channel 1 enable
        chip.set_clock_divider(1);
    }

    [[nodiscard]] std::int16_t capture_peak(scc& chip, int ticks) {
        chip.enable_audio_capture(true);
        chip.tick(static_cast<std::uint64_t>(ticks));
        std::vector<std::int16_t> buf(chip.pending_samples() * 2U, 0);
        const std::size_t pairs = chip.drain_samples(buf.data(), chip.pending_samples());
        if (pairs == 0U) {
            return 0;
        }
        std::int16_t peak = 0;
        for (const std::int16_t s : buf) {
            peak = std::max(peak, static_cast<std::int16_t>(std::abs(s)));
        }
        return peak;
    }

    [[nodiscard]] std::vector<std::int16_t> capture(scc& chip, std::size_t pairs) {
        chip.enable_audio_capture(true);
        while (chip.pending_samples() < pairs) {
            chip.tick(256);
        }
        std::vector<std::int16_t> out(pairs * 2U, 0);
        chip.drain_samples(out.data(), pairs);
        return out;
    }
} // namespace

TEST_CASE("scc registers in the chip factory", "[scc][audio]") {
    const auto chip = mnemos::chips::create_chip("konami.051649");
    REQUIRE(chip != nullptr);
    REQUIRE(chip->metadata().klass == chip_class::audio_synth);
    CHECK(chip->metadata().part_number == "051649");
}

TEST_CASE("scc maps waveform and control registers", "[scc][audio]") {
    scc chip;

    chip.write(0x00U, 0x7FU);
    chip.write(0x80U, 0x34U);
    chip.write(0x81U, 0x02U);
    chip.write(0x8AU, 0xAFU);
    chip.write(0x8FU, 0x01U);

    CHECK(chip.read(0x00U) == 0x7FU);
    CHECK(chip.wave_sample(0, 0) == 0x7FU);
    CHECK(chip.frequency(0) == 0x0234U);
    CHECK(chip.volume(0) == 0x0FU);
    CHECK(chip.channel_enabled(0));
    CHECK_FALSE(chip.channel_enabled(1));
}

TEST_CASE("scc mirrors the control block through the $90-$9F window", "[scc][audio]") {
    scc chip;

    chip.write(0x90U, 0x78U);
    chip.write(0x91U, 0x03U);
    chip.write(0x9AU, 0x0CU);
    chip.write(0x9FU, 0x01U);

    CHECK(chip.read(0x80U) == 0x78U);
    CHECK(chip.read(0x90U) == 0x78U);
    CHECK(chip.frequency(0) == 0x0378U);
    CHECK(chip.volume(0) == 0x0CU);
    CHECK(chip.channel_enabled(0));
}

TEST_CASE("scc channels four and five share waveform RAM", "[scc][audio]") {
    scc chip;

    chip.write(0x60U, 0x55U);
    chip.write(0x7FU, 0xAAU);

    CHECK(chip.wave_sample(3, 0) == 0x55U);
    CHECK(chip.wave_sample(4, 0) == 0x55U);
    CHECK(chip.wave_sample(3, 31) == 0xAAU);
    CHECK(chip.wave_sample(4, 31) == 0xAAU);
}

TEST_CASE("scc plays a captured wavetable tone", "[scc][audio]") {
    scc chip;
    program_square_tone(chip);

    CHECK(capture_peak(chip, 256) > 1000);
}

TEST_CASE("scc save_state/load_state round-trips registers and output phase", "[scc][audio]") {
    scc a;
    program_square_tone(a);
    a.tick(127);

    std::vector<std::uint8_t> blob;
    state_writer writer(blob);
    a.save_state(writer);

    constexpr std::size_t k_pairs = 512;
    const std::vector<std::int16_t> from_a = capture(a, k_pairs);

    scc b;
    state_reader reader(std::span<const std::uint8_t>{blob});
    b.load_state(reader);
    REQUIRE(reader.ok());

    CHECK(b.frequency(0) == a.frequency(0));
    CHECK(b.volume(0) == a.volume(0));
    CHECK(b.channel_enabled(0));
    CHECK(b.wave_sample(0, 0) == a.wave_sample(0, 0));
    CHECK(capture(b, k_pairs) == from_a);
}
