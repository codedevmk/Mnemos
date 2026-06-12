#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::chips::mapper {

    // Standard Korean memory mapper for the Sega Master System.
    //
    // The simplest of the Korean cartridge designs (used by Daou / Zemina / Sang-Il
    // originals such as Dodgeball King and the Sangokushi titles). Spec per
    // SMS Power and the community emulator references (see THIRD-PARTY-REFERENCES.md):
    //
    //   - Three 16 KiB ROM slots: $0000-$3FFF, $4000-$7FFF, $8000-$BFFF.
    //   - Slots 0 and 1 are FIXED to ROM banks 0 and 1; only slot 2 banks.
    //   - The slot-2 page is selected by a byte written to $A000 (and only $A000;
    //     it overlaps the slot-2 read window, which stays ROM).
    //   - No on-cart RAM.
    //   - Power-on page: slot 2 = bank 0 (the post-reset default); a cart pages
    //     slot 2 before it uses the $8000-$BFFF window.
    //
    // The mapper has no clocked behaviour, so tick() is a no-op; only the slot-2
    // page register is saved.
    class korean_mapper final : public imapper {
      public:
        static constexpr int rom_page_size = 0x4000;            // 16 KiB
        static constexpr std::uint16_t bank_register = 0xA000U; // slot-2 page select
        static constexpr std::uint8_t power_on_page = 0U;

        korean_mapper() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Attach the cartridge ROM image (borrowed; must outlive the mapper).
        void attach_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }

        // CPU access to the $0000-$BFFF window (full CPU address). Reads route ROM
        // through the fixed slots 0/1 and the live slot-2 page; a write to $A000
        // selects the slot-2 page, every other write is dropped (ROM is read-only).
        [[nodiscard]] std::uint8_t cpu_read(std::uint16_t address) const noexcept;
        void cpu_write(std::uint16_t address, std::uint8_t value) noexcept;

        // imapper overlay surface. Same shape as the Sega/Codemasters mappers: the
        // build_system mapper-region path routes a `backing="mapper"` region's bus
        // accesses through these; the 32-bit address is truncated to the Z80's
        // 16-bit window because the SMS bus is 16-bit.
        [[nodiscard]] std::uint8_t read_overlay(std::uint32_t address) noexcept override {
            return cpu_read(static_cast<std::uint16_t>(address));
        }
        void write_overlay(std::uint32_t address, std::uint8_t value) noexcept override {
            cpu_write(static_cast<std::uint16_t>(address), value);
        }

        [[nodiscard]] std::uint8_t page() const noexcept { return slot2_page_; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        // Resolve a 16 KiB ROM page + in-page offset to a byte, wrapping the page
        // index modulo the image's page count (real carts drop the unmapped high
        // bits on smaller ROMs).
        [[nodiscard]] std::uint8_t rom_read_page(std::uint8_t page,
                                                 std::uint16_t offset) const noexcept;

        std::span<const std::uint8_t> rom_{};
        std::uint8_t slot2_page_{power_on_page};

        std::array<register_descriptor, 1> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::mapper
