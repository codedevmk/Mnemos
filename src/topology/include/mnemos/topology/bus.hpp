#pragma once

#include <mnemos/chips/common/bus.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::topology {

    enum class endianness : std::uint8_t { little, big };

    // A single completed bus access, delivered to an access observer. `value` is the
    // byte read or written; `write` distinguishes the two. Used by the tier-6
    // instrumentation layer for memory watchpoints (the bus itself stays oblivious).
    struct access_event final {
        std::uint32_t address{};
        std::uint8_t value{};
        bool write{};
    };

    // A typed address space (TDS §9): a width, an endianness, and a table of
    // regions backed by RAM, ROM, or an MMIO chip. Implements chips::i_bus so a
    // CPU attaches directly to it.
    //
    // Regions may overlap. Each carries a priority and an optional "active"
    // predicate evaluated per access; the highest-priority active region covering
    // an address wins. This models C64-style banking driven by the PLA: ROM
    // overlays are registered active on reads only (writes fall through to the RAM
    // underneath), I/O overlays active for both, all gated by the PLA decode the
    // system supplies through the predicate. With no overlapping regions and no
    // predicates this degenerates to a plain disjoint memory map.
    class bus final : public chips::i_bus {
      public:
        using read_handler = std::function<std::uint8_t(std::uint32_t address)>;
        using write_handler = std::function<void(std::uint32_t address, std::uint8_t value)>;
        // True when this region should handle the access at `address` (the system
        // typically consults a mapper/PLA decode here). Empty == always active.
        using active_predicate = std::function<bool(std::uint32_t address, bool is_write)>;

        bus() = default;
        explicit bus(unsigned address_bits, endianness endian = endianness::little);

        [[nodiscard]] unsigned address_bits() const noexcept { return address_bits_; }
        [[nodiscard]] endianness byte_order() const noexcept { return endian_; }

        // Map a region over the inclusive range starting at `start`. RAM is
        // read/write over caller storage; ROM is read-only (writes dropped); MMIO
        // routes to the handlers (address is the full bus address). priority breaks
        // ties between overlapping regions (higher wins); active gates the region
        // per access.
        void map_ram(std::uint32_t start, std::span<std::uint8_t> storage, int priority = 0,
                     active_predicate active = {});
        void map_rom(std::uint32_t start, std::span<const std::uint8_t> storage, int priority = 0,
                     active_predicate active = {});
        void map_mmio(std::uint32_t start, std::uint32_t size, read_handler on_read,
                      write_handler on_write, int priority = 0, active_predicate active = {});

        // i_bus: unmapped reads return 0xFF (open bus); unmapped writes are dropped.
        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override;
        void write8(std::uint32_t address, std::uint8_t value) override;

        // Optional observer invoked after every completed read/write (the value is
        // the byte transferred). Null by default — a single null check on the hot
        // path when unset. The instrumentation layer installs one for watchpoints;
        // pass {} to clear it.
        using access_observer = std::function<void(const access_event&)>;
        void set_access_observer(access_observer observer) { observer_ = std::move(observer); }

      private:
        enum class kind : std::uint8_t { ram, rom, mmio };

        struct region final {
            std::uint32_t start{};
            std::uint32_t end{}; // inclusive
            kind backing{kind::ram};
            int priority{0};
            std::span<std::uint8_t> ram{};
            std::span<const std::uint8_t> rom{};
            read_handler on_read{};
            write_handler on_write{};
            active_predicate active{};
        };

        [[nodiscard]] const region* resolve(std::uint32_t address, bool is_write) const noexcept;

        unsigned address_bits_{16};
        endianness endian_{endianness::little};
        std::uint32_t address_mask_{0xFFFFU};
        std::vector<region> regions_;
        access_observer observer_{};
    };

} // namespace mnemos::topology
