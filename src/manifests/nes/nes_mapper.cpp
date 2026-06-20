#include "nes_mapper.hpp"

namespace mnemos::manifests::nes {

    namespace {
        constexpr std::size_t k_prg_bank = 0x4000U; // 16 KiB

        // NROM (iNES 0): no banking. A 16 KiB PRG image mirrors into both halves
        // (so $FFFC resolves); 32 KiB fills $8000-$FFFF. CHR is fixed.
        class nrom_mapper final : public nes_mapper {
          public:
            using nes_mapper::nes_mapper;

            void reset() override {
                if (!prg_.empty()) {
                    bus_->map_rom(0x8000U, prg_);
                    if (prg_.size() <= k_prg_bank) {
                        bus_->map_rom(0xC000U, prg_); // mirror the 16 KiB image
                    }
                }
                attach_chr();
            }
            void write(std::uint16_t /*addr*/, std::uint8_t /*value*/) override {} // no registers
        };

        // UxROM (iNES 2): a switchable 16 KiB PRG bank at $8000 (selected by any
        // write to $8000-$FFFF) over a fixed last bank at $C000; 8 KiB CHR-RAM.
        class uxrom_mapper final : public nes_mapper {
          public:
            uxrom_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                         std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                         bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), bank_count_(prg.size() / k_prg_bank) {
            }

            void reset() override {
                attach_chr();
                if (bank_count_ == 0U) {
                    return; // malformed (no full 16 KiB bank)
                }
                bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_bank)); // bank 0 (switchable)
                bus_->map_rom(0xC000U, last_bank());                 // last bank (fixed)
                // Catch the bank-select writes ($8000-$FFFF) without slowing the
                // read path: a write-gated MMIO over the same range leaves reads to
                // the map_rom regions (retarget_rom matches only the ROM region).
                bus_->map_mmio(
                    0x8000U, 0x8000U, [](std::uint32_t) -> std::uint8_t { return 0xFFU; },
                    [this](std::uint32_t addr, std::uint8_t value) {
                        write(static_cast<std::uint16_t>(addr), value);
                    },
                    1, [](std::uint32_t, bool is_write) { return is_write; });
            }

            void write(std::uint16_t /*addr*/, std::uint8_t value) override {
                if (bank_count_ == 0U) {
                    return;
                }
                const std::size_t bank = value % bank_count_;
                bus_->retarget_rom(0x8000U, prg_.subspan(bank * k_prg_bank, k_prg_bank));
            }

          private:
            [[nodiscard]] std::span<const std::uint8_t> last_bank() const {
                return prg_.subspan((bank_count_ - 1U) * k_prg_bank, k_prg_bank);
            }
            std::size_t bank_count_;
        };
    } // namespace

    std::unique_ptr<nes_mapper> make_mapper(int number, topology::bus& bus,
                                            chips::video::ppu2c02& ppu,
                                            std::span<const std::uint8_t> prg,
                                            std::span<std::uint8_t> chr, bool chr_is_ram) {
        switch (number) {
        case 2:
            return std::make_unique<uxrom_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 0:
        default:
            return std::make_unique<nrom_mapper>(bus, ppu, prg, chr, chr_is_ram);
        }
    }

} // namespace mnemos::manifests::nes
