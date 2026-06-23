#include "nes_mapper_taito.hpp"

#include "nes_mapper_helpers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace mnemos::manifests::nes {

    using detail::apply_mirror_mode;
    using detail::chr_window;
    using detail::k_prg_8k;

    namespace {
        // Taito TC0190 (iNES 33) / TC0690 (iNES 48). Two switchable 8 KiB PRG banks
        // at $8000/$A000 with $C000/$E000 fixed to the last two banks. CHR is two
        // 2 KiB banks ($0000/$0800) plus four 1 KiB banks ($1000-$1FFF) composed into
        // an 8 KiB window. The TC0690 (48) additionally provides an MMC3-style A12
        // scanline IRQ ($C000-$C003) and selects mirroring at $E000 bit 6; the plain
        // TC0190 (33) has no IRQ and selects mirroring from $8000 bit 6.
        class taito_tc0190_mapper final : public nes_mapper {
          public:
            taito_tc0190_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                                std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                                bool chr_is_ram, bool with_irq) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), with_irq_(with_irq),
                  chr_win_(chr, chr_is_ram) {}

            void reset() override {
                prg_bank_.fill(0U);
                chr2k_.fill(0U);
                chr1k_.fill(0U);
                mirror_ = 0U;
                irq_latch_ = 0U;
                irq_counter_ = 0U;
                irq_enabled_ = false;
                irq_reload_ = false;
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
                if (with_irq_) {
                    // TC0690: $8xxx/$Axxx banking, $Cxxx IRQ, $E000 mirroring.
                    switch (addr & 0xE003U) {
                    case 0x8000U:
                        prg_bank_[0] = value & 0x3FU;
                        apply_prg();
                        break;
                    case 0x8001U:
                        prg_bank_[1] = value & 0x3FU;
                        apply_prg();
                        break;
                    case 0x8002U:
                        chr2k_[0] = value;
                        apply_chr();
                        break;
                    case 0x8003U:
                        chr2k_[1] = value;
                        apply_chr();
                        break;
                    case 0xA000U:
                    case 0xA001U:
                    case 0xA002U:
                    case 0xA003U:
                        chr1k_[addr & 0x03U] = value;
                        apply_chr();
                        break;
                    case 0xC000U:
                        irq_latch_ = value;
                        break;
                    case 0xC001U:
                        irq_counter_ = 0U; // reload on the next scanline clock
                        irq_reload_ = true;
                        break;
                    case 0xC002U:
                        irq_enabled_ = true;
                        break;
                    case 0xC003U:
                        irq_enabled_ = false;
                        raise_irq(false); // disable acknowledges any pending IRQ
                        break;
                    case 0xE000U:
                        mirror_ = static_cast<std::uint8_t>((value >> 6U) & 0x01U);
                        apply_mirroring();
                        break;
                    default:
                        break;
                    }
                    return;
                }
                // TC0190: $8000 bit 6 is mirroring; $8xxx/$Axxx banking; no IRQ.
                switch (addr & 0xA003U) {
                case 0x8000U:
                    mirror_ = static_cast<std::uint8_t>((value >> 6U) & 0x01U);
                    prg_bank_[0] = value & 0x3FU;
                    apply_prg();
                    apply_mirroring();
                    break;
                case 0x8001U:
                    prg_bank_[1] = value & 0x3FU;
                    apply_prg();
                    break;
                case 0x8002U:
                    chr2k_[0] = value;
                    apply_chr();
                    break;
                case 0x8003U:
                    chr2k_[1] = value;
                    apply_chr();
                    break;
                case 0xA000U:
                case 0xA001U:
                case 0xA002U:
                case 0xA003U:
                    chr1k_[addr & 0x03U] = value;
                    apply_chr();
                    break;
                default:
                    break;
                }
            }

            void clock_scanline(std::uint32_t line) override {
                if (!with_irq_ || line >= 240U) {
                    return; // TC0190 has no IRQ; TC0690 counts on visible scanlines
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
                for (const std::uint8_t b : prg_bank_) {
                    writer.u8(b);
                }
                for (const std::uint8_t b : chr2k_) {
                    writer.u8(b);
                }
                for (const std::uint8_t b : chr1k_) {
                    writer.u8(b);
                }
                writer.u8(mirror_);
                writer.u8(irq_latch_);
                writer.u8(irq_counter_);
                writer.boolean(irq_enabled_);
                writer.boolean(irq_reload_);
            }
            void load_state(chips::state_reader& reader) override {
                for (std::uint8_t& b : prg_bank_) {
                    b = reader.u8();
                }
                for (std::uint8_t& b : chr2k_) {
                    b = reader.u8();
                }
                for (std::uint8_t& b : chr1k_) {
                    b = reader.u8();
                }
                mirror_ = reader.u8();
                irq_latch_ = reader.u8();
                irq_counter_ = reader.u8();
                irq_enabled_ = reader.boolean();
                irq_reload_ = reader.boolean();
                apply();
            }

          private:
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
                map_prg_8k(0x8000U, prg_bank_[0]);
                map_prg_8k(0xA000U, prg_bank_[1]);
                map_prg_8k(0xC000U, count >= 2U ? count - 2U : 0U);
                map_prg_8k(0xE000U, count - 1U);
            }

            void apply_chr() {
                // Two 2 KiB banks ($0000/$0800) then four 1 KiB banks ($1000-$1FFF).
                chr_win_.set_2k(0U, chr2k_[0]);
                chr_win_.set_2k(1U, chr2k_[1]);
                chr_win_.set_1k(4U, chr1k_[0]);
                chr_win_.set_1k(5U, chr1k_[1]);
                chr_win_.set_1k(6U, chr1k_[2]);
                chr_win_.set_1k(7U, chr1k_[3]);
            }

            void apply_mirroring() { apply_mirror_mode(*ppu_, mirror_); }

            bool with_irq_;
            std::array<std::uint8_t, 2> prg_bank_{}; // $8000 / $A000 8 KiB selects
            std::array<std::uint8_t, 2> chr2k_{};    // $0000 / $0800 2 KiB selects
            std::array<std::uint8_t, 4> chr1k_{};    // $1000-$1FFF 1 KiB selects
            std::uint8_t mirror_{};
            std::uint8_t irq_latch_{};
            std::uint8_t irq_counter_{};
            bool irq_enabled_{};
            bool irq_reload_{};
            chr_window chr_win_;
        };
    } // namespace

    std::unique_ptr<nes_mapper> make_taito_tc0190_mapper(nes_mapper_build_context context) {
        return std::make_unique<taito_tc0190_mapper>(context.bus, context.ppu, context.prg,
                                                     context.chr, context.chr_is_ram, false);
    }

    std::unique_ptr<nes_mapper> make_taito_tc0690_mapper(nes_mapper_build_context context) {
        return std::make_unique<taito_tc0190_mapper>(context.bus, context.ppu, context.prg,
                                                     context.chr, context.chr_is_ram, true);
    }

} // namespace mnemos::manifests::nes
