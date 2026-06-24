// NES controller/input tests. The ROM fixture is intentionally local so this
// file stays independent from the large mapper-focused system-test helpers.

#include "nes_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace {

    using namespace mnemos::manifests::nes;

    // A 16 KiB-PRG / 8 KiB-CHR NROM whose code does: LDA #$42 / STA $0200 / spin.
    // The reset vector ($FFFC, which a 16 KiB PRG mirrors to prg[0x3FFC]) -> $8000.
    std::vector<std::uint8_t> make_synthetic_nrom() {
        std::vector<std::uint8_t> rom(16U + 0x4000U + 0x2000U, 0x00U);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1AU;
        rom[4] = 1U; // 1 x 16 KiB PRG
        rom[5] = 1U; // 1 x 8 KiB CHR

        const std::size_t prg = 16U; // PRG starts right after the 16-byte header
        const std::array<std::uint8_t, 8> code = {
            0xA9U, 0x42U,        // LDA #$42
            0x8DU, 0x00U, 0x02U, // STA $0200
            0x4CU, 0x05U, 0x80U, // JMP $8005 (self-loop)
        };
        for (std::size_t i = 0; i < code.size(); ++i) {
            rom[prg + i] = code[i];
        }
        rom[prg + 0x3FFCU] = 0x00U; // reset vector low
        rom[prg + 0x3FFDU] = 0x80U; // reset vector high -> $8000
        return rom;
    }

} // namespace

TEST_CASE("Zapper on port 2 reports the trigger and off-screen light sense", "[manifests][nes]") {
    auto sys = assemble_nes(make_synthetic_nrom(), nes_config{.zapper = true});

    // Trigger pulled, aimed off-screen (no frame rendered -> no light): $4017 bit 4
    // set (trigger), bit 3 set (no light).
    sys->set_zapper(-1, -1, true);
    std::uint8_t v = sys->bus.read8(0x4017U);
    CHECK((v & 0x10U) != 0U); // trigger
    CHECK((v & 0x08U) != 0U); // no light off-screen

    // Trigger released.
    sys->set_zapper(-1, -1, false);
    v = sys->bus.read8(0x4017U);
    CHECK((v & 0x10U) == 0U);

    // Port 1 ($4016) is unaffected -- it is still a standard pad.
    sys->set_pad(0, nes_system::btn_a);
    sys->bus.write8(0x4016U, 0x01U);
    sys->bus.write8(0x4016U, 0x00U);
    CHECK((sys->bus.read8(0x4016U) & 0x01U) == 0x01U); // A shifts out first
}

TEST_CASE("Four Score clocks four pads + the adapter signature", "[manifests][nes]") {
    auto sys = assemble_nes(make_synthetic_nrom(), nes_config{.four_score = true});

    // Four distinct single-button pads so each shows up at a unique bit position.
    sys->set_pad(0, nes_system::btn_a);      // bit 0
    sys->set_pad(1, nes_system::btn_b);      // bit 1
    sys->set_pad(2, nes_system::btn_select); // bit 2
    sys->set_pad(3, nes_system::btn_start);  // bit 3

    sys->bus.write8(0x4016U, 0x01U); // strobe
    sys->bus.write8(0x4016U, 0x00U);

    std::array<std::uint8_t, 24> p0{};
    std::array<std::uint8_t, 24> p1{};
    for (std::size_t i = 0; i < 24U; ++i) {
        p0[i] = static_cast<std::uint8_t>(sys->bus.read8(0x4016U) & 0x01U);
        p1[i] = static_cast<std::uint8_t>(sys->bus.read8(0x4017U) & 0x01U);
    }

    // $4016: controller 1 (A at bit 0) then controller 3 (Select at bit 8+2=10) then
    // the signature 1 at bit 19.
    CHECK(p0[0] == 1U);
    CHECK(p0[10] == 1U);
    CHECK(p0[19] == 1U);
    // $4017: controller 2 (B at bit 1) then controller 4 (Start at bit 8+3=11) then
    // the signature 1 at bit 18.
    CHECK(p1[1] == 1U);
    CHECK(p1[11] == 1U);
    CHECK(p1[18] == 1U);

    // Every other bit in the controller+signature window is 0.
    int ones0 = 0;
    int ones1 = 0;
    for (std::size_t i = 0; i < 24U; ++i) {
        ones0 += p0[i];
        ones1 += p1[i];
    }
    CHECK(ones0 == 3); // A + Select + signature
    CHECK(ones1 == 3); // B + Start + signature

    // Reads past the 24th return 1.
    CHECK((sys->bus.read8(0x4016U) & 0x01U) == 1U);
    CHECK((sys->bus.read8(0x4017U) & 0x01U) == 1U);
}

TEST_CASE("controller shift register clocks buttons in $4016 read order", "[manifests][nes]") {
    auto sys = assemble_nes(make_synthetic_nrom());
    sys->set_pad(0, nes_system::btn_a | nes_system::btn_right);

    // Strobe high then low ($4016) latches the button state for serial reads.
    sys->bus.write8(0x4016U, 0x01U);
    sys->bus.write8(0x4016U, 0x00U);

    // Eight reads emit A, B, Select, Start, Up, Down, Left, Right in bit 0.
    const std::array<bool, 8> expect = {true, false, false, false, false, false, false, true};
    for (std::size_t i = 0; i < expect.size(); ++i) {
        const std::uint8_t r = sys->bus.read8(0x4016U);
        CHECK((r & 0x01U) == (expect[i] ? 0x01U : 0x00U));
    }
    // Reads past the 8th return 1 (the open-bus / shifted-in level).
    CHECK((sys->bus.read8(0x4016U) & 0x01U) == 0x01U);
    // Controller 2 ($4017) was never pressed -> first read is 0.
    sys->bus.write8(0x4016U, 0x01U);
    sys->bus.write8(0x4016U, 0x00U);
    CHECK((sys->bus.read8(0x4017U) & 0x01U) == 0x00U);
}
