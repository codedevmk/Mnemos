#include "genesis_runtime.hpp"

#include "gated_chip.hpp"        // mnemos::manifests::gated_chip (scheduler wrappers)
#include "genesis_manifests.hpp" // manifest_toml
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

        // $A10001 version: bit7 = export, bit6 = PAL, bit5 = no expansion unit.
        // Set before any I/O-controller read (matches assemble_genesis).
        const bool pal = config.video_region == mnemos::video_region::pal;
        rt->state.version_register =
            static_cast<std::uint8_t>(0x80U | (pal ? 0x40U : 0x00U) | 0x20U);

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

        // Cartridge battery-RAM (SRAM). If the header declares it, back it with a
        // byte buffer and map it on the main bus above the ROM, gated by the
        // $A130F1 enable latch (so disabling it falls through to the ROM region).
        rt->sram.info = parse_cart_sram(rt->rom);
        if (rt->sram.info) {
            const cart_sram info = *rt->sram.info;
            rt->sram.data.assign(info.byte_count(), 0xFFU);
            auto* s = &rt->sram;

            // Header byte address -> backing-buffer index, or ~0 when the address
            // is outside the region or on the unpopulated byte lane.
            const auto index = [info](std::uint32_t addr) -> std::size_t {
                if (addr < info.start || addr > info.end) {
                    return ~std::size_t{0};
                }
                const std::uint32_t off = addr - info.start;
                switch (info.map) {
                case cart_sram::mapping::word:
                    return off;
                case cart_sram::mapping::odd_byte:
                    return (addr & 1U) != 0U ? off / 2U : ~std::size_t{0};
                case cart_sram::mapping::even_byte:
                    return (addr & 1U) == 0U ? off / 2U : ~std::size_t{0};
                }
                return ~std::size_t{0};
            };

            rt->state.main_bus->map_mmio(
                info.start, info.end - info.start + 1U,
                [s, index](std::uint32_t addr) -> std::uint8_t {
                    const std::size_t i = index(addr);
                    return i < s->data.size() ? s->data[i] : 0xFFU;
                },
                [s, index](std::uint32_t addr, std::uint8_t v) {
                    const std::size_t i = index(addr);
                    if (i < s->data.size()) {
                        s->data[i] = v;
                    }
                },
                /*priority=*/1, [s](std::uint32_t, bool) { return s->enabled; });

            // $A130F1: the SRAM enable/bank-control latch (bit 0 = SRAM mapped).
            rt->state.main_bus->map_mmio(
                0xA130F1U, 1U, [s](std::uint32_t) -> std::uint8_t { return s->enabled ? 1U : 0U; },
                [s](std::uint32_t, std::uint8_t v) { s->enabled = (v & 1U) != 0U; }, 1);
        }

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

} // namespace mnemos::manifests::genesis
