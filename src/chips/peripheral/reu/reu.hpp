#pragma once

#include "chip.hpp"
#include "ibus.hpp"

#include <cstdint>
#include <vector>

namespace mnemos::chips::peripheral {

    // Commodore 1700/1764/1750 RAM Expansion Unit (REU).
    //
    // A bus-mastering DMA controller with banked expansion RAM, mapped at the I/O-2
    // window ($DF00-$DFFF). Writing the command register with the execute bit runs
    // a stash (C64->REU), fetch (REU->C64), swap, or verify between C64 memory and
    // the expansion RAM. Ported per ADR 0006. The transfer is modelled as immediate
    // (the real REU steals bus cycles); the /IRQ-on-completion line is reflected in
    // the status register but not yet wired to the CPU.
    class reu final : public i_peripheral, public i_mmio {
      public:
        enum class model : std::uint8_t { ram_128k, ram_256k, ram_512k };
        [[nodiscard]] static std::size_t ram_bytes(model m) noexcept;

        explicit reu(model m = model::ram_512k);

        [[nodiscard]] chip_metadata metadata() const noexcept override;
        void tick(std::uint64_t cycles) override;
        void reset(reset_kind kind) override;

        void save_state(state_writer& writer) const override;
        void load_state(state_reader& reader) override;

        [[nodiscard]] instrumentation::i_chip_introspection& introspection() noexcept override;

        // I/O-2 register window ($DF00-$DFFF); the 11 registers mirror every $20.
        [[nodiscard]] std::uint8_t mmio_read(std::uint16_t offset) override;
        void mmio_write(std::uint16_t offset, std::uint8_t value) override;

        void attach_bus(i_bus& bus) noexcept { bus_ = &bus; }
        // Re-size the expansion RAM to a model (clears it); use before first run.
        void set_model(model m) { ram_.assign(ram_bytes(m), 0U); }
        [[nodiscard]] std::size_t ram_size() const noexcept { return ram_.size(); }
        [[nodiscard]] std::uint8_t peek(std::size_t address) const noexcept {
            return address < ram_.size() ? ram_[address] : 0xFFU;
        }
        void poke(std::size_t address, std::uint8_t value) noexcept {
            if (address < ram_.size()) {
                ram_[address] = value;
            }
        }
        // /IRQ output level (status bit 7); the C64 may OR it into the 6510 /IRQ.
        [[nodiscard]] bool irq_asserted() const noexcept { return (status_ & 0x80U) != 0U; }

      private:
        class introspection_surface final : public instrumentation::i_chip_introspection {};

        void execute(std::uint8_t command);

        i_bus* bus_{};
        std::vector<std::uint8_t> ram_;

        std::uint8_t status_{};         // $DF00 (bits 7 IRQ / 6 end-of-block / 5 fault)
        std::uint8_t command_{0x10U};   // $DF01
        std::uint16_t c64_addr_{};      // $DF02/03
        std::uint32_t reu_addr_{};      // $DF04/05/06 (17-24 bit, banked)
        std::uint16_t length_{0xFFFFU}; // $DF07/08 (0 => 64K)
        std::uint8_t irq_mask_{};       // $DF09
        std::uint8_t addr_ctrl_{};      // $DF0A (bit7 fix C64, bit6 fix REU)

        introspection_surface introspection_{};
    };

} // namespace mnemos::chips::peripheral
