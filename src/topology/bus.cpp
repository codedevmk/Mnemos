#include "bus.hpp"

#include <utility>

namespace mnemos::topology {

    namespace {
        [[nodiscard]] std::uint32_t mask_for(unsigned address_bits) {
            if (address_bits == 0U || address_bits >= 32U) {
                return 0xFFFFFFFFU;
            }
            return (1U << address_bits) - 1U;
        }
    } // namespace

    bus::bus(unsigned address_bits, endianness endian)
        : address_bits_(address_bits), endian_(endian), address_mask_(mask_for(address_bits)) {}

    void bus::map_ram(std::uint32_t start, std::span<std::uint8_t> storage, int priority,
                      active_predicate active) {
        region r;
        r.start = start;
        r.end = static_cast<std::uint32_t>(start + storage.size() - 1U);
        r.backing = kind::ram;
        r.priority = priority;
        r.ram = storage;
        r.active = std::move(active);
        regions_.push_back(std::move(r));
    }

    void bus::map_rom(std::uint32_t start, std::span<const std::uint8_t> storage, int priority,
                      active_predicate active) {
        region r;
        r.start = start;
        r.end = static_cast<std::uint32_t>(start + storage.size() - 1U);
        r.backing = kind::rom;
        r.priority = priority;
        r.rom = storage;
        r.active = std::move(active);
        regions_.push_back(std::move(r));
    }

    void bus::map_mmio(std::uint32_t start, std::uint32_t size, read_handler on_read,
                       write_handler on_write, int priority, active_predicate active) {
        region r;
        r.start = start;
        r.end = start + size - 1U;
        r.backing = kind::mmio;
        r.priority = priority;
        r.on_read = std::move(on_read);
        r.on_write = std::move(on_write);
        r.active = std::move(active);
        regions_.push_back(std::move(r));
    }

    const bus::region* bus::resolve(std::uint32_t address, bool is_write) const noexcept {
        // Highest-priority region that covers the address and is active for this
        // access. (Region counts are tiny; a linear scan is fine for v0.1. Cached
        // per-chip resolution is a later optimisation.)
        const region* best = nullptr;
        for (const region& r : regions_) {
            if (address < r.start || address > r.end) {
                continue;
            }
            if (r.active && !r.active(address, is_write)) {
                continue;
            }
            if (best == nullptr || r.priority > best->priority) {
                best = &r;
            }
        }
        return best;
    }

    std::uint8_t bus::read8(std::uint32_t address) {
        const std::uint32_t addr = address & address_mask_;
        const region* r = resolve(addr, false);
        std::uint8_t value = 0xFFU; // open bus default
        if (r != nullptr) {
            switch (r->backing) {
            case kind::ram:
                value = r->ram[addr - r->start];
                break;
            case kind::rom:
                value = r->rom[addr - r->start];
                break;
            case kind::mmio:
                value = r->on_read ? r->on_read(addr) : 0xFFU;
                break;
            }
        }
        if (observer_) {
            observer_({.address = addr, .value = value, .write = false});
        }
        return value;
    }

    void bus::write8(std::uint32_t address, std::uint8_t value) {
        const std::uint32_t addr = address & address_mask_;
        const region* r = resolve(addr, true);
        if (r != nullptr) {
            switch (r->backing) {
            case kind::ram:
                r->ram[addr - r->start] = value;
                break;
            case kind::rom:
                break; // ROM ignores writes
            case kind::mmio:
                if (r->on_write) {
                    r->on_write(addr, value);
                }
                break;
            }
        }
        // Report the attempted write (even if dropped/ROM) so watchpoints see it.
        if (observer_) {
            observer_({.address = addr, .value = value, .write = true});
        }
    }

} // namespace mnemos::topology
