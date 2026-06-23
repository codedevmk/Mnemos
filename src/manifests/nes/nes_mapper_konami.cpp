#include "nes_mapper_konami.hpp"

#include "nes_mapper_helpers.hpp"
#include "vrc6.hpp"   // chips::audio::vrc6 (the VRC6's pulse + sawtooth sound)
#include "ym2413.hpp" // chips::audio::ym2413 (the VRC7's OPLL sound block)

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace mnemos::manifests::nes {

    using detail::chr_window;
    using detail::k_prg_8k;
    using detail::k_prg_bank;
    using detail::konami_vrc_irq;

    namespace {
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
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_win_(chr, chr_is_ram) {}

            void reset() override {
                prg_regs_ = {0U, 0U, 0U};
                chr_banks_.fill(0U);
                mirror_ = 0U;
                irq_.reset();

                if (prg_8k_count() != 0U) {
                    for (const std::uint32_t slot : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
                        bus_->map_rom(slot, prg_.subspan(0, k_prg_8k));
                    }
                }
                chr_win_.attach(*ppu_);

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
                        irq_.latch = value;
                    }
                    break;
                case 0xF000U:
                    if (!a4) { // $F000 = IRQ control
                        irq_.write_control(value, [this](bool a) { raise_irq(a); });
                    } else { // $F010 = IRQ acknowledge
                        irq_.acknowledge([this](bool a) { raise_irq(a); });
                    }
                    break;
                default:
                    break;
                }
            }

            void clock_cpu_timer(std::uint32_t cpu_cycles) override {
                irq_.clock(cpu_cycles, [this](bool a) { raise_irq(a); });
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
                irq_.save(writer);
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
                irq_.load(reader);
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

            void apply_prg() {
                const std::size_t count = prg_8k_count();
                if (count == 0U) {
                    return;
                }
                map_prg_8k(0x8000U, prg_regs_[0] & 0x3FU);
                map_prg_8k(0xA000U, prg_regs_[1] & 0x3FU);
                map_prg_8k(0xC000U, prg_regs_[2] & 0x3FU);
                map_prg_8k(0xE000U, count - 1U); // last bank fixed at $E000
            }

            void apply_chr() {
                for (std::size_t s = 0; s < 8U; ++s) {
                    chr_win_.set_1k(s, chr_banks_[s]);
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

            chips::audio::ym2413 opll_;
            std::array<std::uint8_t, 3> prg_regs_{}; // $8000, $A000, $C000 8 KiB selects
            std::array<std::uint8_t, 8> chr_banks_{};
            std::uint8_t mirror_{};
            konami_vrc_irq irq_; // $E010 latch + $F000/$F010 control/ack
            chr_window chr_win_;
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
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), variant_b_(variant_b),
                  chr_win_(chr, chr_is_ram) {}

            void reset() override {
                prg16_ = 0U;
                prg8_ = 0U;
                chr_banks_.fill(0U);
                ppu_ctrl_ = 0U;
                irq_.reset();

                if (prg_8k_count() != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_bank)); // 16 KiB switchable
                    bus_->map_rom(0xC000U, prg_.subspan(0, k_prg_8k));   // 8 KiB switchable
                    bus_->map_rom(0xE000U, prg_.subspan(0, k_prg_8k));   // 8 KiB fixed (last)
                }
                chr_win_.attach(*ppu_);

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
                        irq_.latch = value;
                    } else if (sub == 0x01U) {
                        irq_.write_control(value, [this](bool a) { raise_irq(a); });
                    } else if (sub == 0x02U) {
                        irq_.acknowledge([this](bool a) { raise_irq(a); });
                    }
                    break;
                default:
                    break;
                }
            }

            void clock_cpu_timer(std::uint32_t cpu_cycles) override {
                irq_.clock(cpu_cycles, [this](bool a) { raise_irq(a); });
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
                irq_.save(writer);
                sound_.save_state(writer);
            }
            void load_state(chips::state_reader& reader) override {
                prg16_ = reader.u8();
                prg8_ = reader.u8();
                for (std::uint8_t& b : chr_banks_) {
                    b = reader.u8();
                }
                ppu_ctrl_ = reader.u8();
                irq_.load(reader);
                sound_.load_state(reader);
                apply_prg();
                apply_chr();
                apply_mirroring();
            }

          private:
            void apply_prg() {
                const std::size_t count8 = prg_8k_count();
                if (count8 == 0U) {
                    return;
                }
                map_prg_16k(0x8000U, prg16_);
                map_prg_8k(0xC000U, prg8_);
                map_prg_8k(0xE000U, count8 - 1U); // last 8 KiB fixed at $E000
            }

            void apply_chr() {
                for (std::size_t s = 0; s < 8U; ++s) {
                    chr_win_.set_1k(s, chr_banks_[s]);
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

            bool variant_b_;
            chips::audio::vrc6 sound_;
            std::uint8_t prg16_{};
            std::uint8_t prg8_{};
            std::array<std::uint8_t, 8> chr_banks_{};
            std::uint8_t ppu_ctrl_{}; // $B003
            konami_vrc_irq irq_;      // $F000-$F002
            chr_window chr_win_;
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
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_win_(chr, chr_is_ram) {
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
                irq_.reset();

                if (prg_8k_count() != 0U) {
                    for (const std::uint32_t slot : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
                        bus_->map_rom(slot, prg_.subspan(0, k_prg_8k));
                    }
                }
                chr_win_.attach(*ppu_);

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
                        irq_.latch =
                            static_cast<std::uint8_t>((irq_.latch & 0xF0U) | (value & 0x0FU));
                    } else if (sub == 1U) {
                        irq_.latch = static_cast<std::uint8_t>((irq_.latch & 0x0FU) |
                                                               ((value & 0x0FU) << 4U));
                    } else if (sub == 2U) {
                        irq_.write_control(value, [this](bool a) { raise_irq(a); });
                    } else {
                        irq_.acknowledge([this](bool a) { raise_irq(a); });
                    }
                    break;
                default:
                    break;
                }
            }

            void clock_cpu_timer(std::uint32_t cpu_cycles) override {
                irq_.clock(cpu_cycles, [this](bool a) { raise_irq(a); });
            }

            void save_state(chips::state_writer& writer) const override {
                writer.u8(prg0_);
                writer.u8(prg1_);
                writer.boolean(swap_mode_);
                writer.u8(mirror_);
                for (const std::uint16_t b : chr_bank_) {
                    writer.u16(b);
                }
                irq_.save(writer);
            }
            void load_state(chips::state_reader& reader) override {
                prg0_ = reader.u8();
                prg1_ = reader.u8();
                swap_mode_ = reader.boolean();
                mirror_ = reader.u8();
                for (std::uint16_t& b : chr_bank_) {
                    b = reader.u16();
                }
                irq_.load(reader);
                apply_prg();
                apply_chr();
                apply_mirroring();
            }

          private:
            void apply_prg() {
                const std::size_t count = prg_8k_count();
                if (count == 0U) {
                    return;
                }
                const std::size_t second_last = count >= 2U ? count - 2U : 0U;
                if (swap_mode_) { // VRC4: $8000 fixed second-last, select moves to $C000
                    map_prg_8k(0x8000U, second_last);
                    map_prg_8k(0xC000U, prg0_);
                } else {
                    map_prg_8k(0x8000U, prg0_);
                    map_prg_8k(0xC000U, second_last);
                }
                map_prg_8k(0xA000U, prg1_);
                map_prg_8k(0xE000U, count - 1U); // last bank fixed at $E000
            }

            void apply_chr() {
                for (std::size_t s = 0; s < 8U; ++s) {
                    const std::size_t bank =
                        chr_shift_ != 0 ? (chr_bank_[s] >> chr_shift_) : chr_bank_[s];
                    chr_win_.set_1k(s, bank);
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

            std::uint16_t a_mask_{}; // address bit forming register-index bit 0
            std::uint16_t b_mask_{}; // address bit forming register-index bit 1
            bool is_vrc4_{};         // IRQ + PRG-swap + single-screen present
            int chr_shift_{};        // VRC2a drops the low CHR bit
            std::uint8_t prg0_{};
            std::uint8_t prg1_{};
            bool swap_mode_{};
            std::uint8_t mirror_{};
            std::array<std::uint16_t, 8> chr_bank_{}; // 9-bit each (low + high nibble)
            konami_vrc_irq irq_;                      // $F000-$F003 (VRC4 only)
            chr_window chr_win_;
        };

        // Konami VRC1 (iNES 75). Three switchable 8 KiB PRG banks ($8000/$A000/$C000)
        // over the fixed last bank ($E000); two 4 KiB CHR banks ($E000/$F000 low
        // nibble) whose 5th bit + the mirroring come from $9000. No IRQ, no audio.
        class vrc1_mapper final : public nes_mapper {
          public:
            vrc1_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                        std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                        bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_win_(chr, chr_is_ram) {}

            void reset() override {
                prg_reg_ = {0U, 0U, 0U};
                chr_lo_ = {0U, 0U};
                chr_hi_ = {0U, 0U};
                mirror_ = 0U;
                if (prg_8k_count() != 0U) {
                    for (const std::uint32_t slot : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
                        bus_->map_rom(slot, prg_.subspan(0, k_prg_8k));
                    }
                }
                chr_win_.attach(*ppu_);
                apply_prg();
                apply_chr();
                apply_mirroring();
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                switch (addr & 0xF000U) {
                case 0x8000U:
                    prg_reg_[0] = static_cast<std::uint8_t>(value & 0x0FU);
                    apply_prg();
                    break;
                case 0x9000U: // mirroring (bit 0) + CHR high bits (bit 1 -> CHR0.4, bit 2 ->
                              // CHR1.4)
                    mirror_ = static_cast<std::uint8_t>(value & 0x01U);
                    chr_hi_[0] = static_cast<std::uint8_t>((value >> 1U) & 0x01U);
                    chr_hi_[1] = static_cast<std::uint8_t>((value >> 2U) & 0x01U);
                    apply_chr();
                    apply_mirroring();
                    break;
                case 0xA000U:
                    prg_reg_[1] = static_cast<std::uint8_t>(value & 0x0FU);
                    apply_prg();
                    break;
                case 0xC000U:
                    prg_reg_[2] = static_cast<std::uint8_t>(value & 0x0FU);
                    apply_prg();
                    break;
                case 0xE000U:
                    chr_lo_[0] = static_cast<std::uint8_t>(value & 0x0FU);
                    apply_chr();
                    break;
                case 0xF000U:
                    chr_lo_[1] = static_cast<std::uint8_t>(value & 0x0FU);
                    apply_chr();
                    break;
                default:
                    break;
                }
            }

            void save_state(chips::state_writer& writer) const override {
                for (const std::uint8_t b : prg_reg_) {
                    writer.u8(b);
                }
                for (const std::uint8_t b : chr_lo_) {
                    writer.u8(b);
                }
                for (const std::uint8_t b : chr_hi_) {
                    writer.u8(b);
                }
                writer.u8(mirror_);
            }
            void load_state(chips::state_reader& reader) override {
                for (std::uint8_t& b : prg_reg_) {
                    b = reader.u8();
                }
                for (std::uint8_t& b : chr_lo_) {
                    b = reader.u8();
                }
                for (std::uint8_t& b : chr_hi_) {
                    b = reader.u8();
                }
                mirror_ = reader.u8();
                apply_prg();
                apply_chr();
                apply_mirroring();
            }

          private:
            void apply_prg() {
                const std::size_t count = prg_8k_count();
                map_prg_8k(0x8000U, prg_reg_[0]);
                map_prg_8k(0xA000U, prg_reg_[1]);
                map_prg_8k(0xC000U, prg_reg_[2]);
                map_prg_8k(0xE000U, count != 0U ? count - 1U : 0U);
            }
            void apply_chr() {
                for (std::size_t h = 0; h < 2U; ++h) {
                    const std::size_t bank =
                        (static_cast<std::size_t>(chr_hi_[h]) << 4U) | chr_lo_[h];
                    chr_win_.set_4k(h, bank);
                }
            }
            void apply_mirroring() {
                using m = chips::video::ppu2c02::mirroring;
                ppu_->set_mirroring(mirror_ != 0U ? m::horizontal : m::vertical);
            }
            std::array<std::uint8_t, 3> prg_reg_{}; // $8000/$A000/$C000 8 KiB selects
            std::array<std::uint8_t, 2> chr_lo_{};
            std::array<std::uint8_t, 2> chr_hi_{};
            std::uint8_t mirror_{};
            chr_window chr_win_;
        };

        // Konami VRC3 (iNES 73, Salamander). A 16 KiB switchable PRG bank ($F000) over
        // the fixed last 16 KiB, 8 KiB CHR-RAM, and a 16-bit (or 8-bit-mode) CPU-cycle
        // IRQ counter whose latch is loaded a nibble at a time through $8000-$B000.
        class vrc3_mapper final : public nes_mapper {
          public:
            vrc3_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                        std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                        bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram) {}

            void reset() override {
                prg_bank_ = 0U;
                irq_latch_ = 0U;
                irq_counter_ = 0U;
                irq_enabled_ = false;
                irq_ack_enable_ = false;
                irq_8bit_ = false;
                if (prg_16k_count() != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_bank));
                    bus_->map_rom(0xC000U,
                                  prg_.subspan((prg_16k_count() - 1U) * k_prg_bank, k_prg_bank));
                }
                attach_chr(); // 8 KiB CHR-RAM
                apply_prg();
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                switch (addr & 0xF000U) {
                case 0x8000U: // IRQ latch nibbles
                    irq_latch_ =
                        static_cast<std::uint16_t>((irq_latch_ & 0xFFF0U) | (value & 0x0FU));
                    break;
                case 0x9000U:
                    irq_latch_ = static_cast<std::uint16_t>((irq_latch_ & 0xFF0FU) |
                                                            ((value & 0x0FU) << 4U));
                    break;
                case 0xA000U:
                    irq_latch_ = static_cast<std::uint16_t>((irq_latch_ & 0xF0FFU) |
                                                            ((value & 0x0FU) << 8U));
                    break;
                case 0xB000U:
                    irq_latch_ = static_cast<std::uint16_t>((irq_latch_ & 0x0FFFU) |
                                                            ((value & 0x0FU) << 12U));
                    break;
                case 0xC000U: // IRQ control
                    irq_8bit_ = (value & 0x04U) != 0U;
                    irq_ack_enable_ = (value & 0x01U) != 0U;
                    irq_enabled_ = (value & 0x02U) != 0U;
                    if (irq_enabled_) {
                        irq_counter_ = irq_latch_;
                    }
                    raise_irq(false);
                    break;
                case 0xD000U: // IRQ acknowledge
                    raise_irq(false);
                    irq_enabled_ = irq_ack_enable_;
                    break;
                case 0xF000U: // 16 KiB PRG bank at $8000
                    prg_bank_ = static_cast<std::uint8_t>(value & 0x0FU);
                    apply_prg();
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
                    if (irq_8bit_) {
                        if ((irq_counter_ & 0x00FFU) == 0x00FFU) {
                            irq_counter_ = static_cast<std::uint16_t>((irq_counter_ & 0xFF00U) |
                                                                      (irq_latch_ & 0x00FFU));
                            raise_irq(true);
                        } else {
                            ++irq_counter_;
                        }
                    } else {
                        if (irq_counter_ == 0xFFFFU) {
                            irq_counter_ = irq_latch_;
                            raise_irq(true);
                        } else {
                            ++irq_counter_;
                        }
                    }
                }
            }

            void save_state(chips::state_writer& writer) const override {
                writer.u8(prg_bank_);
                writer.u16(irq_latch_);
                writer.u16(irq_counter_);
                writer.boolean(irq_enabled_);
                writer.boolean(irq_ack_enable_);
                writer.boolean(irq_8bit_);
            }
            void load_state(chips::state_reader& reader) override {
                prg_bank_ = reader.u8();
                irq_latch_ = reader.u16();
                irq_counter_ = reader.u16();
                irq_enabled_ = reader.boolean();
                irq_ack_enable_ = reader.boolean();
                irq_8bit_ = reader.boolean();
                apply_prg();
            }

          private:
            void apply_prg() { map_prg_16k(0x8000U, prg_bank_); }
            std::uint8_t prg_bank_{};
            std::uint16_t irq_latch_{};
            std::uint16_t irq_counter_{};
            bool irq_enabled_{};
            bool irq_ack_enable_{};
            bool irq_8bit_{};
        };

    } // namespace

    std::unique_ptr<nes_mapper> make_vrc1_mapper(nes_mapper_build_context context) {
        return std::make_unique<vrc1_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                             context.chr_is_ram);
    }

    std::unique_ptr<nes_mapper> make_vrc2_4_mapper(nes_mapper_build_context context,
                                                   int mapper_number) {
        return std::make_unique<vrc2_4_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                               context.chr_is_ram, mapper_number);
    }

    std::unique_ptr<nes_mapper> make_vrc3_mapper(nes_mapper_build_context context) {
        return std::make_unique<vrc3_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                             context.chr_is_ram);
    }

    std::unique_ptr<nes_mapper> make_vrc6_mapper(nes_mapper_build_context context, bool variant_b) {
        return std::make_unique<vrc6_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                             context.chr_is_ram, variant_b);
    }

    std::unique_ptr<nes_mapper> make_vrc7_mapper(nes_mapper_build_context context) {
        return std::make_unique<vrc7_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                             context.chr_is_ram);
    }

} // namespace mnemos::manifests::nes
