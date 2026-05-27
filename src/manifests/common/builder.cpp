#include "builder.hpp"

#include "chip_registry.hpp"
#include "gated_chip.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <span>
#include <utility>

namespace mnemos::manifests {

    chips::ichip* system_graph::chip(std::string_view id) const {
        const auto it = chip_by_id.find(std::string{id});
        return it != chip_by_id.end() ? it->second : nullptr;
    }

    topology::bus* system_graph::bus(std::string_view id) const {
        const auto it = bus_by_id.find(std::string{id});
        return it != bus_by_id.end() ? it->second : nullptr;
    }

    namespace {
        [[nodiscard]] topology::endianness to_topology_endian(endianness e) {
            return e == endianness::big ? topology::endianness::big : topology::endianness::little;
        }
    } // namespace

    build_result build_system(const manifest& m, const rom_provider& roms,
                              const callback_table& callbacks,
                              const predicate_table& predicates,
                              const mmio_factory_table& mmio_factories) {
        build_result out;
        system_graph graph;
        auto& errs = out.errors;
        auto report = [&](std::string message) {
            errs.push_back({std::move(message), m.id, 0U, 0U});
        };

        // Mapper-backed regions can't be wired in the bus loop because their
        // mapper chip hasn't been constructed yet. Collect them here and
        // resolve in a post-chip pass below.
        struct pending_mapper_region final {
            topology::bus* bus;
            std::uint32_t start;
            std::uint32_t size;
            std::string mapper_id;
            std::string region_name;
            bool overlay;
        };
        std::vector<pending_mapper_region> pending_mapper_regions;

        // Buses first, so chips can attach and MMIO windows can be mapped.
        for (const bus_decl& bd : m.buses) {
            auto b =
                std::make_unique<topology::bus>(bd.address_bits, to_topology_endian(bd.endian));
            for (const region_decl& rd : bd.regions) {
                const std::uint32_t span_size = rd.range.end - rd.range.start + 1U;
                switch (rd.backing) {
                case region_backing::ram: {
                    auto block = std::make_unique<std::vector<std::uint8_t>>(span_size, 0U);
                    b->map_ram(rd.range.start, std::span<std::uint8_t>(*block), 0);
                    graph.memory.push_back(std::move(block));
                    break;
                }
                case region_backing::rom: {
                    if (!rd.file) {
                        report("rom region '" + rd.name + "' has no file");
                        break;
                    }
                    const auto bytes = roms(*rd.file);
                    if (!bytes) {
                        report("rom region '" + rd.name + "': file not available: " + *rd.file);
                        break;
                    }
                    if (rd.sha256) {
                        const auto digest =
                            foundation::sha256(std::span<const std::uint8_t>(*bytes)).hex();
                        if (digest != *rd.sha256) {
                            report("rom region '" + rd.name + "': sha256 mismatch (got " + digest +
                                   ")");
                            break;
                        }
                    }
                    auto block = std::make_unique<std::vector<std::uint8_t>>(*bytes);
                    // Overlay ROMs shadow on reads only; writes fall through to the
                    // RAM beneath. Machine wirers refine the gating (e.g. the PLA).
                    topology::bus::active_predicate active;
                    if (rd.overlay) {
                        active = [](std::uint32_t, bool is_write) { return !is_write; };
                    }
                    b->map_rom(rd.range.start, std::span<const std::uint8_t>(*block),
                               rd.overlay ? 1 : 0, std::move(active));
                    graph.memory.push_back(std::move(block));
                    break;
                }
                case region_backing::mmio_chip:
                    // MMIO windows are bound from the chip's mmio_range below.
                    break;
                case region_backing::mapper:
                    // Mapper-backed regions need the mapper chip in hand;
                    // defer wiring until after chips are constructed.
                    if (rd.mapper_id) {
                        pending_mapper_regions.push_back(
                            {b.get(), rd.range.start, span_size, *rd.mapper_id,
                             rd.name, rd.overlay});
                    }
                    break;
                }
            }
            graph.bus_by_id.emplace(bd.id, b.get());
            graph.buses.push_back(std::move(b));
        }

        // Chips: instantiate by factory id, attach to buses, bind MMIO windows.
        for (const chip_decl& cd : m.chips) {
            auto chip = chips::create_chip(cd.type);
            if (chip == nullptr) {
                report("chip '" + cd.id + "': unknown or unregistered type '" + cd.type + "'");
                continue;
            }
            chips::ichip* raw = chip.get();
            // Apply per-chip config + host-supplied callback table BEFORE bus
            // attachment + reset. Chips ignore keys they don't recognize and
            // fall back to their built-in defaults for missing callbacks.
            raw->configure(cd.config, callbacks);
            graph.chip_by_id.emplace(cd.id, raw);
            graph.chips.push_back(std::move(chip));

            topology::bus* attached = graph.bus(cd.attached_bus);
            if (attached == nullptr) {
                report("chip '" + cd.id + "': attached_bus '" + cd.attached_bus + "' not found");
                continue;
            }

            if (auto* cpu = dynamic_cast<chips::icpu*>(raw)) {
                cpu->attach_bus(*attached);
            }

            if (cd.mmio_range) {
                auto* mmio = dynamic_cast<chips::immio*>(raw);
                if (mmio == nullptr) {
                    report("chip '" + cd.id + "' has an mmio_range but is not MMIO-capable");
                } else {
                    const std::uint32_t base = cd.mmio_range->start;
                    const std::uint32_t size = cd.mmio_range->end - base + 1U;
                    attached->map_mmio(
                        base, size,
                        [mmio, base](std::uint32_t address) {
                            return mmio->mmio_read(static_cast<std::uint16_t>(address - base));
                        },
                        [mmio, base](std::uint32_t address, std::uint8_t value) {
                            mmio->mmio_write(static_cast<std::uint16_t>(address - base), value);
                        },
                        2);
                }
            }
        }

        // Mapper-backed regions: now that chips exist, resolve each pending
        // mapper region to its imapper chip and wire the overlay
        // read/write/predicate trio onto its bus. Overlay regions get
        // priority 1 (above plain RAM); non-overlay mapper regions get
        // priority 2 (above ROM overlays + RAM but below MMIO chip
        // windows at priority 2 -- the bus's priority-resolution tie-
        // breaking handles the rest).
        for (const auto& p : pending_mapper_regions) {
            const auto chip_it = graph.chip_by_id.find(p.mapper_id);
            if (chip_it == graph.chip_by_id.end()) {
                report("mapper region '" + p.region_name + "' references unknown chip '" +
                       p.mapper_id + "'");
                continue;
            }
            auto* mapper = dynamic_cast<chips::imapper*>(chip_it->second);
            if (mapper == nullptr) {
                report("mapper region '" + p.region_name + "': chip '" + p.mapper_id +
                       "' is not an imapper");
                continue;
            }
            const int priority = p.overlay ? 1 : 2;
            p.bus->map_mmio(
                p.start, p.size,
                [mapper](std::uint32_t address) { return mapper->read_overlay(address); },
                [mapper](std::uint32_t address, std::uint8_t value) {
                    mapper->write_overlay(address, value);
                },
                priority,
                [mapper](std::uint32_t address, bool is_write) {
                    return mapper->overlay_active(address, is_write);
                });
        }

        // [[mmio_block]] entries: resolve each named factory through the
        // host-supplied mmio_factory_table and bind its returned handler pair
        // onto the named bus. Priority 2 matches chip-MMIO-window placement
        // (above RAM/ROM, below any higher-priority mapper overlays).
        for (const mmio_block_decl& mb : m.mmio_blocks) {
            const auto bus_it = graph.bus_by_id.find(mb.attached_bus);
            if (bus_it == graph.bus_by_id.end()) {
                report("mmio_block '" + mb.name + "': attached_bus '" + mb.attached_bus +
                       "' not found");
                continue;
            }
            const auto factory_it = mmio_factories.find(mb.name);
            if (factory_it == mmio_factories.end()) {
                report("mmio_block '" + mb.name +
                       "' has no host-registered factory");
                continue;
            }
            const std::uint32_t size = mb.range.end - mb.range.start + 1U;
            auto handlers = factory_it->second(mb.range.start, size);
            bus_it->second->map_mmio(mb.range.start, size,
                                     std::move(handlers.on_read),
                                     std::move(handlers.on_write), 2);
        }

        // Gates: wrap any chip named in a [[gate]] entry with gated_chip
        // controlled by the host-supplied named predicate. The original chip
        // pointer in chip_by_id stays valid (the wrapper takes ownership of
        // the chip's unique_ptr but the raw ichip* hasn't moved); the chips
        // vector entry is replaced with the wrapper so the scheduler ticks
        // through the gate.
        for (const gate_decl& gd : m.gates) {
            const auto chip_it = graph.chip_by_id.find(gd.chip_id);
            if (chip_it == graph.chip_by_id.end()) {
                report("gate '" + gd.chip_id + "' references unknown chip");
                continue;
            }
            const auto pred_it = predicates.find(gd.predicate);
            if (pred_it == predicates.end()) {
                report("gate '" + gd.chip_id + "' predicate '" + gd.predicate +
                       "' is not registered");
                continue;
            }
            chips::ichip* raw = chip_it->second;
            // Find the unique_ptr in graph.chips that owns raw.
            auto own_it = std::find_if(graph.chips.begin(), graph.chips.end(),
                                        [raw](const std::unique_ptr<chips::ichip>& p) {
                                            return p.get() == raw;
                                        });
            if (own_it == graph.chips.end()) {
                report("gate '" + gd.chip_id +
                       "': chip ownership not found (internal error)");
                continue;
            }
            // Steal ownership into the wrapper. We can't store the original
            // unique_ptr inside the wrapper directly (gated_chip holds a raw
            // pointer), so park the original alongside in graph.gated_originals
            // to keep the unique_ptr alive, then replace the original slot in
            // graph.chips with the wrapper.
            std::unique_ptr<chips::ichip> orig = std::move(*own_it);
            auto wrapper = std::make_unique<gated_chip>(*orig, pred_it->second);
            *own_it = std::move(wrapper);
            graph.gated_originals.push_back(std::move(orig));
        }

        if (errs.empty()) {
            out.value = std::move(graph);
        }
        return out;
    }

} // namespace mnemos::manifests
