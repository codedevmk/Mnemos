#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::chips::mapper {

    // Korean "4x8K" multicart mapper for the Sega Master System (the single-
    // register XOR multicarts -- 128 Hap, Game Mo-eumjip 188 Hap). Spec per SMS
    // Power and the community emulator references (see THIRD-PARTY.md):
    //
    //   - 8 KiB banks. $0000-$3FFF is fixed to ROM banks 0/1 (the first 16 KiB).
    //   - A single register at $2000 banks all four 8 KiB windows at once, each
    //     from the written value XORed with a fixed per-window mask:
    //         $4000-$5FFF = value ^ 0x1F   $6000-$7FFF = value ^ 0x1E
    //         $8000-$9FFF = value ^ 0x1D   $A000-$BFFF = value ^ 0x1C
    //     ($2000 sits in the fixed region, so it reads back as ROM bank 1 -- the
    //     write is a register, the read is ordinary ROM.)
    //   - Reset clears all four banks to 0. No on-cart RAM.
    //
    // No clocked behaviour, so tick() is a no-op; only the four bank registers
    // are saved.
    class multi_4x8k_mapper final : public imapper {
      public:
        static constexpr int rom_bank_size = 0x2000;            // 8 KiB
        static constexpr int register_count = 4;                // four 8 KiB windows
        static constexpr std::uint16_t bank_register = 0x2000U; // banks all four windows

        multi_4x8k_mapper() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Attach the cartridge ROM image (borrowed; must outlive the mapper).
        void attach_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }

        // CPU access to the $0000-$BFFF window (full CPU address). Reads route ROM
        // through the fixed region + the four live 8 KiB windows; a write to $2000
        // banks all four windows, every other write is dropped.
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

        [[nodiscard]] std::uint8_t page(int reg) const noexcept {
            return (reg >= 0 && reg < register_count) ? slot_[static_cast<std::size_t>(reg)] : 0U;
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        // Resolve an 8 KiB ROM page + in-page offset to a byte, wrapping the page
        // index modulo the image's 8 KiB page count (smaller ROMs drop high bits).
        [[nodiscard]] std::uint8_t rom_read_page(std::uint8_t page,
                                                 std::uint16_t offset) const noexcept;

        std::span<const std::uint8_t> rom_{};
        std::array<std::uint8_t, register_count> slot_{}; // 8 KiB window banks

        std::array<register_descriptor, register_count> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::mapper
