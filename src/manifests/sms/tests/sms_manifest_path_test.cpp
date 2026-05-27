// Smoke test for the SMS manifest-path migration (B.1.5).
//
// Loads sms.ntsc.toml into the builder, applies the host-side
// sms_callbacks bundle, populates chip pointers from the constructed
// system_graph, attaches a synthetic ROM, and steps the Z80 a few
// instructions. Verifies the full pipeline (parse_manifest +
// build_system + chip configure + callback installation + manifest
// region wiring) produces a runnable system equivalent to what
// assemble_sms produces by hand. End-to-end behavioural parity with
// the hand-written path is the responsibility of the eventual
// adapter cutover (B.1.5+).

#include "builder.hpp"
#include "manifest.hpp"
#include "sms_callbacks.hpp"

#include "sms_mapper.hpp"
#include "sms_vdp.hpp"
#include "sn76489.hpp"
#include "z80.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

    // Copy of sms.ntsc.toml content for in-test parsing. Drifts if the
    // on-disk file does -- B.1.5+ will replace this with a load_manifest_file
    // path that reads the canonical file once the adapter wires it in.
    constexpr std::string_view kSmsNtscManifest = R"toml(
[manifest]
schema       = "mnemos-manifest/1"
id           = "sega.sms.ntsc"
display_name = "Sega Master System (NTSC)"
family       = "sega"
revision     = 1

[clock]
master_hz               = 3579545
master_to_cpu_divider   = 1
master_to_video_divider = 1

[[chip]]
id           = "cpu"
type         = "zilog.z80"
attached_bus = "main"
[chip.config]
port_in_callback  = "sms.z80_port_in"
port_out_callback = "sms.z80_port_out"

[[chip]]
id           = "video"
type         = "sega.sms_vdp"
attached_bus = "main"
[chip.config]
irq_callback = "sms.vdp_irq"

[[chip]]
id           = "audio"
type         = "ti.sn76489"
attached_bus = "main"

[[chip]]
id           = "mapper"
type         = "sega.sms_mapper"
attached_bus = "main"

[[bus]]
id           = "main"
address_bits = 16
endianness   = "little"

[[bus.region]]
name      = "cartridge"
range     = "0x0000-0xBFFF"
backing   = "mapper"
mapper_id = "mapper"

[[bus.region]]
name    = "work_ram"
range   = "0xC000-0xFFFF"
backing = "ram"
size    = 8192

[[mmio_block]]
name         = "sms.mapper_register_overlay"
attached_bus = "main"
range        = "0xFFFC-0xFFFF"
)toml";

} // namespace

TEST_CASE("SMS manifest path builds a runnable system", "[sms][manifest][smoke]") {
    using namespace mnemos::manifests;

    const auto parsed = parse_manifest(kSmsNtscManifest);
    REQUIRE(parsed.ok());

    // Host state captures chip pointers + non-chip state. Closures inside
    // the returned tables capture &state, so it must outlive the system.
    mnemos::manifests::sms::sms_callbacks_state state;
    auto tables = mnemos::manifests::sms::make_sms_host_tables(state);

    // The SMS manifest declares no rom-backed regions (cart is supplied via
    // the mapper, not a manifest [[bus.region]] backing="rom"). The
    // rom_provider here is never consulted.
    const auto no_roms =
        [](std::string_view) -> std::optional<std::vector<std::uint8_t>> {
        return std::nullopt;
    };

    auto built = build_system(*parsed.value, no_roms, tables.callbacks, {},
                              tables.mmio_factories);
    REQUIRE(built.ok());

    // Wire chip pointers from the constructed system_graph. After this,
    // every closure captured by &state can safely deref the chip slots.
    state.cpu = dynamic_cast<mnemos::chips::cpu::z80*>(built.value->chip("cpu"));
    state.vdp = dynamic_cast<mnemos::chips::video::sms_vdp*>(built.value->chip("video"));
    state.psg =
        dynamic_cast<mnemos::chips::audio::sn76489*>(built.value->chip("audio"));
    state.mapper =
        dynamic_cast<mnemos::chips::mapper::sms_mapper*>(built.value->chip("mapper"));
    REQUIRE(state.cpu != nullptr);
    REQUIRE(state.vdp != nullptr);
    REQUIRE(state.psg != nullptr);
    REQUIRE(state.mapper != nullptr);

    // Synthetic 32 KiB cart -- all NOPs (0x00) so the Z80 walks through the
    // address space cleanly. The host owns the buffer; attach_rom takes a
    // borrowed span the mapper holds until reset / re-attach.
    const std::vector<std::uint8_t> rom(0x8000U, 0x00U);
    state.mapper->attach_rom(std::span<const std::uint8_t>(rom));

    state.cpu->reset(mnemos::chips::reset_kind::power_on);

    // Step the Z80 a few instructions. NOP costs 4 cycles each; 32 NOPs
    // should land elapsed_cycles between 100 and 200.
    for (int i = 0; i < 32; ++i) {
        state.cpu->step_instruction();
    }
    CHECK(state.cpu->elapsed_cycles() >= 128U);

    // The work-RAM mirror should let a write at $C123 reappear at $E123.
    auto* bus = built.value->bus("main");
    REQUIRE(bus != nullptr);
    bus->write8(0xC123U, 0x5AU);
    CHECK(bus->read8(0xE123U) == 0x5AU);

    // The mapper register overlay at $FFFC-$FFFF should drive the mapper
    // through write_register; checking through the mapper's public state.
    // The Sega mapper's $FFFD selects the slot-0 page.
    bus->write8(0xFFFDU, 0x02U);
    CHECK(state.mapper->page(0) == 0x02U);
}
