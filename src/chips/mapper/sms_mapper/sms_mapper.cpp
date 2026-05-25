#include "sms_mapper.hpp"

#include "chip_registry.hpp"
#include "state.hpp"

#include <memory>

namespace mnemos::chips::mapper {

    chip_metadata sms_mapper::metadata() const noexcept {
        return {
            .manufacturer = "Sega",
            .part_number = "Sega Mapper",
            .family = "Mapper",
            .klass = chip_class::mapper,
            .revision = 1U,
        };
    }

    void sms_mapper::tick(std::uint64_t /*cycles*/) {
        // The mapper has no clocked behaviour; banking is purely register-driven.
    }

    void sms_mapper::reset(reset_kind /*kind*/) {
        // Power-on / reset leaves the slots paged to the first three ROM pages with
        // cart RAM disabled, matching the post-BIOS state real carts see on entry.
        control_ = 0U;
        page_ = {0U, 1U, 2U};
        cart_ram_.fill(0U);
    }

    std::uint8_t sms_mapper::rom_read_page(std::uint8_t page, std::uint16_t offset) const noexcept {
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

    std::size_t sms_mapper::cart_ram_offset(std::uint16_t address) const noexcept {
        const int bank = (control_ & ram_bank_bit) != 0U ? 1 : 0;
        return static_cast<std::size_t>(bank * rom_page_size) |
               static_cast<std::size_t>(address & 0x3FFFU);
    }

    std::uint8_t sms_mapper::cpu_read(std::uint16_t address) const noexcept {
        // Slot 0: $0000-$3FFF. The first 1 KiB is always physical page 0, unbanked,
        // so the reset vectors and the mapper-init stub stay reachable while the
        // rest of the slot pages through register $FFFD.
        if (address < 0x4000U) {
            if (address < 0x0400U) {
                return address < rom_.size() ? rom_[address] : 0xFFU;
            }
            return rom_read_page(page_[0], address);
        }

        // Slot 1: $4000-$7FFF, paged through register $FFFE.
        if (address < 0x8000U) {
            return rom_read_page(page_[1], address);
        }

        // Slot 2: $8000-$BFFF, either the cart RAM window or ROM page $FFFF.
        if (cart_ram_enabled()) {
            return cart_ram_[cart_ram_offset(address)];
        }
        return rom_read_page(page_[2], address);
    }

    void sms_mapper::cpu_write(std::uint16_t address, std::uint8_t value) noexcept {
        // Only the slot-2 cart RAM window is writable; ROM ignores writes.
        if (address >= 0x8000U && address < 0xC000U && cart_ram_enabled()) {
            cart_ram_[cart_ram_offset(address)] = value;
        }
    }

    void sms_mapper::write_register(std::uint16_t address, std::uint8_t value) noexcept {
        switch (address) {
        case 0xFFFCU:
            control_ = value;
            break;
        case 0xFFFDU:
            page_[0] = value;
            break;
        case 0xFFFEU:
            page_[1] = value;
            break;
        case 0xFFFFU:
            page_[2] = value;
            break;
        default:
            break;
        }
    }

    void sms_mapper::save_state(state_writer& writer) const {
        writer.u8(control_);
        writer.u8(page_[0]);
        writer.u8(page_[1]);
        writer.u8(page_[2]);
        writer.bytes(cart_ram_);
    }

    void sms_mapper::load_state(state_reader& reader) {
        control_ = reader.u8();
        page_[0] = reader.u8();
        page_[1] = reader.u8();
        page_[2] = reader.u8();
        reader.bytes(cart_ram_);
    }

    instrumentation::ichip_introspection& sms_mapper::introspection() noexcept {
        return introspection_;
    }

    std::span<const register_descriptor> sms_mapper::register_snapshot() noexcept {
        register_view_[0] = {"CTRL", control_, 8U, register_value_format::flags};
        register_view_[1] = {"PAGE0", page_[0], 8U, register_value_format::unsigned_integer};
        register_view_[2] = {"PAGE1", page_[1], 8U, register_value_format::unsigned_integer};
        register_view_[3] = {"PAGE2", page_[2], 8U, register_value_format::unsigned_integer};
        return register_view_;
    }

    namespace {
        [[maybe_unused]] const auto sms_mapper_registration =
            register_factory("sega.sms_mapper", chip_class::mapper, []() -> std::unique_ptr<ichip> {
                return std::make_unique<sms_mapper>();
            });
    } // namespace

} // namespace mnemos::chips::mapper
