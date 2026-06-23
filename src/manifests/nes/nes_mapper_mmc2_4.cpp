#include "nes_mapper_mmc2_4.hpp"

#include "nes_mapper_helpers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace mnemos::manifests::nes {

    using detail::chr_window;
    using detail::k_prg_8k;
    using detail::k_prg_bank;

    namespace {
        // MMC2 (iNES 9, Punch-Out!!) / MMC4 (iNES 10, Fire Emblem, Famicom Wars).
        // Two 4 KiB CHR latches -- one per pattern table -- automatically flip between
        // a $FD and a $FE bank when the PPU fetches tile $FD or $FE from that table
        // (the addresses $xFD8 / $xFE8), which is how these games animate big sprites
        // without CPU intervention. The PPU's chr_fetch callback drives the flip; the
        // mapper re-points its CHR window on a change. Registers: $A000 PRG bank,
        // $B000/$C000 = $0xxx CHR-$FD/$FE banks, $D000/$E000 = $1xxx CHR-$FD/$FE, $F000
        // mirroring. PRG differs: MMC2 = 8 KiB switchable + three fixed last banks;
        // MMC4 = 16 KiB switchable + fixed last 16 KiB (UxROM-style, with $6000 RAM).
        class mmc2_4_mapper final : public nes_mapper {
          public:
            mmc2_4_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                          std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                          bool chr_is_ram, bool is_mmc4) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), is_mmc4_(is_mmc4),
                  chr_win_(chr, chr_is_ram) {}

            void reset() override {
                prg_bank_ = 0U;
                chr_bank_.fill(0U);
                latch_ = {true, true}; // power-on: both latches select the $FE bank
                mirror_ = 0U;

                // Map the initial regions at the SIZE apply_prg() will retarget (so
                // retarget_rom finds a matching region): MMC4 uses two 16 KiB windows,
                // MMC2 four 8 KiB windows.
                if (is_mmc4_) {
                    if (prg_16k_count() != 0U) {
                        bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_bank));
                        bus_->map_rom(0xC000U, prg_.subspan(0, k_prg_bank));
                    }
                } else if (prg_8k_count() != 0U) {
                    for (const std::uint32_t slot : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
                        bus_->map_rom(slot, prg_.subspan(0, k_prg_8k));
                    }
                }
                chr_win_.attach(*ppu_);
                ppu_->set_chr_fetch_callback([this](std::uint16_t addr) { chr_latch(addr); });

                apply_prg();
                apply_chr();
                apply_mirroring();
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                switch (addr & 0xF000U) {
                case 0xA000U: // PRG bank
                    prg_bank_ = static_cast<std::uint8_t>(value & 0x0FU);
                    apply_prg();
                    break;
                case 0xB000U: // $0xxx CHR bank, latch = $FD
                    chr_bank_[0] = static_cast<std::uint8_t>(value & 0x1FU);
                    apply_chr();
                    break;
                case 0xC000U: // $0xxx CHR bank, latch = $FE
                    chr_bank_[1] = static_cast<std::uint8_t>(value & 0x1FU);
                    apply_chr();
                    break;
                case 0xD000U: // $1xxx CHR bank, latch = $FD
                    chr_bank_[2] = static_cast<std::uint8_t>(value & 0x1FU);
                    apply_chr();
                    break;
                case 0xE000U: // $1xxx CHR bank, latch = $FE
                    chr_bank_[3] = static_cast<std::uint8_t>(value & 0x1FU);
                    apply_chr();
                    break;
                case 0xF000U: // mirroring (bit 0)
                    mirror_ = static_cast<std::uint8_t>(value & 0x01U);
                    apply_mirroring();
                    break;
                default:
                    break;
                }
            }

            void save_state(chips::state_writer& writer) const override {
                writer.u8(prg_bank_);
                for (const std::uint8_t b : chr_bank_) {
                    writer.u8(b);
                }
                writer.boolean(latch_[0]);
                writer.boolean(latch_[1]);
                writer.u8(mirror_);
            }
            void load_state(chips::state_reader& reader) override {
                prg_bank_ = reader.u8();
                for (std::uint8_t& b : chr_bank_) {
                    b = reader.u8();
                }
                latch_[0] = reader.boolean();
                latch_[1] = reader.boolean();
                mirror_ = reader.u8();
                apply_prg();
                apply_chr();
                apply_mirroring();
            }

          private:
            // PPU pattern-fetch hook: tile $FD/$FE in a table flips that table's latch.
            void chr_latch(std::uint16_t addr) noexcept {
                const std::uint16_t a = static_cast<std::uint16_t>(addr & 0x1FFFU);
                const std::size_t table = (a >> 12U) & 1U; // 0 = $0xxx, 1 = $1xxx
                const std::uint16_t region = static_cast<std::uint16_t>(a & 0x0FF8U);
                bool next = latch_[table];
                if (region == 0x0FD8U) {
                    next = false; // $FD
                } else if (region == 0x0FE8U) {
                    next = true; // $FE
                } else {
                    return;
                }
                if (next != latch_[table]) {
                    latch_[table] = next;
                    apply_chr();
                }
            }

            void apply_prg() {
                if (is_mmc4_) { // 16 KiB switchable at $8000 + fixed last 16 KiB
                    const std::size_t count16 = prg_16k_count();
                    if (count16 == 0U) {
                        return;
                    }
                    map_prg_16k(0x8000U, prg_bank_);
                    map_prg_16k(0xC000U, count16 - 1U);
                    return;
                }
                const std::size_t count8 = prg_8k_count();
                if (count8 == 0U) { // MMC2: 8 KiB switchable + three fixed last
                    return;
                }
                map_prg_8k(0x8000U, prg_bank_);
                map_prg_8k(0xA000U, count8 - 3U);
                map_prg_8k(0xC000U, count8 - 2U);
                map_prg_8k(0xE000U, count8 - 1U);
            }

            void apply_chr() {
                // $0xxx half: bank chr_bank_[0/1] selected by latch 0; $1xxx half:
                // chr_bank_[2/3] by latch 1.
                chr_win_.set_4k(0U, chr_bank_[latch_[0] ? 1U : 0U]);
                chr_win_.set_4k(1U, chr_bank_[latch_[1] ? 3U : 2U]);
            }

            void apply_mirroring() {
                using m = chips::video::ppu2c02::mirroring;
                ppu_->set_mirroring(mirror_ != 0U ? m::horizontal : m::vertical);
            }

            bool is_mmc4_;
            std::uint8_t prg_bank_{};
            std::array<std::uint8_t, 4> chr_bank_{}; // [0]=$0/FD [1]=$0/FE [2]=$1/FD [3]=$1/FE
            std::array<bool, 2> latch_{true, true};  // false=$FD, true=$FE per table
            std::uint8_t mirror_{};
            chr_window chr_win_;
        };
    } // namespace

    std::unique_ptr<nes_mapper> make_mmc2_mapper(nes_mapper_build_context context) {
        return std::make_unique<mmc2_4_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                               context.chr_is_ram, false);
    }

    std::unique_ptr<nes_mapper> make_mmc4_mapper(nes_mapper_build_context context) {
        return std::make_unique<mmc2_4_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                               context.chr_is_ram, true);
    }

} // namespace mnemos::manifests::nes
