#pragma once

#include "chip.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mnemos::chips::mapper {

    // Sega Master System / Game Gear standard memory mapper (the "Sega mapper").
    //
    // Ported from the Emu reference core (ADR 0006). The mapper is cartridge glue:
    // it splits the Z80's lower 48 KiB into three 16 KiB ROM slots plus an optional
    // 16 KiB on-cart RAM window, driven by four control registers the CPU writes
    // through the top of RAM:
    //
    //   $FFFC  control  — bit 3 enables cart RAM at slot 2, bit 2 selects its bank
    //   $FFFD  slot 0 page  ($0400-$3FFF; $0000-$03FF is always physical page 0)
    //   $FFFE  slot 1 page  ($4000-$7FFF)
    //   $FFFF  slot 2 page  ($8000-$BFFF, unless cart RAM is enabled)
    //
    //   $C000-$DFFF is the 8 KiB system RAM, mirrored to $E000-$FFFF; the four
    //   register addresses overlap that mirror, so the system writes them through to
    //   RAM *and* to write_register(). The mapper itself owns only the banking
    //   registers, the borrowed ROM image, and the cart RAM.
    //
    // The mapper has no clocked behaviour, so tick() is a no-op; only the registers
    // and cart RAM are saved.
    class sms_mapper final : public i_mapper {
      public:
        static constexpr int cart_ram_size = 32 * 1024; // two 16 KiB banks
        static constexpr int rom_page_size = 0x4000;    // 16 KiB
        static constexpr std::uint8_t ram_enable_bit = 0x08U;
        static constexpr std::uint8_t ram_bank_bit = 0x04U;
        static constexpr std::uint16_t register_base = 0xFFFCU;

        sms_mapper() { reset(reset_kind::power_on); }

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::i_chip_introspection& introspection() noexcept override;

        // Attach the cartridge ROM image. The span is borrowed and must outlive the
        // mapper; banking simply drops unmapped high address bits on smaller images.
        void attach_rom(std::span<const std::uint8_t> rom) noexcept { rom_ = rom; }

        // CPU access to the banked window $0000-$BFFF (address is the full CPU
        // address). Reads route ROM / cart RAM through the live page registers;
        // writes reach only the cart RAM window when it is enabled (ROM is ignored).
        [[nodiscard]] std::uint8_t cpu_read(std::uint16_t address) const noexcept;
        void cpu_write(std::uint16_t address, std::uint8_t value) noexcept;

        // Update a control register from a CPU write to $FFFC-$FFFF. Addresses
        // outside that range are ignored (the mapper fully decodes the four bytes
        // and never responds to the $DFFC-$DFFF RAM-mirror copy games use as work
        // RAM).
        void write_register(std::uint16_t address, std::uint8_t value) noexcept;

        [[nodiscard]] std::uint8_t control() const noexcept { return control_; }
        [[nodiscard]] std::uint8_t page(int slot) const noexcept {
            return (slot >= 0 && slot < 3) ? page_[static_cast<std::size_t>(slot)] : 0U;
        }
        [[nodiscard]] bool cart_ram_enabled() const noexcept {
            return (control_ & ram_enable_bit) != 0U;
        }

        [[nodiscard]] std::span<const register_descriptor> register_snapshot() noexcept;

      private:
        class introspection_surface final : public instrumentation::i_chip_introspection {};

        // Resolve a 16 KiB ROM page + in-page offset to a byte, wrapping the page
        // index modulo the image's page count (real carts drop the unmapped high
        // bits on smaller ROMs).
        [[nodiscard]] std::uint8_t rom_read_page(std::uint8_t page,
                                                 std::uint16_t offset) const noexcept;
        [[nodiscard]] std::size_t cart_ram_offset(std::uint16_t address) const noexcept;

        std::span<const std::uint8_t> rom_{};
        std::array<std::uint8_t, cart_ram_size> cart_ram_{};

        std::uint8_t control_{};
        std::array<std::uint8_t, 3> page_{{0U, 1U, 2U}};

        std::array<register_descriptor, 4> register_view_{};
        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::mapper
