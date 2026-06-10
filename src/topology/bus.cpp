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
        if (storage.empty()) {
            return; // an empty span would underflow `end` and claim the whole space
        }
        region r;
        r.start = start;
        r.end = static_cast<std::uint32_t>(start + storage.size() - 1U);
        r.backing = kind::ram;
        r.priority = priority;
        r.ram = storage;
        r.active = std::move(active);
        regions_.push_back(std::move(r));
        invalidate_fast_path(); // mapping changes; regions_ may also reallocate
    }

    void bus::map_rom(std::uint32_t start, std::span<const std::uint8_t> storage, int priority,
                      active_predicate active) {
        if (storage.empty()) {
            return; // an empty span would underflow `end` and claim the whole space
        }
        region r;
        r.start = start;
        r.end = static_cast<std::uint32_t>(start + storage.size() - 1U);
        r.backing = kind::rom;
        r.priority = priority;
        r.rom = storage;
        r.active = std::move(active);
        regions_.push_back(std::move(r));
        invalidate_fast_path();
    }

    void bus::map_mmio(std::uint32_t start, std::uint32_t size, read_handler on_read,
                       write_handler on_write, int priority, active_predicate active) {
        if (size == 0U) {
            return; // a zero size would underflow `end` and claim the whole space
        }
        region r;
        r.start = start;
        r.end = start + size - 1U;
        r.backing = kind::mmio;
        r.priority = priority;
        r.on_read = std::move(on_read);
        r.on_write = std::move(on_write);
        r.active = std::move(active);
        regions_.push_back(std::move(r));
        invalidate_fast_path();
    }

    void bus::retarget_ram(std::uint32_t start, std::span<std::uint8_t> storage) noexcept {
        for (region& r : regions_) {
            if (r.backing == kind::ram && r.start == start && r.ram.size() == storage.size()) {
                r.ram = storage;
            }
        }
        // The fast path may cache a span into the old storage.
        invalidate_fast_path();
    }

    void bus::retarget_rom(std::uint32_t start, std::span<const std::uint8_t> storage) noexcept {
        for (region& r : regions_) {
            if (r.backing == kind::rom && r.start == start && r.rom.size() == storage.size()) {
                r.rom = storage;
            }
        }
        invalidate_fast_path(); // cached spans may point into the old storage
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

    void bus::update_fast_path(std::uint32_t address, const region* winner) noexcept {
        // Only a predicate-free RAM/ROM winner can be served without a resolve.
        if (winner == nullptr || winner->backing == kind::mmio || winner->active) {
            return;
        }
        // Narrow the winner's span to the part around `address` where no other
        // region can outrank it. A region outranks on strictly higher priority,
        // or on equal priority when it was mapped earlier (resolve keeps the
        // first of equals). Predicate-gated regions count as if active -- they
        // may become active at any time.
        std::uint32_t lo = winner->start;
        std::uint32_t hi = winner->end;
        for (const region& o : regions_) {
            if (&o == winner || o.end < lo || o.start > hi) {
                continue;
            }
            const bool could_outrank =
                o.priority > winner->priority || (o.priority == winner->priority && &o < winner);
            if (!could_outrank) {
                continue;
            }
            if (address >= o.start && address <= o.end) {
                return; // a (currently inactive) rival covers this very address
            }
            if (o.end < address) {
                lo = o.end + 1U;
            } else {
                hi = o.start - 1U;
            }
        }
        for (std::size_t i = fast_.size() - 1U; i > 0U; --i) {
            fast_[i] = fast_[i - 1U];
        }
        fast_[0] = {.start = lo, .end = hi, .r = winner};
    }

    std::uint8_t bus::read8(std::uint32_t address) {
        const std::uint32_t addr = address & address_mask_;
        std::uint8_t value = 0xFFU; // open bus default
        for (std::size_t i = 0; i < fast_.size(); ++i) {
            const fast_span& f = fast_[i];
            if (addr < f.start || addr > f.end) {
                continue;
            }
            const region* r = f.r;
            value = r->backing == kind::ram ? r->ram[addr - r->start] : r->rom[addr - r->start];
            if (i != 0U) {
                std::swap(fast_[0], fast_[i]);
            }
            if (observer_) {
                observer_({.address = addr, .value = value, .write = false});
            }
            return value;
        }
        const region* r = resolve(addr, false);
        if (r != nullptr) {
            if (r->backing == kind::mmio) {
                // Copy the handler before invoking it: a handler may remap this
                // bus (the 32X ADEN write maps a BIOS overlay), which can
                // reallocate regions_ -- moving the stored std::function out
                // from under its own call and dangling `r`.
                const read_handler handler = r->on_read;
                r = nullptr;
                value = handler ? handler(addr) : 0xFFU;
            } else {
                value = r->backing == kind::ram ? r->ram[addr - r->start]
                                                : r->rom[addr - r->start];
                update_fast_path(addr, r);
            }
        }
        if (observer_) {
            observer_({.address = addr, .value = value, .write = false});
        }
        return value;
    }

    bool bus::direct_read_span(std::uint32_t address, chips::ibus::direct_span& out) {
        if (observer_) {
            return false; // watchpoints must observe every access
        }
        const std::uint32_t addr = address & address_mask_;
        const region* r = resolve(addr, false);
        if (r == nullptr || r->backing == kind::mmio || r->active) {
            return false;
        }
        update_fast_path(addr, r);
        const fast_span& f = fast_[0];
        if (f.r != r || addr < f.start || addr > f.end) {
            return false; // a rival region covers this very address
        }
        const std::uint8_t* base = r->backing == kind::ram ? r->ram.data() : r->rom.data();
        out = {.data = base + (f.start - r->start), .start = f.start, .end = f.end};
        return true;
    }

    std::uint16_t bus::read16_be(std::uint32_t address) {
        const std::uint32_t addr = address & address_mask_;
        if (!observer_ && addr + 1U > addr) {
            for (std::size_t i = 0; i < fast_.size(); ++i) {
                const fast_span& f = fast_[i];
                if (addr < f.start || addr + 1U > f.end) {
                    continue;
                }
                const region* r = f.r;
                const std::size_t off = addr - r->start;
                const std::uint8_t* p = r->backing == kind::ram ? r->ram.data() : r->rom.data();
                if (i != 0U) {
                    std::swap(fast_[0], fast_[i]);
                }
                return static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[off]) << 8U) |
                                                  p[off + 1U]);
            }
        }
        return ibus::read16_be(address);
    }

    std::uint32_t bus::read32_be(std::uint32_t address) {
        const std::uint32_t addr = address & address_mask_;
        if (!observer_ && addr + 3U > addr) {
            for (std::size_t i = 0; i < fast_.size(); ++i) {
                const fast_span& f = fast_[i];
                if (addr < f.start || addr + 3U > f.end) {
                    continue;
                }
                const region* r = f.r;
                const std::size_t off = addr - r->start;
                const std::uint8_t* p = r->backing == kind::ram ? r->ram.data() : r->rom.data();
                if (i != 0U) {
                    std::swap(fast_[0], fast_[i]);
                }
                return (static_cast<std::uint32_t>(p[off]) << 24U) |
                       (static_cast<std::uint32_t>(p[off + 1U]) << 16U) |
                       (static_cast<std::uint32_t>(p[off + 2U]) << 8U) | p[off + 3U];
            }
        }
        return ibus::read32_be(address);
    }

    void bus::write16_be(std::uint32_t address, std::uint16_t value) {
        const std::uint32_t addr = address & address_mask_;
        if (!observer_ && addr + 1U > addr) {
            for (std::size_t i = 0; i < fast_.size(); ++i) {
                const fast_span& f = fast_[i];
                if (addr < f.start || addr + 1U > f.end) {
                    continue;
                }
                const region* r = f.r;
                if (r->backing == kind::ram) {
                    const std::size_t off = addr - r->start;
                    r->ram[off] = static_cast<std::uint8_t>(value >> 8U);
                    r->ram[off + 1U] = static_cast<std::uint8_t>(value);
                } // ROM ignores writes
                if (i != 0U) {
                    std::swap(fast_[0], fast_[i]);
                }
                return;
            }
        }
        ibus::write16_be(address, value);
    }

    std::uint16_t bus::read16_le(std::uint32_t address) {
        const std::uint32_t addr = address & address_mask_;
        if (!observer_ && addr + 1U > addr) {
            for (std::size_t i = 0; i < fast_.size(); ++i) {
                const fast_span& f = fast_[i];
                if (addr < f.start || addr + 1U > f.end) {
                    continue;
                }
                const region* r = f.r;
                const std::size_t off = addr - r->start;
                const std::uint8_t* p = r->backing == kind::ram ? r->ram.data() : r->rom.data();
                if (i != 0U) {
                    std::swap(fast_[0], fast_[i]);
                }
                return static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[off]) |
                                                  (static_cast<std::uint16_t>(p[off + 1U]) << 8U));
            }
        }
        return ibus::read16_le(address);
    }

    void bus::write16_le(std::uint32_t address, std::uint16_t value) {
        const std::uint32_t addr = address & address_mask_;
        if (!observer_ && addr + 1U > addr) {
            for (std::size_t i = 0; i < fast_.size(); ++i) {
                const fast_span& f = fast_[i];
                if (addr < f.start || addr + 1U > f.end) {
                    continue;
                }
                const region* r = f.r;
                if (r->backing == kind::ram) {
                    const std::size_t off = addr - r->start;
                    r->ram[off] = static_cast<std::uint8_t>(value);
                    r->ram[off + 1U] = static_cast<std::uint8_t>(value >> 8U);
                } // ROM ignores writes
                if (i != 0U) {
                    std::swap(fast_[0], fast_[i]);
                }
                return;
            }
        }
        ibus::write16_le(address, value);
    }

    void bus::write32_be(std::uint32_t address, std::uint32_t value) {
        const std::uint32_t addr = address & address_mask_;
        if (!observer_ && addr + 3U > addr) {
            for (std::size_t i = 0; i < fast_.size(); ++i) {
                const fast_span& f = fast_[i];
                if (addr < f.start || addr + 3U > f.end) {
                    continue;
                }
                const region* r = f.r;
                if (r->backing == kind::ram) {
                    const std::size_t off = addr - r->start;
                    r->ram[off] = static_cast<std::uint8_t>(value >> 24U);
                    r->ram[off + 1U] = static_cast<std::uint8_t>(value >> 16U);
                    r->ram[off + 2U] = static_cast<std::uint8_t>(value >> 8U);
                    r->ram[off + 3U] = static_cast<std::uint8_t>(value);
                } // ROM ignores writes
                if (i != 0U) {
                    std::swap(fast_[0], fast_[i]);
                }
                return;
            }
        }
        ibus::write32_be(address, value);
    }

    void bus::write8(std::uint32_t address, std::uint8_t value) {
        const std::uint32_t addr = address & address_mask_;
        for (std::size_t i = 0; i < fast_.size(); ++i) {
            const fast_span& f = fast_[i];
            if (addr < f.start || addr > f.end) {
                continue;
            }
            const region* r = f.r;
            if (r->backing == kind::ram) {
                r->ram[addr - r->start] = value;
            } // ROM ignores writes
            if (i != 0U) {
                std::swap(fast_[0], fast_[i]);
            }
            if (observer_) {
                observer_({.address = addr, .value = value, .write = true});
            }
            return;
        }
        const region* r = resolve(addr, true);
        if (r != nullptr) {
            if (r->backing == kind::mmio) {
                // Copy the handler before invoking it: a handler may remap this
                // bus, which can reallocate regions_ -- moving the stored
                // std::function out from under its own call and dangling `r`.
                const write_handler handler = r->on_write;
                r = nullptr;
                if (handler) {
                    handler(addr, value);
                }
            } else {
                if (r->backing == kind::ram) {
                    r->ram[addr - r->start] = value;
                } // ROM ignores writes
                update_fast_path(addr, r);
            }
        }
        // Report the attempted write (even if dropped/ROM) so watchpoints see it.
        if (observer_) {
            observer_({.address = addr, .value = value, .write = true});
        }
    }

} // namespace mnemos::topology
