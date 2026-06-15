#include "capcom_cps1_adapter.hpp"

#include "adapter_registry.hpp"
#include "cps_b_profiles.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

    namespace cps1 = mnemos::manifests::capcom_cps1;
    using mnemos::apps::player::adapters::capcom_cps1::capcom_cps1_adapter;

    void poke32(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint32_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 24U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value >> 16U);
        bytes[at + 2U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 3U] = static_cast<std::uint8_t>(value);
    }

    void poke16(std::vector<std::uint8_t>& bytes, std::size_t at, std::uint16_t value) {
        bytes[at + 0U] = static_cast<std::uint8_t>(value >> 8U);
        bytes[at + 1U] = static_cast<std::uint8_t>(value);
    }

    // A bare 68000 program image: reset vectors ($0 = SSP, $4 = PC) point at a
    // handler at $000400 that writes a marker word into work RAM ($FF0000) then
    // branches to self, so the board keeps running across frames.
    [[nodiscard]] std::vector<std::uint8_t> make_program() {
        std::vector<std::uint8_t> rom(cps1::main_rom_size, 0xFFU);
        poke32(rom, 0x0U, 0x00FF0000U); // initial SSP -> top of work RAM
        poke32(rom, 0x4U, 0x00000400U); // initial PC -> the program below
        // MOVE.W #$4242, ($00FF0000).L  -- stamp a marker into work RAM word 0.
        poke16(rom, 0x400U, 0x33FCU);
        poke16(rom, 0x402U, 0x4242U);     // immediate
        poke32(rom, 0x404U, 0x00FF0000U); // absolute long destination
        poke16(rom, 0x408U, 0x60FEU);     // BRA * (branch to self)
        return rom;
    }

    void put16(std::vector<std::uint8_t>& out, std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v));
        out.push_back(static_cast<std::uint8_t>(v >> 8U));
    }

    void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
        put16(out, static_cast<std::uint16_t>(v));
        put16(out, static_cast<std::uint16_t>(v >> 16U));
    }

    // Minimal STORED-method zip over the given entries (CRC fields zeroed; the
    // reader does not verify them). Mirrors the M72 adapter test helper.
    [[nodiscard]] std::vector<std::uint8_t>
    make_stored_zip(const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& entries) {
        std::vector<std::uint8_t> out;
        struct central final {
            std::string name;
            std::uint32_t size;
            std::uint32_t local_offset;
        };
        std::vector<central> directory;
        for (const auto& [name, data] : entries) {
            const auto local_offset = static_cast<std::uint32_t>(out.size());
            const auto size = static_cast<std::uint32_t>(data.size());
            put32(out, 0x04034B50U);
            put16(out, 20U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, 0U); // crc (unchecked by the reader)
            put32(out, size);
            put32(out, size);
            put16(out, static_cast<std::uint16_t>(name.size()));
            put16(out, 0U);
            out.insert(out.end(), name.begin(), name.end());
            out.insert(out.end(), data.begin(), data.end());
            directory.push_back({name, size, local_offset});
        }
        const auto cd_offset = static_cast<std::uint32_t>(out.size());
        for (const central& c : directory) {
            put32(out, 0x02014B50U);
            put16(out, 20U);
            put16(out, 20U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, 0U);
            put32(out, c.size);
            put32(out, c.size);
            put16(out, static_cast<std::uint16_t>(c.name.size()));
            put16(out, 0U);
            put16(out, 0U);
            put16(out, 0U);
            put16(out, 0U);
            put32(out, 0U);
            put32(out, c.local_offset);
            out.insert(out.end(), c.name.begin(), c.name.end());
        }
        const auto cd_size = static_cast<std::uint32_t>(out.size()) - cd_offset;
        put32(out, 0x06054B50U);
        put16(out, 0U);
        put16(out, 0U);
        put16(out, static_cast<std::uint16_t>(directory.size()));
        put16(out, static_cast<std::uint16_t>(directory.size()));
        put32(out, cd_size);
        put32(out, cd_offset);
        put16(out, 0U);
        return out;
    }

} // namespace

TEST_CASE("capcom_cps1_adapter boots a bare program through the registry",
          "[capcom_cps1][adapter]") {
    mnemos::frontend_sdk::adapter_options options{};
    options.rom = make_program();
    options.display_name = "smoke";
    auto system =
        mnemos::frontend_sdk::adapter_registry::instance().create("cps1", std::move(options));
    REQUIRE(system != nullptr);
    auto& adapter = dynamic_cast<capcom_cps1_adapter&>(*system);

    REQUIRE(adapter.machine().video.frame_index() == 0U);
    adapter.step_one_frame();
    adapter.step_one_frame();
    CHECK(adapter.frames_stepped() == 2U);
    CHECK(adapter.machine().video.frame_index() == 2U);
    // The program ran: the marker landed in work RAM (big-endian word $4242).
    CHECK(adapter.machine().work_ram[0] == 0x42U);
    CHECK(adapter.machine().work_ram[1] == 0x42U);

    const auto frame = adapter.current_frame();
    CHECK(frame.width == 384U);
    CHECK(frame.height == 224U);
    CHECK(adapter.region().frames_per_second_x1000 == 59600U);
    CHECK(adapter.region().orientation == mnemos::frontend_sdk::display_orientation::horizontal);
    CHECK(adapter.chips().size() == 5U);
    CHECK(adapter.system_spec().size() == 3U);
    CHECK(adapter.system_spec()[1].value == "Capcom CPS1");
}

TEST_CASE("capcom_cps1_adapter threads cps_b_profile from the game.toml into the board",
          "[capcom_cps1][adapter][profile]") {
    // Profile 24 carries an ID protection port + the 16x16 multiplier, so the
    // selection is observable through a CPS-B protection read-back.
    constexpr std::uint16_t profile_id = 24U;
    const auto profile = cps1::profile_for_id(profile_id);
    REQUIRE(profile.has_value());

    const std::string manifest = R"(
[set]
schema = "mnemos-romset/1"
name = "cps1_synth"
board = "capcom_cps1"
cps_b_profile = 24

[[region]]
name = "maincpu"
size = 0x800000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(manifest.begin(), manifest.end())},
        {"prog", make_program()},
    });

    capcom_cps1_adapter adapter(zip, "cps1_synth");
    // The declared profile reached the board.
    CHECK(adapter.machine().profile.id == profile_id);

    // The CPS-B ID port returns the profile's identity value (the protection
    // read-back the game's 68000 check reads).
    const auto cps_b_addr = [](std::uint8_t off) { return cps1::cps_b_reg_base + off; };
    CHECK(adapter.machine().main_bus.read16_be(cps_b_addr(profile->id_offset)) ==
          profile->id_value);

    // An absent cps_b_profile keeps the chip's legacy default (id 0).
    const std::string no_profile = R"(
[set]
schema = "mnemos-romset/1"
name = "cps1_synth"
board = "capcom_cps1"

[[region]]
name = "maincpu"
size = 0x800000

[[region.file]]
name = "prog"
offset = 0
)";
    const auto zip2 = make_stored_zip({
        {"game.toml", std::vector<std::uint8_t>(no_profile.begin(), no_profile.end())},
        {"prog", make_program()},
    });
    capcom_cps1_adapter adapter2(zip2, "cps1_synth");
    CHECK(adapter2.machine().profile.id == 0U);
}

TEST_CASE("capcom_cps1_adapter maps pads onto the board's input words",
          "[capcom_cps1][adapter][input]") {
    capcom_cps1_adapter adapter(make_program());

    mnemos::frontend_sdk::controller_state p1{};
    p1.up = true;
    p1.a = true;      // button 1
    p1.start = true;  // start 1
    p1.select = true; // coin 1
    adapter.apply_input(0, p1);

    mnemos::frontend_sdk::controller_state p2{};
    p2.right = true;
    adapter.apply_input(1, p2);

    auto& machine = adapter.machine();
    // P1 (low byte): up = bit3, button1 = bit4. P2 (high byte): right = bit0.
    const std::uint8_t p1_byte = static_cast<std::uint8_t>(0xFFU & ~0x08U & ~0x10U);
    const std::uint8_t p2_byte = static_cast<std::uint8_t>(0xFFU & ~0x01U);
    CHECK(machine.input_p == static_cast<std::uint16_t>((p2_byte << 8U) | p1_byte));
    // The board surfaces the player word on the read window (P2 high, P1 low).
    CHECK(machine.main_bus.read16_be(cps1::player_input_base) ==
          static_cast<std::uint16_t>((p2_byte << 8U) | p1_byte));

    // System byte: start1 = bit0, coin1 = bit4 held low.
    const std::uint8_t sys_byte = static_cast<std::uint8_t>(0xFFU & ~0x01U & ~0x10U);
    CHECK(static_cast<std::uint8_t>(machine.input_sys & 0xFFU) == sys_byte);

    adapter.apply_input(2, p1); // out-of-range port ignored
    CHECK(machine.input_p == static_cast<std::uint16_t>((p2_byte << 8U) | p1_byte));
}

TEST_CASE("capcom_cps1_adapter advances frames and drains YM2151-clocked audio",
          "[capcom_cps1][adapter][audio]") {
    capcom_cps1_adapter adapter(make_program());
    adapter.step_one_frame();

    const auto chunk = adapter.drain_audio();
    // One stereo frame per 64 YM2151 clocks; a frame is ~3.58 MHz worth of Z80/
    // YM2151 time, so the first drain is a non-trivial run of frames.
    CHECK(chunk.frame_count > 0U);
    CHECK(chunk.sample_rate == 55930U);
    REQUIRE(chunk.samples != nullptr);

    // Nothing stepped since the last drain: nothing further is due.
    CHECK(adapter.drain_audio().frame_count == 0U);
}
