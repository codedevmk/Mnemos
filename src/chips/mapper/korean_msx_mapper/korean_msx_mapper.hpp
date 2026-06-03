#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::chips::mapper {

    // Korean MSX 8 KiB memory mapper for the Sega Master System (the "Korean MSX"
    // mapper used by the Korean MSX-to-SMS ports -- Cyborg Z, Penguin Adventure,
    // the Zemina titles). Spec per SMS Power and the community emulator references
    // (see THIRD-PARTY.md):
    //
    //   - 8 KiB banks (not the Sega/Codemasters 16 KiB).
    //   - $0000-$3FFF is fixed to ROM bank 0 (the first 16 KiB).
    //   - Four page registers at $0000-$0003 (writes into ROM space) bank the four
    //     8 KiB windows -- note the non-linear register->window order:
    //         reg 0 -> $8000-$9FFF   reg 1 -> $A000-$BFFF
    //         reg 2 -> $4000-$5FFF   reg 3 -> $6000-$7FFF
    //   - Reset clears all four registers to bank 0. No on-cart RAM.
    //
    // The `nemesis` variant (Konami's Nemesis Korean port) is identical EXCEPT
    // that $0000-$1FFF reads the LAST 8 KiB bank of the image instead of bank 0,
    // which is where its reset vector + early boot code live. Emulators select it
    // by ROM CRC; the chip exposes it as a settable variant.
    //
    // No clocked behaviour, so tick() is a no-op; only the four page registers are
    // saved (the variant is fixed configuration, set before reset).
    class korean_msx_mapper final : public imapper {
      public:
        enum class variant : std::uint8_t { msx, nemesis };

        static constexpr int rom_bank_size = 0x2000;            // 8 KiB
        static constexpr std::uint16_t register_base = 0x0000U; // $0000-$0003
        static constexpr int register_count = 4;

        korean_msx_mapper() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::ichip_introspection& introspection() noexcept override;

        // Attach the cartridge ROM image (borrowed; must outlive the mapper).
        void attach_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }

        // Select the silicon variant. Set once before reset; defaults to `msx`.
        void set_variant(variant v) noexcept { variant_ = v; }
        [[nodiscard]] variant chip_variant() const noexcept { return variant_; }

        // CPU access to the $0000-$BFFF window (full CPU address). Reads route ROM
        // through the fixed region + the four live 8 KiB pages; a write to
        // $0000-$0003 sets a page register, every other write is dropped.
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
        variant variant_{variant::msx};
        std::array<std::uint8_t, register_count> slot_{}; // 8 KiB page registers

        std::array<register_descriptor, register_count> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::mapper
