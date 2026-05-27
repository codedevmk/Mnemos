#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::chips::mapper {

    // Codemasters memory mapper for the Sega Master System / Game Gear.
    //
    // A simpler, distinct design from the Sega mapper (it is genuinely different
    // hardware, so it is a separate chip rather than a mode of sms_mapper). Spec per
    // Community-documented mapper notes (see THIRD-PARTY.md):
    //
    //   - Three fully-banked 16 KiB ROM slots: $0000-$3FFF, $4000-$7FFF, $8000-$BFFF.
    //     Unlike the Sega mapper there is NO fixed first 1 KiB -- slot 0 banks whole.
    //   - The page registers live in ROM space (not $FFFC-$FFFF): a write to $0000
    //     sets the slot-0 page, $4000 the slot-1 page, $8000 the slot-2 page.
    //   - On-cart RAM (8 KiB, present on a few titles): when bit 7 of the value written
    //     to $4000 is set, the upper half of slot 2 ($A000-$BFFF) maps to that RAM;
    //     the lower half ($8000-$9FFF) stays ROM. Bit 7 clear unmaps it.
    //   - Power-on pages: slot 0 = 0, slot 1 = 1, slot 2 = 0.
    //
    // The mapper has no clocked behaviour, so tick() is a no-op; only the page
    // registers, the RAM-enable latch, and the cart RAM are saved.
    class codemasters_mapper final : public imapper {
      public:
        static constexpr int cart_ram_size = 8 * 1024;        // $A000-$BFFF
        static constexpr int rom_page_size = 0x4000;          // 16 KiB
        static constexpr std::uint8_t ram_enable_bit = 0x80U; // bit 7 of the $4000 write
        static constexpr std::uint16_t ram_window_base = 0xA000U;

        codemasters_mapper() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Attach the cartridge ROM image (borrowed; must outlive the mapper).
        void attach_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }

        // CPU access to the $0000-$BFFF window (full CPU address). Reads route ROM /
        // cart RAM through the live page registers; writes set the page registers
        // (and, in the RAM window when enabled, the cart RAM).
        [[nodiscard]] std::uint8_t cpu_read(std::uint16_t address) const noexcept;
        void cpu_write(std::uint16_t address, std::uint8_t value) noexcept;

        // imapper overlay surface. Same shape as sms_mapper's: the
        // build_system mapper-region path routes a manifest-declared
        // `backing="mapper"` region's bus accesses through these; the 32-bit
        // address is truncated to the Z80's 16-bit window because the SMS
        // bus is 16-bit.
        [[nodiscard]] std::uint8_t read_overlay(std::uint32_t address) noexcept override {
            return cpu_read(static_cast<std::uint16_t>(address));
        }
        void write_overlay(std::uint32_t address, std::uint8_t value) noexcept override {
            cpu_write(static_cast<std::uint16_t>(address), value);
        }

        [[nodiscard]] std::uint8_t page(int slot) const noexcept {
            return (slot >= 0 && slot < 3) ? page_[static_cast<std::size_t>(slot)] : 0U;
        }
        [[nodiscard]] bool cart_ram_enabled() const noexcept { return ram_enabled_; }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        [[nodiscard]] std::uint8_t rom_read_page(std::uint8_t page,
                                                 std::uint16_t offset) const noexcept;
        [[nodiscard]] bool in_ram_window(std::uint16_t address) const noexcept {
            return ram_enabled_ && address >= ram_window_base;
        }

        std::span<const std::uint8_t> rom_{};
        std::array<std::uint8_t, cart_ram_size> cart_ram_{};

        std::array<std::uint8_t, 3> page_{{0U, 1U, 0U}};
        bool ram_enabled_{};

        std::array<register_descriptor, 4> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::mapper
