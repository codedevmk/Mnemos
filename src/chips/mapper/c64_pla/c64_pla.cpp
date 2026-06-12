#include "c64_pla.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::mapper {

    chip_metadata c64_pla::metadata() const noexcept {
        return {
            .manufacturer = "MOS Technology",
            .part_number = "906114",
            .family = "PLA",
            .klass = chip_class::mapper,
            .revision = 1U,
        };
    }

    void c64_pla::tick(std::uint64_t /*cycles*/) {
        // The PLA is pure combinational logic; it has no clocked state.
    }

    void c64_pla::reset(reset_kind /*kind*/) {
        // Cold start: the CPU port reads all-ones through the DDR pull-ups and a
        // bare machine floats /GAME and /EXROM high.
        loram_ = true;
        hiram_ = true;
        charen_ = true;
        game_ = true;
        exrom_ = true;
    }

    void c64_pla::set_cpu_port(bool loram, bool hiram, bool charen) noexcept {
        loram_ = loram;
        hiram_ = hiram;
        charen_ = charen;
    }

    void c64_pla::set_cart_lines(bool game, bool exrom) noexcept {
        game_ = game;
        exrom_ = exrom;
    }

    // Ultimax mode is /GAME=0, /EXROM=1: the cartridge ignores the CPU port and
    // takes over most of the address space.
    bool c64_pla::ultimax() const noexcept { return !game_ && exrom_; }

    c64_pla::region c64_pla::decode_a000_bfff() const noexcept {
        // BASIC is visible only when LORAM and HIRAM are both high.
        return (loram_ && hiram_) ? region::basic : region::ram;
    }

    c64_pla::region c64_pla::decode_d000_dfff() const noexcept {
        if (!loram_ && !hiram_) {
            return region::ram;
        }
        return charen_ ? region::io : region::chargen;
    }

    c64_pla::region c64_pla::decode_e000_ffff() const noexcept {
        return hiram_ ? region::kernal : region::ram;
    }

    c64_pla::region c64_pla::decode_cpu_address(std::uint16_t address) const noexcept {
        if (ultimax()) {
            // The cart owns most of the space; $C000-$CFFF is left open to match
            // the documented open-bus / ultimax convention (see THIRD-PARTY-REFERENCES.md).
            if (address < 0x1000U) {
                return region::ram;
            }
            if (address < 0x8000U) {
                return region::open;
            }
            if (address < 0xA000U) {
                return region::roml;
            }
            if (address < 0xD000U) {
                return region::open;
            }
            if (address < 0xE000U) {
                return region::io;
            }
            return region::romh;
        }

        // 8 KB cart (/GAME=1, /EXROM=0): ROML at $8000-$9FFF when LORAM & HIRAM.
        if (game_ && !exrom_) {
            if (address >= 0x8000U && address < 0xA000U && loram_ && hiram_) {
                return region::roml;
            }
        }

        // 16 KB cart (/GAME=0, /EXROM=0): adds ROMH at $A000-$BFFF when HIRAM.
        if (!game_ && !exrom_) {
            if (address >= 0x8000U && address < 0xA000U && loram_ && hiram_) {
                return region::roml;
            }
            if (address >= 0xA000U && address < 0xC000U && hiram_) {
                return region::romh;
            }
        }

        // Non-cart (or cart lines high) paths.
        if (address < 0xA000U) {
            return region::ram;
        }
        if (address < 0xC000U) {
            return decode_a000_bfff();
        }
        if (address < 0xD000U) {
            return region::ram;
        }
        if (address < 0xE000U) {
            return decode_d000_dfff();
        }
        return decode_e000_ffff();
    }

    c64_pla::region c64_pla::decode_vic_address(std::uint16_t address) const noexcept {
        // Ultimax carts intercept VIC fetches at $3000-$3FFF via ROMH.
        if (ultimax() && address >= 0x3000U && address < 0x4000U) {
            return region::romh;
        }

        // CHARGEN appears at offset $1000-$1FFF within each 16 KB VIC bank; the
        // mask collapses the two CPU-view positions ($1xxx and $9xxx) to one.
        const auto low = static_cast<std::uint16_t>(address & 0x3FFFU);
        if (low >= 0x1000U && low < 0x2000U) {
            return region::chargen;
        }
        return region::ram;
    }

    void c64_pla::save_state(state_writer& writer) const {
        writer.boolean(loram_);
        writer.boolean(hiram_);
        writer.boolean(charen_);
        writer.boolean(game_);
        writer.boolean(exrom_);
    }

    void c64_pla::load_state(state_reader& reader) {
        loram_ = reader.boolean();
        hiram_ = reader.boolean();
        charen_ = reader.boolean();
        game_ = reader.boolean();
        exrom_ = reader.boolean();
    }

    instrumentation::ichip_introspection& c64_pla::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> c64_pla::register_snapshot() noexcept {
        // One packed view of the five logic inputs:
        // bit0 LORAM, bit1 HIRAM, bit2 CHAREN, bit3 GAME, bit4 EXROM.
        const auto config = static_cast<std::uint64_t>(
            (loram_ ? 0x01U : 0U) | (hiram_ ? 0x02U : 0U) | (charen_ ? 0x04U : 0U) |
            (game_ ? 0x08U : 0U) | (exrom_ ? 0x10U : 0U));
        register_view_[0] = {"CONFIG", config, 5U, register_value_format::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto c64_pla_registration =
            register_factory("mos.906114", chip_class::mapper, []() -> std::unique_ptr<ichip> {
                return std::make_unique<c64_pla>();
            });
    } // namespace

} // namespace mnemos::chips::mapper
