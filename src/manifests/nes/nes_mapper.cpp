#include "nes_mapper.hpp"

#include "mmc5.hpp" // chips::audio::mmc5 (the MMC5's 2 pulse + raw PCM sound block)
#include "n163.hpp" // chips::audio::n163 (the Namco 163's wavetable sound block)
#include "nes_mapper_bandai.hpp"
#include "nes_mapper_discrete.hpp"
#include "nes_mapper_helpers.hpp"
#include "nes_mapper_jaleco.hpp"
#include "nes_mapper_konami.hpp"
#include "nes_mapper_mmc1.hpp"
#include "nes_mapper_mmc3.hpp"
#include "nes_mapper_sunsoft.hpp"
#include "nes_mapper_taito.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace mnemos::manifests::nes {

    using detail::chr_window;
    using detail::k_chr_1k;
    using detail::k_chr_4k;
    using detail::k_prg_8k;
    using detail::k_prg_bank;

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
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_win_(chr, chr_is_ram) {}

            void reset() override {
                prg_regs_ = {0U, 0U, 0U};
                chr_banks_.fill(0U);
                nt_banks_ = {0xE0U, 0xE0U, 0xE1U, 0xE1U};
                irq_counter_ = 0U;
                irq_enabled_ = false;

                if (prg_8k_count() != 0U) {
                    for (const std::uint32_t slot : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
                        bus_->map_rom(slot, prg_.subspan(0, k_prg_8k));
                    }
                }
                chr_win_.attach(*ppu_);

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

            void apply_prg() {
                const std::size_t count = prg_8k_count();
                if (count == 0U) {
                    return;
                }
                map_prg_8k(0x8000U, prg_regs_[0]);
                map_prg_8k(0xA000U, prg_regs_[1]);
                map_prg_8k(0xC000U, prg_regs_[2]);
                map_prg_8k(0xE000U, count - 1U); // last 8 KiB fixed at $E000
            }

            void apply_chr() {
                for (std::size_t s = 0; s < 8U; ++s) {
                    chr_win_.set_1k(s, chr_banks_[s]);
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

            chips::audio::n163 sound_;
            std::array<std::uint8_t, 3> prg_regs_{}; // $8000/$A000/$C000 8 KiB selects
            std::array<std::uint8_t, 8> chr_banks_{};
            std::array<std::uint8_t, 4> nt_banks_{}; // nametable page selectors
            std::uint16_t irq_counter_{};            // 15-bit CPU-cycle up-counter
            bool irq_enabled_{};
            chr_window chr_win_;
        };

    } // namespace

    std::unique_ptr<nes_mapper> make_mapper(int number, topology::bus& bus,
                                            chips::video::ppu2c02& ppu,
                                            std::span<const std::uint8_t> prg,
                                            std::span<std::uint8_t> chr, bool chr_is_ram) {
        const nes_mapper_build_context context{bus, ppu, prg, chr, chr_is_ram};
        switch (number) {
        case 1:
            return make_mmc1_mapper(context);
        case 2:
            return make_uxrom_mapper(context);
        case 3:
            return make_cnrom_mapper(context);
        case 4:
            return make_mmc3_mapper(context);
        case 5:
            return std::make_unique<mmc5_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 7:
            return make_axrom_mapper(context);
        case 9: // MMC2 (Punch-Out!!)
            return std::make_unique<mmc2_4_mapper>(bus, ppu, prg, chr, chr_is_ram, false);
        case 10: // MMC4 (Fire Emblem, Famicom Wars)
            return std::make_unique<mmc2_4_mapper>(bus, ppu, prg, chr, chr_is_ram, true);
        case 11:
            return make_color_dreams_mapper(context);
        case 16: // Bandai FCG / LZ93D50
            return make_bandai_fcg_mapper(context);
        case 18: // Jaleco SS88006
            return make_jaleco_ss88006_mapper(context);
        case 33: // Taito TC0190 (no IRQ)
            return make_taito_tc0190_mapper(context);
        case 48: // Taito TC0690 (TC0190 + MMC3-style scanline IRQ)
            return make_taito_tc0690_mapper(context);
        case 19:
            return std::make_unique<namco163_mapper>(bus, ppu, prg, chr, chr_is_ram);
        case 34: // BNROM (CHR-RAM) / NINA-001 (CHR-ROM)
            return make_bnrom_nina_mapper(context);
        case 21: // VRC4a/c
        case 22: // VRC2a
        case 23: // VRC2b / VRC4e/f
        case 25: // VRC2c / VRC4b/d
            return make_vrc2_4_mapper(context, number);
        case 24: // VRC6a
            return make_vrc6_mapper(context, false);
        case 26: // VRC6b (A0/A1 swapped)
            return make_vrc6_mapper(context, true);
        case 64: // RAMBO-1 / Tengen 800032
            return make_rambo1_mapper(context);
        case 66:
            return make_gxrom_mapper(context);
        case 67: // Sunsoft-3
            return make_sunsoft3_mapper(context);
        case 68:
            return make_sunsoft4_mapper(context);
        case 69:
            return make_sunsoft5b_mapper(context);
        case 71:
            return make_camerica_mapper(context);
        case 73:
            return make_vrc3_mapper(context);
        case 75:
            return make_vrc1_mapper(context);
        case 85:
            return make_vrc7_mapper(context);
        case 206:
            return make_namco118_mapper(context);
        case 0:
        default:
            return make_nrom_mapper(context);
        }
    }

} // namespace mnemos::manifests::nes
