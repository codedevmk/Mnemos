#include "nes_mapper_mmc3.hpp"

#include "nes_mapper_helpers.hpp"

#include <array>
#include <cstddef>
#include <memory>

namespace mnemos::manifests::nes {

    using detail::chr_window;
    using detail::k_prg_8k;

    namespace {
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
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_win_(chr, chr_is_ram) {}

            void reset() override {
                bank_select_ = 0U;
                mirror_reg_ = 0U;
                regs_.fill(0U);
                if (prg_8k_count() != 0U) {
                    for (const std::uint32_t slot : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
                        bus_->map_rom(slot, prg_.subspan(0, k_prg_8k));
                    }
                }
                chr_win_.attach(*ppu_);
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

            void apply_prg() {
                const std::size_t count = prg_8k_count();
                if (count == 0U) {
                    return;
                }
                const std::size_t last = count - 1U;
                const std::size_t second_last = count >= 2U ? count - 2U : last;
                if ((bank_select_ & 0x40U) == 0U) { // mode 0: R6 @ $8000, fixed @ $C000
                    map_prg_8k(0x8000U, regs_[6]);
                    map_prg_8k(0xA000U, regs_[7]);
                    map_prg_8k(0xC000U, second_last);
                } else { // mode 1: fixed @ $8000, R6 @ $C000
                    map_prg_8k(0x8000U, second_last);
                    map_prg_8k(0xA000U, regs_[7]);
                    map_prg_8k(0xC000U, regs_[6]);
                }
                map_prg_8k(0xE000U, last); // the last bank is always fixed at $E000
            }

            void apply_chr() {
                // Eight 1 KiB CHR slots. R0/R1 are 2 KiB banks (low bit ignored);
                // bit 7 of bank-select swaps the $0000 and $1000 halves.
                const std::size_t r0 = regs_[0] & 0xFEU;
                const std::size_t r1 = regs_[1] & 0xFEU;
                const std::array<std::size_t, 8> normal = {r0,       r0 + 1U,  r1,       r1 + 1U,
                                                           regs_[2], regs_[3], regs_[4], regs_[5]};
                std::array<std::size_t, 8> slot = normal;
                if ((bank_select_ & 0x80U) != 0U) { // A12 inversion: swap the halves
                    slot = {regs_[2], regs_[3], regs_[4], regs_[5], r0, r0 + 1U, r1, r1 + 1U};
                }
                for (std::size_t s = 0; s < slot.size(); ++s) {
                    chr_win_.set_1k(s, slot[s]);
                }
            }

            virtual void apply_mirroring() {
                using m = chips::video::ppu2c02::mirroring;
                // MMC3 $A000 bit 0: 0 = vertical, 1 = horizontal.
                ppu_->set_mirroring((mirror_reg_ & 0x01U) != 0U ? m::horizontal : m::vertical);
            }

            std::array<std::uint8_t, 8> regs_{};
            std::uint8_t bank_select_{};
            std::uint8_t mirror_reg_{};
            std::uint8_t irq_latch_{};
            std::uint8_t irq_counter_{};
            bool irq_reload_{};
            bool irq_enabled_{};
            chr_window chr_win_;
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

        // RAMBO-1 / Tengen 800032 (iNES 64): an enhanced MMC3. It shares MMC3's
        // bus/CHR-window/mirroring machinery (hence the subclass) but adds a THIRD
        // switchable 8 KiB PRG bank (register RF), a 1 KiB CHR mode (the K bit
        // splits R0/R1 into four 1 KiB banks supplied by R8/R9), a sixteen-entry
        // register file, and an IRQ that clocks either per scanline (like MMC3) or
        // per CPU cycle (selected by a $C001 mode bit). Games: the Tengen library
        // -- Klax, Ms. Pac-Man, Skull & Crossbones, Shinobi, Rolling Thunder.
        class rambo1_mapper final : public mmc3_mapper {
          public:
            using mmc3_mapper::mmc3_mapper;

            void reset() override {
                bank_select_ = 0U;
                mirror_reg_ = 0U;
                r_.fill(0U);
                irq_latch_ = 0U;
                irq_counter_ = 0U;
                irq_reload_ = false;
                irq_enabled_ = false;
                irq_cycle_mode_ = false;
                cycle_prescaler_ = 0U;
                if (prg_8k_count() != 0U) {
                    for (const std::uint32_t slot : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
                        bus_->map_rom(slot, prg_.subspan(0, k_prg_8k));
                    }
                }
                chr_win_.attach(*ppu_);
                apply_banks();
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                switch (addr & 0xE001U) {
                case 0x8000U: // bank select: CPKx RRRR
                    bank_select_ = value;
                    apply_banks();
                    break;
                case 0x8001U: // bank data -> the selected register (R0-RF)
                    r_[bank_select_ & 0x0FU] = value;
                    apply_banks();
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
                    irq_cycle_mode_ = (value & 0x01U) != 0U; // bit 0: 0 = scanline, 1 = CPU cycle
                    irq_reload_ = true;                      // reload on the next clock
                    cycle_prescaler_ = 0U;                   // $C001 resets the cycle prescaler
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

            // Scanline-mode clock: the board calls this only while the PPU renders
            // (the A12 proxy), matching the hardware's A12-driven scanline counter.
            void clock_scanline(std::uint32_t /*line*/) override {
                if (!irq_cycle_mode_) {
                    irq_clock();
                }
            }

            // CPU-cycle-mode clock: the counter advances once every 4 CPU cycles.
            // The board calls this every scanline (ungated) with the line's CPU
            // cycles, so accumulate and step the counter for each group of four.
            void clock_cpu_timer(std::uint32_t cpu_cycles) override {
                if (!irq_cycle_mode_) {
                    return;
                }
                cycle_prescaler_ += cpu_cycles;
                while (cycle_prescaler_ >= 4U) {
                    cycle_prescaler_ -= 4U;
                    irq_clock();
                }
            }

            void save_state(chips::state_writer& writer) const override {
                writer.u8(bank_select_);
                writer.u8(mirror_reg_);
                for (const std::uint8_t r : r_) {
                    writer.u8(r);
                }
                writer.u8(irq_latch_);
                writer.u8(irq_counter_);
                writer.boolean(irq_reload_);
                writer.boolean(irq_enabled_);
                writer.boolean(irq_cycle_mode_);
                writer.u32(cycle_prescaler_);
            }
            void load_state(chips::state_reader& reader) override {
                bank_select_ = reader.u8();
                mirror_reg_ = reader.u8();
                for (std::uint8_t& r : r_) {
                    r = reader.u8();
                }
                irq_latch_ = reader.u8();
                irq_counter_ = reader.u8();
                irq_reload_ = reader.boolean();
                irq_enabled_ = reader.boolean();
                irq_cycle_mode_ = reader.boolean();
                cycle_prescaler_ = reader.u32();
                apply_banks(); // re-point PRG/CHR/mirroring
            }

          private:
            void apply_banks() {
                apply_prg_banks();
                apply_chr_banks();
                apply_mirroring();
            }

            void apply_prg_banks() {
                const std::size_t count = prg_8k_count();
                if (count == 0U) {
                    return;
                }
                // Three switchable 8 KiB banks (R6, R7, RF) + the fixed last bank
                // at $E000. The P bit (0x40) swaps R6 and RF between $8000 and $C000.
                if ((bank_select_ & 0x40U) == 0U) { // P=0
                    map_prg_8k(0x8000U, r_[6]);
                    map_prg_8k(0xA000U, r_[7]);
                    map_prg_8k(0xC000U, r_[15]);
                } else { // P=1
                    map_prg_8k(0x8000U, r_[15]);
                    map_prg_8k(0xA000U, r_[7]);
                    map_prg_8k(0xC000U, r_[6]);
                }
                map_prg_8k(0xE000U, count - 1U); // last bank always fixed at $E000
            }

            void apply_chr_banks() {
                // The "normal" (C=0) layout is a pair of 2 KiB regions at $0000 then
                // four 1 KiB banks at $1000. K=1 splits each 2 KiB region into two
                // independent 1 KiB banks (R0/R8 and R1/R9); K=0 takes 2 KiB pairs
                // from R0/R1 with the low bit ignored.
                std::array<std::size_t, 8> normal{};
                if ((bank_select_ & 0x20U) != 0U) { // K=1: all 1 KiB
                    normal = {r_[0], r_[8], r_[1], r_[9], r_[2], r_[3], r_[4], r_[5]};
                } else { // K=0: R0/R1 are 2 KiB (low bit ignored)
                    const std::size_t r0 = r_[0] & 0xFEU;
                    const std::size_t r1 = r_[1] & 0xFEU;
                    normal = {r0, r0 + 1U, r1, r1 + 1U, r_[2], r_[3], r_[4], r_[5]};
                }
                // The C bit (0x80) swaps the $0000 and $1000 halves, exactly as
                // MMC3's A12 inversion does.
                std::array<std::size_t, 8> slot = normal;
                if ((bank_select_ & 0x80U) != 0U) {
                    slot = {normal[4], normal[5], normal[6], normal[7],
                            normal[0], normal[1], normal[2], normal[3]};
                }
                for (std::size_t s = 0; s < slot.size(); ++s) {
                    chr_win_.set_1k(s, slot[s]);
                }
            }

            // The 8-bit IRQ down-counter shared by both clock sources. On a reload
            // forced by a $C001 write the value is ORed with 1 when non-zero -- a
            // RAMBO-1 quirk; an ordinary count-to-0 auto-reload uses the latch as-is.
            void irq_clock() {
                if (irq_reload_) {
                    irq_counter_ = irq_latch_;
                    if (irq_latch_ != 0U) {
                        irq_counter_ = static_cast<std::uint8_t>(irq_counter_ | 0x01U);
                    }
                    irq_reload_ = false;
                } else if (irq_counter_ == 0U) {
                    irq_counter_ = irq_latch_;
                } else {
                    --irq_counter_;
                }
                if (irq_counter_ == 0U && irq_enabled_) {
                    raise_irq(true); // hardware delays ~4 CPU cycles; approximated immediate
                }
            }

            std::array<std::uint8_t, 16> r_{}; // R0-RF (R10-R14 unused)
            bool irq_cycle_mode_{};
            std::uint32_t cycle_prescaler_{};
        };

    } // namespace

    std::unique_ptr<nes_mapper> make_mmc3_mapper(nes_mapper_build_context context) {
        return std::make_unique<mmc3_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                             context.chr_is_ram);
    }

    std::unique_ptr<nes_mapper> make_rambo1_mapper(nes_mapper_build_context context) {
        return std::make_unique<rambo1_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                               context.chr_is_ram);
    }

    std::unique_ptr<nes_mapper> make_namco118_mapper(nes_mapper_build_context context) {
        return std::make_unique<namco118_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                                 context.chr_is_ram);
    }

} // namespace mnemos::manifests::nes
