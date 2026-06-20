#include "nes_mapper.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace mnemos::manifests::nes {

    namespace {
        constexpr std::size_t k_prg_bank = 0x4000U; // 16 KiB
        constexpr std::size_t k_chr_4k = 0x1000U;   // 4 KiB CHR bank

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
                install_register_write_hook();
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

        // MMC1 (iNES 1): a serial-shift-register mapper. Five writes of bit 0 load
        // a 5-bit value into the register the high address bits select; bit 7 of a
        // write resets the shift register and forces PRG mode 3. Registers:
        // control (mirroring + PRG/CHR bank modes), CHR bank 0/1, PRG bank.
        // CHR-ROM banks are composed into an 8 KiB window the PPU reads; a CHR-RAM
        // cart attaches its writable 8 KiB instead.
        class mmc1_mapper final : public nes_mapper {
          public:
            mmc1_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                        std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                        bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), prg_count_(prg.size() / k_prg_bank),
                  chr_4k_count_(chr.size() / k_chr_4k) {}

            void reset() override {
                control_ = 0x0CU; // power-on: PRG mode 3 ($8000 switchable, $C000 fixed last)
                shift_ = 0U;
                count_ = 0U;
                chr_bank0_ = chr_bank1_ = prg_bank_ = 0U;
                if (prg_count_ != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_bank));
                    bus_->map_rom(0xC000U,
                                  prg_.subspan((prg_count_ - 1U) * k_prg_bank, k_prg_bank));
                }
                if (chr_is_ram_) {
                    ppu_->attach_chr_ram(chr_); // 8 KiB CHR-RAM (no ROM banking)
                } else {
                    ppu_->attach_chr(std::span<const std::uint8_t>(chr_window_));
                }
                apply();
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                if ((value & 0x80U) != 0U) { // reset the shift register
                    shift_ = 0U;
                    count_ = 0U;
                    control_ |= 0x0CU;
                    apply();
                    return;
                }
                shift_ |= static_cast<std::uint8_t>((value & 0x01U) << count_);
                if (++count_ < 5U) {
                    return;
                }
                switch ((addr >> 13U) & 0x03U) { // the register the 5th write commits
                case 0:
                    control_ = shift_;
                    break;
                case 1:
                    chr_bank0_ = shift_;
                    break;
                case 2:
                    chr_bank1_ = shift_;
                    break;
                default:
                    prg_bank_ = shift_ & 0x0FU;
                    break;
                }
                shift_ = 0U;
                count_ = 0U;
                apply();
            }

          private:
            void apply() {
                using m = chips::video::ppu2c02::mirroring;
                switch (control_ & 0x03U) {
                case 0:
                    ppu_->set_mirroring(m::single_a);
                    break;
                case 1:
                    ppu_->set_mirroring(m::single_b);
                    break;
                case 2:
                    ppu_->set_mirroring(m::vertical);
                    break;
                default:
                    ppu_->set_mirroring(m::horizontal);
                    break;
                }
                apply_prg();
                apply_chr();
            }

            void map_prg(std::uint32_t slot, std::size_t bank16) {
                if (prg_count_ == 0U) {
                    return;
                }
                bus_->retarget_rom(slot,
                                   prg_.subspan((bank16 % prg_count_) * k_prg_bank, k_prg_bank));
            }

            void apply_prg() {
                const unsigned mode = (control_ >> 2U) & 0x03U;
                if (mode <= 1U) { // 32 KiB at $8000 (low PRG-bank bit ignored)
                    const std::size_t base = static_cast<std::size_t>(prg_bank_ & 0x0EU);
                    map_prg(0x8000U, base);
                    map_prg(0xC000U, base + 1U);
                } else if (mode == 2U) { // fix first bank at $8000, switch 16 KiB at $C000
                    map_prg(0x8000U, 0U);
                    map_prg(0xC000U, prg_bank_);
                } else { // mode 3: switch 16 KiB at $8000, fix last bank at $C000
                    map_prg(0x8000U, prg_bank_);
                    map_prg(0xC000U, prg_count_ - 1U);
                }
            }

            void apply_chr() {
                if (chr_is_ram_ || chr_4k_count_ == 0U) {
                    return; // CHR-RAM: the 8 KiB window is the RAM itself
                }
                if ((control_ & 0x10U) == 0U) { // 8 KiB CHR (low bit of chr_bank0 ignored)
                    copy_chr_4k(0U, static_cast<std::size_t>(chr_bank0_ & 0x1EU));
                    copy_chr_4k(1U, static_cast<std::size_t>(chr_bank0_ & 0x1EU) + 1U);
                } else { // two independent 4 KiB banks
                    copy_chr_4k(0U, chr_bank0_);
                    copy_chr_4k(1U, chr_bank1_);
                }
            }

            // Copy a 4 KiB CHR-ROM bank into half `half` (0 or 1) of the 8 KiB window.
            void copy_chr_4k(std::size_t half, std::size_t bank4) {
                const std::size_t src = (bank4 % chr_4k_count_) * k_chr_4k;
                std::copy_n(chr_.begin() + static_cast<std::ptrdiff_t>(src), k_chr_4k,
                            chr_window_.begin() + static_cast<std::ptrdiff_t>(half * k_chr_4k));
            }

            std::size_t prg_count_;
            std::size_t chr_4k_count_;
            std::uint8_t control_{0x0CU};
            std::uint8_t shift_{};
            std::uint8_t count_{};
            std::uint8_t chr_bank0_{};
            std::uint8_t chr_bank1_{};
            std::uint8_t prg_bank_{};
            std::array<std::uint8_t, 0x2000U> chr_window_{};
        };
    } // namespace

    std::unique_ptr<nes_mapper> make_mapper(int number, topology::bus& bus,
                                            chips::video::ppu2c02& ppu,
                                            std::span<const std::uint8_t> prg,
                                            std::span<std::uint8_t> chr, bool chr_is_ram) {
        switch (number) {
        case 1:
            return std::make_unique<mmc1_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 2:
            return std::make_unique<uxrom_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 0:
        default:
            return std::make_unique<nrom_mapper>(bus, ppu, prg, chr, chr_is_ram);
        }
    }

} // namespace mnemos::manifests::nes
