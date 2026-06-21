#include "genesis_runtime.hpp"

#include "gated_chip.hpp"        // mnemos::manifests::gated_chip (scheduler wrappers)
#include "genesis_manifests.hpp" // manifest_toml
#include "genesis_region.hpp"    // parse_market (cart territory for $A10001)
#include "manifest.hpp"          // parse_manifest

#include "mk1653.hpp" // default controller-port peripheral (6-button pad)

#include <cstdio>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

namespace mnemos::manifests::genesis {

    namespace {

        // Scheduler-view entry for a chip id: the gated_chip wrapper if the chip
        // was gated by a [[gate]], else the chip itself. build_system replaces
        // gated chips in system_graph::chips with the wrapper, while chip_by_id
        // keeps resolving to the inner chip.
        [[nodiscard]] chips::ichip* scheduler_entry(const system_graph& graph,
                                                    std::string_view id) {
            chips::ichip* inner = graph.chip(id);
            for (const auto& up : graph.chips) {
                chips::ichip* c = up.get();
                if (c == inner) {
                    return c;
                }
                // NB: build_system wraps with the COMMON manifests::gated_chip,
                // not the genesis-local gated_chip in genesis_system.hpp (which
                // assemble_genesis still uses and which shadows the name here).
                if (auto* g = dynamic_cast<mnemos::manifests::gated_chip*>(c);
                    g != nullptr && &g->inner() == inner) {
                    return c;
                }
            }
            return inner;
        }

    } // namespace

    std::unique_ptr<genesis_runtime> build_genesis_runtime(std::vector<std::uint8_t> rom,
                                                           const genesis_config& config) {
        auto rt = std::make_unique<genesis_runtime>();
        rt->rom = std::move(rom);

        // Host glue: closures capture &rt->state (stable -- rt is heap-allocated).
        auto tables = make_genesis_host_tables(rt->state);

        // $A10001 version: bit7 = export (overseas), bit6 = PAL, bit5 = no
        // expansion unit. The export bit is DOMESTIC (0) only when the cart's
        // market is market::japan -- the region parser maps both the Japanese
        // 'J' and Korean 'K' header codes there; US/Europe/multi-region read as
        // overseas. Hardcoding bit7=1 made domestic carts read $A0 instead of
        // $20, forking region-gated boot code (asset/tileset selection, H32/H40
        // mode) -> wrong VRAM/resolution. Set before any I/O read (matches assemble).
        const bool pal = config.video_region == mnemos::video_region::pal;
        const bool domestic = parse_market(rt->rom) == mnemos::market::japan;
        rt->state.version_register =
            static_cast<std::uint8_t>((domestic ? 0x00U : 0x80U) | (pal ? 0x40U : 0x00U) | 0x20U);

        // The manifest's cartridge region (backing="rom" file="cart") pulls the
        // cart bytes from here; build_system keeps its own copy in graph.memory.
        const auto roms = [&rt](std::string_view file) -> std::optional<std::vector<std::uint8_t>> {
            if (file == "cart") {
                return rt->rom;
            }
            return std::nullopt;
        };

        const auto parsed = parse_manifest(manifest_toml(config.video_region));
        if (!parsed.ok()) {
            for (const auto& d : parsed.errors) {
                std::fprintf(stderr, "[genesis-manifest parse] %s:%u:%u: %s\n", d.source.c_str(),
                             d.line, d.column, d.message.c_str());
            }
            std::fflush(stderr);
            return rt;
        }
        auto built = build_system(*parsed.value, roms, tables.callbacks, tables.predicates,
                                  tables.mmio_factories);
        if (!built.ok()) {
            for (const auto& d : built.errors) {
                std::fprintf(stderr, "[genesis-manifest build] %s:%u:%u: %s\n", d.source.c_str(),
                             d.line, d.column, d.message.c_str());
            }
            std::fflush(stderr);
            return rt;
        }
        rt->graph = std::move(*built.value);

        // Wire chip + bus pointers from the constructed graph into the state the
        // callbacks already close over.
        rt->state.cpu = dynamic_cast<chips::cpu::m68000*>(rt->graph.chip("cpu"));
        rt->state.z80 = dynamic_cast<chips::cpu::z80*>(rt->graph.chip("z80"));
        rt->state.vdp = dynamic_cast<chips::video::genesis_vdp*>(rt->graph.chip("video"));
        rt->state.fm = dynamic_cast<chips::audio::ym2612*>(rt->graph.chip("fm"));
        rt->state.psg = dynamic_cast<chips::audio::sn76489*>(rt->graph.chip("psg"));
        rt->state.main_bus = rt->graph.bus("main");
        rt->state.z80_bus = rt->graph.bus("z80");

        // Cartridge battery-RAM (SRAM): same wiring assemble_genesis uses, so the
        // two paths stay byte-identical (see wire_cart_sram).
        wire_cart_sram(*rt->state.main_bus, rt->sram, rt->rom);
        // Serial EEPROM carts (Acclaim et al.): a priority-2 region over the port,
        // above any flat SRAM the header may also declare.
        wire_cart_eeprom(*rt->state.main_bus, rt->eeprom, rt->rom);
        // >4 MiB ROMs: page the upper banks into the cartridge window ($A130F3-FF).
        wire_cart_banking(*rt->state.main_bus, rt->banking, rt->rom);
        // Lock-on inserted cartridge: `rt->rom` above is the boot-master base
        // (mapped at $000000 by the manifest); this maps the inserted game at
        // $300000 and takes over $A130F1 (composing with the SRAM latch). No-op
        // for a single cart. Mirrors assemble_genesis so the two paths match.
        rt->inserted_rom = config.inserted_rom;
        wire_cart_lockon(*rt->state.main_bus, rt->lockon, rt->inserted_rom, &rt->sram);

        // Reset the CPUs now the buses (with the cart ROM mapped) are wired,
        // mirroring assemble_genesis: the 68000 loads SSP/PC from the cart's
        // reset vectors, and the Z80 powers on (held idle by the z80_running gate
        // until the 68K releases its RESET via $A11200). build_system does NOT
        // reset chips, and the 68000 ctor can't read vectors with no bus
        // attached, so without this the 68000 would start at PC=0 and run garbage.
        // Resetting after configure() is safe -- the irq_ack/tas/z80_bus_latency
        // settings survive reset (assemble_genesis installs them pre-reset too).
        rt->state.z80->reset(chips::reset_kind::power_on);
        rt->state.cpu->reset(chips::reset_kind::power_on);

        // Resolve the gated scheduler wrappers for the two CPUs.
        rt->cpu_sched = scheduler_entry(rt->graph, "cpu");
        rt->z80_sched = scheduler_entry(rt->graph, "z80");

        // Default-plug MK-1653 6-button pads into both sockets (matches
        // assemble_genesis). Adapters can swap them after build.
        rt->state.ports[0] = std::make_unique<peripheral::input::mk1653>();
        rt->state.ports[1] = std::make_unique<peripheral::input::mk1653>();

        return rt;
    }

    namespace {
        // Layout guard for the system-latch chunk. Independent of (but identical
        // in value + field order to) genesis_system's marker, so a snapshot taken
        // through either assembly path describes the same bytes. v2 added the
        // lock-on window-select latch.
        constexpr std::uint32_t genesis_runtime_state_version = 2U;
    } // namespace

    void genesis_runtime::save_state(chips::state_writer& writer) const {
        writer.u32(genesis_runtime_state_version);
        writer.u8(state.version_register);
        writer.bytes(state.io_regs);
        writer.u8(state.vdp_write_high);
        writer.u8(state.vdp_read_low);
        writer.boolean(state.z80_bus_requested);
        writer.boolean(state.z80_reset_released);
        writer.u16(state.z80_bank);
        // Cartridge mapper control latches (the bytes/SRAM contents ride the
        // machine-save memory chunks; only the routing-control state is here).
        writer.boolean(sram.enabled);
        writer.boolean(sram.write_protect);
        writer.boolean(banking.active);
        writer.bytes(banking.bank);
        writer.boolean(eeprom.scl);
        writer.boolean(eeprom.sda);
        writer.boolean(lockon.patch_window);
        // z80_running is derived (reset released AND bus not requested), so it is
        // reconstructed on load rather than serialized.
    }

    void genesis_runtime::load_state(chips::state_reader& reader) {
        if (reader.u32() != genesis_runtime_state_version) {
            reader.fail(); // unknown layout; leave the system at its assembled state
            return;
        }
        state.version_register = reader.u8();
        reader.bytes(state.io_regs);
        state.vdp_write_high = reader.u8();
        state.vdp_read_low = reader.u8();
        state.z80_bus_requested = reader.boolean();
        state.z80_reset_released = reader.boolean();
        state.z80_bank = reader.u16();
        sram.enabled = reader.boolean();
        sram.write_protect = reader.boolean();
        banking.active = reader.boolean();
        reader.bytes(banking.bank);
        eeprom.scl = reader.boolean();
        eeprom.sda = reader.boolean();
        lockon.patch_window = reader.boolean();
        // Reconstruct the derived Z80 run gate (the gated_chip reads it live).
        state.z80_running = state.z80_reset_released && !state.z80_bus_requested;
    }

} // namespace mnemos::manifests::genesis
