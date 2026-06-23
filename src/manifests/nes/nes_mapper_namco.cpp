#include "nes_mapper_namco.hpp"

#include "n163.hpp" // chips::audio::n163 (the Namco 163's wavetable sound block)
#include "nes_mapper_helpers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace mnemos::manifests::nes {

    using detail::chr_window;
    using detail::k_prg_8k;

    namespace {
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

    std::unique_ptr<nes_mapper> make_namco163_mapper(nes_mapper_build_context context) {
        return std::make_unique<namco163_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                                 context.chr_is_ram);
    }

} // namespace mnemos::manifests::nes