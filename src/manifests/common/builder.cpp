#include "builder.hpp"

#include "chip_registry.hpp"
#include "sha256.hpp"

#include <span>
#include <utility>

namespace mnemos::manifests {

    chips::i_chip* system_graph::chip(std::string_view id) const {
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

    build_result build_system(const manifest& m, const rom_provider& roms) {
        build_result out;
        system_graph graph;
        auto& errs = out.errors;
        auto report = [&](std::string message) {
            errs.push_back({std::move(message), m.id, 0U, 0U});
        };

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
                case region_backing::mapper:
                    // MMIO windows are bound from the chip's mmio_range below;
                    // standalone mapper regions arrive with cartridge support.
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
            chips::i_chip* raw = chip.get();
            graph.chip_by_id.emplace(cd.id, raw);
            graph.chips.push_back(std::move(chip));

            topology::bus* attached = graph.bus(cd.attached_bus);
            if (attached == nullptr) {
                report("chip '" + cd.id + "': attached_bus '" + cd.attached_bus + "' not found");
                continue;
            }

            if (auto* cpu = dynamic_cast<chips::i_cpu*>(raw)) {
                cpu->attach_bus(*attached);
            }

            if (cd.mmio_range) {
                auto* mmio = dynamic_cast<chips::i_mmio*>(raw);
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

        if (errs.empty()) {
            out.value = std::move(graph);
        }
        return out;
    }

} // namespace mnemos::manifests
