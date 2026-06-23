#include "nes_mapper_discrete.hpp"

#include "nes_mapper_helpers.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace mnemos::manifests::nes {

    using detail::chr_window;
    using detail::k_chr_8k;
    using detail::k_prg_32k;
    using detail::k_prg_bank;

    namespace {
        // NROM (iNES 0): no banking. A 16 KiB PRG image mirrors into both halves
        // (so $FFFC resolves); 32 KiB fills $8000-$FFFF. CHR is fixed.
        class nrom_mapper final : public nes_mapper {
          public:
            using nes_mapper::nes_mapper;

            void reset() override {
                if (!prg_.empty()) {
                    bus_->map_rom(0x8000U, prg_);
                    if (prg_.size() <= k_prg_bank) {
                        bus_->map_rom(0xC000U, prg_); // mirror the 16 KiB image
                    }
                }
                attach_chr();
            }
            void write(std::uint16_t /*addr*/, std::uint8_t /*value*/) override {} // no registers
        };

        // UxROM (iNES 2): a switchable 16 KiB PRG bank at $8000 (selected by any
        // write to $8000-$FFFF) over a fixed last bank at $C000; 8 KiB CHR-RAM.
        class uxrom_mapper final : public nes_mapper {
          public:
            uxrom_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                         std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                         bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram) {}

            void reset() override {
                attach_chr();
                if (prg_16k_count() == 0U) {
                    return; // malformed (no full 16 KiB bank)
                }
                bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_bank)); // bank 0 (switchable)
                bus_->map_rom(0xC000U,
                              prg_.subspan((prg_16k_count() - 1U) * k_prg_bank, k_prg_bank));
                install_register_write_hook();
            }

            void write(std::uint16_t /*addr*/, std::uint8_t value) override {
                bank_ = value;
                map_prg_16k(0x8000U, bank_);
            }

            void save_state(chips::state_writer& writer) const override { writer.u8(bank_); }
            void load_state(chips::state_reader& reader) override {
                bank_ = reader.u8();
                map_prg_16k(0x8000U, bank_);
            }

          private:
            std::uint8_t bank_{};
        };

        // CNROM (iNES 3): fixed PRG, 8 KiB CHR-bank switching. A write to
        // $8000-$FFFF selects which 8 KiB CHR-ROM bank the PPU sees (no PRG
        // banking, no mirroring control). The bank is a contiguous CHR subspan,
        // so it just re-points the PPU's CHR window.
        class cnrom_mapper final : public nes_mapper {
          public:
            cnrom_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                         std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                         bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_8k_count_(chr.size() / k_chr_8k) {
            }

            void reset() override {
                if (!prg_.empty()) {
                    bus_->map_rom(0x8000U, prg_);
                    if (prg_.size() <= k_prg_bank) {
                        bus_->map_rom(0xC000U, prg_); // mirror a 16 KiB image
                    }
                }
                select_chr(0U);
                install_register_write_hook();
            }

            void write(std::uint16_t /*addr*/, std::uint8_t value) override {
                chr_bank_ = value;
                select_chr(chr_bank_);
            }

            void save_state(chips::state_writer& writer) const override { writer.u8(chr_bank_); }
            void load_state(chips::state_reader& reader) override {
                chr_bank_ = reader.u8();
                select_chr(chr_bank_);
            }

          private:
            void select_chr(std::uint8_t bank) noexcept {
                if (chr_is_ram_ || chr_8k_count_ == 0U) {
                    attach_chr(); // CHR-RAM / no banks: the whole window
                    return;
                }
                const std::size_t b = bank % chr_8k_count_;
                ppu_->attach_chr(
                    std::span<const std::uint8_t>(chr_.subspan(b * k_chr_8k, k_chr_8k)));
            }
            std::size_t chr_8k_count_;
            std::uint8_t chr_bank_{};
        };

        // AxROM (iNES 7): a single switchable 32 KiB PRG bank over $8000-$FFFF +
        // single-screen mirroring select; CHR is normally 8 KiB CHR-RAM. A write
        // to $8000-$FFFF sets the PRG bank (bits 0-2) and the nametable (bit 4).
        class axrom_mapper final : public nes_mapper {
          public:
            axrom_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                         std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                         bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram) {}

            void reset() override {
                if (prg_32k_count() != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_32k)); // 32 KiB bank 0
                }
                attach_chr();
                ppu_->set_mirroring(chips::video::ppu2c02::mirroring::single_a);
                install_register_write_hook();
            }

            void write(std::uint16_t /*addr*/, std::uint8_t value) override {
                bank_value_ = value;
                apply();
            }

            void save_state(chips::state_writer& writer) const override { writer.u8(bank_value_); }
            void load_state(chips::state_reader& reader) override {
                bank_value_ = reader.u8();
                apply();
            }

          private:
            void apply() {
                map_prg_32k(0x8000U, bank_value_ & 0x07U);
                ppu_->set_mirroring((bank_value_ & 0x10U) != 0U
                                        ? chips::video::ppu2c02::mirroring::single_b
                                        : chips::video::ppu2c02::mirroring::single_a);
            }
            std::uint8_t bank_value_{};
        };

        // GxROM / GNROM (iNES 66): one $8000-$FFFF register switches both a 32 KiB
        // PRG bank (bits 5-4) and an 8 KiB CHR bank (bits 1-0). No mirroring control.
        class gxrom_mapper final : public nes_mapper {
          public:
            gxrom_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                         std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                         bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_8k_count_(chr.size() / k_chr_8k) {
            }

            void reset() override {
                if (prg_32k_count() != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_32k));
                }
                bank_ = 0U;
                apply();
                install_register_write_hook();
            }
            void write(std::uint16_t /*addr*/, std::uint8_t value) override {
                bank_ = value;
                apply();
            }
            void save_state(chips::state_writer& writer) const override { writer.u8(bank_); }
            void load_state(chips::state_reader& reader) override {
                bank_ = reader.u8();
                apply();
            }

          private:
            void apply() {
                map_prg_32k(0x8000U, (bank_ >> 4U) & 0x03U);
                if (chr_is_ram_ || chr_8k_count_ == 0U) {
                    attach_chr();
                } else {
                    const std::size_t cb = (bank_ & 0x03U) % chr_8k_count_;
                    ppu_->attach_chr(
                        std::span<const std::uint8_t>(chr_.subspan(cb * k_chr_8k, k_chr_8k)));
                }
            }
            std::size_t chr_8k_count_;
            std::uint8_t bank_{};
        };

        // Color Dreams (iNES 11): one $8000-$FFFF register switches a 32 KiB PRG bank
        // (bits 1-0) and an 8 KiB CHR bank (bits 7-4). Header mirroring.
        class color_dreams_mapper final : public nes_mapper {
          public:
            color_dreams_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                                std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                                bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram), chr_8k_count_(chr.size() / k_chr_8k) {
            }

            void reset() override {
                if (prg_32k_count() != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_32k));
                }
                bank_ = 0U;
                apply();
                install_register_write_hook();
            }
            void write(std::uint16_t /*addr*/, std::uint8_t value) override {
                bank_ = value;
                apply();
            }
            void save_state(chips::state_writer& writer) const override { writer.u8(bank_); }
            void load_state(chips::state_reader& reader) override {
                bank_ = reader.u8();
                apply();
            }

          private:
            void apply() {
                map_prg_32k(0x8000U, bank_ & 0x03U);
                if (chr_is_ram_ || chr_8k_count_ == 0U) {
                    attach_chr();
                } else {
                    const std::size_t cb = ((bank_ >> 4U) & 0x0FU) % chr_8k_count_;
                    ppu_->attach_chr(
                        std::span<const std::uint8_t>(chr_.subspan(cb * k_chr_8k, k_chr_8k)));
                }
            }
            std::size_t chr_8k_count_;
            std::uint8_t bank_{};
        };

        // Camerica / Codemasters BF909x (iNES 71): a switchable 16 KiB PRG bank at
        // $8000 over the fixed last bank at $C000 (UxROM-style) with 8 KiB CHR-RAM.
        // The bank register responds across $8000-$FFFF; the Fire Hawk variant carves
        // out $9000-$9FFF for single-screen mirroring (bit 4).
        class camerica_mapper final : public nes_mapper {
          public:
            camerica_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                            std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                            bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram) {}

            void reset() override {
                bank_ = 0U;
                if (prg_16k_count() != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_bank));
                    bus_->map_rom(0xC000U, last_bank()); // fixed last 16 KiB
                }
                attach_chr(); // 8 KiB CHR-RAM
                apply_prg();
                install_register_write_hook();
            }
            void write(std::uint16_t addr, std::uint8_t value) override {
                if (addr >= 0x9000U && addr < 0xA000U) { // Fire Hawk single-screen mirroring
                    ppu_->set_mirroring((value & 0x10U) != 0U
                                            ? chips::video::ppu2c02::mirroring::single_b
                                            : chips::video::ppu2c02::mirroring::single_a);
                } else { // PRG bank at $8000
                    bank_ = value;
                    apply_prg();
                }
            }
            void save_state(chips::state_writer& writer) const override { writer.u8(bank_); }
            void load_state(chips::state_reader& reader) override {
                bank_ = reader.u8();
                apply_prg();
            }

          private:
            [[nodiscard]] std::span<const std::uint8_t> last_bank() const noexcept {
                return prg_.subspan((prg_16k_count() - 1U) * k_prg_bank, k_prg_bank);
            }
            void apply_prg() { map_prg_16k(0x8000U, bank_); }
            std::uint8_t bank_{};
        };

        // BNROM / NINA-001 (iNES 34). Two unrelated boards share the number; the CHR
        // type disambiguates. BNROM (CHR-RAM): a write anywhere in $8000-$FFFF sets a
        // 32 KiB PRG bank. NINA-001 (CHR-ROM): registers sit in the $7FFD-$7FFF tail
        // of the $6000 RAM -- $7FFD = 32 KiB PRG bank, $7FFE / $7FFF = the two 4 KiB
        // CHR banks at $0000 / $1000.
        class bnrom_nina_mapper final : public nes_mapper {
          public:
            bnrom_nina_mapper(topology::bus& bus, chips::video::ppu2c02& ppu,
                              std::span<const std::uint8_t> prg, std::span<std::uint8_t> chr,
                              bool chr_is_ram) noexcept
                : nes_mapper(bus, ppu, prg, chr, chr_is_ram),
                  is_nina_(!chr_is_ram && chr.size() >= k_chr_8k), chr_win_(chr, chr_is_ram) {}

            void reset() override {
                prg_bank_ = 0U;
                chr_bank_ = {0U, 1U};
                if (prg_32k_count() != 0U) {
                    bus_->map_rom(0x8000U, prg_.subspan(0, k_prg_32k));
                }
                chr_win_.attach(*ppu_); // NINA: compose window; BNROM: 8 KiB CHR-RAM
                if (is_nina_) {
                    apply_chr();
                    // NINA-001 registers live in the $6000 RAM tail; capture writes to
                    // them (reads fall through to the RAM).
                    bus_->map_mmio(
                        0x7FFDU, 0x0003U, [](std::uint32_t) -> std::uint8_t { return 0xFFU; },
                        [this](std::uint32_t addr, std::uint8_t value) { write_nina(addr, value); },
                        1, [](std::uint32_t, bool is_write) { return is_write; });
                } else {
                    install_register_write_hook();
                }
            }

            void write(std::uint16_t /*addr*/, std::uint8_t value) override { // BNROM only
                prg_bank_ = value;
                apply_prg();
            }

            void save_state(chips::state_writer& writer) const override {
                writer.u8(prg_bank_);
                writer.u8(chr_bank_[0]);
                writer.u8(chr_bank_[1]);
            }
            void load_state(chips::state_reader& reader) override {
                prg_bank_ = reader.u8();
                chr_bank_[0] = reader.u8();
                chr_bank_[1] = reader.u8();
                apply_prg();
                if (is_nina_) {
                    apply_chr();
                }
            }

          private:
            void write_nina(std::uint32_t addr, std::uint8_t value) {
                switch (addr) {
                case 0x7FFDU:
                    prg_bank_ = value;
                    apply_prg();
                    break;
                case 0x7FFEU:
                    chr_bank_[0] = value;
                    apply_chr();
                    break;
                case 0x7FFFU:
                    chr_bank_[1] = value;
                    apply_chr();
                    break;
                default:
                    break;
                }
            }
            void apply_prg() { map_prg_32k(0x8000U, prg_bank_); }
            void apply_chr() {
                for (std::size_t h = 0; h < 2U; ++h) {
                    chr_win_.set_4k(h, chr_bank_[h]);
                }
            }
            bool is_nina_;
            std::uint8_t prg_bank_{};
            std::array<std::uint8_t, 2> chr_bank_{0U, 1U};
            chr_window chr_win_;
        };
    } // namespace

    std::unique_ptr<nes_mapper> make_nrom_mapper(nes_mapper_build_context context) {
        return std::make_unique<nrom_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                             context.chr_is_ram);
    }

    std::unique_ptr<nes_mapper> make_uxrom_mapper(nes_mapper_build_context context) {
        return std::make_unique<uxrom_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                              context.chr_is_ram);
    }

    std::unique_ptr<nes_mapper> make_cnrom_mapper(nes_mapper_build_context context) {
        return std::make_unique<cnrom_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                              context.chr_is_ram);
    }

    std::unique_ptr<nes_mapper> make_axrom_mapper(nes_mapper_build_context context) {
        return std::make_unique<axrom_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                              context.chr_is_ram);
    }

    std::unique_ptr<nes_mapper> make_gxrom_mapper(nes_mapper_build_context context) {
        return std::make_unique<gxrom_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                              context.chr_is_ram);
    }

    std::unique_ptr<nes_mapper> make_color_dreams_mapper(nes_mapper_build_context context) {
        return std::make_unique<color_dreams_mapper>(context.bus, context.ppu, context.prg,
                                                     context.chr, context.chr_is_ram);
    }

    std::unique_ptr<nes_mapper> make_camerica_mapper(nes_mapper_build_context context) {
        return std::make_unique<camerica_mapper>(context.bus, context.ppu, context.prg, context.chr,
                                                 context.chr_is_ram);
    }

    std::unique_ptr<nes_mapper> make_bnrom_nina_mapper(nes_mapper_build_context context) {
        return std::make_unique<bnrom_nina_mapper>(context.bus, context.ppu, context.prg,
                                                   context.chr, context.chr_is_ram);
    }

} // namespace mnemos::manifests::nes
