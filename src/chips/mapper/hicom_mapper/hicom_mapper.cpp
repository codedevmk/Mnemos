#include "hicom_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::mapper {

    chip_metadata hicom_mapper::metadata() const noexcept {
        return {
            .manufacturer = "HiCom",
            .part_number = "HiCom 188-in-1 Mapper",
            .family = "Mapper",
            .klass = chip_class::mapper,
            .revision = 1U,
        };
    }

    void hicom_mapper::tick(std::uint64_t /*cycles*/) {
        // No clocked behaviour; banking is purely register-driven.
    }

    void hicom_mapper::reset(reset_kind /*kind*/) { page_ = 0U; }

    std::uint8_t hicom_mapper::rom_read_page(std::uint8_t page,
                                             std::uint16_t offset) const noexcept {
        if (rom_.empty()) {
            return 0xFFU;
        }
        const auto size = static_cast<std::uint32_t>(rom_.size());
        const std::uint32_t pages = (size + (rom_page_size - 1U)) >> 15U; // 32 KiB pages
        if (pages == 0U) {
            return 0xFFU;
        }
        std::uint32_t phys =
            ((static_cast<std::uint32_t>(page) % pages) << 15U) | (offset & 0x7FFFU);
        if (phys >= size) {
            phys %= size;
        }
        return rom_[phys];
    }

    std::uint8_t hicom_mapper::cpu_read(std::uint16_t address) const noexcept {
        // $0000-$7FFF: the selected 32 KiB page.
        if (address < 0x8000U) {
            return rom_read_page(page_, address);
        }
        // $8000-$BFFF: mirror of the page's lower 16 KiB ($0000-$3FFF).
        if (address < 0xC000U) {
            return rom_read_page(page_, static_cast<std::uint16_t>(address & 0x3FFFU));
        }
        return 0xFFU;
    }

    void hicom_mapper::cpu_write(std::uint16_t address, std::uint8_t value) noexcept {
        // The page register is at $FFFF; every other address is ROM (read-only).
        if (address == bank_register) {
            page_ = value;
        }
    }

    void hicom_mapper::save_state(state_writer& writer) const { writer.u8(page_); }

    void hicom_mapper::load_state(state_reader& reader) { page_ = reader.u8(); }

    instrumentation::ichip_introspection& hicom_mapper::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> hicom_mapper::register_snapshot() noexcept {
        register_view_[0] = {"PAGE", page_, 8U, register_value_format::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto hicom_mapper_registration = register_factory(
            "korean.hicom_mapper", chip_class::mapper,
            []() -> std::unique_ptr<ichip> { return std::make_unique<hicom_mapper>(); });
    } // namespace

} // namespace mnemos::chips::mapper
