#include "multi_4x8k_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::mapper {

    chip_metadata multi_4x8k_mapper::metadata() const noexcept {
        return {
            .manufacturer = "Korean",
            .part_number = "Multi 4x8K Mapper",
            .family = "Mapper",
            .klass = chip_class::mapper,
            .revision = 1U,
        };
    }

    void multi_4x8k_mapper::tick(std::uint64_t /*cycles*/) {
        // No clocked behaviour; banking is purely register-driven.
    }

    void multi_4x8k_mapper::reset(reset_kind /*kind*/) { slot_.fill(0U); }

    std::uint8_t multi_4x8k_mapper::rom_read_page(std::uint8_t page,
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

    std::uint8_t multi_4x8k_mapper::cpu_read(std::uint16_t address) const noexcept {
        // $0000-$1FFF / $2000-$3FFF: fixed ROM banks 0 / 1 (the first 16 KiB).
        if (address < 0x2000U) {
            return rom_read_page(0U, address);
        }
        if (address < 0x4000U) {
            return rom_read_page(1U, address);
        }
        // The four banked 8 KiB windows, in register order 2/3/0/1.
        if (address < 0x6000U) {
            return rom_read_page(slot_[2], address);
        }
        if (address < 0x8000U) {
            return rom_read_page(slot_[3], address);
        }
        if (address < 0xA000U) {
            return rom_read_page(slot_[0], address);
        }
        if (address < 0xC000U) {
            return rom_read_page(slot_[1], address);
        }
        return 0xFFU;
    }

    void multi_4x8k_mapper::cpu_write(std::uint16_t address, std::uint8_t value) noexcept {
        // A write to $2000 banks all four windows from the value XORed with a
        // fixed per-window mask; every other address is ROM (read-only).
        if (address == bank_register) {
            slot_[2] = static_cast<std::uint8_t>(value ^ 0x1FU); // $4000-$5FFF
            slot_[3] = static_cast<std::uint8_t>(value ^ 0x1EU); // $6000-$7FFF
            slot_[0] = static_cast<std::uint8_t>(value ^ 0x1DU); // $8000-$9FFF
            slot_[1] = static_cast<std::uint8_t>(value ^ 0x1CU); // $A000-$BFFF
        }
    }

    void multi_4x8k_mapper::save_state(state_writer& writer) const {
        for (const std::uint8_t reg : slot_) {
            writer.u8(reg);
        }
    }

    void multi_4x8k_mapper::load_state(state_reader& reader) {
        for (std::uint8_t& reg : slot_) {
            reg = reader.u8();
        }
    }

    instrumentation::ichip_introspection& multi_4x8k_mapper::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> multi_4x8k_mapper::register_snapshot() noexcept {
        register_view_[0] = {"BANK8000", slot_[0], 8U, register_value_format::unsigned_integer};
        register_view_[1] = {"BANKA000", slot_[1], 8U, register_value_format::unsigned_integer};
        register_view_[2] = {"BANK4000", slot_[2], 8U, register_value_format::unsigned_integer};
        register_view_[3] = {"BANK6000", slot_[3], 8U, register_value_format::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto multi_4x8k_mapper_registration = register_factory(
            "korean.multi_4x8k_mapper", chip_class::mapper,
            []() -> std::unique_ptr<ichip> { return std::make_unique<multi_4x8k_mapper>(); });
    } // namespace

} // namespace mnemos::chips::mapper
