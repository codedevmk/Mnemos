// Proves the C64 manifest path's PLA dynamic banking: build the C64 system via
// build_system + the c64.bank.* overlay predicates, then flip the 6510 $01 port
// and confirm RAM / BASIC / KERNAL / CHARGEN / I/O visibility tracks the PLA
// decode -- the mechanism the static overlay model couldn't express. (Full
// framebuffer parity vs assemble_c64 is the c64_runtime cutover's job.)

#include "builder.hpp"
#include "manifest.hpp"

#include "c64_callbacks.hpp"
#include "c64_manifests.hpp"
#include "c64_system.hpp" // c64_config

#include "c64_cartridge.hpp"
#include "c64_pla.hpp"
#include "cia_6526.hpp"
#include "m6510.hpp"
#include "sid_6581.hpp"
#include "vic_ii_6569.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

TEST_CASE("c64 manifest PLA banking tracks the 6510 $01 port", "[c64][manifest][banking]") {
    using namespace mnemos::manifests;
    namespace c64 = mnemos::manifests::c64;

    // Force-link the chip TUs so their static-init register_factory runs (the
    // linker drops a static-lib .obj nothing references). 6510/PLA/cartridge are
    // pulled in below; touch the rest. (Production whole-archive-links the set.)
    [[maybe_unused]] const mnemos::chips::video::vic_ii_6569 force_vic{};
    [[maybe_unused]] const mnemos::chips::audio::sid_6581 force_sid{};
    [[maybe_unused]] const mnemos::chips::bus_controller::cia_6526 force_cia{};

    const auto parsed = parse_manifest(c64::manifest_toml(c64::c64_config::region::ntsc));
    REQUIRE(parsed.ok());

    // Distinguishable ROM fills so a read reveals which region won.
    const std::vector<std::uint8_t> basic(0x2000U, 0xB1U);
    const std::vector<std::uint8_t> kernal(0x2000U, 0xE1U);
    const std::vector<std::uint8_t> chargen(0x1000U, 0xC1U);
    const auto roms = [&](std::string_view f) -> std::optional<std::vector<std::uint8_t>> {
        if (f == "basic.bin") {
            return basic;
        }
        if (f == "kernal.bin") {
            return kernal;
        }
        if (f == "chargen.bin") {
            return chargen;
        }
        return std::nullopt;
    };

    c64::c64_callbacks_state state;
    mnemos::chips::mapper::c64_cartridge cart; // empty -> /GAME, /EXROM float high
    state.cart = &cart;
    auto preds = c64::make_c64_overlay_predicates(state);

    auto built = build_system(*parsed.value, roms, {}, {}, {}, preds);
    for (const auto& d : built.errors) {
        UNSCOPED_INFO("build error: " << d.message);
    }
    REQUIRE(built.ok());
    state.cpu = dynamic_cast<mnemos::chips::cpu::m6510*>(built.value->chip("cpu"));
    state.pla = dynamic_cast<mnemos::chips::mapper::c64_pla*>(built.value->chip("pla"));
    REQUIRE(state.cpu != nullptr);
    REQUIRE(state.pla != nullptr);
    auto* bus = built.value->bus("main");
    REQUIRE(bus != nullptr);

    // Set the 6510 I/O port (standard C64 DDR = 0x2F makes bits 0-2 outputs, so
    // LORAM/HIRAM/CHAREN are driven by the written value).
    const auto set_port = [&](std::uint8_t value) {
        state.cpu->write(0x0000U, 0x2FU);
        state.cpu->write(0x0001U, value);
    };

    // All banks in (LORAM=HIRAM=CHAREN=1): BASIC @ $A000, KERNAL @ $E000, I/O @ $D000.
    set_port(0x37U);
    CHECK(bus->read8(0xA000U) == 0xB1U); // BASIC ROM
    CHECK(bus->read8(0xE000U) == 0xE1U); // KERNAL ROM

    // LORAM=HIRAM=CHAREN=0: $A000 and $E000 fall through to RAM.
    set_port(0x30U);
    CHECK(bus->read8(0xA000U) == 0x00U);
    CHECK(bus->read8(0xE000U) == 0x00U);

    // CHAREN=0, HIRAM/LORAM=1 (port 0x33): $D000 shows CHARGEN, not I/O.
    set_port(0x33U);
    CHECK(bus->read8(0xD000U) == 0xC1U); // CHARGEN ROM

    // ROM overlays shadow reads only -- a write at $A000 lands in the RAM beneath.
    set_port(0x37U);
    bus->write8(0xA000U, 0x5AU);
    set_port(0x30U);
    CHECK(bus->read8(0xA000U) == 0x5AU);
}
