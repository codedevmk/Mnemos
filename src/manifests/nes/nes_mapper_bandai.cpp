#include "nes_mapper_bandai.hpp"

#include "eeprom_i2c.hpp" // chips::storage::eeprom_i2c (the Bandai FCG's serial save EEPROM)
#include "nes_mapper_helpers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace mnemos::manifests::nes {

    using detail::apply_mirror_mode;
    using detail::chr_window;
    using detail::k_prg_bank;

    namespace {
        // Bandai FCG / LZ93D50 (iNES 16): the Dragon Ball / SD Gundam board family.
        // Eight 1 KiB CHR banks, a 16 KiB switchable PRG bank (the last 16 KiB is
        // fixed), four mirroring modes, a 16-bit down-counter IRQ clocked on the CPU
        // (M2) clock, and -- on the LZ93D50 -- a serial I2C EEPROM (24C02) for battery
        // saves. iNES mapper 16 carries no submapper, so the registers are decoded in
        // BOTH the FCG-1/2 range ($6000-$7FFF) and the LZ93D50 range ($8000-$FFFF) --
        // the register is the low nibble of the address either way -- and the EEPROM
        // read port answers at $6000-$7FFF.
        class bandai_fcg_mapper final : public nes_mapper {
          public:
            bandai_fcg_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                              std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                              bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_win_(chr, chr_is_ram) {}

            void reset() override {
                chr_bank_.fill(0U);
                prg_bank_ = 0U;
                mirror_ = 0U;
                irq_latch_ = 0U;
                irq_counter_ = 0U;
                irq_enabled_ = false;
                eeprom_.reset();
                if (prg_16k_count() != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_bank));
                    bus_->map_rom(0xC000U,
                                  prg_.subspan((prg_16k_count() - 1U) * k_prg_bank, k_prg_bank));
                }
                chr_win_.attach(*ppu_);
                apply_prg();
                apply_chr();
                apply_mirroring();
                // Registers at $8000-$FFFF (writes only; reads stay PRG ROM).
                install_register_write_hook();
                // The FCG-1/2 register range + the LZ93D50 EEPROM read port live at
                // $6000-$7FFF, overlaying the (unused on this board) work RAM.
                bus_->map_mmio(
                    0x6000U, 0x2000U,
                    [this](std::uint32_t) -> std::uint8_t {
                        return eeprom_.sda() ? 0x01U : 0x00U; // EEPROM data out on D0
                    },
                    [this](std::uint32_t addr, std::uint8_t value) {
                        write(static_cast<std::uint16_t>(addr), value);
                    },
                    1);
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                switch (addr & 0x000FU) {
                case 0x0U:
                case 0x1U:
                case 0x2U:
                case 0x3U:
                case 0x4U:
                case 0x5U:
                case 0x6U:
                case 0x7U:
                    chr_bank_[addr & 0x07U] = value; // 1 KiB CHR bank for $0000 + n*$400
                    apply_chr();
                    break;
                case 0x8U:
                    prg_bank_ = value & 0x0FU; // 16 KiB bank at $8000 (lower 4 bits)
                    apply_prg();
                    break;
                case 0x9U:
                    mirror_ = value & 0x03U;
                    apply_mirroring();
                    break;
                case 0xAU: // IRQ control: bit 0 enables; the write acks + copies latch
                    raise_irq(false);
                    irq_counter_ = irq_latch_; // LZ93D50: latch -> counter on $xA
                    irq_enabled_ = (value & 0x01U) != 0U;
                    if (irq_enabled_ && irq_counter_ == 0U) {
                        raise_irq(true); // enabled while holding zero -> immediate IRQ
                    }
                    break;
                case 0xBU:
                    irq_latch_ = static_cast<std::uint16_t>((irq_latch_ & 0xFF00U) | value);
                    break;
                case 0xCU:
                    irq_latch_ = static_cast<std::uint16_t>(
                        (irq_latch_ & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U));
                    break;
                case 0xDU: { // EEPROM control: bit 7 read-enable, bit 6 SDA, bit 5 SCL
                    const bool scl = (value & 0x20U) != 0U;
                    const bool read_enable = (value & 0x80U) != 0U;
                    // In read mode the CPU releases SDA so the EEPROM drives the line.
                    const bool sda = read_enable ? true : ((value & 0x40U) != 0U);
                    eeprom_.update(scl, sda);
                    break;
                }
                default:
                    break;
                }
            }

            // The 16-bit counter decrements on every M2 (CPU) cycle; the board clocks
            // it ungated (it is not A12-driven). It asserts /IRQ as it passes through
            // zero; the line holds until $xA acknowledges it.
            void clock_cpu_timer(std::uint32_t cpu_cycles) override {
                if (!irq_enabled_) {
                    return;
                }
                if (irq_counter_ <= cpu_cycles) {
                    raise_irq(true);
                }
                irq_counter_ = static_cast<std::uint16_t>(irq_counter_ - cpu_cycles);
            }

            void save_state(chips::state_writer& writer) const override {
                for (const std::uint8_t b : chr_bank_) {
                    writer.u8(b);
                }
                writer.u8(prg_bank_);
                writer.u8(mirror_);
                writer.u32(irq_latch_);
                writer.u32(irq_counter_);
                writer.boolean(irq_enabled_);
                eeprom_.save_state(writer);
            }
            void load_state(chips::state_reader& reader) override {
                for (std::uint8_t& b : chr_bank_) {
                    b = reader.u8();
                }
                prg_bank_ = reader.u8();
                mirror_ = reader.u8();
                irq_latch_ = static_cast<std::uint16_t>(reader.u32());
                irq_counter_ = static_cast<std::uint16_t>(reader.u32());
                irq_enabled_ = reader.boolean();
                eeprom_.load_state(reader);
                apply_prg();
                apply_chr();
                apply_mirroring();
            }

            // The serial EEPROM is the cart's battery medium -- exposed for .srm
            // persistence. Always non-empty (a game that never writes it simply
            // leaves it erased, so the save guard's change check writes no file).
            [[nodiscard]] std::span<std::uint8_t> battery_ram() noexcept override {
                return eeprom_.bytes();
            }

          private:
            void apply_prg() { map_prg_16k(0x8000U, prg_bank_); }

            void apply_chr() {
                for (std::size_t s = 0; s < 8U; ++s) {
                    chr_win_.set_1k(s, chr_bank_[s]);
                }
            }

            void apply_mirroring() { apply_mirror_mode(*ppu_, mirror_); }

            std::array<std::uint8_t, 8> chr_bank_{};
            std::uint8_t prg_bank_{};
            std::uint8_t mirror_{};
            std::uint16_t irq_latch_{};
            std::uint16_t irq_counter_{};
            bool irq_enabled_{};
            chr_window chr_win_;
            chips::storage::eeprom_i2c eeprom_{256U}; // 24C02 serial EEPROM (saves)
        };

    } // namespace

    std::unique_ptr<nes_mapper> make_bandai_fcg_mapper(nes_mapper_build_context context) {
        return std::make_unique<bandai_fcg_mapper>(context.bus, context.ppu, context.prg,
                                                   context.chr, context.chr_is_ram);
    }

} // namespace mnemos::manifests::nes
