#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::chips::mapper {

    // "Janggun" 8 KiB memory mapper for the Sega Master System (the Korean cart
    // "Janggun-ui Adeul"). Spec per SMS Power and the community emulator
    // references (see THIRD-PARTY.md):
    //
    //   - 8 KiB banks. $0000-$3FFF is fixed to ROM banks 0/1 (the first 16 KiB).
    //   - Four banked 8 KiB windows, each a frame-control register (FCR):
    //         FCR2 -> $4000-$5FFF   FCR3 -> $6000-$7FFF
    //         FCR0 -> $8000-$9FFF   FCR1 -> $A000-$BFFF
    //   - Two register surfaces write the FCRs:
    //       * direct 8 KiB selects in the cartridge window:
    //             $4000 -> FCR2   $6000 -> FCR3   $8000 -> FCR0   $A000 -> FCR1
    //       * Sega-style 16 KiB pair selects in the work-RAM mirror, each setting
    //         two consecutive 8 KiB banks (value n -> banks 2n, 2n+1):
    //             $FFFE -> FCR2/FCR3 (the $4000-$7FFF page)
    //             $FFFF -> FCR0/FCR1 (the $8000-$BFFF page)
    //         A $FFFE/$FFFF write also lands in work RAM (the host routes it like
    //         the Sega mapper's $FFFC-$FFFF), so its overlay is RAM + mapper.
    //   - Bit-reversal: when a 16 KiB page's low-window FCR has bit 7 set, reads
    //     from that whole page return the byte with its bits mirrored (D0<->D7,
    //     D1<->D6, ...). $4000-$7FFF keys off FCR2 bit 7; $8000-$BFFF off FCR0
    //     bit 7. The bank index is the FCR value modulo the 8 KiB page count, so
    //     on a <=128-page image bit 7 flags reversal without disturbing the bank.
    //   - Reset clears all four FCRs to bank 0. No on-cart RAM.
    //
    // No clocked behaviour, so tick() is a no-op; only the four FCRs are saved.
    class janggun_mapper final : public imapper {
      public:
        static constexpr int rom_bank_size = 0x2000; // 8 KiB
        static constexpr int register_count = 4;
        static constexpr std::uint8_t reverse_flag = 0x80U; // FCR bit 7

        // Direct 8 KiB bank-select addresses (inside the cartridge window).
        static constexpr std::uint16_t reg_bank_4000 = 0x4000U; // -> FCR2
        static constexpr std::uint16_t reg_bank_6000 = 0x6000U; // -> FCR3
        static constexpr std::uint16_t reg_bank_8000 = 0x8000U; // -> FCR0
        static constexpr std::uint16_t reg_bank_a000 = 0xA000U; // -> FCR1
        // Sega-style 16 KiB pair-select addresses (in the work-RAM mirror).
        static constexpr std::uint16_t reg_pair_lower = 0xFFFEU; // -> FCR2/FCR3
        static constexpr std::uint16_t reg_pair_upper = 0xFFFFU; // -> FCR0/FCR1

        janggun_mapper() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Attach the cartridge ROM image (borrowed; must outlive the mapper).
        void attach_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }

        // CPU access to the $0000-$BFFF window (full CPU address). Reads route ROM
        // through the fixed region + the four live 8 KiB pages (bit-reversed when
        // the page's FCR flags it); a write to a bank/pair register updates the
        // FCRs, every other write is dropped.
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

        [[nodiscard]] std::uint8_t fcr(int reg) const noexcept {
            return (reg >= 0 && reg < register_count) ? fcr_[static_cast<std::size_t>(reg)] : 0U;
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::ichip_introspection {};

        // Resolve an 8 KiB ROM page + in-page offset to a byte, wrapping the page
        // index modulo the image's 8 KiB page count (smaller ROMs drop high bits).
        [[nodiscard]] std::uint8_t rom_read_page(std::uint8_t page,
                                                 std::uint16_t offset) const noexcept;

        // Mirror the eight data bits (D0<->D7, D1<->D6, D2<->D5, D3<->D4).
        [[nodiscard]] static std::uint8_t reverse_bits(std::uint8_t value) noexcept;

        std::span<const std::uint8_t> rom_{};
        std::array<std::uint8_t, register_count> fcr_{}; // 8 KiB bank registers

        std::array<register_descriptor, register_count> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::mapper
