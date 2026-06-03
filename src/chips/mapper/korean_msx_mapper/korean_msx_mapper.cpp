#include "korean_msx_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::mapper {

    chip_metadata korean_msx_mapper::metadata() const noexcept {
        return {
            .manufacturer = "Korean",
            .part_number = "Korean MSX Mapper",
            .family = "Mapper",
            .klass = chip_class::mapper,
            .revision = 1U,
        };
    }

    void korean_msx_mapper::tick(std::uint64_t /*cycles*/) {
        // No clocked behaviour; banking is purely register-driven.
    }

    void korean_msx_mapper::reset(reset_kind /*kind*/) { slot_.fill(0U); }

    std::uint8_t korean_msx_mapper::rom_read_page(std::uint8_t page,
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

    std::uint8_t korean_msx_mapper::cpu_read(std::uint16_t address) const noexcept {
        // $0000-$1FFF: bank 0 (msx) or the last 8 KiB bank (nemesis boot region).
        if (address < 0x2000U) {
            if (variant_ == variant::nemesis) {
                if (rom_.size() < static_cast<std::size_t>(rom_bank_size)) {
                    return 0xFFU;
                }
                const std::size_t phys = rom_.size() - rom_bank_size + address;
                return phys < rom_.size() ? rom_[phys] : 0xFFU;
            }
            return rom_read_page(0U, address);
        }
        // $2000-$3FFF: the second 8 KiB half of fixed bank 0 (both variants).
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

    void korean_msx_mapper::cpu_write(std::uint16_t address, std::uint8_t value) noexcept {
        // Page registers at $0000-$0003; every other address is ROM (read-only).
        if (address < static_cast<std::uint16_t>(register_count)) {
            slot_[address] = value;
        }
    }

    void korean_msx_mapper::save_state(state_writer& writer) const {
        for (const std::uint8_t reg : slot_) {
            writer.u8(reg);
        }
    }

    void korean_msx_mapper::load_state(state_reader& reader) {
        for (std::uint8_t& reg : slot_) {
            reg = reader.u8();
        }
    }

    instrumentation::ichip_introspection& korean_msx_mapper::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> korean_msx_mapper::register_snapshot() noexcept {
        register_view_[0] = {"PAGE8000", slot_[0], 8U, register_value_format::unsigned_integer};
        register_view_[1] = {"PAGEA000", slot_[1], 8U, register_value_format::unsigned_integer};
        register_view_[2] = {"PAGE4000", slot_[2], 8U, register_value_format::unsigned_integer};
        register_view_[3] = {"PAGE6000", slot_[3], 8U, register_value_format::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto korean_msx_mapper_registration = register_factory(
            "korean.msx_mapper", chip_class::mapper,
            []() -> std::unique_ptr<ichip> { return std::make_unique<korean_msx_mapper>(); });
    } // namespace

} // namespace mnemos::chips::mapper
