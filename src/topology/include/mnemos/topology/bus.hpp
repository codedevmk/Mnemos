#pragma once

#include <mnemos/chips/common/bus.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace mnemos::topology {

    enum class endianness : std::uint8_t { little, big };

    // A typed address space (TDS §9): a width, an endianness, and an ordered table
    // of regions backed by RAM, ROM, or an MMIO chip. Implements chips::i_bus so a
    // CPU attaches directly to it. Resolution is O(log N) over the sorted table.
    //
    // This is the disjoint-region core; PLA-driven overlay control and cartridge
    // mappers layer on top in a later slice.
    class bus final : public chips::i_bus {
      public:
        using read_handler = std::function<std::uint8_t(std::uint32_t address)>;
        using write_handler = std::function<void(std::uint32_t address, std::uint8_t value)>;

        bus() = default;
        explicit bus(unsigned address_bits, endianness endian = endianness::little);

        [[nodiscard]] unsigned address_bits() const noexcept { return address_bits_; }
        [[nodiscard]] endianness byte_order() const noexcept { return endian_; }

        // Map a region over the inclusive address range starting at `start`.
        // RAM is read/write over caller-owned storage; ROM is read-only (writes
        // dropped); MMIO routes to the supplied handlers (address is the full bus
        // address, not an offset).
        void map_ram(std::uint32_t start, std::span<std::uint8_t> storage);
        void map_rom(std::uint32_t start, std::span<const std::uint8_t> storage);
        void map_mmio(std::uint32_t start, std::uint32_t size, read_handler on_read,
                      write_handler on_write);

        // i_bus: unmapped reads return 0xFF (open bus); unmapped writes are dropped.
        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override;
        void write8(std::uint32_t address, std::uint8_t value) override;

      private:
        enum class kind : std::uint8_t { ram, rom, mmio };

        struct region final {
            std::uint32_t start{};
            std::uint32_t end{}; // inclusive
            kind backing{kind::ram};
            std::span<std::uint8_t> ram{};
            std::span<const std::uint8_t> rom{};
            read_handler on_read{};
            write_handler on_write{};
        };

        void insert_region(region r);
        [[nodiscard]] const region* resolve(std::uint32_t address) const noexcept;

        unsigned address_bits_{16};
        endianness endian_{endianness::little};
        std::uint32_t address_mask_{0xFFFFU};
        std::vector<region> regions_; // sorted by start, disjoint
    };

} // namespace mnemos::topology
