#include "nes_mapper_mmc1.hpp"

#include "nes_mapper_helpers.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace mnemos::manifests::nes {

    using detail::chr_window;
    using detail::k_prg_bank;

    namespace {
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
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_win_(chr, chr_is_ram) {}

            void reset() override {
                control_ = 0x0CU; // power-on: PRG mode 3 ($8000 switchable, $C000 fixed last)
                shift_ = 0U;
                count_ = 0U;
                chr_bank0_ = chr_bank1_ = prg_bank_ = 0U;
                if (prg_16k_count() != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_bank));
                    bus_->map_rom(0xC000U,
                                  prg_.subspan((prg_16k_count() - 1U) * k_prg_bank, k_prg_bank));
                }
                chr_win_.attach(*ppu_);
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

            void apply_prg() {
                const unsigned mode = (control_ >> 2U) & 0x03U;
                if (mode <= 1U) { // 32 KiB at $8000 (low PRG-bank bit ignored)
                    const std::size_t base = static_cast<std::size_t>(prg_bank_ & 0x0EU);
                    map_prg_16k(0x8000U, base);
                    map_prg_16k(0xC000U, base + 1U);
                } else if (mode == 2U) { // fix first bank at $8000, switch 16 KiB at $C000
                    map_prg_16k(0x8000U, 0U);
                    map_prg_16k(0xC000U, prg_bank_);
                } else { // mode 3: switch 16 KiB at $8000, fix last bank at $C000
                    map_prg_16k(0x8000U, prg_bank_);
                    map_prg_16k(0xC000U, prg_16k_count() != 0U ? prg_16k_count() - 1U : 0U);
                }
            }

            void apply_chr() {
                if ((control_ & 0x10U) == 0U) { // 8 KiB CHR (low bit of chr_bank0 ignored)
                    chr_win_.set_4k(0U, static_cast<std::size_t>(chr_bank0_ & 0x1EU));
                    chr_win_.set_4k(1U, static_cast<std::size_t>(chr_bank0_ & 0x1EU) + 1U);
                } else { // two independent 4 KiB banks
                    chr_win_.set_4k(0U, chr_bank0_);
                    chr_win_.set_4k(1U, chr_bank1_);
                }
            }

            std::uint8_t control_{0x0CU};
            std::uint8_t shift_{};
            std::uint8_t count_{};
            std::uint8_t chr_bank0_{};
            std::uint8_t chr_bank1_{};
            std::uint8_t prg_bank_{};
            chr_window chr_win_;
        };

    } // namespace

    std::unique_ptr<nes_mapper> make_mmc1_mapper(nes_mapper_build_context context) {
        return std::make_unique<mmc1_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                             context.chr_is_ram);
    }

} // namespace mnemos::manifests::nes
