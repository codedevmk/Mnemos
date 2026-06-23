#include "nes_mapper_mmc5.hpp"

#include "mmc5.hpp" // chips::audio::mmc5 (the MMC5's 2 pulse + raw PCM sound block)
#include "nes_mapper_helpers.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace mnemos::manifests::nes {

    using detail::k_chr_1k;
    using detail::k_chr_4k;
    using detail::k_prg_8k;

    namespace {
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
    } // namespace

    std::unique_ptr<nes_mapper> make_mmc5_mapper(nes_mapper_build_context context) {
        return std::make_unique<mmc5_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                             context.chr_is_ram);
    }

} // namespace mnemos::manifests::nes
