#include "nes_mapper.hpp"

#include "mmc5.hpp"   // chips::audio::mmc5 (the MMC5's 2 pulse + raw PCM sound block)
#include "n163.hpp"   // chips::audio::n163 (the Namco 163's wavetable sound block)
#include "ssg.hpp"    // chips::audio::ssg (the Sunsoft 5B's YM2149 sound block)
#include "vrc6.hpp"   // chips::audio::vrc6 (the VRC6's pulse + sawtooth sound)
#include "ym2413.hpp" // chips::audio::ym2413 (the VRC7's OPLL sound block)

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
                bank_ = value;
                map_bank();
            }

            void save_state(chips::state_writer& writer) const override { writer.u8(bank_); }
            void load_state(chips::state_reader& reader) override {
                bank_ = reader.u8();
                map_bank();
            }

          private:
            void map_bank() {
                if (bank_count_ == 0U) {
                    return;
                }
                bus_->retarget_rom(0x8000U,
                                   prg_.subspan((bank_ % bank_count_) * k_prg_bank, k_prg_bank));
            }
            [[nodiscard]] std::span<const std::uint8_t> last_bank() const {
                return prg_.subspan((bank_count_ - 1U) * k_prg_bank, k_prg_bank);
            }
            std::size_t bank_count_;
            std::uint8_t bank_{};
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

            void save_state(chips::state_writer& writer) const override {
                writer.u8(control_);
                writer.u8(shift_);
                writer.u8(count_);
                writer.u8(chr_bank0_);
                writer.u8(chr_bank1_);
                writer.u8(prg_bank_);
            }
            void load_state(chips::state_reader& reader) override {
                control_ = reader.u8();
                shift_ = reader.u8();
                count_ = reader.u8();
                chr_bank0_ = reader.u8();
                chr_bank1_ = reader.u8();
                prg_bank_ = reader.u8();
                apply(); // re-point PRG/CHR/mirroring from the restored registers
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
        class mmc3_mapper : public nes_mapper {
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

            void save_state(chips::state_writer& writer) const override {
                writer.u8(bank_select_);
                writer.u8(mirror_reg_);
                for (const std::uint8_t r : regs_) {
                    writer.u8(r);
                }
                writer.u8(irq_latch_);
                writer.u8(irq_counter_);
                writer.boolean(irq_reload_);
                writer.boolean(irq_enabled_);
            }
            void load_state(chips::state_reader& reader) override {
                bank_select_ = reader.u8();
                mirror_reg_ = reader.u8();
                for (std::uint8_t& r : regs_) {
                    r = reader.u8();
                }
                irq_latch_ = reader.u8();
                irq_counter_ = reader.u8();
                irq_reload_ = reader.boolean();
                irq_enabled_ = reader.boolean();
                apply(); // re-point PRG/CHR/mirroring (apply_mirroring is virtual)
            }

          protected:
            // Banking internals are protected so the Namco-118 (iNES 206) subclass
            // can drive the same PRG/CHR layout through a reduced register set.
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

            virtual void apply_mirroring() {
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

        // Namco 118 / DxROM (iNES 206): the MMC3 predecessor. Same eight bank
        // registers and PRG/CHR layout as MMC3 mode 0, but the bank-select byte has
        // no PRG-mode or CHR-A12-inversion bits (6-7), there is no $A000 mirroring
        // register (the header solders it), and there is no scanline IRQ. So it is
        // MMC3 with a reduced register set -- reuse the MMC3 banking wholesale.
        class namco118_mapper final : public mmc3_mapper {
          public:
            using mmc3_mapper::mmc3_mapper;

            void write(std::uint16_t addr, std::uint8_t value) override {
                switch (addr & 0xE001U) {
                case 0x8000U:
                    bank_select_ = value & 0x07U; // bits 6-7 (mode / inversion) absent
                    apply();
                    break;
                case 0x8001U:
                    regs_[bank_select_ & 0x07U] = value;
                    apply();
                    break;
                default:
                    break; // no mirroring register, no IRQ registers
                }
            }

            void clock_scanline(std::uint32_t /*line*/) override {} // no IRQ hardware

          private:
            // Mirroring is fixed by the cartridge wiring (the header), not a register.
            void apply_mirroring() override {}
        };

        // MMC5 (iNES 5): the most capable NES mapper. This core increment models
        // the $5000-$5FFF register window, PRG banking (4 modes, ROM/RAM select),
        // CHR banking (4 modes, sprite-'A' set composed into the 8 KiB window),
        // nametable mirroring ($5105) and the hardware multiplier ($5205/$5206).
        // The CHR sprite/background split, scanline IRQ, ExRAM graphics modes and
        // expansion audio are later increments; ExRAM is backed as plain RAM so a
        // game poking it does not fault.
        class mmc5_mapper final : public nes_mapper {
          public:
            mmc5_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                        std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                        bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), prg_8k_count_(prg.size() / k_prg_8k),
                  chr_1k_count_(chr.size() / k_chr_1k) {}

            void reset() override {
                prg_mode_ = 3U; // power-on: four switchable 8 KiB PRG banks
                chr_mode_ = 0U;
                prg_regs_ = {0xFFU, 0xFFU, 0xFFU, 0xFFU}; // last bank everywhere
                chr_regs_.fill(0U);
                chr_regs_b_.fill(0U);
                nametable_mode_ = 0x00U;
                mult_a_ = mult_b_ = 0U;
                irq_target_ = 0U;
                irq_enabled_ = irq_pending_ = in_frame_ = false;
                exram_.fill(0U);

                sound_.reset(chips::reset_kind::power_on);
                sound_.set_clock_divider(37);
                sound_.enable_audio_capture(true);

                if (prg_8k_count_ != 0U) {
                    for (const std::uint32_t slot : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
                        bus_->map_rom(slot, prg_.subspan(0, k_prg_8k));
                    }
                }
                ppu_->attach_chr(std::span<const std::uint8_t>(chr_window_));
                ppu_->attach_chr_bg(std::span<const std::uint8_t>(chr_window_bg_));

                // The MMC5 control + ExRAM window. Reads serve ExRAM ($5C00-$5FFF)
                // and the multiplier ($5205/$5206); writes hit the register file.
                bus_->map_mmio(
                    0x5000U, 0x1000U,
                    [this](std::uint32_t addr) -> std::uint8_t {
                        if (addr >= 0x5C00U) {
                            return exram_[addr - 0x5C00U];
                        }
                        if (addr == 0x5015U) { // expansion-audio length status
                            return sound_.read_status();
                        }
                        if (addr == 0x5204U) {
                            // IRQ status: bit 7 = pending, bit 6 = in-frame. Reading
                            // acknowledges the IRQ (clears it + drops the line).
                            const std::uint8_t s = static_cast<std::uint8_t>(
                                (irq_pending_ ? 0x80U : 0x00U) | (in_frame_ ? 0x40U : 0x00U));
                            irq_pending_ = false;
                            raise_irq(false);
                            return s;
                        }
                        const std::uint16_t product = static_cast<std::uint16_t>(
                            static_cast<std::uint16_t>(mult_a_) * mult_b_);
                        if (addr == 0x5205U) {
                            return static_cast<std::uint8_t>(product & 0xFFU);
                        }
                        if (addr == 0x5206U) {
                            return static_cast<std::uint8_t>(product >> 8U);
                        }
                        return 0x00U;
                    },
                    [this](std::uint32_t addr, std::uint8_t value) {
                        write(static_cast<std::uint16_t>(addr), value);
                    });

                apply_prg();
                apply_chr();
                apply_chr_bg();
                apply_mirroring();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                if (addr >= 0x5C00U) { // ExRAM (backed as plain RAM this increment)
                    exram_[addr - 0x5C00U] = value;
                    return;
                }
                if (addr >= 0x5000U && addr <= 0x5015U) { // expansion audio
                    sound_.write_reg(addr, value);
                    return;
                }
                switch (addr) {
                case 0x5100U:
                    prg_mode_ = static_cast<std::uint8_t>(value & 0x03U);
                    apply_prg();
                    break;
                case 0x5101U:
                    chr_mode_ = static_cast<std::uint8_t>(value & 0x03U);
                    apply_chr();
                    apply_chr_bg();
                    break;
                case 0x5105U:
                    nametable_mode_ = value;
                    apply_mirroring();
                    break;
                case 0x5114U:
                case 0x5115U:
                case 0x5116U:
                case 0x5117U:
                    prg_regs_[addr - 0x5114U] = value;
                    apply_prg();
                    break;
                case 0x5120U:
                case 0x5121U:
                case 0x5122U:
                case 0x5123U:
                case 0x5124U:
                case 0x5125U:
                case 0x5126U:
                case 0x5127U:
                    chr_regs_[addr - 0x5120U] = value;
                    apply_chr();
                    break;
                case 0x5128U:
                case 0x5129U:
                case 0x512AU:
                case 0x512BU:
                    chr_regs_b_[addr - 0x5128U] = value;
                    apply_chr_bg();
                    break;
                case 0x5203U:
                    irq_target_ = value; // scanline to interrupt on
                    break;
                case 0x5204U:
                    irq_enabled_ = (value & 0x80U) != 0U;
                    if (!irq_enabled_) {
                        raise_irq(false);
                    }
                    break;
                case 0x5205U:
                    mult_a_ = value;
                    break;
                case 0x5206U:
                    mult_b_ = value;
                    break;
                default:
                    break; // $5113/$5130/vertical split/expansion audio: unused by CV3
                }
            }

            // The MMC5 scanline counter (clocked once per visible line by the board)
            // fires the IRQ when it reaches the $5203 target -- this is how CV3 times
            // its mid-frame CHR-bank swaps and the status-bar split.
            void clock_scanline(std::uint32_t line) override {
                if (line >= 240U) {
                    in_frame_ = false; // left the visible region
                    return;
                }
                in_frame_ = true;
                if (line == irq_target_) {
                    irq_pending_ = true;
                    if (irq_enabled_) {
                        raise_irq(true);
                    }
                }
            }

            [[nodiscard]] chips::ichip* expansion_audio() noexcept override { return &sound_; }
            [[nodiscard]] std::size_t expansion_audio_pending() const noexcept override {
                return sound_.pending_samples();
            }
            std::size_t drain_expansion_audio(std::int16_t* out,
                                              std::size_t max_pairs) noexcept override {
                return sound_.drain_samples(out, max_pairs);
            }

            void save_state(chips::state_writer& writer) const override {
                writer.u8(prg_mode_);
                writer.u8(chr_mode_);
                for (const std::uint8_t r : prg_regs_) {
                    writer.u8(r);
                }
                for (const std::uint8_t r : chr_regs_) {
                    writer.u8(r);
                }
                for (const std::uint8_t r : chr_regs_b_) {
                    writer.u8(r);
                }
                writer.u8(nametable_mode_);
                writer.u8(mult_a_);
                writer.u8(mult_b_);
                writer.u8(irq_target_);
                writer.boolean(irq_enabled_);
                writer.boolean(irq_pending_);
                writer.boolean(in_frame_);
                for (const std::uint8_t b : exram_) {
                    writer.u8(b);
                }
                sound_.save_state(writer);
            }
            void load_state(chips::state_reader& reader) override {
                prg_mode_ = reader.u8();
                chr_mode_ = reader.u8();
                for (std::uint8_t& r : prg_regs_) {
                    r = reader.u8();
                }
                for (std::uint8_t& r : chr_regs_) {
                    r = reader.u8();
                }
                for (std::uint8_t& r : chr_regs_b_) {
                    r = reader.u8();
                }
                nametable_mode_ = reader.u8();
                mult_a_ = reader.u8();
                mult_b_ = reader.u8();
                irq_target_ = reader.u8();
                irq_enabled_ = reader.boolean();
                irq_pending_ = reader.boolean();
                in_frame_ = reader.boolean();
                for (std::uint8_t& b : exram_) {
                    b = reader.u8();
                }
                sound_.load_state(reader);
                apply_prg(); // re-point PRG/CHR/mirroring from the restored registers
                apply_chr();
                apply_chr_bg();
                apply_mirroring();
            }

          private:
            void map_prg8(std::uint32_t slot, std::size_t bank8) {
                if (prg_8k_count_ == 0U) {
                    return;
                }
                bus_->retarget_rom(slot,
                                   prg_.subspan((bank8 % prg_8k_count_) * k_prg_8k, k_prg_8k));
            }
            // Map an 8 KiB window from a $5114-$5117 register: a ROM bank (bits 0-6)
            // when bit 7 is set ($5117 is always ROM). A RAM bank (bit 7 clear) falls
            // back to ROM bank 0 in this increment -- CV3 keeps work RAM at $6000 and
            // its code in ROM, so no $8000-region RAM is needed yet.
            void map_window(std::uint32_t slot, std::uint8_t reg, bool force_rom) {
                const bool rom = force_rom || (reg & 0x80U) != 0U;
                map_prg8(slot, rom ? static_cast<std::size_t>(reg & 0x7FU) : 0U);
            }

            void apply_prg() {
                switch (prg_mode_) {
                case 0: { // one 32 KiB bank from $5117 (8 KiB granular, 32 KiB aligned)
                    const std::size_t base = static_cast<std::size_t>(prg_regs_[3] & 0x7CU);
                    map_prg8(0x8000U, base + 0U);
                    map_prg8(0xA000U, base + 1U);
                    map_prg8(0xC000U, base + 2U);
                    map_prg8(0xE000U, base + 3U);
                    break;
                }
                case 1: { // two 16 KiB banks: $5115 @ $8000, $5117 @ $C000
                    const std::size_t a = static_cast<std::size_t>(prg_regs_[1] & 0x7EU);
                    const std::size_t b = static_cast<std::size_t>(prg_regs_[3] & 0x7EU);
                    const bool a_rom = (prg_regs_[1] & 0x80U) != 0U;
                    map_prg8(0x8000U, a_rom ? a : 0U);
                    map_prg8(0xA000U, a_rom ? a + 1U : 0U);
                    map_prg8(0xC000U, b);
                    map_prg8(0xE000U, b + 1U);
                    break;
                }
                case 2: { // 16 KiB $5115 @ $8000, 8 KiB $5116 @ $C000, 8 KiB $5117 @ $E000
                    const std::size_t a = static_cast<std::size_t>(prg_regs_[1] & 0x7EU);
                    const bool a_rom = (prg_regs_[1] & 0x80U) != 0U;
                    map_prg8(0x8000U, a_rom ? a : 0U);
                    map_prg8(0xA000U, a_rom ? a + 1U : 0U);
                    map_window(0xC000U, prg_regs_[2], false);
                    map_window(0xE000U, prg_regs_[3], true);
                    break;
                }
                default: { // mode 3: four 8 KiB banks $5114-$5117
                    map_window(0x8000U, prg_regs_[0], false);
                    map_window(0xA000U, prg_regs_[1], false);
                    map_window(0xC000U, prg_regs_[2], false);
                    map_window(0xE000U, prg_regs_[3], true);
                    break;
                }
                }
            }

            void copy_chr_1k(std::size_t slot, std::size_t bank1k) {
                if (chr_1k_count_ == 0U) {
                    return;
                }
                const std::size_t src = (bank1k % chr_1k_count_) * k_chr_1k;
                std::copy_n(chr_.begin() + static_cast<std::ptrdiff_t>(src), k_chr_1k,
                            chr_window_.begin() + static_cast<std::ptrdiff_t>(slot * k_chr_1k));
            }

            void apply_chr() {
                if (chr_is_ram_ || chr_1k_count_ == 0U) {
                    return;
                }
                switch (chr_mode_) {
                case 0: // 8 KiB bank from $5127
                    for (std::size_t s = 0; s < 8U; ++s) {
                        copy_chr_1k(s, static_cast<std::size_t>(chr_regs_[7]) * 8U + s);
                    }
                    break;
                case 1: // two 4 KiB banks: $5123 ($0000), $5127 ($1000)
                    for (std::size_t s = 0; s < 4U; ++s) {
                        copy_chr_1k(s, static_cast<std::size_t>(chr_regs_[3]) * 4U + s);
                        copy_chr_1k(4U + s, static_cast<std::size_t>(chr_regs_[7]) * 4U + s);
                    }
                    break;
                case 2: // four 2 KiB banks: $5121, $5123, $5125, $5127
                    for (std::size_t p = 0; p < 4U; ++p) {
                        const std::size_t reg = chr_regs_[p * 2U + 1U];
                        copy_chr_1k(p * 2U, reg * 2U);
                        copy_chr_1k(p * 2U + 1U, reg * 2U + 1U);
                    }
                    break;
                default: // mode 3: eight 1 KiB banks $5120-$5127
                    for (std::size_t s = 0; s < 8U; ++s) {
                        copy_chr_1k(s, chr_regs_[s]);
                    }
                    break;
                }
            }

            void copy_chr_bg_1k(std::size_t slot, std::size_t bank1k) {
                if (chr_1k_count_ == 0U) {
                    return;
                }
                const std::size_t src = (bank1k % chr_1k_count_) * k_chr_1k;
                std::copy_n(chr_.begin() + static_cast<std::ptrdiff_t>(src), k_chr_1k,
                            chr_window_bg_.begin() + static_cast<std::ptrdiff_t>(slot * k_chr_1k));
            }

            // Compose the background CHR window from set B ($5128-$512B): four banks
            // cover the lower 4 KiB at the active granularity; the upper 4 KiB mirror
            // them (set B is a 4 KiB bank set the PPU sees twice).
            void apply_chr_bg() {
                if (chr_is_ram_ || chr_1k_count_ == 0U) {
                    return;
                }
                switch (chr_mode_) {
                case 0:
                case 1: // 4 KiB low half from $512B
                    for (std::size_t s = 0; s < 4U; ++s) {
                        copy_chr_bg_1k(s, static_cast<std::size_t>(chr_regs_b_[3]) * 4U + s);
                    }
                    break;
                case 2: // two 2 KiB: $5129, $512B
                    copy_chr_bg_1k(0, static_cast<std::size_t>(chr_regs_b_[1]) * 2U);
                    copy_chr_bg_1k(1, static_cast<std::size_t>(chr_regs_b_[1]) * 2U + 1U);
                    copy_chr_bg_1k(2, static_cast<std::size_t>(chr_regs_b_[3]) * 2U);
                    copy_chr_bg_1k(3, static_cast<std::size_t>(chr_regs_b_[3]) * 2U + 1U);
                    break;
                default: // mode 3: four 1 KiB banks $5128-$512B
                    for (std::size_t s = 0; s < 4U; ++s) {
                        copy_chr_bg_1k(s, chr_regs_b_[s]);
                    }
                    break;
                }
                // The upper 4 KiB mirror the lower 4 KiB.
                std::copy_n(chr_window_bg_.begin(), k_chr_4k,
                            chr_window_bg_.begin() + static_cast<std::ptrdiff_t>(k_chr_4k));
            }

            void apply_mirroring() {
                using m = chips::video::ppu2c02::mirroring;
                // Translate the standard $5105 nametable patterns. ExRAM/fill-backed
                // nametables arrive with the ExRAM increment; approximate to single A.
                switch (nametable_mode_) {
                case 0x50U:
                    ppu_->set_mirroring(m::horizontal);
                    break;
                case 0x44U:
                    ppu_->set_mirroring(m::vertical);
                    break;
                case 0x55U:
                    ppu_->set_mirroring(m::single_b);
                    break;
                case 0x00U:
                default:
                    ppu_->set_mirroring(m::single_a);
                    break;
                }
            }

            std::size_t prg_8k_count_;
            std::size_t chr_1k_count_;
            chips::audio::mmc5 sound_; // $5000-$5015 expansion audio
            std::uint8_t prg_mode_{3U};
            std::uint8_t chr_mode_{};
            std::array<std::uint8_t, 4> prg_regs_{0xFFU, 0xFFU, 0xFFU, 0xFFU};
            std::array<std::uint8_t, 8> chr_regs_{};   // sprite CHR set A ($5120-$5127)
            std::array<std::uint8_t, 4> chr_regs_b_{}; // background CHR set B ($5128-$512B)
            std::uint8_t nametable_mode_{};
            std::uint8_t mult_a_{};
            std::uint8_t mult_b_{};
            std::uint8_t irq_target_{}; // $5203: scanline to fire the IRQ on
            bool irq_enabled_{};        // $5204 bit 7
            bool irq_pending_{};        // latched at the target scanline; cleared on $5204 read
            bool in_frame_{};           // $5204 bit 6: the PPU is in the visible region
            std::array<std::uint8_t, 0x2000U> chr_window_{};    // composed set A (sprites)
            std::array<std::uint8_t, 0x2000U> chr_window_bg_{}; // composed set B (background)
            std::array<std::uint8_t, 0x400U> exram_{};
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

            void write(std::uint16_t /*addr*/, std::uint8_t value) override {
                chr_bank_ = value;
                select_chr(chr_bank_);
            }

            void save_state(chips::state_writer& writer) const override { writer.u8(chr_bank_); }
            void load_state(chips::state_reader& reader) override {
                chr_bank_ = reader.u8();
                select_chr(chr_bank_);
            }

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
            std::uint8_t chr_bank_{};
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
                bank_value_ = value;
                apply();
            }

            void save_state(chips::state_writer& writer) const override { writer.u8(bank_value_); }
            void load_state(chips::state_reader& reader) override {
                bank_value_ = reader.u8();
                apply();
            }

          private:
            void apply() {
                if (prg_32k_count_ != 0U) {
                    const std::size_t bank = (bank_value_ & 0x07U) % prg_32k_count_;
                    bus_->retarget_rom(0x8000U, prg_.subspan(bank * k_prg_32k, k_prg_32k));
                }
                ppu_->set_mirroring((bank_value_ & 0x10U) != 0U
                                        ? chips::video::ppu2c02::mirroring::single_b
                                        : chips::video::ppu2c02::mirroring::single_a);
            }
            std::size_t prg_32k_count_;
            std::uint8_t bank_value_{};
        };

        // GxROM / GNROM (iNES 66): one $8000-$FFFF register switches both a 32 KiB
        // PRG bank (bits 5-4) and an 8 KiB CHR bank (bits 1-0). No mirroring control.
        class gxrom_mapper final : public nes_mapper {
          public:
            gxrom_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                         std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                         bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram),
                  prg_32k_count_(prg.size() / k_prg_32k), chr_8k_count_(chr.size() / k_chr_8k) {}

            void reset() override {
                if (prg_32k_count_ != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_32k));
                }
                bank_ = 0U;
                apply();
                install_register_write_hook();
            }
            void write(std::uint16_t /*addr*/, std::uint8_t value) override {
                bank_ = value;
                apply();
            }
            void save_state(chips::state_writer& writer) const override { writer.u8(bank_); }
            void load_state(chips::state_reader& reader) override {
                bank_ = reader.u8();
                apply();
            }

          private:
            void apply() {
                if (prg_32k_count_ != 0U) {
                    const std::size_t pb = ((bank_ >> 4U) & 0x03U) % prg_32k_count_;
                    bus_->retarget_rom(0x8000U, prg_.subspan(pb * k_prg_32k, k_prg_32k));
                }
                if (chr_is_ram_ || chr_8k_count_ == 0U) {
                    attach_chr();
                } else {
                    const std::size_t cb = (bank_ & 0x03U) % chr_8k_count_;
                    ppu_->attach_chr(
                        std::span<const std::uint8_t>(chr_.subspan(cb * k_chr_8k, k_chr_8k)));
                }
            }
            std::size_t prg_32k_count_;
            std::size_t chr_8k_count_;
            std::uint8_t bank_{};
        };

        // Color Dreams (iNES 11): one $8000-$FFFF register switches a 32 KiB PRG bank
        // (bits 1-0) and an 8 KiB CHR bank (bits 7-4). Header mirroring.
        class color_dreams_mapper final : public nes_mapper {
          public:
            color_dreams_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                                std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                                bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram),
                  prg_32k_count_(prg.size() / k_prg_32k), chr_8k_count_(chr.size() / k_chr_8k) {}

            void reset() override {
                if (prg_32k_count_ != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_32k));
                }
                bank_ = 0U;
                apply();
                install_register_write_hook();
            }
            void write(std::uint16_t /*addr*/, std::uint8_t value) override {
                bank_ = value;
                apply();
            }
            void save_state(chips::state_writer& writer) const override { writer.u8(bank_); }
            void load_state(chips::state_reader& reader) override {
                bank_ = reader.u8();
                apply();
            }

          private:
            void apply() {
                if (prg_32k_count_ != 0U) {
                    const std::size_t pb = (bank_ & 0x03U) % prg_32k_count_;
                    bus_->retarget_rom(0x8000U, prg_.subspan(pb * k_prg_32k, k_prg_32k));
                }
                if (chr_is_ram_ || chr_8k_count_ == 0U) {
                    attach_chr();
                } else {
                    const std::size_t cb = ((bank_ >> 4U) & 0x0FU) % chr_8k_count_;
                    ppu_->attach_chr(
                        std::span<const std::uint8_t>(chr_.subspan(cb * k_chr_8k, k_chr_8k)));
                }
            }
            std::size_t prg_32k_count_;
            std::size_t chr_8k_count_;
            std::uint8_t bank_{};
        };

        // Camerica / Codemasters BF909x (iNES 71): a switchable 16 KiB PRG bank at
        // $8000 over the fixed last bank at $C000 (UxROM-style) with 8 KiB CHR-RAM.
        // The bank register responds across $8000-$FFFF; the Fire Hawk variant carves
        // out $9000-$9FFF for single-screen mirroring (bit 4).
        class camerica_mapper final : public nes_mapper {
          public:
            camerica_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                            std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                            bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram),
                  prg_16k_count_(prg.size() / k_prg_bank) {}

            void reset() override {
                bank_ = 0U;
                if (prg_16k_count_ != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_bank));
                    bus_->map_rom(0xC000U, last_bank()); // fixed last 16 KiB
                }
                attach_chr(); // 8 KiB CHR-RAM
                apply_prg();
                install_register_write_hook();
            }
            void write(std::uint16_t addr, std::uint8_t value) override {
                if (addr >= 0x9000U && addr < 0xA000U) { // Fire Hawk single-screen mirroring
                    ppu_->set_mirroring((value & 0x10U) != 0U
                                            ? chips::video::ppu2c02::mirroring::single_b
                                            : chips::video::ppu2c02::mirroring::single_a);
                } else { // PRG bank at $8000
                    bank_ = value;
                    apply_prg();
                }
            }
            void save_state(chips::state_writer& writer) const override { writer.u8(bank_); }
            void load_state(chips::state_reader& reader) override {
                bank_ = reader.u8();
                apply_prg();
            }

          private:
            [[nodiscard]] std::span<const std::uint8_t> last_bank() const noexcept {
                return prg_.subspan((prg_16k_count_ - 1U) * k_prg_bank, k_prg_bank);
            }
            void apply_prg() {
                if (prg_16k_count_ != 0U) {
                    const std::size_t b = bank_ % prg_16k_count_;
                    bus_->retarget_rom(0x8000U, prg_.subspan(b * k_prg_bank, k_prg_bank));
                }
            }
            std::size_t prg_16k_count_;
            std::uint8_t bank_{};
        };

        // Sunsoft FME-7 / 5B (iNES 69). Banking: three switchable 8 KiB PRG banks
        // ($8000/$A000/$C000) over a fixed last bank ($E000); eight switchable 1 KiB
        // CHR banks composed into the 8 KiB window; programmable mirroring; and a
        // 16-bit CPU-cycle down-counter IRQ. The board has two register pairs: the
        // BANKING ports at $8000 (command 0-F) + $A000 (parameter), and -- on the
        // "5B" variant -- the AUDIO ports at $C000 (YM2149 register select) + $E000
        // (data). The 5B's sound chip is the on-board ssg; the board clocks it at the
        // CPU rate (its native /16 prescaler) and mixes it into the 2A03 output.
        //
        // Not modelled (no local ROM depends on it; documented for the next pass):
        // the command-8 $6000 ROM bank (the manifest maps $6000 as work RAM, which is
        // what Gimmick! and the other 5B carts use). The IRQ counter is advanced once
        // per scanline (clock_cpu_timer), so its precision is scanline-granular.
        class sunsoft5b_mapper final : public nes_mapper {
          public:
            sunsoft5b_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                             std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                             bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), prg_8k_count_(prg.size() / k_prg_8k),
                  chr_1k_count_(chr.size() / k_chr_1k) {}

            void reset() override {
                command_ = 0U;
                chr_banks_.fill(0U);
                prg_banks_ = {0U, 0U, 0U, 0U};
                mirror_ = 0U;
                irq_counter_ = 0U;
                irq_enabled_ = false;
                irq_counter_enabled_ = false;

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

                // The 5B sound chip is clocked at the CPU rate divided by the YM2149's
                // own /16 prescaler; the board enables capture so the adapter can drain
                // and mix it.
                ssg_.reset(chips::reset_kind::power_on);
                ssg_.set_clock_divider(16);
                ssg_.enable_audio_capture(true);

                apply_prg();
                apply_chr();
                apply_mirroring();
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                switch (addr & 0xE000U) {
                case 0x8000U: // command register: which parameter the next $A000 sets
                    command_ = static_cast<std::uint8_t>(value & 0x0FU);
                    break;
                case 0xA000U: // parameter register
                    set_parameter(value);
                    break;
                case 0xC000U: // 5B audio: latch the YM2149 register to access
                    ssg_.address(static_cast<std::uint8_t>(value & 0x0FU));
                    break;
                case 0xE000U: // 5B audio: write the latched register
                    ssg_.write(value);
                    break;
                default:
                    break;
                }
            }

            // The FME-7 IRQ counter free-runs on the CPU clock; advanced once per
            // scanline by the board. It asserts /IRQ when it wraps past $0000 (with
            // both the counter + IRQ enabled); writing command $D acknowledges.
            void clock_cpu_timer(std::uint32_t cpu_cycles) override {
                if (!irq_counter_enabled_) {
                    return;
                }
                if (cpu_cycles > irq_counter_ && irq_enabled_) {
                    raise_irq(true); // the down-counter passed through zero
                }
                irq_counter_ = static_cast<std::uint16_t>((irq_counter_ - cpu_cycles) & 0xFFFFU);
            }

            [[nodiscard]] chips::ichip* expansion_audio() noexcept override { return &ssg_; }
            [[nodiscard]] std::size_t expansion_audio_pending() const noexcept override {
                return ssg_.pending_samples();
            }
            std::size_t drain_expansion_audio(std::int16_t* out,
                                              std::size_t max_pairs) noexcept override {
                return ssg_.drain_samples(out, max_pairs);
            }

            void save_state(chips::state_writer& writer) const override {
                writer.u8(command_);
                for (const std::uint8_t b : chr_banks_) {
                    writer.u8(b);
                }
                for (const std::uint8_t b : prg_banks_) {
                    writer.u8(b);
                }
                writer.u8(mirror_);
                writer.u16(irq_counter_);
                writer.boolean(irq_enabled_);
                writer.boolean(irq_counter_enabled_);
                ssg_.save_state(writer);
            }
            void load_state(chips::state_reader& reader) override {
                command_ = reader.u8();
                for (std::uint8_t& b : chr_banks_) {
                    b = reader.u8();
                }
                for (std::uint8_t& b : prg_banks_) {
                    b = reader.u8();
                }
                mirror_ = reader.u8();
                irq_counter_ = reader.u16();
                irq_enabled_ = reader.boolean();
                irq_counter_enabled_ = reader.boolean();
                ssg_.load_state(reader);
                apply_prg();
                apply_chr();
                apply_mirroring();
            }

          private:
            void set_parameter(std::uint8_t value) {
                if (command_ <= 0x07U) { // commands 0-7: the eight 1 KiB CHR banks
                    chr_banks_[command_] = value;
                    apply_chr();
                    return;
                }
                switch (command_) {
                case 0x08U: // $6000 bank (ROM/RAM select) -- stored; $6000 stays work RAM
                    prg_banks_[0] = value;
                    break;
                case 0x09U: // $8000 8 KiB ROM bank
                    prg_banks_[1] = value;
                    apply_prg();
                    break;
                case 0x0AU: // $A000 8 KiB ROM bank
                    prg_banks_[2] = value;
                    apply_prg();
                    break;
                case 0x0BU: // $C000 8 KiB ROM bank
                    prg_banks_[3] = value;
                    apply_prg();
                    break;
                case 0x0CU: // nametable mirroring
                    mirror_ = static_cast<std::uint8_t>(value & 0x03U);
                    apply_mirroring();
                    break;
                case 0x0DU: // IRQ control: bit 7 = counter enable, bit 0 = IRQ enable
                    irq_counter_enabled_ = (value & 0x80U) != 0U;
                    irq_enabled_ = (value & 0x01U) != 0U;
                    raise_irq(false); // a write to $D acknowledges any pending IRQ
                    break;
                case 0x0EU: // IRQ counter low byte
                    irq_counter_ = static_cast<std::uint16_t>((irq_counter_ & 0xFF00U) | value);
                    break;
                case 0x0FU: // IRQ counter high byte
                    irq_counter_ = static_cast<std::uint16_t>(
                        (irq_counter_ & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U));
                    break;
                default:
                    break;
                }
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
                map_prg8(0x8000U, prg_banks_[1]);
                map_prg8(0xA000U, prg_banks_[2]);
                map_prg8(0xC000U, prg_banks_[3]);
                map_prg8(0xE000U, prg_8k_count_ - 1U); // last bank fixed at $E000
            }

            void apply_chr() {
                if (chr_is_ram_ || chr_1k_count_ == 0U) {
                    return;
                }
                for (std::size_t s = 0; s < 8U; ++s) {
                    const std::size_t src = (chr_banks_[s] % chr_1k_count_) * k_chr_1k;
                    std::copy_n(chr_.begin() + static_cast<std::ptrdiff_t>(src), k_chr_1k,
                                chr_window_.begin() + static_cast<std::ptrdiff_t>(s * k_chr_1k));
                }
            }

            void apply_mirroring() {
                using m = chips::video::ppu2c02::mirroring;
                switch (mirror_) {
                case 0:
                    ppu_->set_mirroring(m::vertical);
                    break;
                case 1:
                    ppu_->set_mirroring(m::horizontal);
                    break;
                case 2:
                    ppu_->set_mirroring(m::single_a);
                    break;
                default:
                    ppu_->set_mirroring(m::single_b);
                    break;
                }
            }

            std::size_t prg_8k_count_;
            std::size_t chr_1k_count_;
            chips::audio::ssg ssg_;
            std::uint8_t command_{};
            std::array<std::uint8_t, 8> chr_banks_{};
            std::array<std::uint8_t, 4> prg_banks_{}; // [0]=$6000 [1]=$8000 [2]=$A000 [3]=$C000
            std::uint8_t mirror_{};
            std::uint16_t irq_counter_{};
            bool irq_enabled_{};
            bool irq_counter_enabled_{};
            std::array<std::uint8_t, 0x2000U> chr_window_{};
        };

        // Konami VRC7 (iNES 85). PRG: three switchable 8 KiB banks ($8000/$A000/
        // $C000) over a fixed last bank ($E000). CHR: eight switchable 1 KiB banks
        // composed into the 8 KiB window. Programmable mirroring + the Konami VRC
        // IRQ (scanline + cycle modes). The on-board sound is a Yamaha OPLL
        // (ym2413), programmed through $9010 (register select) / $9030 (data),
        // clocked at the CPU rate / 36 (~49.7 kHz native) and mixed into the 2A03.
        //
        // Register addresses use A4 (bit 4) to select the second register of each
        // group -- the VRC7a wiring, which the only licensed VRC7 game (Lagrange
        // Point) uses.
        //
        // ⚠ FIDELITY NOTE: the ym2413 carries the OPLL mask-ROM instrument patches,
        // NOT the VRC7's own (different) patch set -- a game's preset instruments are
        // timbrally off, though the FM engine, envelopes, pitch and the user patch
        // are correct. Loading the VRC7 patch table is a follow-up (it would touch
        // the shared ym2413, which the SMS FM unit also uses).
        class vrc7_mapper final : public nes_mapper {
          public:
            vrc7_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                        std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                        bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), prg_8k_count_(prg.size() / k_prg_8k),
                  chr_1k_count_(chr.size() / k_chr_1k) {}

            void reset() override {
                prg_regs_ = {0U, 0U, 0U};
                chr_banks_.fill(0U);
                mirror_ = 0U;
                irq_latch_ = 0U;
                irq_counter_ = 0U;
                irq_prescaler_ = 0;
                irq_enabled_ = false;
                irq_ack_enable_ = false;
                cycle_mode_ = false;

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

                opll_.reset(chips::reset_kind::power_on);
                opll_.set_clock_divider(36); // CPU clock / 36 ~= 49716 Hz OPLL native rate
                opll_.enable_audio_capture(true);

                apply_prg();
                apply_chr();
                apply_mirroring();
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                const std::uint16_t group = addr & 0xF000U;
                const bool a4 = (addr & 0x0010U) != 0U;
                switch (group) {
                case 0x8000U: // $8000 = PRG bank 0, $8010 = PRG bank 1
                    prg_regs_[a4 ? 1U : 0U] = value;
                    apply_prg();
                    break;
                case 0x9000U:
                    if ((addr & 0x0030U) == 0x0000U) { // $9000 = PRG bank 2
                        prg_regs_[2] = value;
                        apply_prg();
                    } else if ((addr & 0x0030U) == 0x0010U) { // $9010 = OPLL register select
                        opll_.write_address(value);
                    } else { // $9030 = OPLL data
                        opll_.write_data(value);
                    }
                    break;
                case 0xA000U: // $A000/$A010 = CHR banks 0/1
                    set_chr(a4 ? 1U : 0U, value);
                    break;
                case 0xB000U:
                    set_chr(a4 ? 3U : 2U, value);
                    break;
                case 0xC000U:
                    set_chr(a4 ? 5U : 4U, value);
                    break;
                case 0xD000U:
                    set_chr(a4 ? 7U : 6U, value);
                    break;
                case 0xE000U:
                    if (!a4) { // $E000 = mirroring (bits 1-0)
                        mirror_ = static_cast<std::uint8_t>(value & 0x03U);
                        apply_mirroring();
                    } else { // $E010 = IRQ latch (reload value)
                        irq_latch_ = value;
                    }
                    break;
                case 0xF000U:
                    if (!a4) { // $F000 = IRQ control
                        write_irq_control(value);
                    } else { // $F010 = IRQ acknowledge
                        irq_acknowledge();
                    }
                    break;
                default:
                    break;
                }
            }

            // The Konami VRC IRQ. In cycle mode the 8-bit counter ticks every CPU
            // cycle; in scanline mode a +3/cycle prescaler clocks it once per ~341
            // cycles (~one scanline). An overflow ($FF -> reload latch) asserts /IRQ.
            void clock_cpu_timer(std::uint32_t cpu_cycles) override {
                if (!irq_enabled_) {
                    return;
                }
                for (std::uint32_t i = 0; i < cpu_cycles; ++i) {
                    if (cycle_mode_) {
                        tick_irq_counter();
                    } else {
                        irq_prescaler_ += 3;
                        if (irq_prescaler_ >= 341) {
                            irq_prescaler_ -= 341;
                            tick_irq_counter();
                        }
                    }
                }
            }

            [[nodiscard]] chips::ichip* expansion_audio() noexcept override { return &opll_; }
            [[nodiscard]] std::size_t expansion_audio_pending() const noexcept override {
                return opll_.pending_samples();
            }
            std::size_t drain_expansion_audio(std::int16_t* out,
                                              std::size_t max_pairs) noexcept override {
                return opll_.drain_samples(out, max_pairs);
            }

            void save_state(chips::state_writer& writer) const override {
                for (const std::uint8_t r : prg_regs_) {
                    writer.u8(r);
                }
                for (const std::uint8_t b : chr_banks_) {
                    writer.u8(b);
                }
                writer.u8(mirror_);
                writer.u8(irq_latch_);
                writer.u8(irq_counter_);
                writer.u32(static_cast<std::uint32_t>(irq_prescaler_));
                writer.boolean(irq_enabled_);
                writer.boolean(irq_ack_enable_);
                writer.boolean(cycle_mode_);
                opll_.save_state(writer);
            }
            void load_state(chips::state_reader& reader) override {
                for (std::uint8_t& r : prg_regs_) {
                    r = reader.u8();
                }
                for (std::uint8_t& b : chr_banks_) {
                    b = reader.u8();
                }
                mirror_ = reader.u8();
                irq_latch_ = reader.u8();
                irq_counter_ = reader.u8();
                irq_prescaler_ = static_cast<int>(reader.u32());
                irq_enabled_ = reader.boolean();
                irq_ack_enable_ = reader.boolean();
                cycle_mode_ = reader.boolean();
                opll_.load_state(reader);
                apply_prg();
                apply_chr();
                apply_mirroring();
            }

          private:
            void set_chr(std::size_t slot, std::uint8_t value) {
                chr_banks_[slot] = value;
                apply_chr();
            }

            void write_irq_control(std::uint8_t value) {
                irq_ack_enable_ = (value & 0x01U) != 0U; // restore-enable on the next ack
                irq_enabled_ = (value & 0x02U) != 0U;
                cycle_mode_ = (value & 0x04U) != 0U;
                if (irq_enabled_) {
                    irq_counter_ = irq_latch_; // reload + restart the prescaler
                    irq_prescaler_ = 0;
                }
                raise_irq(false); // writing control acknowledges any pending IRQ
            }

            void irq_acknowledge() {
                raise_irq(false);
                irq_enabled_ = irq_ack_enable_; // the ack restores enable from bit 0
            }

            void tick_irq_counter() {
                if (irq_counter_ == 0xFFU) {
                    irq_counter_ = irq_latch_;
                    raise_irq(true);
                } else {
                    ++irq_counter_;
                }
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
                map_prg8(0x8000U, prg_regs_[0] & 0x3FU);
                map_prg8(0xA000U, prg_regs_[1] & 0x3FU);
                map_prg8(0xC000U, prg_regs_[2] & 0x3FU);
                map_prg8(0xE000U, prg_8k_count_ - 1U); // last bank fixed at $E000
            }

            void apply_chr() {
                if (chr_is_ram_ || chr_1k_count_ == 0U) {
                    return;
                }
                for (std::size_t s = 0; s < 8U; ++s) {
                    const std::size_t src = (chr_banks_[s] % chr_1k_count_) * k_chr_1k;
                    std::copy_n(chr_.begin() + static_cast<std::ptrdiff_t>(src), k_chr_1k,
                                chr_window_.begin() + static_cast<std::ptrdiff_t>(s * k_chr_1k));
                }
            }

            void apply_mirroring() {
                using m = chips::video::ppu2c02::mirroring;
                switch (mirror_) {
                case 0:
                    ppu_->set_mirroring(m::vertical);
                    break;
                case 1:
                    ppu_->set_mirroring(m::horizontal);
                    break;
                case 2:
                    ppu_->set_mirroring(m::single_a);
                    break;
                default:
                    ppu_->set_mirroring(m::single_b);
                    break;
                }
            }

            std::size_t prg_8k_count_;
            std::size_t chr_1k_count_;
            chips::audio::ym2413 opll_;
            std::array<std::uint8_t, 3> prg_regs_{}; // $8000, $A000, $C000 8 KiB selects
            std::array<std::uint8_t, 8> chr_banks_{};
            std::uint8_t mirror_{};
            std::uint8_t irq_latch_{};   // $E010 reload value
            std::uint8_t irq_counter_{}; // 8-bit up-counter; overflow -> reload + IRQ
            int irq_prescaler_{};        // scanline-mode +3/cycle accumulator
            bool irq_enabled_{};
            bool irq_ack_enable_{}; // $F000 bit 0: enable to restore on the next ack
            bool cycle_mode_{};     // $F000 bit 2: 1 = per-cycle, 0 = per-scanline
            std::array<std::uint8_t, 0x2000U> chr_window_{};
        };

        // Konami VRC6 (iNES 24 = VRC6a, 26 = VRC6b). PRG: a switchable 16 KiB bank
        // ($8000) over a switchable 8 KiB bank ($C000) + the fixed last 8 KiB bank
        // ($E000). CHR: eight 1 KiB banks ($D000-$D003 + $E000-$E003) composed into
        // the window. $B003 sets mirroring; $F000-$F002 are the Konami VRC IRQ (same
        // as VRC7). The on-board sound is the vrc6 chip (2 pulse + sawtooth) at
        // $9000-$9003/$A000-$A002/$B000-$B002. VRC6b swaps the A0/A1 register lines.
        class vrc6_mapper final : public nes_mapper {
          public:
            vrc6_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                        std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                        bool chr_is_ram, bool variant_b) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram),
                  prg_16k_count_(prg.size() / k_prg_bank), prg_8k_count_(prg.size() / k_prg_8k),
                  chr_1k_count_(chr.size() / k_chr_1k), variant_b_(variant_b) {}

            void reset() override {
                prg16_ = 0U;
                prg8_ = 0U;
                chr_banks_.fill(0U);
                ppu_ctrl_ = 0U;
                irq_latch_ = 0U;
                irq_counter_ = 0U;
                irq_prescaler_ = 0;
                irq_enabled_ = false;
                irq_ack_enable_ = false;
                cycle_mode_ = false;

                if (prg_8k_count_ != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_bank)); // 16 KiB switchable
                    bus_->map_rom(0xC000U, prg_.subspan(0, k_prg_8k));   // 8 KiB switchable
                    bus_->map_rom(0xE000U, prg_.subspan(0, k_prg_8k));   // 8 KiB fixed (last)
                }
                if (chr_is_ram_) {
                    ppu_->attach_chr_ram(chr_);
                } else {
                    ppu_->attach_chr(std::span<const std::uint8_t>(chr_window_));
                }

                sound_.reset(chips::reset_kind::power_on);
                sound_.set_clock_divider(37);
                sound_.enable_audio_capture(true);

                apply_prg();
                apply_chr();
                apply_mirroring();
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                const std::uint16_t group = addr & 0xF000U;
                std::uint16_t sub = static_cast<std::uint16_t>(addr & 0x03U);
                if (variant_b_) { // VRC6b swaps A0 and A1
                    sub = static_cast<std::uint16_t>(((sub & 0x01U) << 1U) | ((sub >> 1U) & 0x01U));
                }
                switch (group) {
                case 0x8000U: // PRG 16 KiB bank
                    prg16_ = value;
                    apply_prg();
                    break;
                case 0x9000U: // pulse 1 + global audio control
                case 0xA000U: // pulse 2
                    sound_.write_reg(static_cast<std::uint16_t>(group | sub), value);
                    break;
                case 0xB000U:
                    if (sub == 0x03U) { // $B003: PPU banking mode + mirroring
                        ppu_ctrl_ = value;
                        apply_mirroring();
                    } else { // sawtooth ($B000-$B002)
                        sound_.write_reg(static_cast<std::uint16_t>(0xB000U | sub), value);
                    }
                    break;
                case 0xC000U: // PRG 8 KiB bank
                    prg8_ = value;
                    apply_prg();
                    break;
                case 0xD000U: // CHR banks 0-3
                    chr_banks_[sub] = value;
                    apply_chr();
                    break;
                case 0xE000U: // CHR banks 4-7
                    chr_banks_[4U + sub] = value;
                    apply_chr();
                    break;
                case 0xF000U: // Konami VRC IRQ
                    if (sub == 0x00U) {
                        irq_latch_ = value;
                    } else if (sub == 0x01U) {
                        write_irq_control(value);
                    } else if (sub == 0x02U) {
                        irq_acknowledge();
                    }
                    break;
                default:
                    break;
                }
            }

            void clock_cpu_timer(std::uint32_t cpu_cycles) override {
                if (!irq_enabled_) {
                    return;
                }
                for (std::uint32_t i = 0; i < cpu_cycles; ++i) {
                    if (cycle_mode_) {
                        tick_irq_counter();
                    } else {
                        irq_prescaler_ += 3;
                        if (irq_prescaler_ >= 341) {
                            irq_prescaler_ -= 341;
                            tick_irq_counter();
                        }
                    }
                }
            }

            [[nodiscard]] chips::ichip* expansion_audio() noexcept override { return &sound_; }
            [[nodiscard]] std::size_t expansion_audio_pending() const noexcept override {
                return sound_.pending_samples();
            }
            std::size_t drain_expansion_audio(std::int16_t* out,
                                              std::size_t max_pairs) noexcept override {
                return sound_.drain_samples(out, max_pairs);
            }

            void save_state(chips::state_writer& writer) const override {
                writer.u8(prg16_);
                writer.u8(prg8_);
                for (const std::uint8_t b : chr_banks_) {
                    writer.u8(b);
                }
                writer.u8(ppu_ctrl_);
                writer.u8(irq_latch_);
                writer.u8(irq_counter_);
                writer.u32(static_cast<std::uint32_t>(irq_prescaler_));
                writer.boolean(irq_enabled_);
                writer.boolean(irq_ack_enable_);
                writer.boolean(cycle_mode_);
                sound_.save_state(writer);
            }
            void load_state(chips::state_reader& reader) override {
                prg16_ = reader.u8();
                prg8_ = reader.u8();
                for (std::uint8_t& b : chr_banks_) {
                    b = reader.u8();
                }
                ppu_ctrl_ = reader.u8();
                irq_latch_ = reader.u8();
                irq_counter_ = reader.u8();
                irq_prescaler_ = static_cast<int>(reader.u32());
                irq_enabled_ = reader.boolean();
                irq_ack_enable_ = reader.boolean();
                cycle_mode_ = reader.boolean();
                sound_.load_state(reader);
                apply_prg();
                apply_chr();
                apply_mirroring();
            }

          private:
            void map_prg8(std::uint32_t slot, std::size_t bank8) {
                if (prg_8k_count_ == 0U) {
                    return;
                }
                bus_->retarget_rom(slot,
                                   prg_.subspan((bank8 % prg_8k_count_) * k_prg_8k, k_prg_8k));
            }

            void apply_prg() {
                if (prg_16k_count_ == 0U) {
                    return;
                }
                bus_->retarget_rom(
                    0x8000U, prg_.subspan((prg16_ % prg_16k_count_) * k_prg_bank, k_prg_bank));
                map_prg8(0xC000U, prg8_);
                map_prg8(0xE000U, prg_8k_count_ - 1U); // last 8 KiB fixed at $E000
            }

            void apply_chr() {
                if (chr_is_ram_ || chr_1k_count_ == 0U) {
                    return;
                }
                for (std::size_t s = 0; s < 8U; ++s) {
                    const std::size_t src = (chr_banks_[s] % chr_1k_count_) * k_chr_1k;
                    std::copy_n(chr_.begin() + static_cast<std::ptrdiff_t>(src), k_chr_1k,
                                chr_window_.begin() + static_cast<std::ptrdiff_t>(s * k_chr_1k));
                }
            }

            void apply_mirroring() {
                using m = chips::video::ppu2c02::mirroring;
                // $B003 bits 3-2 select the nametable arrangement (CHR-mode-0 layout).
                switch ((ppu_ctrl_ >> 2U) & 0x03U) {
                case 0:
                    ppu_->set_mirroring(m::vertical);
                    break;
                case 1:
                    ppu_->set_mirroring(m::horizontal);
                    break;
                case 2:
                    ppu_->set_mirroring(m::single_a);
                    break;
                default:
                    ppu_->set_mirroring(m::single_b);
                    break;
                }
            }

            void write_irq_control(std::uint8_t value) {
                irq_ack_enable_ = (value & 0x01U) != 0U;
                irq_enabled_ = (value & 0x02U) != 0U;
                cycle_mode_ = (value & 0x04U) != 0U;
                if (irq_enabled_) {
                    irq_counter_ = irq_latch_;
                    irq_prescaler_ = 0;
                }
                raise_irq(false);
            }
            void irq_acknowledge() {
                raise_irq(false);
                irq_enabled_ = irq_ack_enable_;
            }
            void tick_irq_counter() {
                if (irq_counter_ == 0xFFU) {
                    irq_counter_ = irq_latch_;
                    raise_irq(true);
                } else {
                    ++irq_counter_;
                }
            }

            std::size_t prg_16k_count_;
            std::size_t prg_8k_count_;
            std::size_t chr_1k_count_;
            bool variant_b_;
            chips::audio::vrc6 sound_;
            std::uint8_t prg16_{};
            std::uint8_t prg8_{};
            std::array<std::uint8_t, 8> chr_banks_{};
            std::uint8_t ppu_ctrl_{}; // $B003
            std::uint8_t irq_latch_{};
            std::uint8_t irq_counter_{};
            int irq_prescaler_{};
            bool irq_enabled_{};
            bool irq_ack_enable_{};
            bool cycle_mode_{};
            std::array<std::uint8_t, 0x2000U> chr_window_{};
        };

        // Namco 163 (iNES 19). PRG: three switchable 8 KiB banks ($8000/$A000/$C000
        // via the $E000/$E800/$F000 registers) over the fixed last 8 KiB ($E000).
        // CHR: eight 1 KiB banks ($8000-$BFFF) composed into the window. Four
        // nametable registers ($C000-$D800) select the CIRAM page per screen (the
        // CHR-ROM-backed-nametable option is approximated to the matching CIRAM
        // arrangement). A 15-bit CPU-cycle counter ($5000 low / $5800 high+enable)
        // fires /IRQ at $7FFF and stops. The on-board sound is the n163 wavetable
        // chip, addressed through $F800 (address) + $4800 (data, read/write);
        // $E000 bit 6 mutes it.
        class namco163_mapper final : public nes_mapper {
          public:
            namco163_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                            std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                            bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), prg_8k_count_(prg.size() / k_prg_8k),
                  chr_1k_count_(chr.size() / k_chr_1k) {}

            void reset() override {
                prg_regs_ = {0U, 0U, 0U};
                chr_banks_.fill(0U);
                nt_banks_ = {0xE0U, 0xE0U, 0xE1U, 0xE1U};
                irq_counter_ = 0U;
                irq_enabled_ = false;

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

                sound_.reset(chips::reset_kind::power_on);
                sound_.set_clock_divider(37);
                sound_.enable_audio_capture(true);

                // The sound data port ($4800) and the IRQ counter ($5000/$5800) are
                // readable, so they need a full read/write MMIO below $8000; the
                // banking + sound-address registers ($8000-$FFFF) ride the write hook.
                bus_->map_mmio(
                    0x4800U, 0x1800U,
                    [this](std::uint32_t addr) -> std::uint8_t { return read_low(addr); },
                    [this](std::uint32_t addr, std::uint8_t value) { write_low(addr, value); });

                apply_prg();
                apply_chr();
                apply_mirroring();
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                switch (addr & 0xF800U) {
                case 0x8000U:
                case 0x8800U:
                case 0x9000U:
                case 0x9800U:
                case 0xA000U:
                case 0xA800U:
                case 0xB000U:
                case 0xB800U:
                    chr_banks_[(addr - 0x8000U) >> 11U] = value;
                    apply_chr();
                    break;
                case 0xC000U:
                case 0xC800U:
                case 0xD000U:
                case 0xD800U:
                    nt_banks_[(addr - 0xC000U) >> 11U] = value;
                    apply_mirroring();
                    break;
                case 0xE000U: // PRG bank 0 (bits 0-5) + sound enable (bit 6, 0 = on)
                    prg_regs_[0] = static_cast<std::uint8_t>(value & 0x3FU);
                    sound_.set_enabled((value & 0x40U) == 0U);
                    apply_prg();
                    break;
                case 0xE800U: // PRG bank 1 (bits 0-5); bits 6-7 select CHR-ROM NT (unmodelled)
                    prg_regs_[1] = static_cast<std::uint8_t>(value & 0x3FU);
                    apply_prg();
                    break;
                case 0xF000U: // PRG bank 2 (bits 0-5)
                    prg_regs_[2] = static_cast<std::uint8_t>(value & 0x3FU);
                    apply_prg();
                    break;
                case 0xF800U: // sound RAM address port
                    sound_.write_address(value);
                    break;
                default:
                    break;
                }
            }

            // The 15-bit counter free-runs on the CPU clock; on reaching $7FFF it
            // asserts /IRQ and stops until the program rewrites the counter.
            void clock_cpu_timer(std::uint32_t cpu_cycles) override {
                if (!irq_enabled_) {
                    return;
                }
                for (std::uint32_t i = 0; i < cpu_cycles; ++i) {
                    if (irq_counter_ < 0x7FFFU) {
                        ++irq_counter_;
                        if (irq_counter_ == 0x7FFFU) {
                            raise_irq(true);
                        }
                    }
                }
            }

            [[nodiscard]] chips::ichip* expansion_audio() noexcept override { return &sound_; }
            [[nodiscard]] std::size_t expansion_audio_pending() const noexcept override {
                return sound_.pending_samples();
            }
            std::size_t drain_expansion_audio(std::int16_t* out,
                                              std::size_t max_pairs) noexcept override {
                return sound_.drain_samples(out, max_pairs);
            }

            void save_state(chips::state_writer& writer) const override {
                for (const std::uint8_t r : prg_regs_) {
                    writer.u8(r);
                }
                for (const std::uint8_t b : chr_banks_) {
                    writer.u8(b);
                }
                for (const std::uint8_t b : nt_banks_) {
                    writer.u8(b);
                }
                writer.u16(irq_counter_);
                writer.boolean(irq_enabled_);
                sound_.save_state(writer);
            }
            void load_state(chips::state_reader& reader) override {
                for (std::uint8_t& r : prg_regs_) {
                    r = reader.u8();
                }
                for (std::uint8_t& b : chr_banks_) {
                    b = reader.u8();
                }
                for (std::uint8_t& b : nt_banks_) {
                    b = reader.u8();
                }
                irq_counter_ = reader.u16();
                irq_enabled_ = reader.boolean();
                sound_.load_state(reader);
                apply_prg();
                apply_chr();
                apply_mirroring();
            }

          private:
            [[nodiscard]] std::uint8_t read_low(std::uint32_t addr) noexcept {
                if (addr < 0x5000U) { // $4800-$4FFF: sound data port
                    return sound_.read_data();
                }
                if (addr < 0x5800U) { // $5000-$57FF: IRQ counter low
                    return static_cast<std::uint8_t>(irq_counter_ & 0xFFU);
                }
                // $5800-$5FFF: IRQ counter high (bits 0-6) + enable (bit 7)
                return static_cast<std::uint8_t>(((irq_counter_ >> 8U) & 0x7FU) |
                                                 (irq_enabled_ ? 0x80U : 0x00U));
            }
            void write_low(std::uint32_t addr, std::uint8_t value) noexcept {
                if (addr < 0x5000U) { // sound data port
                    sound_.write_data(value);
                    return;
                }
                if (addr < 0x5800U) { // IRQ counter low
                    irq_counter_ = static_cast<std::uint16_t>((irq_counter_ & 0x7F00U) | value);
                } else { // IRQ counter high + enable
                    irq_counter_ = static_cast<std::uint16_t>(
                        (irq_counter_ & 0x00FFU) |
                        (static_cast<std::uint16_t>(value & 0x7FU) << 8U));
                    irq_enabled_ = (value & 0x80U) != 0U;
                }
                raise_irq(false); // writing the counter acknowledges a pending IRQ
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
                map_prg8(0x8000U, prg_regs_[0]);
                map_prg8(0xA000U, prg_regs_[1]);
                map_prg8(0xC000U, prg_regs_[2]);
                map_prg8(0xE000U, prg_8k_count_ - 1U); // last 8 KiB fixed at $E000
            }

            void apply_chr() {
                if (chr_is_ram_ || chr_1k_count_ == 0U) {
                    return;
                }
                for (std::size_t s = 0; s < 8U; ++s) {
                    const std::size_t src = (chr_banks_[s] % chr_1k_count_) * k_chr_1k;
                    std::copy_n(chr_.begin() + static_cast<std::ptrdiff_t>(src), k_chr_1k,
                                chr_window_.begin() + static_cast<std::ptrdiff_t>(s * k_chr_1k));
                }
            }

            void apply_mirroring() {
                using m = chips::video::ppu2c02::mirroring;
                // Treat each nametable register as a CIRAM page selector (bit 0) and
                // map the recognised two-page arrangements; everything else (incl.
                // CHR-ROM-backed nametables) falls back to vertical.
                const std::uint8_t a = static_cast<std::uint8_t>(nt_banks_[0] & 0x01U);
                const std::uint8_t b = static_cast<std::uint8_t>(nt_banks_[1] & 0x01U);
                const std::uint8_t c = static_cast<std::uint8_t>(nt_banks_[2] & 0x01U);
                const std::uint8_t d = static_cast<std::uint8_t>(nt_banks_[3] & 0x01U);
                if (a == 0U && b == 0U && c == 0U && d == 0U) {
                    ppu_->set_mirroring(m::single_a);
                } else if (a == 1U && b == 1U && c == 1U && d == 1U) {
                    ppu_->set_mirroring(m::single_b);
                } else if (a == 0U && b == 0U && c == 1U && d == 1U) {
                    ppu_->set_mirroring(m::horizontal);
                } else {
                    ppu_->set_mirroring(m::vertical); // A,B,A,B and the default
                }
            }

            std::size_t prg_8k_count_;
            std::size_t chr_1k_count_;
            chips::audio::n163 sound_;
            std::array<std::uint8_t, 3> prg_regs_{}; // $8000/$A000/$C000 8 KiB selects
            std::array<std::uint8_t, 8> chr_banks_{};
            std::array<std::uint8_t, 4> nt_banks_{}; // nametable page selectors
            std::uint16_t irq_counter_{};            // 15-bit CPU-cycle up-counter
            bool irq_enabled_{};
            std::array<std::uint8_t, 0x2000U> chr_window_{};
        };

        // Konami VRC2 / VRC4 (iNES 21/22/23/25). One chip family that differs only in
        // which two CPU address lines select the 2-bit register index within each
        // $x000 group -- iNES folds several pin wirings under each mapper number, so
        // each number ORs a pair of address bits to cover its sub-variants. PRG: two
        // switchable 8 KiB banks ($8000 + $A000) over the fixed second-last ($C000)
        // and last ($E000), with a VRC4 swap mode that moves the $8000 select to
        // $C000. CHR: eight 1 KiB banks, each a 9-bit value written as a low + high
        // nibble register pair. Mirroring is $9000 (2 bits on VRC4, 1 on VRC2). VRC4
        // adds the Konami VRC IRQ at $F000-$F003 (same as VRC6/7); VRC2 has none and
        // (on VRC2a = mapper 22) drops the low CHR bit.
        class vrc2_4_mapper final : public nes_mapper {
          public:
            vrc2_4_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                          std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                          bool chr_is_ram, int number) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), prg_8k_count_(prg.size() / k_prg_8k),
                  chr_1k_count_(chr.size() / k_chr_1k) {
                // (a_mask, b_mask) = the address bits forming index bit 0 / bit 1.
                switch (number) {
                case 21: // VRC4a (A1/A2) + VRC4c (A6/A7)
                    a_mask_ = 0x0042U;
                    b_mask_ = 0x0084U;
                    is_vrc4_ = true;
                    break;
                case 22: // VRC2a: A1/A0 swapped, low CHR bit dropped, no IRQ
                    a_mask_ = 0x0002U;
                    b_mask_ = 0x0001U;
                    is_vrc4_ = false;
                    chr_shift_ = 1;
                    break;
                case 25: // VRC4b/d (A0/A1 swapped) + A2/A3
                    a_mask_ = 0x000AU;
                    b_mask_ = 0x0005U;
                    is_vrc4_ = true;
                    break;
                case 23: // VRC2b / VRC4e/f: A0/A1 + A2/A3
                default:
                    a_mask_ = 0x0005U;
                    b_mask_ = 0x000AU;
                    is_vrc4_ = true;
                    break;
                }
            }

            void reset() override {
                prg0_ = 0U;
                prg1_ = 0U;
                swap_mode_ = false;
                mirror_ = 0U;
                chr_bank_.fill(0U);
                irq_latch_ = 0U;
                irq_counter_ = 0U;
                irq_prescaler_ = 0;
                irq_enabled_ = false;
                irq_ack_enable_ = false;
                cycle_mode_ = false;

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

                apply_prg();
                apply_chr();
                apply_mirroring();
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                const std::uint16_t group = static_cast<std::uint16_t>((addr >> 12U) & 0x0FU);
                const std::uint8_t sub = static_cast<std::uint8_t>(
                    ((addr & b_mask_) != 0U ? 2U : 0U) | ((addr & a_mask_) != 0U ? 1U : 0U));
                switch (group) {
                case 0x8U: // PRG select 0
                    prg0_ = static_cast<std::uint8_t>(value & 0x1FU);
                    apply_prg();
                    break;
                case 0x9U:
                    if (sub <= 1U) { // mirroring
                        mirror_ = static_cast<std::uint8_t>(value & (is_vrc4_ ? 0x03U : 0x01U));
                        apply_mirroring();
                    } else if (is_vrc4_) { // $9002 bit 1 = PRG swap mode
                        swap_mode_ = (value & 0x02U) != 0U;
                        apply_prg();
                    }
                    break;
                case 0xAU: // PRG select 1
                    prg1_ = static_cast<std::uint8_t>(value & 0x1FU);
                    apply_prg();
                    break;
                case 0xBU:
                case 0xCU:
                case 0xDU:
                case 0xEU: { // CHR: a low + high nibble register per 1 KiB bank
                    const std::size_t bank = (group - 0xBU) * 2U + (sub >> 1U);
                    if ((sub & 1U) != 0U) { // high nibble (bits 8-4)
                        chr_bank_[bank] = static_cast<std::uint16_t>((chr_bank_[bank] & 0x000FU) |
                                                                     ((value & 0x1FU) << 4U));
                    } else { // low nibble (bits 3-0)
                        chr_bank_[bank] = static_cast<std::uint16_t>((chr_bank_[bank] & 0x01F0U) |
                                                                     (value & 0x0FU));
                    }
                    apply_chr();
                    break;
                }
                case 0xFU: // VRC IRQ (VRC4 only)
                    if (!is_vrc4_) {
                        break;
                    }
                    if (sub == 0U) {
                        irq_latch_ =
                            static_cast<std::uint8_t>((irq_latch_ & 0xF0U) | (value & 0x0FU));
                    } else if (sub == 1U) {
                        irq_latch_ = static_cast<std::uint8_t>((irq_latch_ & 0x0FU) |
                                                               ((value & 0x0FU) << 4U));
                    } else if (sub == 2U) {
                        write_irq_control(value);
                    } else {
                        irq_acknowledge();
                    }
                    break;
                default:
                    break;
                }
            }

            void clock_cpu_timer(std::uint32_t cpu_cycles) override {
                if (!irq_enabled_) {
                    return;
                }
                for (std::uint32_t i = 0; i < cpu_cycles; ++i) {
                    if (cycle_mode_) {
                        tick_irq_counter();
                    } else {
                        irq_prescaler_ += 3;
                        if (irq_prescaler_ >= 341) {
                            irq_prescaler_ -= 341;
                            tick_irq_counter();
                        }
                    }
                }
            }

            void save_state(chips::state_writer& writer) const override {
                writer.u8(prg0_);
                writer.u8(prg1_);
                writer.boolean(swap_mode_);
                writer.u8(mirror_);
                for (const std::uint16_t b : chr_bank_) {
                    writer.u16(b);
                }
                writer.u8(irq_latch_);
                writer.u8(irq_counter_);
                writer.u32(static_cast<std::uint32_t>(irq_prescaler_));
                writer.boolean(irq_enabled_);
                writer.boolean(irq_ack_enable_);
                writer.boolean(cycle_mode_);
            }
            void load_state(chips::state_reader& reader) override {
                prg0_ = reader.u8();
                prg1_ = reader.u8();
                swap_mode_ = reader.boolean();
                mirror_ = reader.u8();
                for (std::uint16_t& b : chr_bank_) {
                    b = reader.u16();
                }
                irq_latch_ = reader.u8();
                irq_counter_ = reader.u8();
                irq_prescaler_ = static_cast<int>(reader.u32());
                irq_enabled_ = reader.boolean();
                irq_ack_enable_ = reader.boolean();
                cycle_mode_ = reader.boolean();
                apply_prg();
                apply_chr();
                apply_mirroring();
            }

          private:
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
                const std::size_t second_last = prg_8k_count_ >= 2U ? prg_8k_count_ - 2U : 0U;
                if (swap_mode_) { // VRC4: $8000 fixed second-last, select moves to $C000
                    map_prg8(0x8000U, second_last);
                    map_prg8(0xC000U, prg0_);
                } else {
                    map_prg8(0x8000U, prg0_);
                    map_prg8(0xC000U, second_last);
                }
                map_prg8(0xA000U, prg1_);
                map_prg8(0xE000U, prg_8k_count_ - 1U); // last bank fixed at $E000
            }

            void apply_chr() {
                if (chr_is_ram_ || chr_1k_count_ == 0U) {
                    return;
                }
                for (std::size_t s = 0; s < 8U; ++s) {
                    const std::size_t bank =
                        chr_shift_ != 0 ? (chr_bank_[s] >> chr_shift_) : chr_bank_[s];
                    const std::size_t src = (bank % chr_1k_count_) * k_chr_1k;
                    std::copy_n(chr_.begin() + static_cast<std::ptrdiff_t>(src), k_chr_1k,
                                chr_window_.begin() + static_cast<std::ptrdiff_t>(s * k_chr_1k));
                }
            }

            void apply_mirroring() {
                using m = chips::video::ppu2c02::mirroring;
                switch (mirror_) {
                case 0:
                    ppu_->set_mirroring(m::vertical);
                    break;
                case 1:
                    ppu_->set_mirroring(m::horizontal);
                    break;
                case 2:
                    ppu_->set_mirroring(m::single_a);
                    break;
                default:
                    ppu_->set_mirroring(m::single_b);
                    break;
                }
            }

            void write_irq_control(std::uint8_t value) {
                irq_ack_enable_ = (value & 0x01U) != 0U;
                irq_enabled_ = (value & 0x02U) != 0U;
                cycle_mode_ = (value & 0x04U) != 0U;
                if (irq_enabled_) {
                    irq_counter_ = irq_latch_;
                    irq_prescaler_ = 0;
                }
                raise_irq(false);
            }
            void irq_acknowledge() {
                raise_irq(false);
                irq_enabled_ = irq_ack_enable_;
            }
            void tick_irq_counter() {
                if (irq_counter_ == 0xFFU) {
                    irq_counter_ = irq_latch_;
                    raise_irq(true);
                } else {
                    ++irq_counter_;
                }
            }

            std::size_t prg_8k_count_;
            std::size_t chr_1k_count_;
            std::uint16_t a_mask_{}; // address bit forming register-index bit 0
            std::uint16_t b_mask_{}; // address bit forming register-index bit 1
            bool is_vrc4_{};         // IRQ + PRG-swap + single-screen present
            int chr_shift_{};        // VRC2a drops the low CHR bit
            std::uint8_t prg0_{};
            std::uint8_t prg1_{};
            bool swap_mode_{};
            std::uint8_t mirror_{};
            std::array<std::uint16_t, 8> chr_bank_{}; // 9-bit each (low + high nibble)
            std::uint8_t irq_latch_{};
            std::uint8_t irq_counter_{};
            int irq_prescaler_{};
            bool irq_enabled_{};
            bool irq_ack_enable_{};
            bool cycle_mode_{};
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
        case 3:
            return std::make_unique<cnrom_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 4:
            return std::make_unique<mmc3_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 5:
            return std::make_unique<mmc5_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 7:
            return std::make_unique<axrom_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 11:
            return std::make_unique<color_dreams_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 19:
            return std::make_unique<namco163_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 21: // VRC4a/c
        case 22: // VRC2a
        case 23: // VRC2b / VRC4e/f
        case 25: // VRC2c / VRC4b/d
            return std::make_unique<vrc2_4_mapper>(bus, ppu, prg, chr, chr_is_ram, number);
        case 24: // VRC6a
            return std::make_unique<vrc6_mapper>(bus, ppu, prg, chr, chr_is_ram, false);
        case 26: // VRC6b (A0/A1 swapped)
            return std::make_unique<vrc6_mapper>(bus, ppu, prg, chr, chr_is_ram, true);
        case 66:
            return std::make_unique<gxrom_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 69:
            return std::make_unique<sunsoft5b_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 71:
            return std::make_unique<camerica_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 85:
            return std::make_unique<vrc7_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 206:
            return std::make_unique<namco118_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 0:
        default:
            return std::make_unique<nrom_mapper>(bus, ppu, prg, chr, chr_is_ram);
        }
    }

} // namespace mnemos::manifests::nes
