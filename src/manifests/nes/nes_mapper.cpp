#include "nes_mapper.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace mnemos::manifests::nes {

    namespace {
        constexpr std::size_t k_prg_bank = 0x4000U; // 16 KiB
        constexpr std::size_t k_prg_8k = 0x2000U;   // 8 KiB PRG bank (MMC3)
        constexpr std::size_t k_prg_32k = 0x8000U;  // 32 KiB PRG bank (AxROM)
        constexpr std::size_t k_chr_4k = 0x1000U;   // 4 KiB CHR bank
        constexpr std::size_t k_chr_1k = 0x0400U;   // 1 KiB CHR bank (MMC3)
        constexpr std::size_t k_chr_8k = 0x2000U;   // 8 KiB CHR bank (CNROM)

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

        // MMC3 (iNES 4): bank-select ($8000 even) names one of eight registers +
        // the PRG/CHR bank modes; bank-data ($8001 odd) writes it. R0-R1 are 2 KiB
        // CHR banks, R2-R5 1 KiB CHR banks, R6-R7 8 KiB PRG banks. PRG is mapped as
        // four 8 KiB windows ($8000/$A000/$C000/$E000, two of them fixed to the
        // last / second-last bank); CHR is composed 1 KiB at a time into an 8 KiB
        // window the PPU reads. Mirroring is set by $A000. The scanline IRQ
        // ($C000-$E001) registers are latched but not yet fired (a later step).
        class mmc3_mapper final : public nes_mapper {
          public:
            mmc3_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                        std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                        bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), prg_8k_count_(prg.size() / k_prg_8k),
                  chr_1k_count_(chr.size() / k_chr_1k) {}

            void reset() override {
                bank_select_ = 0U;
                mirror_reg_ = 0U;
                regs_.fill(0U);
                if (prg_8k_count_ != 0U) {
                    for (const std::uint32_t slot : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
                        bus_->map_rom(slot, prg_.subspan(0, k_prg_8k));
                    }
                }
                if (chr_is_ram_) {
                    ppu_->attach_chr_ram(chr_);
                } else {
                    ppu_->attach_chr(std::span<const std::uint8_t>(chr_window_));
                }
                apply();
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                switch (addr & 0xE001U) {
                case 0x8000U:
                    bank_select_ = value;
                    apply();
                    break;
                case 0x8001U:
                    regs_[bank_select_ & 0x07U] = value;
                    apply();
                    break;
                case 0xA000U:
                    mirror_reg_ = value;
                    apply_mirroring();
                    break;
                case 0xA001U:
                    break; // PRG-RAM protect -- not modelled
                case 0xC000U:
                    irq_latch_ = value;
                    break;
                case 0xC001U:
                    irq_counter_ = 0U; // reload on the next scanline clock
                    irq_reload_ = true;
                    break;
                case 0xE000U:
                    irq_enabled_ = false;
                    raise_irq(false); // disable acknowledges any pending IRQ
                    break;
                case 0xE001U:
                    irq_enabled_ = true;
                    break;
                default:
                    break;
                }
            }

            // Clocked once per visible scanline (an approximation of the PPU A12
            // rises the real MMC3 counts). When the counter reaches zero with the
            // IRQ enabled it asserts /IRQ -- the game's handler does its mid-frame
            // split. (The split's pixels need per-scanline rendering to look right;
            // this at least lets the games that wait on the IRQ run.)
            void clock_scanline(std::uint32_t line) override {
                if (line >= 240U) {
                    return; // visible scanlines only
                }
                if (irq_counter_ == 0U || irq_reload_) {
                    irq_counter_ = irq_latch_;
                    irq_reload_ = false;
                } else {
                    --irq_counter_;
                }
                if (irq_counter_ == 0U && irq_enabled_) {
                    raise_irq(true);
                }
            }

          private:
            void apply() {
                apply_prg();
                apply_chr();
                apply_mirroring();
            }

            void map_prg8(std::uint32_t slot, std::size_t bank8) {
                if (prg_8k_count_ == 0U) {
                    return;
                }
                bus_->retarget_rom(slot,
                                   prg_.subspan((bank8 % prg_8k_count_) * k_prg_8k, k_prg_8k));
            }

            void apply_prg() {
                if (prg_8k_count_ == 0U) {
                    return;
                }
                const std::size_t last = prg_8k_count_ - 1U;
                const std::size_t second_last = prg_8k_count_ >= 2U ? prg_8k_count_ - 2U : last;
                if ((bank_select_ & 0x40U) == 0U) { // mode 0: R6 @ $8000, fixed @ $C000
                    map_prg8(0x8000U, regs_[6]);
                    map_prg8(0xA000U, regs_[7]);
                    map_prg8(0xC000U, second_last);
                } else { // mode 1: fixed @ $8000, R6 @ $C000
                    map_prg8(0x8000U, second_last);
                    map_prg8(0xA000U, regs_[7]);
                    map_prg8(0xC000U, regs_[6]);
                }
                map_prg8(0xE000U, last); // the last bank is always fixed at $E000
            }

            void apply_chr() {
                if (chr_is_ram_ || chr_1k_count_ == 0U) {
                    return;
                }
                // Eight 1 KiB CHR slots. R0/R1 are 2 KiB banks (low bit ignored);
                // bit 7 of bank-select swaps the $0000 and $1000 halves.
                std::array<std::size_t, 8> slot{};
                const std::size_t r0 = regs_[0] & 0xFEU;
                const std::size_t r1 = regs_[1] & 0xFEU;
                const std::array<std::size_t, 8> normal = {r0,       r0 + 1U,  r1,       r1 + 1U,
                                                           regs_[2], regs_[3], regs_[4], regs_[5]};
                if ((bank_select_ & 0x80U) == 0U) {
                    slot = normal;
                } else { // A12 inversion: 1 KiB banks at $0000, 2 KiB banks at $1000
                    slot = {regs_[2], regs_[3], regs_[4], regs_[5], r0, r0 + 1U, r1, r1 + 1U};
                }
                for (std::size_t s = 0; s < slot.size(); ++s) {
                    const std::size_t src = (slot[s] % chr_1k_count_) * k_chr_1k;
                    std::copy_n(chr_.begin() + static_cast<std::ptrdiff_t>(src), k_chr_1k,
                                chr_window_.begin() + static_cast<std::ptrdiff_t>(s * k_chr_1k));
                }
            }

            void apply_mirroring() {
                using m = chips::video::ppu2c02::mirroring;
                // MMC3 $A000 bit 0: 0 = vertical, 1 = horizontal.
                ppu_->set_mirroring((mirror_reg_ & 0x01U) != 0U ? m::horizontal : m::vertical);
            }

            std::size_t prg_8k_count_;
            std::size_t chr_1k_count_;
            std::array<std::uint8_t, 8> regs_{};
            std::uint8_t bank_select_{};
            std::uint8_t mirror_reg_{};
            std::uint8_t irq_latch_{};
            std::uint8_t irq_counter_{};
            bool irq_reload_{};
            bool irq_enabled_{};
            std::array<std::uint8_t, 0x2000U> chr_window_{};
        };

        // CNROM (iNES 3): fixed PRG, 8 KiB CHR-bank switching. A write to
        // $8000-$FFFF selects which 8 KiB CHR-ROM bank the PPU sees (no PRG
        // banking, no mirroring control). The bank is a contiguous CHR subspan,
        // so it just re-points the PPU's CHR window.
        class cnrom_mapper final : public nes_mapper {
          public:
            cnrom_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                         std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                         bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_8k_count_(chr.size() / k_chr_8k) {
            }

            void reset() override {
                if (!prg_.empty()) {
                    bus_->map_rom(0x8000U, prg_);
                    if (prg_.size() <= k_prg_bank) {
                        bus_->map_rom(0xC000U, prg_); // mirror a 16 KiB image
                    }
                }
                select_chr(0U);
                install_register_write_hook();
            }

            void write(std::uint16_t /*addr*/, std::uint8_t value) override { select_chr(value); }

          private:
            void select_chr(std::uint8_t bank) noexcept {
                if (chr_is_ram_ || chr_8k_count_ == 0U) {
                    attach_chr(); // CHR-RAM / no banks: the whole window
                    return;
                }
                const std::size_t b = bank % chr_8k_count_;
                ppu_->attach_chr(
                    std::span<const std::uint8_t>(chr_.subspan(b * k_chr_8k, k_chr_8k)));
            }
            std::size_t chr_8k_count_;
        };

        // AxROM (iNES 7): a single switchable 32 KiB PRG bank over $8000-$FFFF +
        // single-screen mirroring select; CHR is normally 8 KiB CHR-RAM. A write
        // to $8000-$FFFF sets the PRG bank (bits 0-2) and the nametable (bit 4).
        class axrom_mapper final : public nes_mapper {
          public:
            axrom_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                         std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                         bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram),
                  prg_32k_count_(prg.size() / k_prg_32k) {}

            void reset() override {
                if (prg_32k_count_ != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_32k)); // 32 KiB bank 0
                }
                attach_chr();
                ppu_->set_mirroring(chips::video::ppu2c02::mirroring::single_a);
                install_register_write_hook();
            }

            void write(std::uint16_t /*addr*/, std::uint8_t value) override {
                if (prg_32k_count_ != 0U) {
                    const std::size_t bank = (value & 0x07U) % prg_32k_count_;
                    bus_->retarget_rom(0x8000U, prg_.subspan(bank * k_prg_32k, k_prg_32k));
                }
                ppu_->set_mirroring((value & 0x10U) != 0U
                                        ? chips::video::ppu2c02::mirroring::single_b
                                        : chips::video::ppu2c02::mirroring::single_a);
            }

          private:
            std::size_t prg_32k_count_;
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
        case 3:
            return std::make_unique<cnrom_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 4:
            return std::make_unique<mmc3_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 7:
            return std::make_unique<axrom_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 0:
        default:
            return std::make_unique<nrom_mapper>(bus, ppu, prg, chr, chr_is_ram);
        }
    }

} // namespace mnemos::manifests::nes
