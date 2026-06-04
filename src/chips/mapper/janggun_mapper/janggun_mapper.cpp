#include "janggun_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::mapper {

    chip_metadata janggun_mapper::metadata() const noexcept {
        return {
            .manufacturer = "Korean",
            .part_number = "Janggun Mapper",
            .family = "Mapper",
            .klass = chip_class::mapper,
            .revision = 1U,
        };
    }

    void janggun_mapper::tick(std::uint64_t /*cycles*/) {
        // No clocked behaviour; banking is purely register-driven.
    }

    void janggun_mapper::reset(reset_kind /*kind*/) { fcr_.fill(0U); }

    std::uint8_t janggun_mapper::reverse_bits(std::uint8_t value) noexcept {
        return static_cast<std::uint8_t>(((value >> 7) & 0x01U) | ((value >> 5) & 0x02U) |
                                         ((value >> 3) & 0x04U) | ((value >> 1) & 0x08U) |
                                         ((value << 1) & 0x10U) | ((value << 3) & 0x20U) |
                                         ((value << 5) & 0x40U) | ((value << 7) & 0x80U));
    }

    std::uint8_t janggun_mapper::rom_read_page(std::uint8_t page,
                                               std::uint16_t offset) const noexcept {
        if (rom_.empty()) {
            return 0xFFU;
        }
        const auto size = static_cast<std::uint32_t>(rom_.size());
        const std::uint32_t pages = (size + (rom_bank_size - 1U)) >> 13U; // 8 KiB pages
        if (pages == 0U) {
            return 0xFFU;
        }
        std::uint32_t phys =
            ((static_cast<std::uint32_t>(page) % pages) << 13U) | (offset & 0x1FFFU);
        if (phys >= size) {
            phys %= size;
        }
        return rom_[phys];
    }

    std::uint8_t janggun_mapper::cpu_read(std::uint16_t address) const noexcept {
        // $0000-$1FFF / $2000-$3FFF: fixed ROM banks 0 / 1 (never reversed).
        if (address < 0x2000U) {
            return rom_read_page(0U, address);
        }
        if (address < 0x4000U) {
            return rom_read_page(1U, address);
        }
        // $4000-$7FFF (16 KiB page): FCR2 / FCR3 banks, reversed when FCR2 bit 7.
        if (address < 0x8000U) {
            const std::uint8_t bank = (address < 0x6000U) ? fcr_[2] : fcr_[3];
            const std::uint8_t data = rom_read_page(bank, address);
            return (fcr_[2] & reverse_flag) != 0U ? reverse_bits(data) : data;
        }
        // $8000-$BFFF (16 KiB page): FCR0 / FCR1 banks, reversed when FCR0 bit 7.
        if (address < 0xC000U) {
            const std::uint8_t bank = (address < 0xA000U) ? fcr_[0] : fcr_[1];
            const std::uint8_t data = rom_read_page(bank, address);
            return (fcr_[0] & reverse_flag) != 0U ? reverse_bits(data) : data;
        }
        return 0xFFU;
    }

    void janggun_mapper::cpu_write(std::uint16_t address, std::uint8_t value) noexcept {
        switch (address) {
        case reg_bank_4000:
            fcr_[2] = value;
            break;
        case reg_bank_6000:
            fcr_[3] = value;
            break;
        case reg_bank_8000:
            fcr_[0] = value;
            break;
        case reg_bank_a000:
            fcr_[1] = value;
            break;
        case reg_pair_lower: // $FFFE: 16 KiB pair n -> FCR2=2n, FCR3=2n+1
            fcr_[2] = static_cast<std::uint8_t>(value << 1U);
            fcr_[3] = static_cast<std::uint8_t>((value << 1U) + 1U);
            break;
        case reg_pair_upper: // $FFFF: 16 KiB pair n -> FCR0=2n, FCR1=2n+1
            fcr_[0] = static_cast<std::uint8_t>(value << 1U);
            fcr_[1] = static_cast<std::uint8_t>((value << 1U) + 1U);
            break;
        default:
            break; // ROM / RAM, not a register: dropped
        }
    }

    void janggun_mapper::save_state(state_writer& writer) const {
        for (const std::uint8_t reg : fcr_) {
            writer.u8(reg);
        }
    }

    void janggun_mapper::load_state(state_reader& reader) {
        for (std::uint8_t& reg : fcr_) {
            reg = reader.u8();
        }
    }

    instrumentation::ichip_introspection& janggun_mapper::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> janggun_mapper::register_snapshot() noexcept {
        register_view_[0] = {"FCR8000", fcr_[0], 8U, register_value_format::unsigned_integer};
        register_view_[1] = {"FCRA000", fcr_[1], 8U, register_value_format::unsigned_integer};
        register_view_[2] = {"FCR4000", fcr_[2], 8U, register_value_format::unsigned_integer};
        register_view_[3] = {"FCR6000", fcr_[3], 8U, register_value_format::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto janggun_mapper_registration = register_factory(
            "korean.janggun_mapper", chip_class::mapper,
            []() -> std::unique_ptr<ichip> { return std::make_unique<janggun_mapper>(); });
    } // namespace

} // namespace mnemos::chips::mapper
