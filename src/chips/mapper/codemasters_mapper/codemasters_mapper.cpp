#include "codemasters_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::mapper {

    chip_metadata codemasters_mapper::metadata() const noexcept {
        return {
            .manufacturer = "Codemasters",
            .part_number = "Codemasters Mapper",
            .family = "Mapper",
            .klass = chip_class::mapper,
            .revision = 1U,
        };
    }

    void codemasters_mapper::tick(std::uint64_t /*cycles*/) {
        // No clocked behaviour; banking is purely register-driven.
    }

    void codemasters_mapper::reset(reset_kind /*kind*/) {
        page_ = {0U, 1U, 0U};
        ram_enabled_ = false;
        cart_ram_.fill(0U);
    }

    std::uint8_t codemasters_mapper::rom_read_page(std::uint8_t page,
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

    std::uint8_t codemasters_mapper::cpu_read(std::uint16_t address) const noexcept {
        // Slot 0: $0000-$3FFF (fully banked -- no fixed first 1 KiB).
        if (address < 0x4000U) {
            return rom_read_page(page_[0], address);
        }
        // Slot 1: $4000-$7FFF.
        if (address < 0x8000U) {
            return rom_read_page(page_[1], address);
        }
        // Slot 2: $8000-$BFFF, with cart RAM overlaying $A000-$BFFF when enabled.
        if (in_ram_window(address)) {
            return cart_ram_[static_cast<std::size_t>(address - ram_window_base)];
        }
        return rom_read_page(page_[2], address);
    }

    void codemasters_mapper::cpu_write(std::uint16_t address, std::uint8_t value) noexcept {
        // The page registers live at the base of each 16 KiB slot; a write there
        // selects that slot's ROM page. (Real Codemasters carts write the slot's
        // lowest address; deciding by slot keeps the whole 16 KiB window live.)
        if (address < 0x4000U) {
            page_[0] = value;
            return;
        }
        if (address < 0x8000U) {
            page_[1] = value;
            ram_enabled_ = (value & ram_enable_bit) != 0U; // bit 7 maps cart RAM into slot 2
            return;
        }
        // Slot 2 window. The cart RAM overlay at $A000-$BFFF takes the write when
        // enabled; otherwise the write selects the slot-2 ROM page.
        if (in_ram_window(address)) {
            cart_ram_[static_cast<std::size_t>(address - ram_window_base)] = value;
            return;
        }
        page_[2] = value;
    }

    void codemasters_mapper::save_state(state_writer& writer) const {
        writer.u8(page_[0]);
        writer.u8(page_[1]);
        writer.u8(page_[2]);
        writer.boolean(ram_enabled_);
        writer.bytes(cart_ram_);
    }

    void codemasters_mapper::load_state(state_reader& reader) {
        page_[0] = reader.u8();
        page_[1] = reader.u8();
        page_[2] = reader.u8();
        ram_enabled_ = reader.boolean();
        reader.bytes(cart_ram_);
    }

    instrumentation::ichip_introspection& codemasters_mapper::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> codemasters_mapper::register_snapshot() noexcept {
        register_view_[0] = {"PAGE0", page_[0], 8U, register_value_format::unsigned_integer};
        register_view_[1] = {"PAGE1", page_[1], 8U, register_value_format::unsigned_integer};
        register_view_[2] = {"PAGE2", page_[2], 8U, register_value_format::unsigned_integer};
        register_view_[3] = {"RAMEN", ram_enabled_ ? 1U : 0U, 1U, register_value_format::flags};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto codemasters_mapper_registration = register_factory(
            "codemasters.mapper", chip_class::mapper,
            []() -> std::unique_ptr<ichip> { return std::make_unique<codemasters_mapper>(); });
    } // namespace

} // namespace mnemos::chips::mapper
