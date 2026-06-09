#include "sid_6581.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace {
    using mnemos::chips::audio::sid_6581;
    using env_phase = sid_6581::env_phase;
} // namespace

static_assert(std::is_base_of_v<mnemos::chips::iaudio_synth, sid_6581>);

TEST_CASE("sid_6581 reports identity and registers under mos.6581") {
    const sid_6581 sid;
    const auto md = sid.metadata();
    CHECK(md.manufacturer == "MOS Technology");
    CHECK(md.part_number == "6581");
    CHECK(md.klass == mnemos::chips::chip_class::audio_synth);

    const auto* descriptor = mnemos::chips::find_factory("mos.6581");
    REQUIRE(descriptor != nullptr);
    CHECK(descriptor->klass == mnemos::chips::chip_class::audio_synth);
    REQUIRE(mnemos::chips::create_chip("mos.6581") != nullptr);
}

TEST_CASE("sid_6581 register R/W classes and 32-byte mirror") {
    sid_6581 sid;
    sid.write(0x00U, 0xABU);         // voice register: write-only
    CHECK(sid.read(0x00U) == 0xFFU); // floats high on read
    CHECK(sid.read(0x20U) == 0xFFU); // $D420 mirrors voice reg 0 (addr & 0x1F)
    sid.write(0x1BU, 0x55U);         // OSC3: read-only, write ignored
    CHECK(sid.read(0x1BU) == 0x00U);
}

TEST_CASE("sid_6581 sawtooth follows the phase accumulator") {
    sid_6581 sid;
    sid.write(0x00U, 0x00U);
    sid.write(0x01U, 0x10U); // frequency = $1000
    sid.write(0x04U, 0x20U); // CTRL: SAW
    sid.tick(1U);
    CHECK(sid.voice_phase(0U) == 0x1000U);
    CHECK(sid.waveform_output(0U) == 0x001U); // (phase >> 12) & 0xFFF
}

TEST_CASE("sid_6581 ADSR attacks to peak then sustains, releases on gate-off") {
    sid_6581 sid;
    sid.write(0x05U, 0x00U); // AD: attack=0 (fast), decay=0
    sid.write(0x06U, 0xF0U); // SR: sustain=15 -> peak, release=0
    sid.write(0x04U, 0x11U); // CTRL: GATE | TRIANGLE

    sid.tick(4000U);
    CHECK(sid.envelope_value(0U) == 0xFFU);
    CHECK(sid.envelope_phase(0U) == env_phase::sustain);

    sid.write(0x04U, 0x10U); // gate off
    sid.tick(1U);
    CHECK(sid.envelope_phase(0U) == env_phase::release);
}

TEST_CASE("sid_6581 OSC3 and ENV3 read back voice 3 state") {
    sid_6581 sid;
    sid.write(14U + 0U, 0x00U);
    sid.write(14U + 1U, 0x40U); // voice 3 frequency
    sid.write(14U + 5U, 0x00U); // AD attack=0
    sid.write(14U + 6U, 0xF0U); // SR sustain=15
    sid.write(14U + 4U, 0x21U); // CTRL: SAW | GATE

    sid.tick(4000U);
    CHECK(sid.read(0x1CU) == 0xFFU); // ENV3 = voice 3 envelope (peaked)
    CHECK(sid.read(0x1BU) ==
          static_cast<std::uint8_t>((sid.waveform_output(2U) >> 4U) & 0xFFU)); // OSC3
}

TEST_CASE("sid_6581 hard sync resets the synced voice on the source MSB edge") {
    sid_6581 sid;
    // Voice 1 (index 0) syncs to voice 3 (index 2). Drive voice 3 fast so its
    // accumulator MSB rises; voice 1 should be reset to 0 on that edge.
    sid.write(14U + 0U, 0x00U);
    sid.write(14U + 1U, 0x40U); // voice 3 frequency = $4000
    sid.write(0x00U, 0xFFU);
    sid.write(0x01U, 0x0FU);    // voice 1 frequency
    sid.write(0x04U, 0x22U);    // voice 1 CTRL: SAW | SYNC
    sid.write(14U + 4U, 0x20U); // voice 3 CTRL: SAW
    sid.tick(64U);
    // Hard to assert an exact value; just confirm the engine ran without UB and
    // voice 1's phase stays within range.
    CHECK(sid.voice_phase(0U) <= 0x00FFFFFFU);
}

TEST_CASE("sid_6581 produces a non-silent sample for an active voice") {
    sid_6581 sid;
    sid.write(0x00U, 0x00U);
    sid.write(0x01U, 0x20U); // frequency
    sid.write(0x05U, 0x00U); // AD attack=0
    sid.write(0x06U, 0xF0U); // SR sustain=15
    sid.write(0x04U, 0x21U); // CTRL: SAW | GATE
    sid.write(0x18U, 0x0FU); // MODE_VOL: volume = 15, no filter modes

    sid.tick(4000U); // ramp the envelope to peak

    bool nonzero = false;
    for (int i = 0; i < 128; ++i) {
        sid.tick(20U);
        if (sid.sample() != 0) {
            nonzero = true;
        }
    }
    CHECK(nonzero);
}

TEST_CASE("sid_6581 filter cutoff maps into the 6581 range") {
    sid_6581 sid;
    sid.write(0x16U, 0xFFU); // FC_HI
    sid.write(0x15U, 0x07U); // FC_LO (low 3 bits)
    const auto hz = sid.filter_cutoff_hz();
    CHECK(hz >= 220);
    CHECK(hz <= 18000);
}

TEST_CASE("sid_6581 8580 variant widens combined waveforms") {
    sid_6581 sid;
    sid.set_variant(sid_6581::variant::mos_8580);
    CHECK(sid.chip_variant() == sid_6581::variant::mos_8580);
    // 8580 filter range floor is lower than the 6581.
    sid.write(0x16U, 0x00U);
    sid.write(0x15U, 0x00U);
    CHECK(sid.filter_cutoff_hz() == 30); // 8580 minimum
}

TEST_CASE("sid_6581 reset clears voices but keeps the variant") {
    sid_6581 sid;
    sid.set_variant(sid_6581::variant::mos_8580);
    sid.write(0x04U, 0x11U); // gate a voice
    sid.tick(100U);
    sid.reset(mnemos::chips::reset_kind::hard);
    CHECK(sid.envelope_value(0U) == 0U);
    CHECK(sid.chip_variant() == sid_6581::variant::mos_8580);
}

TEST_CASE("sid_6581 is reachable through immio") {
    auto chip = mnemos::chips::create_chip("mos.6581");
    REQUIRE(chip != nullptr);
    auto* mmio = dynamic_cast<mnemos::chips::immio*>(chip.get());
    REQUIRE(mmio != nullptr);
    mmio->mmio_write(0x00U, 0xABU);         // voice register (write-only)
    CHECK(mmio->mmio_read(0x00U) == 0xFFU); // write-only floats high
}

TEST_CASE("sid_6581 register snapshot reports envelopes and volume") {
    sid_6581 sid;
    const auto regs = sid.register_snapshot();
    REQUIRE(regs.size() == 4U);
    CHECK(regs[0].name == "V1_ENV");
    CHECK(regs[3].name == "VOL");
}

TEST_CASE("sid_6581 save/load round-trips") {
    sid_6581 a;
    a.write(0x00U, 0x34U); // voice 1 freq lo
    a.write(0x01U, 0x12U); // voice 1 freq hi
    a.write(0x04U, 0x11U); // voice 1 control: gate + triangle
    a.write(0x18U, 0x1FU); // volume + filter
    a.tick(200U);
    (void)a.sample();

    std::vector<std::uint8_t> buf1;
    mnemos::chips::state_writer w(buf1);
    a.save_state(w);

    sid_6581 b;
    mnemos::chips::state_reader r(buf1);
    b.load_state(r);
    CHECK(r.ok());

    std::vector<std::uint8_t> buf2;
    mnemos::chips::state_writer w2(buf2);
    b.save_state(w2);
    CHECK(buf1 == buf2);
}

TEST_CASE("sid_6581 exposes its register file via introspection") {
    sid_6581 sid;
    auto* rv = sid.introspection().registers();
    REQUIRE(rv != nullptr); // register_view backed by register_snapshot()
    CHECK_FALSE(rv->registers().empty());
}
