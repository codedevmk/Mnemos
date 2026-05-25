#include <mnemos/topology/bus.hpp>

#include <algorithm>
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

    void bus::insert_region(region r) {
        const auto pos = std::ranges::upper_bound(regions_, r.start, {}, &region::start);
        regions_.insert(pos, std::move(r));
    }

    void bus::map_ram(std::uint32_t start, std::span<std::uint8_t> storage) {
        region r;
        r.start = start;
        r.end = static_cast<std::uint32_t>(start + storage.size() - 1U);
        r.backing = kind::ram;
        r.ram = storage;
        insert_region(std::move(r));
    }

    void bus::map_rom(std::uint32_t start, std::span<const std::uint8_t> storage) {
        region r;
        r.start = start;
        r.end = static_cast<std::uint32_t>(start + storage.size() - 1U);
        r.backing = kind::rom;
        r.rom = storage;
        insert_region(std::move(r));
    }

    void bus::map_mmio(std::uint32_t start, std::uint32_t size, read_handler on_read,
                       write_handler on_write) {
        region r;
        r.start = start;
        r.end = start + size - 1U;
        r.backing = kind::mmio;
        r.on_read = std::move(on_read);
        r.on_write = std::move(on_write);
        insert_region(std::move(r));
    }

    const bus::region* bus::resolve(std::uint32_t address) const noexcept {
        // First region whose start is greater than the address; step back to the
        // candidate whose range may contain it.
        const auto it = std::ranges::upper_bound(regions_, address, {}, &region::start);
        if (it == regions_.begin()) {
            return nullptr;
        }
        const region& candidate = *(it - 1);
        if (address <= candidate.end) {
            return &candidate;
        }
        return nullptr;
    }

    std::uint8_t bus::read8(std::uint32_t address) {
        const std::uint32_t addr = address & address_mask_;
        const region* r = resolve(addr);
        if (r == nullptr) {
            return 0xFFU; // open bus
        }
        switch (r->backing) {
        case kind::ram:
            return r->ram[addr - r->start];
        case kind::rom:
            return r->rom[addr - r->start];
        case kind::mmio:
            return r->on_read ? r->on_read(addr) : 0xFFU;
        }
        return 0xFFU;
    }

    void bus::write8(std::uint32_t address, std::uint8_t value) {
        const std::uint32_t addr = address & address_mask_;
        const region* r = resolve(addr);
        if (r == nullptr) {
            return; // unmapped write dropped
        }
        switch (r->backing) {
        case kind::ram:
            r->ram[addr - r->start] = value;
            return;
        case kind::rom:
            return; // ROM ignores writes
        case kind::mmio:
            if (r->on_write) {
                r->on_write(addr, value);
            }
            return;
        }
    }

} // namespace mnemos::topology
