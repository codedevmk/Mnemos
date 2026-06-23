#include "nes_mapper_sunsoft.hpp"

#include "nes_mapper_helpers.hpp"
#include "ssg.hpp" // chips::audio::ssg (the Sunsoft 5B's YM2149 sound block)

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace mnemos::manifests::nes {

    using detail::apply_mirror_mode;
    using detail::chr_window;
    using detail::k_chr_2k;
    using detail::k_prg_8k;
    using detail::k_prg_bank;

    namespace {
        // Sunsoft-3 (iNES 67): four 2 KiB CHR banks, a 16 KiB switchable PRG bank
        // (the last 16 KiB fixed), four mirroring modes, and a 16-bit down-counter
        // IRQ clocked on the M2 (CPU) clock. The counter is loaded high-byte-then-
        // low-byte through one port ($C800) gated by a write toggle that $D800 also
        // resets; when the count wraps past zero ($0000 -> $FFFF) the mapper asserts
        // /IRQ and pauses itself, and a write to $8000 acknowledges it. Games:
        // Fantasy Zone 2, Mito Koumon 2. (Completes the Sunsoft family alongside the
        // Sunsoft-4 (68) and 5B (69).)
        class sunsoft3_mapper final : public nes_mapper {
          public:
            sunsoft3_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                            std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                            bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_win_(chr, chr_is_ram) {}

            void reset() override {
                chr_bank_.fill(0U);
                prg_bank_ = 0U;
                mirror_ = 0U;
                irq_counter_ = 0U;
                irq_counting_ = false;
                irq_write_hi_ = true;
                if (prg_16k_count() != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_bank));
                    bus_->map_rom(0xC000U,
                                  prg_.subspan((prg_16k_count() - 1U) * k_prg_bank, k_prg_bank));
                }
                chr_win_.attach(*ppu_);
                apply_prg();
                apply_chr();
                apply_mirror_mode(*ppu_, mirror_);
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                switch (addr & 0xF800U) {
                case 0x8000U: // $8000-$87FF: acknowledge a pending IRQ
                    raise_irq(false);
                    break;
                case 0x8800U:
                    chr_bank_[0] = value;
                    apply_chr();
                    break;
                case 0x9800U:
                    chr_bank_[1] = value;
                    apply_chr();
                    break;
                case 0xA800U:
                    chr_bank_[2] = value;
                    apply_chr();
                    break;
                case 0xB800U:
                    chr_bank_[3] = value;
                    apply_chr();
                    break;
                case 0xC800U: // IRQ load: high byte then low byte, gated by the toggle
                    if (irq_write_hi_) {
                        irq_counter_ = static_cast<std::uint16_t>(
                            (irq_counter_ & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U));
                    } else {
                        irq_counter_ = static_cast<std::uint16_t>((irq_counter_ & 0xFF00U) | value);
                    }
                    irq_write_hi_ = !irq_write_hi_;
                    break;
                case 0xD800U: // IRQ control: bit 4 enables counting; resets the load toggle
                    irq_counting_ = (value & 0x10U) != 0U;
                    irq_write_hi_ = true;
                    break;
                case 0xE800U:
                    mirror_ = value & 0x03U;
                    apply_mirror_mode(*ppu_, mirror_);
                    break;
                case 0xF800U:
                    prg_bank_ = value & 0x0FU; // 16 KiB bank at $8000 (lower 4 bits)
                    apply_prg();
                    break;
                default:
                    break;
                }
            }

            // The 16-bit counter decrements on every M2 cycle while counting; on the
            // $0000 -> $FFFF underflow it asserts /IRQ and pauses. The board clocks
            // this ungated (it is not A12-driven).
            void clock_cpu_timer(std::uint32_t cpu_cycles) override {
                if (!irq_counting_) {
                    return;
                }
                if (irq_counter_ < cpu_cycles) { // passes through zero -> underflow
                    raise_irq(true);
                    irq_counting_ = false; // the mapper pauses itself on the IRQ
                }
                irq_counter_ = static_cast<std::uint16_t>(irq_counter_ - cpu_cycles);
            }

            void save_state(chips::state_writer& writer) const override {
                for (const std::uint8_t b : chr_bank_) {
                    writer.u8(b);
                }
                writer.u8(prg_bank_);
                writer.u8(mirror_);
                writer.u32(irq_counter_);
                writer.boolean(irq_counting_);
                writer.boolean(irq_write_hi_);
            }
            void load_state(chips::state_reader& reader) override {
                for (std::uint8_t& b : chr_bank_) {
                    b = reader.u8();
                }
                prg_bank_ = reader.u8();
                mirror_ = reader.u8();
                irq_counter_ = static_cast<std::uint16_t>(reader.u32());
                irq_counting_ = reader.boolean();
                irq_write_hi_ = reader.boolean();
                apply_prg();
                apply_chr();
                apply_mirror_mode(*ppu_, mirror_);
            }

          private:
            void apply_prg() { map_prg_16k(0x8000U, prg_bank_); }

            void apply_chr() {
                for (std::size_t s = 0; s < 4U; ++s) {
                    chr_win_.set_2k(s, chr_bank_[s]);
                }
            }

            std::array<std::uint8_t, 4> chr_bank_{};
            std::uint8_t prg_bank_{};
            std::uint8_t mirror_{};
            std::uint16_t irq_counter_{};
            bool irq_counting_{};
            bool irq_write_hi_{true};
            chr_window chr_win_;
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
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_win_(chr, chr_is_ram) {}

            void reset() override {
                command_ = 0U;
                chr_banks_.fill(0U);
                prg_banks_ = {0U, 0U, 0U, 0U};
                mirror_ = 0U;
                irq_counter_ = 0U;
                irq_enabled_ = false;
                irq_counter_enabled_ = false;

                if (prg_8k_count() != 0U) {
                    for (const std::uint32_t slot : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
                        bus_->map_rom(slot, prg_.subspan(0, k_prg_8k));
                    }
                }
                chr_win_.attach(*ppu_);

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

            void apply_prg() {
                const std::size_t count = prg_8k_count();
                if (count == 0U) {
                    return;
                }
                map_prg_8k(0x8000U, prg_banks_[1]);
                map_prg_8k(0xA000U, prg_banks_[2]);
                map_prg_8k(0xC000U, prg_banks_[3]);
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

            chips::audio::ssg ssg_;
            std::uint8_t command_{};
            std::array<std::uint8_t, 8> chr_banks_{};
            std::array<std::uint8_t, 4> prg_banks_{}; // [0]=$6000 [1]=$8000 [2]=$A000 [3]=$C000
            std::uint8_t mirror_{};
            std::uint16_t irq_counter_{};
            bool irq_enabled_{};
            bool irq_counter_enabled_{};
            chr_window chr_win_;
        };

        // Sunsoft-4 (iNES 68). PRG: a switchable 16 KiB bank at $8000 ($F000 low 4
        // bits) over the fixed last 16 KiB at $C000. CHR: four switchable 2 KiB banks
        // ($8000/$9000/$A000/$B000) composed into the 8 KiB window. $E000 low 2 bits
        // set the nametable arrangement; $E000 bit 4 enables CHR-ROM-backed
        // nametables, with $C000/$D000 selecting their two 1 KiB pages.
        //
        // inc1 SIMPLIFICATION: the PPU is CIRAM-only, so the CHR-ROM-backed-nametable
        // mode is approximated to standard CIRAM mirroring driven by the $E000 low 2
        // bits (the $C000/$D000 page registers are stored + serialised but not yet
        // sampled). True CHR-ROM nametables -- which one title uses for a pseudo-3D
        // scrolling ground -- are a deferred inc2 needing a PPU change; most of the
        // game still renders and boots without them.
        class sunsoft4_mapper final : public nes_mapper {
          public:
            sunsoft4_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                            std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                            bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_win_(chr, chr_is_ram) {}

            void reset() override {
                prg_bank_ = 0U;
                chr_banks_.fill(0U);
                nt_banks_ = {0U, 0U};
                mirror_ = 0U;
                nt_from_chr_ = false;

                if (prg_16k_count() != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_bank));
                    bus_->map_rom(0xC000U,
                                  prg_.subspan((prg_16k_count() - 1U) * k_prg_bank, k_prg_bank));
                }
                chr_win_.attach(*ppu_);

                apply_prg();
                apply_chr();
                apply_mirroring();
                install_register_write_hook();
            }

            void write(std::uint16_t addr, std::uint8_t value) override {
                switch (addr & 0xF000U) {
                case 0x8000U: // CHR 2 KiB bank at $0000
                case 0x9000U: // CHR 2 KiB bank at $0800
                case 0xA000U: // CHR 2 KiB bank at $1000
                case 0xB000U: // CHR 2 KiB bank at $1800
                    chr_banks_[(addr - 0x8000U) >> 12U] = value;
                    apply_chr();
                    break;
                case 0xC000U: // nametable-0 CHR-ROM page (stored; CIRAM in this inc)
                    nt_banks_[0] = value;
                    break;
                case 0xD000U: // nametable-1 CHR-ROM page (stored; CIRAM in this inc)
                    nt_banks_[1] = value;
                    break;
                case 0xE000U: // mirroring (bits 0-1) + CHR-ROM-nametable enable (bit 4)
                    mirror_ = static_cast<std::uint8_t>(value & 0x03U);
                    nt_from_chr_ = (value & 0x10U) != 0U;
                    apply_mirroring();
                    break;
                case 0xF000U: // 16 KiB PRG bank at $8000 (low 4 bits)
                    prg_bank_ = static_cast<std::uint8_t>(value & 0x0FU);
                    apply_prg();
                    break;
                default:
                    break;
                }
            }

            void save_state(chips::state_writer& writer) const override {
                writer.u8(prg_bank_);
                for (const std::uint8_t b : chr_banks_) {
                    writer.u8(b);
                }
                for (const std::uint8_t b : nt_banks_) {
                    writer.u8(b);
                }
                writer.u8(mirror_);
                writer.boolean(nt_from_chr_);
            }
            void load_state(chips::state_reader& reader) override {
                prg_bank_ = reader.u8();
                for (std::uint8_t& b : chr_banks_) {
                    b = reader.u8();
                }
                for (std::uint8_t& b : nt_banks_) {
                    b = reader.u8();
                }
                mirror_ = reader.u8();
                nt_from_chr_ = reader.boolean();
                apply_prg();
                apply_chr();
                apply_mirroring();
            }

          private:
            void apply_prg() { map_prg_16k(0x8000U, prg_bank_); }

            void apply_chr() {
                for (std::size_t s = 0; s < 4U; ++s) {
                    chr_win_.set_2k(s, chr_banks_[s]);
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

            std::uint8_t prg_bank_{};
            std::array<std::uint8_t, 4> chr_banks_{}; // $8000/$9000/$A000/$B000 2 KiB selects
            std::array<std::uint8_t, 2> nt_banks_{};  // $C000/$D000 CHR-ROM nametable pages
            std::uint8_t mirror_{};
            bool nt_from_chr_{}; // $E000 bit 4: CHR-ROM-backed nametables (inc2)
            chr_window chr_win_;
        };

    } // namespace

    std::unique_ptr<nes_mapper> make_sunsoft3_mapper(nes_mapper_build_context context) {
        return std::make_unique<sunsoft3_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                                 context.chr_is_ram);
    }

    std::unique_ptr<nes_mapper> make_sunsoft4_mapper(nes_mapper_build_context context) {
        return std::make_unique<sunsoft4_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                                 context.chr_is_ram);
    }

    std::unique_ptr<nes_mapper> make_sunsoft5b_mapper(nes_mapper_build_context context) {
        return std::make_unique<sunsoft5b_mapper>(context.bus, context.ppu, context.prg,
                                                  context.chr, context.chr_is_ram);
    }

} // namespace mnemos::manifests::nes
