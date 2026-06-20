#pragma once

#include "bus.hpp"     // topology::bus
#include "ppu2c02.hpp" // chips::video::ppu2c02
#include "state.hpp"   // chips::state_writer / state_reader

#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace mnemos::manifests::nes {

    // A cartridge mapper owns the $8000-$FFFF PRG mapping and the PPU's CHR
    // window, applying the bank switches a program requests by writing into the
    // mapper's register space. reset() installs the initial mapping; write()
    // handles a CPU write into the cartridge space ($8000-$FFFF on the mappers
    // modelled so far). NROM is the trivial (no-banking) case.
    class nes_mapper {
      public:
        nes_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                   std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                   bool chr_is_ram) noexcept
            : bus_(&bus), ppu_(&ppu), prg_(prg), chr_(chr), chr_is_ram_(chr_is_ram) {}
        nes_mapper(const nes_mapper&) = delete;
        nes_mapper& operator=(const nes_mapper&) = delete;
        virtual ~nes_mapper() = default;

        virtual void reset() = 0;
        virtual void write(std::uint16_t addr, std::uint8_t value) = 0;
        // Clock a scanline-counter mapper (MMC3) once per visible scanline. The
        // default is a no-op (NROM / UxROM / MMC1 have no scanline IRQ).
        virtual void clock_scanline(std::uint32_t /*line*/) {}

        // Serialise the mapper's banking state for save-states; load_state restores
        // it AND re-applies the bus/CHR mapping. The stateless NROM uses the no-op
        // default (its banks never move).
        virtual void save_state(chips::state_writer& /*writer*/) const {}
        virtual void load_state(chips::state_reader& /*reader*/) {}

        // How a scanline-IRQ mapper drives the CPU /IRQ line. The board wires this
        // to the CPU; mappers without an IRQ ignore it.
        void set_irq_callback(std::function<void(bool)> on_irq) { set_irq_ = std::move(on_irq); }

      protected:
        void raise_irq(bool asserted) {
            if (set_irq_) {
                set_irq_(asserted);
            }
        }
        // Attach the cart's CHR to the PPU: writable for a CHR-RAM cart, const for
        // CHR-ROM. (CHR-banking mappers re-point this as banks change.)
        void attach_chr() noexcept {
            if (chr_.empty()) {
                return;
            }
            if (chr_is_ram_) {
                ppu_->attach_chr_ram(chr_);
            } else {
                ppu_->attach_chr(std::span<const std::uint8_t>(chr_));
            }
        }

        // Route CPU writes to $8000-$FFFF into this->write() without slowing the
        // read path: a write-gated MMIO over the range (active only for writes, via
        // the bus active-predicate) leaves reads to the underlying map_rom regions.
        // Banking mappers call this once from reset() and retarget_rom the banks.
        void install_register_write_hook() {
            bus_->map_mmio(
                0x8000U, 0x8000U, [](std::uint32_t) -> std::uint8_t { return 0xFFU; },
                [this](std::uint32_t addr, std::uint8_t value) {
                    write(static_cast<std::uint16_t>(addr), value);
                },
                1, [](std::uint32_t, bool is_write) { return is_write; });
        }

        topology::bus* bus_;
        chips::video::ppu2c02* ppu_;
        std::span<const std::uint8_t> prg_;
        std::span<std::uint8_t> chr_;
        bool chr_is_ram_;

      private:
        std::function<void(bool)> set_irq_{};
    };

    // Build the mapper for the iNES `number`. An unsupported number falls back to
    // NROM so the machine still assembles (it may not run, but it won't crash).
    [[nodiscard]] std::unique_ptr<nes_mapper>
    make_mapper(int number, topology::bus& bus, chips::video::ppu2c02& ppu,
                std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr, bool chr_is_ram);

} // namespace mnemos::manifests::nes
