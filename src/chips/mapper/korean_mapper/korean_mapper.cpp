#include "korean_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::mapper {

    chip_metadata korean_mapper::metadata() const noexcept {
        return {
            .manufacturer = "Korean",
            .part_number = "Korean Mapper",
            .family = "Mapper",
            .klass = chip_class::mapper,
            .revision = 1U,
        };
    }

    void korean_mapper::tick(std::uint64_t /*cycles*/) {
        // No clocked behaviour; banking is purely register-driven.
    }

    void korean_mapper::reset(reset_kind /*kind*/) { slot2_page_ = power_on_page; }

    std::uint8_t korean_mapper::rom_read_page(std::uint8_t page,
                                              std::uint16_t offset) const noexcept {
        if (rom_.empty()) {
            return 0xFFU;
        }
        const auto size = static_cast<std::uint32_t>(rom_.size());
        const std::uint32_t pages = (size + (rom_page_size - 1U)) >> 14U;
        if (pages == 0U) {
            return 0xFFU;
        }
        std::uint32_t phys =
            ((static_cast<std::uint32_t>(page) % pages) << 14U) | (offset & 0x3FFFU);
        if (phys >= size) {
            phys %= size;
        }
        return rom_[phys];
    }

    std::uint8_t korean_mapper::cpu_read(std::uint16_t address) const noexcept {
        // Slot 0: $0000-$3FFF, fixed to bank 0.
        if (address < 0x4000U) {
            return rom_read_page(0U, address);
        }
        // Slot 1: $4000-$7FFF, fixed to bank 1.
        if (address < 0x8000U) {
            return rom_read_page(1U, address);
        }
        // Slot 2: $8000-$BFFF, the banked window.
        if (address < 0xC000U) {
            return rom_read_page(slot2_page_, address);
        }
        return 0xFFU;
    }

    void korean_mapper::cpu_write(std::uint16_t address, std::uint8_t value) noexcept {
        // The only register is at $A000; it selects the slot-2 page. Every other
        // address is ROM (read-only), so the write is dropped.
        if (address == bank_register) {
            slot2_page_ = value;
        }
    }

    void korean_mapper::save_state(state_writer& writer) const { writer.u8(slot2_page_); }

    void korean_mapper::load_state(state_reader& reader) { slot2_page_ = reader.u8(); }

    instrumentation::ichip_introspection& korean_mapper::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> korean_mapper::register_snapshot() noexcept {
        register_view_[0] = {"PAGE2", slot2_page_, 8U, register_value_format::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto korean_mapper_registration =
            register_factory("korean.mapper", chip_class::mapper, []() -> std::unique_ptr<ichip> {
                return std::make_unique<korean_mapper>();
            });
    } // namespace

} // namespace mnemos::chips::mapper
