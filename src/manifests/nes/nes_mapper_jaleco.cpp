#include "nes_mapper_jaleco.hpp"

#include "nes_mapper_helpers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace mnemos::manifests::nes {

    using detail::chr_window;
    using detail::k_prg_8k;

    namespace {
        // Jaleco SS88006 (iNES 18): three 8 KiB switchable PRG banks (last 8 KiB
        // fixed), eight 1 KiB CHR banks, four mirroring modes, and a 16-bit down-
        // counter IRQ with a selectable counting WIDTH. Every register takes a 4-bit
        // nibble: the even address carries bits 0-3 of the logical value and the next
        // odd address bits 4-7, so each bank/latch is written in two halves. The
        // board decodes only A12-A14 and A0-A1, so registers mirror across the other
        // address lines (normalise with addr & 0xF003). Games: Cosmo Police Galivan,
        // Plasma Ball, The Lord of King. (The optional uPD7755 ADPCM at $F003 is not
        // modelled.)
        class jaleco_ss88006_mapper final : public nes_mapper {
          public:
            jaleco_ss88006_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                                  std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                                  bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_win_(chr, chr_is_ram) {}

            void reset() override {
                prg_bank_.fill(0U);
                chr_bank_.fill(0U);
                mirror_ = 0U;
                irq_latch_ = 0U;
                irq_counter_ = 0U;
                irq_mask_ = 0xFFFFU;
                irq_enabled_ = false;
                if (prg_8k_count() != 0U) {
                    for (const std::uint32_t slot : {0x8000U, 0xA000U, 0xC000U}) {
                        bus_->map_rom(slot, prg_.subspan(0, k_prg_8k));
                    }
                    bus_->map_rom(0xE000U, // the last 8 KiB bank is fixed here
                                  prg_.subspan((prg_8k_count() - 1U) * k_prg_8k, k_prg_8k));
                }
                chr_win_.attach(*ppu_);
                apply_prg();
                apply_chr();
                apply_mirroring();
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                const std::uint8_t nib = value & 0x0FU;
                // CHR registers span $A000-$DFFF: the group ($A-$D) picks a bank pair,
                // A1 picks the pair member, A0 the nibble half.
                const std::uint16_t group = addr & 0xF000U;
                if (group >= 0xA000U && group <= 0xD000U) {
                    const std::size_t idx = static_cast<std::size_t>((group >> 12U) - 0x0AU) * 2U +
                                            ((addr >> 1U) & 0x01U);
                    if ((addr & 0x01U) == 0U) {
                        set_low(chr_bank_[idx], nib);
                    } else {
                        set_high(chr_bank_[idx], nib);
                    }
                    apply_chr();
                    return;
                }
                switch (addr & 0xF003U) {
                case 0x8000U:
                    set_low(prg_bank_[0], nib);
                    apply_prg();
                    break;
                case 0x8001U:
                    set_high(prg_bank_[0], nib);
                    apply_prg();
                    break;
                case 0x8002U:
                    set_low(prg_bank_[1], nib);
                    apply_prg();
                    break;
                case 0x8003U:
                    set_high(prg_bank_[1], nib);
                    apply_prg();
                    break;
                case 0x9000U:
                    set_low(prg_bank_[2], nib);
                    apply_prg();
                    break;
                case 0x9001U:
                    set_high(prg_bank_[2], nib);
                    apply_prg();
                    break;
                case 0x9002U:
                    break; // PRG-RAM enable/protect -- work RAM stays mapped
                case 0xE000U:
                    irq_latch_ = static_cast<std::uint16_t>((irq_latch_ & 0xFFF0U) | nib);
                    break;
                case 0xE001U:
                    irq_latch_ = static_cast<std::uint16_t>(
                        (irq_latch_ & 0xFF0FU) | (static_cast<std::uint16_t>(nib) << 4U));
                    break;
                case 0xE002U:
                    irq_latch_ = static_cast<std::uint16_t>(
                        (irq_latch_ & 0xF0FFU) | (static_cast<std::uint16_t>(nib) << 8U));
                    break;
                case 0xE003U:
                    irq_latch_ = static_cast<std::uint16_t>(
                        (irq_latch_ & 0x0FFFU) | (static_cast<std::uint16_t>(nib) << 12U));
                    break;
                case 0xF000U: // reload the counter from the latch and acknowledge
                    irq_counter_ = irq_latch_;
                    raise_irq(false);
                    break;
                case 0xF001U: // bit 0 enables, bits 1-3 select the counting width, and
                              // the write acknowledges a pending IRQ -- games ack here
                              // (disable counting) rather than via the $F000 reload.
                    raise_irq(false);
                    irq_enabled_ = (nib & 0x01U) != 0U;
                    if ((nib & 0x08U) != 0U) {
                        irq_mask_ = 0x000FU; // 4-bit
                    } else if ((nib & 0x04U) != 0U) {
                        irq_mask_ = 0x00FFU; // 8-bit
                    } else if ((nib & 0x02U) != 0U) {
                        irq_mask_ = 0x0FFFU; // 12-bit
                    } else {
                        irq_mask_ = 0xFFFFU; // 16-bit
                    }
                    break;
                case 0xF002U:
                    mirror_ = nib & 0x03U;
                    apply_mirroring();
                    break;
                case 0xF003U:
                    break; // expansion ADPCM sound -- not modelled
                default:
                    break;
                }
            }

            // The counter decrements on every M2 cycle while enabled; only the low
            // `irq_mask_` bits participate (the width select), and the IRQ asserts
            // when those bits wrap past zero. The board clocks this ungated.
            void clock_cpu_timer(std::uint32_t cpu_cycles) override {
                if (!irq_enabled_) {
                    return;
                }
                const std::uint32_t mask = irq_mask_;
                const std::uint32_t active = irq_counter_ & mask;
                if (cpu_cycles > active) { // the active width wraps past zero -> IRQ
                    raise_irq(true);
                }
                const std::uint32_t next = (active - cpu_cycles) & mask;
                irq_counter_ = static_cast<std::uint16_t>((irq_counter_ & ~mask) | next);
            }

            void save_state(chips::state_writer& writer) const override {
                for (const std::uint8_t b : prg_bank_) {
                    writer.u8(b);
                }
                for (const std::uint8_t b : chr_bank_) {
                    writer.u8(b);
                }
                writer.u8(mirror_);
                writer.u32(irq_latch_);
                writer.u32(irq_counter_);
                writer.u32(irq_mask_);
                writer.boolean(irq_enabled_);
            }
            void load_state(chips::state_reader& reader) override {
                for (std::uint8_t& b : prg_bank_) {
                    b = reader.u8();
                }
                for (std::uint8_t& b : chr_bank_) {
                    b = reader.u8();
                }
                mirror_ = reader.u8();
                irq_latch_ = static_cast<std::uint16_t>(reader.u32());
                irq_counter_ = static_cast<std::uint16_t>(reader.u32());
                irq_mask_ = reader.u32();
                irq_enabled_ = reader.boolean();
                apply_prg();
                apply_chr();
                apply_mirroring();
            }

          private:
            static void set_low(std::uint8_t& reg, std::uint8_t nib) {
                reg = static_cast<std::uint8_t>((reg & 0xF0U) | nib);
            }
            static void set_high(std::uint8_t& reg, std::uint8_t nib) {
                reg = static_cast<std::uint8_t>((reg & 0x0FU) | (nib << 4U));
            }

            void apply_prg() {
                // Three switchable 8 KiB banks; $E000 stays fixed to the last bank.
                map_prg_8k(0x8000U, prg_bank_[0]);
                map_prg_8k(0xA000U, prg_bank_[1]);
                map_prg_8k(0xC000U, prg_bank_[2]);
            }

            void apply_chr() {
                for (std::size_t s = 0; s < 8U; ++s) {
                    chr_win_.set_1k(s, chr_bank_[s]);
                }
            }

            void apply_mirroring() {
                using m = chips::video::ppu2c02::mirroring;
                // SS88006 order: 0 = horizontal, 1 = vertical, 2/3 = single-screen A/B.
                switch (mirror_) {
                case 0U:
                    ppu_->set_mirroring(m::horizontal);
                    break;
                case 1U:
                    ppu_->set_mirroring(m::vertical);
                    break;
                case 2U:
                    ppu_->set_mirroring(m::single_a);
                    break;
                default:
                    ppu_->set_mirroring(m::single_b);
                    break;
                }
            }

            std::array<std::uint8_t, 3> prg_bank_{};
            std::array<std::uint8_t, 8> chr_bank_{};
            std::uint8_t mirror_{};
            std::uint16_t irq_latch_{};
            std::uint16_t irq_counter_{};
            std::uint32_t irq_mask_{0xFFFFU};
            bool irq_enabled_{};
            chr_window chr_win_;
        };

    } // namespace

    std::unique_ptr<nes_mapper> make_jaleco_ss88006_mapper(nes_mapper_build_context context) {
        return std::make_unique<jaleco_ss88006_mapper>(context.bus, context.ppu, context.prg,
                                                       context.chr, context.chr_is_ram);
    }

} // namespace mnemos::manifests::nes
