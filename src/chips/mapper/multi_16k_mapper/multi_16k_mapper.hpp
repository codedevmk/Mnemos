#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::chips::mapper {

    // Korean "Multi 16K" multicart mapper for the Sega Master System (the cart
    // "4-Pak All Action"). Spec per SMS Power and the community emulator
    // references (see THIRD-PARTY-REFERENCES.md):
    //
    //   - 16 KiB banks. THREE banked slots, with NO fixed region (even the first
    //     1 KiB at $0000-$03FF banks with slot 0, unlike the Sega mapper):
    //         slot 0 -> $0000-$3FFF   slot 1 -> $4000-$7FFF   slot 2 -> $8000-$BFFF
    //   - A register at the top of each slot selects that slot's 16 KiB bank:
    //         $3FFE -> slot 0 = value
    //         $7FFF -> slot 1 = value
    //         $BFFF -> slot 2 = (slot-0 register & 0x30) + value
    //     Slot 2's high two bits come from slot 0's register -- the two are
    //     interdependent (a $BFFF write resolves against the current $3FFE value).
    //   - Reset selects banks {0, 1, 0}. No on-cart RAM.
    //
    // No clocked behaviour, so tick() is a no-op; only the three bank registers
    // are saved.
    class multi_16k_mapper final : public imapper {
      public:
        static constexpr int rom_page_size = 0x4000; // 16 KiB
        static constexpr int slot_count = 3;
        static constexpr std::uint16_t reg_slot0 = 0x3FFEU;    // -> $0000-$3FFF
        static constexpr std::uint16_t reg_slot1 = 0x7FFFU;    // -> $4000-$7FFF
        static constexpr std::uint16_t reg_slot2 = 0xBFFFU;    // -> $8000-$BFFF
        static constexpr std::uint8_t slot2_high_mask = 0x30U; // slot-2 high bits from slot 0

        multi_16k_mapper() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Attach the cartridge ROM image (borrowed; must outlive the mapper).
        void attach_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }

        // CPU access to the $0000-$BFFF window (full CPU address). Reads route ROM
        // through the three live 16 KiB slots; a write to a slot register banks
        // that slot, every other write is dropped.
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

        [[nodiscard]] std::uint8_t page(int slot) const noexcept {
            return (slot >= 0 && slot < slot_count) ? slot_[static_cast<std::size_t>(slot)] : 0U;
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        // Resolve a 16 KiB ROM page + in-page offset to a byte, wrapping the page
        // index modulo the image's 16 KiB page count (smaller ROMs drop high bits).
        [[nodiscard]] std::uint8_t rom_read_page(std::uint8_t page,
                                                 std::uint16_t offset) const noexcept;

        std::span<const std::uint8_t> rom_{};
        std::array<std::uint8_t, slot_count> slot_{}; // 16 KiB slot banks

        std::array<register_descriptor, slot_count> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::mapper
