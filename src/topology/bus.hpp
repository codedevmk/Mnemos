#pragma once

#include "ibus.hpp"

#include <array>
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
    // regions backed by RAM, ROM, or an MMIO chip. Implements chips::ibus so a
    // CPU attaches directly to it.
    //
    // Regions may overlap. Each carries a priority and an optional "active"
    // predicate evaluated per access; the highest-priority active region covering
    // an address wins. This models C64-style banking driven by the PLA: ROM
    // overlays are registered active on reads only (writes fall through to the RAM
    // underneath), I/O overlays active for both, all gated by the PLA decode the
    // system supplies through the predicate. With no overlapping regions and no
    // predicates this degenerates to a plain disjoint memory map.
    class bus final : public chips::ibus {
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

        // Point an existing RAM region (matched by its exact `start`) at new
        // backing storage of the same size -- the bank-window primitive (the
        // 32X frame-buffer access bank flips at each frame-select commit).
        // Cheaper than unmap/remap and keeps region order/priority stable.
        // No-op if no RAM region starts at `start`; asserts on size mismatch.
        void retarget_ram(std::uint32_t start, std::span<std::uint8_t> storage) noexcept;
        // ROM flavour of the same primitive (the 32X $900000 cart bank window).
        void retarget_rom(std::uint32_t start, std::span<const std::uint8_t> storage) noexcept;

        // Opcode-fetch overlay: a ROM span served to fetch_opcode8 over
        // [start, start+size) instead of read8 -- for opcode/data-split program
        // memory (an encrypted Z80 whose decrypted M1 opcode bytes differ from
        // data reads at the same address, e.g. Kabuki on CPS1 QSound). The data
        // side is mapped normally (map_rom/map_ram) and served by read8; one
        // overlay region suffices. Default (none registered) leaves fetch_opcode8
        // == read8, so every other bus is unchanged.
        void map_opcode_rom(std::uint32_t start, std::span<const std::uint8_t> storage) noexcept;

        // ibus: unmapped reads return 0xFF (open bus); unmapped writes are dropped.
        [[nodiscard]] std::uint8_t read8(std::uint32_t address) override;
        void write8(std::uint32_t address, std::uint8_t value) override;
        // Serves the opcode overlay (if mapped over `address`), else read8.
        [[nodiscard]] std::uint8_t fetch_opcode8(std::uint32_t address) override;
        // Wide opcode-fetch counterparts (the 16-bit BE cores: m68000). Serve the
        // opcode overlay when mapped over `address`, else the normal data path.
        [[nodiscard]] std::uint16_t fetch16_be_opcode(std::uint32_t address) override;
        [[nodiscard]] bool direct_opcode_span(std::uint32_t address,
                                              chips::ibus::direct_span& out) override;

        // Wide big-endian accesses: a single fast-span resolution when the whole
        // access fits one cached RAM/ROM span and no observer is installed;
        // otherwise byte-exact composition (MMIO side effects, watchpoints,
        // span-straddling and address-mask wrap all keep read8/write8 semantics).
        [[nodiscard]] std::uint16_t read16_be(std::uint32_t address) override;
        void write16_be(std::uint32_t address, std::uint16_t value) override;
        [[nodiscard]] std::uint32_t read32_be(std::uint32_t address) override;
        void write32_be(std::uint32_t address, std::uint32_t value) override;

        // Little-endian counterparts (the V30 fetch/load hot path), same
        // fast-span-or-byte-exact contract as the big-endian set.
        [[nodiscard]] std::uint16_t read16_le(std::uint32_t address) override;
        void write16_le(std::uint32_t address, std::uint16_t value) override;

        // Optional observer invoked after every completed read/write (the value is
        // the byte transferred). Null by default — a single null check on the hot
        // path when unset. The instrumentation layer installs one for watchpoints;
        // pass {} to clear it. Installing one revokes outstanding direct spans so
        // every access becomes observable again.
        using access_observer = std::function<void(const access_event&)>;
        void set_access_observer(access_observer observer) {
            observer_ = std::move(observer);
            invalidate_fast_path();
        }

        // ibus direct spans: a predicate-free RAM/ROM winner narrowed to where it
        // provably stays the winner (the MRU narrowing), refused while an access
        // observer is installed. Listeners fire on every remap/retarget/observer
        // install -- the holder must drop its span then.
        [[nodiscard]] bool direct_read_span(std::uint32_t address, direct_span& out) override;
        void add_invalidation_listener(std::function<void()> listener) override {
            invalidation_listeners_.push_back(std::move(listener));
        }

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

        // Hot-path cache: recently resolved RAM/ROM regions, each narrowed to
        // the maximal address span where it provably stays the winner (no
        // overlapping region that could outrank it, no predicates involved).
        // Within a cached span an access indexes the storage directly,
        // skipping the region scan. Four MRU entries (swap-to-front on hit)
        // so workloads that alternate regions -- the SH-2 fetching from SDRAM
        // while storing to the frame buffer -- don't thrash a single slot.
        // Cleared by any map_* call (regions_ may reallocate, and the winner
        // set changes). MMIO and predicate-gated regions never enter the
        // cache, so dynamic decode (C64 PLA banking) keeps full resolve
        // semantics.
        struct fast_span final {
            std::uint32_t start{1};
            std::uint32_t end{0}; // empty range = unused entry
            const region* r{};
        };

        void update_fast_path(std::uint32_t address, const region* winner) noexcept;
        void invalidate_fast_path() noexcept {
            fast_.fill(fast_span{});
            for (const auto& listener : invalidation_listeners_) {
                listener(); // contract: listeners do not throw
            }
        }

        std::array<fast_span, 4> fast_{};
        std::vector<std::function<void()>> invalidation_listeners_;

        unsigned address_bits_{16};
        endianness endian_{endianness::little};
        std::uint32_t address_mask_{0xFFFFU};
        std::vector<region> regions_;
        access_observer observer_{};

        // Optional opcode-fetch overlay (see map_opcode_rom): served by
        // fetch_opcode8 over [opcode_rom_start_, +opcode_rom_.size()); empty by
        // default so fetch_opcode8 degenerates to read8.
        std::span<const std::uint8_t> opcode_rom_{};
        std::uint32_t opcode_rom_start_{};
    };

} // namespace mnemos::topology
