#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::chips::mapper {

    // HiCom "188-in-1" Korean multicart mapper for the Sega Master System (the
    // Hi-Com pirate compilation carts). Spec per the community emulator references
    // (Genesis Plus GX MAPPER_HICOM; see THIRD-PARTY-REFERENCES.md):
    //
    //   - 32 KiB pages. A single register at $FFFF selects which 32 KiB page is
    //     mapped at $0000-$7FFF; $8000-$BFFF mirrors the page's lower 16 KiB
    //     ($0000-$3FFF). The whole $0000-$BFFF window follows the page (there is
    //     no fixed bank).
    //   - The register sits at $FFFF, in the work-RAM mirror, so the host routes a
    //     $FFFF write both to RAM and to this mapper (same overlay scheme the Sega
    //     mapper uses for $FFFC-$FFFF).
    //   - Reset selects page 0 (the menu). No on-cart RAM.
    //
    // No clocked behaviour, so tick() is a no-op; only the page register is saved.
    class hicom_mapper final : public imapper {
      public:
        static constexpr int rom_page_size = 0x8000;            // 32 KiB
        static constexpr std::uint16_t bank_register = 0xFFFFU; // page select

        hicom_mapper() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Attach the cartridge ROM image (borrowed; must outlive the mapper).
        void attach_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }

        // CPU access. Reads resolve the 32 KiB page across $0000-$7FFF and its
        // 16 KiB mirror at $8000-$BFFF; a write to $FFFF selects the page, every
        // other write is dropped (ROM is read-only).
        [[nodiscard]] std::uint8_t cpu_read(std::uint16_t address) const noexcept;
        void cpu_write(std::uint16_t address, std::uint8_t value) noexcept;

        // imapper overlay surface (32-bit address truncated to the Z80's 16-bit
        // window; the SMS bus is 16-bit).
        [[nodiscard]] std::uint8_t read_overlay(std::uint32_t address) noexcept override {
            return cpu_read(static_cast<std::uint16_t>(address));
        }
        void write_overlay(std::uint32_t address, std::uint8_t value) noexcept override {
            cpu_write(static_cast<std::uint16_t>(address), value);
        }

        [[nodiscard]] std::uint8_t page() const noexcept { return page_; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        // Resolve a 32 KiB ROM page + in-page offset to a byte, wrapping the page
        // index modulo the image's 32 KiB page count (smaller ROMs drop high bits).
        [[nodiscard]] std::uint8_t rom_read_page(std::uint8_t page,
                                                 std::uint16_t offset) const noexcept;

        std::span<const std::uint8_t> rom_{};
        std::uint8_t page_{};

        std::array<register_descriptor, 1> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::mapper
