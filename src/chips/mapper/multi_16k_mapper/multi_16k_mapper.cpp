#include "multi_16k_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::mapper {

    chip_metadata multi_16k_mapper::metadata() const noexcept {
        return {
            .manufacturer = "Korean",
            .part_number = "Multi 16K Mapper",
            .family = "Mapper",
            .klass = chip_class::mapper,
            .revision = 1U,
        };
    }

    void multi_16k_mapper::tick(std::uint64_t /*cycles*/) {
        // No clocked behaviour; banking is purely register-driven.
    }

    void multi_16k_mapper::reset(reset_kind /*kind*/) { slot_ = {0U, 1U, 0U}; }

    std::uint8_t multi_16k_mapper::rom_read_page(std::uint8_t page,
                                                 std::uint16_t offset) const noexcept {
        if (rom_.empty()) {
            return 0xFFU;
        }
        const auto size = static_cast<std::uint32_t>(rom_.size());
        const std::uint32_t pages = (size + (rom_page_size - 1U)) >> 14U; // 16 KiB pages
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

    std::uint8_t multi_16k_mapper::cpu_read(std::uint16_t address) const noexcept {
        // Three 16 KiB banked slots; no fixed region (the first 1 KiB banks too).
        if (address < 0x4000U) {
            return rom_read_page(slot_[0], address);
        }
        if (address < 0x8000U) {
            return rom_read_page(slot_[1], address);
        }
        if (address < 0xC000U) {
            return rom_read_page(slot_[2], address);
        }
        return 0xFFU;
    }

    void multi_16k_mapper::cpu_write(std::uint16_t address, std::uint8_t value) noexcept {
        switch (address) {
        case reg_slot0:
            slot_[0] = value;
            break;
        case reg_slot1:
            slot_[1] = value;
            break;
        case reg_slot2:
            // Slot 2's high two bits come from slot 0's register.
            slot_[2] = static_cast<std::uint8_t>((slot_[0] & slot2_high_mask) + value);
            break;
        default:
            break; // ROM, not a register: dropped
        }
    }

    void multi_16k_mapper::save_state(state_writer& writer) const {
        for (const std::uint8_t reg : slot_) {
            writer.u8(reg);
        }
    }

    void multi_16k_mapper::load_state(state_reader& reader) {
        for (std::uint8_t& reg : slot_) {
            reg = reader.u8();
        }
    }

    instrumentation::ichip_introspection& multi_16k_mapper::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> multi_16k_mapper::register_snapshot() noexcept {
        register_view_[0] = {"SLOT0", slot_[0], 8U, register_value_format::unsigned_integer};
        register_view_[1] = {"SLOT1", slot_[1], 8U, register_value_format::unsigned_integer};
        register_view_[2] = {"SLOT2", slot_[2], 8U, register_value_format::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto multi_16k_mapper_registration = register_factory(
            "korean.multi_16k_mapper", chip_class::mapper,
            []() -> std::unique_ptr<ichip> { return std::make_unique<multi_16k_mapper>(); });
    } // namespace

} // namespace mnemos::chips::mapper
