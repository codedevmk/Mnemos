#include "msx2_system.hpp"

#include "msx_cartridge_mapper.hpp"
#include "msx_io_ports.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace mnemos::manifests::msx2 {

    namespace {
        constexpr std::uint32_t k_state_version = 20U;
        constexpr std::uint8_t k_no_cartridge_slot = 0xFFU;

        [[nodiscard]] std::size_t rounded_ram_size(std::size_t requested) noexcept {
            constexpr std::size_t min_size = msx2_system::page_size * 4U;
            const std::size_t base = std::max(requested, min_size);
            return ((base + msx2_system::page_size - 1U) / msx2_system::page_size) *
                   msx2_system::page_size;
        }

        [[nodiscard]] std::size_t
        ram_mapper_segment_count(const msx2_system& sys) noexcept {
            return sys.ram.size() / msx2_system::page_size;
        }

        [[nodiscard]] std::uint8_t read_paged(std::span<const std::uint8_t> rom,
                                              std::uint32_t page_size, std::uint32_t page,
                                              std::uint32_t offset) noexcept {
            if (rom.empty() || page_size == 0U) {
                return 0xFFU;
            }
            const std::uint32_t page_count = static_cast<std::uint32_t>(
                (rom.size() + static_cast<std::size_t>(page_size) - 1U) / page_size);
            if (page_count == 0U) {
                return 0xFFU;
            }
            const std::uint64_t physical =
                static_cast<std::uint64_t>(page % page_count) * page_size + offset;
            return physical < rom.size() ? rom[static_cast<std::size_t>(physical)] : 0xFFU;
        }

        [[nodiscard]] common::msx_cartridge_mapper_kind
        mapper_kind(msx2_cartridge_mapper mapper) noexcept {
            using common::msx_cartridge_mapper_kind;
            switch (mapper) {
            case msx2_cartridge_mapper::automatic:
                return msx_cartridge_mapper_kind::automatic;
            case msx2_cartridge_mapper::ascii8:
                return msx_cartridge_mapper_kind::ascii8;
            case msx2_cartridge_mapper::ascii8_sram8:
                return msx_cartridge_mapper_kind::ascii8_sram8;
            case msx2_cartridge_mapper::ascii16:
                return msx_cartridge_mapper_kind::ascii16;
            case msx2_cartridge_mapper::ascii16_sram2:
                return msx_cartridge_mapper_kind::ascii16_sram2;
            case msx2_cartridge_mapper::generic8:
                return msx_cartridge_mapper_kind::generic8;
            case msx2_cartridge_mapper::konami:
                return msx_cartridge_mapper_kind::konami;
            case msx2_cartridge_mapper::konami_scc:
                return msx_cartridge_mapper_kind::konami_scc;
            case msx2_cartridge_mapper::korean_msx:
                return msx_cartridge_mapper_kind::korean_msx;
            case msx2_cartridge_mapper::korean_msx_nemesis:
                return msx_cartridge_mapper_kind::korean_msx_nemesis;
            case msx2_cartridge_mapper::plain:
            default:
                return msx_cartridge_mapper_kind::plain;
            }
        }

        [[nodiscard]] msx2_cartridge_mapper
        mapper_from_kind(common::msx_cartridge_mapper_kind mapper) noexcept {
            using common::msx_cartridge_mapper_kind;
            switch (mapper) {
            case msx_cartridge_mapper_kind::automatic:
                return msx2_cartridge_mapper::automatic;
            case msx_cartridge_mapper_kind::ascii8:
                return msx2_cartridge_mapper::ascii8;
            case msx_cartridge_mapper_kind::ascii8_sram8:
                return msx2_cartridge_mapper::ascii8_sram8;
            case msx_cartridge_mapper_kind::ascii16:
                return msx2_cartridge_mapper::ascii16;
            case msx_cartridge_mapper_kind::ascii16_sram2:
                return msx2_cartridge_mapper::ascii16_sram2;
            case msx_cartridge_mapper_kind::generic8:
                return msx2_cartridge_mapper::generic8;
            case msx_cartridge_mapper_kind::konami:
                return msx2_cartridge_mapper::konami;
            case msx_cartridge_mapper_kind::konami_scc:
                return msx2_cartridge_mapper::konami_scc;
            case msx_cartridge_mapper_kind::korean_msx:
                return msx2_cartridge_mapper::korean_msx;
            case msx_cartridge_mapper_kind::korean_msx_nemesis:
                return msx2_cartridge_mapper::korean_msx_nemesis;
            case msx_cartridge_mapper_kind::plain:
            default:
                return msx2_cartridge_mapper::plain;
            }
        }

        [[nodiscard]] msx2_cartridge_mapper
        resolve_cartridge_mapper(msx2_cartridge_mapper mapper,
                                 std::span<const std::uint8_t> rom) noexcept {
            return mapper_from_kind(common::resolve_msx_cartridge_mapper(mapper_kind(mapper), rom));
        }

        [[nodiscard]] std::uint16_t mirror_konami_address(std::uint16_t address) noexcept {
            return common::mirror_msx_konami_address(address);
        }

        [[nodiscard]] std::uint32_t konami_window(std::uint16_t address) noexcept {
            return static_cast<std::uint32_t>((address - 0x4000U) >> 13U);
        }

        [[nodiscard]] bool is_korean_mapper(msx2_cartridge_mapper mapper) noexcept {
            return common::msx_mapper_is_korean(mapper_kind(mapper));
        }

        [[nodiscard]] chips::mapper::korean_msx_mapper::variant
        korean_variant(msx2_cartridge_mapper mapper) noexcept {
            return mapper == msx2_cartridge_mapper::korean_msx_nemesis
                       ? chips::mapper::korean_msx_mapper::variant::nemesis
                       : chips::mapper::korean_msx_mapper::variant::msx;
        }

        [[nodiscard]] std::size_t cart_sram_size(msx2_cartridge_mapper mapper) noexcept {
            return common::msx_mapper_sram_size(mapper_kind(mapper));
        }

        [[nodiscard]] bool slot_selected(std::uint8_t slot, std::uint8_t subslot,
                                         std::uint8_t primary, std::uint8_t secondary) noexcept {
            return (slot & 0x03U) == (primary & 0x03U) && (subslot & 0x03U) == (secondary & 0x03U);
        }

        void save_mouse_ports(chips::state_writer& writer,
                              const std::array<common::msx_mouse_port, 2>& ports) {
            for (const common::msx_mouse_port& port : ports) {
                writer.boolean(port.attached());
                writer.boolean(port.pin8_high());
                writer.u8(port.phase());
                writer.u8(port.protocol_delta_x());
                writer.u8(port.protocol_delta_y());
                writer.boolean(port.left_button());
                writer.boolean(port.right_button());
            }
        }

        void load_mouse_ports(chips::state_reader& reader,
                              std::array<common::msx_mouse_port, 2>& ports) {
            for (common::msx_mouse_port& port : ports) {
                const bool attached = reader.boolean();
                const bool pin8_high = reader.boolean();
                const std::uint8_t phase = reader.u8();
                const std::uint8_t delta_x = reader.u8();
                const std::uint8_t delta_y = reader.u8();
                const bool left_button = reader.boolean();
                const bool right_button = reader.boolean();
                port.restore(attached, pin8_high, phase, delta_x, delta_y, left_button,
                             right_button);
            }
        }

    } // namespace

    void msx2_system::set_key(int row, int bit, bool pressed) noexcept {
        if (row < 0 || row >= static_cast<int>(keyboard_rows.size()) || bit < 0 || bit >= 8) {
            return;
        }
        const std::uint8_t mask = static_cast<std::uint8_t>(1U << static_cast<unsigned>(bit));
        std::uint8_t& value = keyboard_rows[static_cast<std::size_t>(row)];
        value = pressed ? static_cast<std::uint8_t>(value & ~mask)
                        : static_cast<std::uint8_t>(value | mask);
    }

    void msx2_system::set_joystick(int port, bool up, bool down, bool left, bool right,
                                   bool trigger_a, bool trigger_b) noexcept {
        if (port < 0 || port >= static_cast<int>(joystick_ports.size())) {
            return;
        }

        joystick_ports[static_cast<std::size_t>(port)] =
            common::msx_joystick_port_bits(up, down, left, right, trigger_a, trigger_b);
        mouse_ports[static_cast<std::size_t>(port)].detach();
    }

    void msx2_system::set_mouse(int port, std::int16_t delta_x, std::int16_t delta_y,
                                bool left_button, bool right_button) noexcept {
        if (port < 0 || port >= static_cast<int>(mouse_ports.size())) {
            return;
        }

        mouse_ports[static_cast<std::size_t>(port)].attach_protocol_delta(
            delta_x, delta_y, left_button, right_button);
        sync_psg_outputs();
    }

    std::uint8_t msx2_system::slot_for_page(std::uint16_t address) const noexcept {
        const unsigned page = static_cast<unsigned>(address >> 14U);
        return static_cast<std::uint8_t>((primary_slot >> (page * 2U)) & 0x03U);
    }

    std::uint8_t msx2_system::secondary_for_page(std::uint8_t slot,
                                                 std::uint16_t address) const noexcept {
        if (!expanded_slot[slot & 0x03U]) {
            return 0U;
        }
        const unsigned page = static_cast<unsigned>(address >> 14U);
        return static_cast<std::uint8_t>((secondary_slot[slot & 0x03U] >> (page * 2U)) & 0x03U);
    }

    std::uint8_t msx2_system::read_ram(std::uint16_t address) const noexcept {
        if (ram.empty()) {
            return 0xFFU;
        }
        const std::size_t segment_count = ram.size() / page_size;
        const std::size_t page = static_cast<std::size_t>(address >> 14U);
        const std::size_t segment = ram_segment[page] % segment_count;
        const std::size_t physical = segment * page_size + (address & 0x3FFFU);
        return ram[physical];
    }

    void msx2_system::write_ram(std::uint16_t address, std::uint8_t value) noexcept {
        if (ram.empty()) {
            return;
        }
        const std::size_t segment_count = ram.size() / page_size;
        const std::size_t page = static_cast<std::size_t>(address >> 14U);
        const std::size_t segment = ram_segment[page] % segment_count;
        const std::size_t physical = segment * page_size + (address & 0x3FFFU);
        ram[physical] = value;
    }

    std::uint8_t msx2_system::read_cartridge(std::uint8_t slot_index,
                                             std::uint16_t address) const noexcept {
        const bool primary_cart = slot_index == 0U;
        const auto active_mapper = primary_cart ? cart_mapper : cart2_mapper;
        const auto& rom = primary_cart ? cartridge : cartridge2;
        const auto& bank8 = primary_cart ? ascii8_bank : cart2_ascii8_bank;
        const auto& bank16 = primary_cart ? ascii16_bank : cart2_ascii16_bank;
        const auto& konami = primary_cart ? konami_bank : cart2_konami_bank;
        const auto& sram = primary_cart ? cart_sram : cart2_sram;
        const auto& korean = primary_cart ? korean_mapper : korean_mapper2;

        if (primary_cart && fmpac_sram_active && fmpac_sram_unlocked() && address >= 0x4000U &&
            address < 0x6000U) {
            return fmpac_sram[static_cast<std::size_t>(address - 0x4000U)];
        }
        if (rom.empty()) {
            return 0xFFU;
        }
        if (is_korean_mapper(active_mapper)) {
            return korean.cpu_read(address);
        }

        switch (active_mapper) {
        case msx2_cartridge_mapper::konami:
        case msx2_cartridge_mapper::konami_scc: {
            address = mirror_konami_address(address);
            if (address < 0x4000U || address >= 0xC000U) {
                return 0xFFU;
            }
            if (scc_register_selected(slot_index, address)) {
                return scc.read(address);
            }
            const std::uint32_t window = konami_window(address);
            const std::uint32_t offset = address & 0x1FFFU;
            return read_paged(rom, ascii8_page_size, konami[window], offset);
        }
        case msx2_cartridge_mapper::ascii8:
        case msx2_cartridge_mapper::generic8:
        case msx2_cartridge_mapper::ascii8_sram8: {
            if (address < 0x4000U || address >= 0xC000U) {
                return 0xFFU;
            }
            const std::uint32_t window = (address - 0x4000U) / ascii8_page_size;
            const std::uint32_t offset = address & 0x1FFFU;
            if (active_mapper == msx2_cartridge_mapper::ascii8_sram8 && !sram.empty() &&
                address >= 0x8000U && (bank8[window] & 0x80U) != 0U) {
                return sram[static_cast<std::size_t>(offset) % sram.size()];
            }
            const std::uint8_t bank = active_mapper == msx2_cartridge_mapper::ascii8_sram8
                                          ? static_cast<std::uint8_t>(bank8[window] & 0x7FU)
                                          : bank8[window];
            return read_paged(rom, ascii8_page_size, bank, offset);
        }
        case msx2_cartridge_mapper::ascii16:
        case msx2_cartridge_mapper::ascii16_sram2: {
            if (address < 0x4000U || address >= 0xC000U) {
                return 0xFFU;
            }
            const std::uint32_t window = (address - 0x4000U) / ascii16_page_size;
            const std::uint32_t offset = address & 0x3FFFU;
            if (active_mapper == msx2_cartridge_mapper::ascii16_sram2 && !sram.empty() &&
                address >= 0x8000U && (bank16[1] & 0x10U) != 0U) {
                return sram[static_cast<std::size_t>(address & 0x07FFU) % sram.size()];
            }
            const std::uint8_t bank = active_mapper == msx2_cartridge_mapper::ascii16_sram2
                                          ? static_cast<std::uint8_t>(bank16[window] & 0x0FU)
                                          : bank16[window];
            return read_paged(rom, ascii16_page_size, bank, offset);
        }
        case msx2_cartridge_mapper::plain:
        default: {
            if (address < 0x4000U || address >= 0xC000U) {
                return rom.size() > 0x8000U ? rom[address % rom.size()] : 0xFFU;
            }
            const std::size_t offset = common::msx_plain_rom_physical_offset(
                rom, address, plain_16k_lower_page_visible(slot_index));
            return offset < rom.size() ? rom[offset] : 0xFFU;
        }
        }
    }

    void msx2_system::write_cartridge(std::uint8_t slot_index, std::uint16_t address,
                                      std::uint8_t value) noexcept {
        const bool primary_cart = slot_index == 0U;
        const auto active_mapper = primary_cart ? cart_mapper : cart2_mapper;
        auto& bank8 = primary_cart ? ascii8_bank : cart2_ascii8_bank;
        auto& bank16 = primary_cart ? ascii16_bank : cart2_ascii16_bank;
        auto& konami = primary_cart ? konami_bank : cart2_konami_bank;
        auto& sram = primary_cart ? cart_sram : cart2_sram;
        auto& korean = primary_cart ? korean_mapper : korean_mapper2;

        if (primary_cart && fmpac_sram_active && address >= 0x4000U && address < 0x6000U) {
            const std::size_t offset = static_cast<std::size_t>(address - 0x4000U);
            if (address == 0x5FFEU) {
                fmpac_unlock_latch[0] = value;
                fmpac_sram[offset] = value;
                return;
            }
            if (address == 0x5FFFU) {
                fmpac_unlock_latch[1] = value;
                fmpac_sram[offset] = value;
                return;
            }
            if (fmpac_sram_unlocked()) {
                fmpac_sram[offset] = value;
                return;
            }
        }
        if (is_korean_mapper(active_mapper)) {
            korean.cpu_write(address, value);
            return;
        }
        if (active_mapper == msx2_cartridge_mapper::ascii8_sram8 && !sram.empty() &&
            address >= 0x8000U && address < 0xC000U) {
            const std::uint32_t window = (address - 0x4000U) / ascii8_page_size;
            if ((bank8[window] & 0x80U) != 0U) {
                sram[static_cast<std::size_t>(address & 0x1FFFU) % sram.size()] = value;
                return;
            }
        }
        if (active_mapper == msx2_cartridge_mapper::ascii16_sram2 && !sram.empty() &&
            address >= 0x8000U && address < 0xC000U && (bank16[1] & 0x10U) != 0U) {
            sram[static_cast<std::size_t>(address & 0x07FFU) % sram.size()] = value;
            return;
        }

        switch (active_mapper) {
        case msx2_cartridge_mapper::konami_scc:
            address = mirror_konami_address(address);
            if (scc_register_selected(slot_index, address)) {
                scc.write(address, value);
            } else if (address >= 0x5000U && address < 0x5800U) {
                konami[0] = value;
            } else if (address >= 0x7000U && address < 0x7800U) {
                konami[1] = value;
            } else if (address >= 0x9000U && address < 0x9800U) {
                konami[2] = value;
            } else if (address >= 0xB000U && address < 0xB800U) {
                konami[3] = value;
            }
            break;
        case msx2_cartridge_mapper::konami:
            address = mirror_konami_address(address);
            if (address >= 0x6000U && address < 0x8000U) {
                konami[1] = value;
            } else if (address >= 0x8000U && address < 0xA000U) {
                konami[2] = value;
            } else if (address >= 0xA000U && address < 0xC000U) {
                konami[3] = value;
            }
            break;
        case msx2_cartridge_mapper::ascii8:
        case msx2_cartridge_mapper::ascii8_sram8:
            if (address >= 0x6000U && address < 0x6800U) {
                bank8[0] = value;
            } else if (address >= 0x6800U && address < 0x7000U) {
                bank8[1] = value;
            } else if (address >= 0x7000U && address < 0x7800U) {
                bank8[2] = value;
            } else if (address >= 0x7800U && address < 0x8000U) {
                bank8[3] = value;
            }
            break;
        case msx2_cartridge_mapper::generic8:
            if (address >= 0x4000U && address < 0xC000U) {
                bank8[(address - 0x4000U) / ascii8_page_size] = value;
            }
            break;
        case msx2_cartridge_mapper::ascii16:
        case msx2_cartridge_mapper::ascii16_sram2:
            if (const std::uint8_t reg = common::msx_ascii16_bank_register(address);
                reg < bank16.size()) {
                bank16[reg] = value;
            }
            break;
        case msx2_cartridge_mapper::plain:
        default:
            break;
        }
    }

    bool msx2_system::fmpac_sram_unlocked() const noexcept {
        return fmpac_unlock_latch[0] == 0x4DU && fmpac_unlock_latch[1] == 0x69U;
    }

    bool msx2_system::scc_register_selected(std::uint8_t slot_index,
                                            std::uint16_t address) const noexcept {
        const bool primary_cart = slot_index == 0U;
        const auto active_mapper = primary_cart ? cart_mapper : cart2_mapper;
        const auto& konami = primary_cart ? konami_bank : cart2_konami_bank;
        return active_mapper == msx2_cartridge_mapper::konami_scc && (konami[2] & 0x3FU) == 0x3FU &&
               address >= 0x9800U && address < 0xA000U;
    }

    bool msx2_system::fdc_mmio_selected(std::uint16_t address) const noexcept {
        if (!disk_active) {
            return false;
        }
        return (address >= 0x7FF8U && address <= 0x7FFFU) ||
               (address >= 0xBFF8U && address <= 0xBFFFU);
    }

    std::uint8_t msx2_system::read_fdc_mmio(std::uint16_t address) noexcept {
        switch (address & 0x0007U) {
        case 0x00U:
        case 0x01U:
        case 0x02U:
        case 0x03U:
            return fdc.read_register(static_cast<std::uint8_t>(address & 0x0003U));
        case 0x04U:
            return fdc.selected_side();
        case 0x05U:
            return fdc.selected_drive();
        case 0x07U:
            // MSX2 memory-mapped FDC status: bit7 = DRQ, bit6 = BUSY.
            return static_cast<std::uint8_t>((fdc.drq() ? 0x80U : 0U) | (fdc.busy() ? 0x40U : 0U));
        case 0x06U:
        default:
            return 0xFFU;
        }
    }

    void msx2_system::write_fdc_mmio(std::uint16_t address, std::uint8_t value) noexcept {
        switch (address & 0x0007U) {
        case 0x00U:
        case 0x01U:
        case 0x02U:
        case 0x03U:
            fdc.write_register(static_cast<std::uint8_t>(address & 0x0003U), value);
            break;
        case 0x04U:
            fdc.write_memory_register(0x04U, value);
            break;
        case 0x05U:
            // The flat DSK-backed FDC is ready when drive A is selected and media is mounted;
            // analog motor spin-up is intentionally outside this deterministic model.
            fdc.write_memory_register(0x05U, value);
            break;
        case 0x06U:
        case 0x07U:
        default:
            break;
        }
    }

    std::uint8_t msx2_system::read_psg_data() const noexcept {
        if (psg.selected_register() == chips::audio::ssg::reg_port_a) {
            if (!common::msx_psg_port_a_input(psg.read_reg(chips::audio::ssg::reg_mixer))) {
                return psg.read_reg(chips::audio::ssg::reg_port_a);
            }
            const std::uint8_t effective_port_b = common::msx_psg_effective_port_b_latch(
                psg.read_reg(chips::audio::ssg::reg_mixer),
                psg.read_reg(chips::audio::ssg::reg_port_b));
            return common::msx_psg_port_a_value(joystick_ports, mouse_ports,
                                                effective_port_b, cassette.input_high());
        }
        return psg.read();
    }

    std::uint8_t msx2_system::cartridge_slot_index(std::uint8_t slot,
                                                   std::uint8_t subslot) const noexcept {
        if ((slot & 0x03U) == 1U && subslot == 0U) {
            return 0U;
        }
        if (!cartridge2.empty() &&
            slot_selected(slot, subslot, cartridge2_primary_slot, cartridge2_secondary_slot)) {
            return 1U;
        }
        return k_no_cartridge_slot;
    }

    std::uint8_t msx2_system::plain_32k_handoff_cart_slot(std::uint8_t slot, std::uint8_t subslot,
                                                          std::uint16_t address) const noexcept {
        const bool ram_selected = slot_selected(slot, subslot, ram_primary_slot, ram_secondary_slot);
        const bool bios_selected = (slot & 0x03U) == 0U && subslot == 0U;
        if (!ram_selected && !bios_selected) {
            return k_no_cartridge_slot;
        }

        const bool lower_window =
            common::msx_plain_32k_lower_rom_window(common::msx_cartridge_mapper_kind::plain,
                                                   0x8000U, address);
        const bool upper_window =
            common::msx_plain_32k_upper_rom_window(common::msx_cartridge_mapper_kind::plain,
                                                   0x8000U, address);
        if (!lower_window && !upper_window) {
            return k_no_cartridge_slot;
        }

        const std::uint16_t reference_address = lower_window ? 0x8000U : 0x4000U;
        const std::uint8_t reference_slot = slot_for_page(reference_address);
        const std::uint8_t reference_subslot =
            secondary_for_page(reference_slot, reference_address);
        const std::uint8_t reference_cart_slot =
            cartridge_slot_index(reference_slot, reference_subslot);
        if (reference_cart_slot == k_no_cartridge_slot) {
            return k_no_cartridge_slot;
        }

        const bool primary_cart = reference_cart_slot == 0U;
        const auto active_mapper = primary_cart ? cart_mapper : cart2_mapper;
        const auto& rom = primary_cart ? cartridge : cartridge2;
        const bool lower_handoff =
            primary_cart ? cartridge_lower_handoff : cartridge2_lower_handoff;
        if (lower_window && !lower_handoff) {
            return k_no_cartridge_slot;
        }

        const bool in_partner_window =
            lower_window
                ? common::msx_plain_32k_lower_rom_window(mapper_kind(active_mapper), rom.size(),
                                                         address)
                : common::msx_plain_32k_upper_rom_window(mapper_kind(active_mapper), rom.size(),
                                                         address);
        return in_partner_window ? reference_cart_slot : k_no_cartridge_slot;
    }

    bool msx2_system::plain_16k_lower_page_visible(std::uint8_t slot_index) const noexcept {
        const std::uint8_t page1_slot = slot_for_page(0x4000U);
        const std::uint8_t page1_subslot = secondary_for_page(page1_slot, 0x4000U);
        return cartridge_slot_index(page1_slot, page1_subslot) == slot_index;
    }

    void msx2_system::sync_ppi_outputs() noexcept {
        if (common::msx_ppi_port_a_output(ppi_control)) {
            primary_slot = ppi_a;
        }
        sync_cassette_control();
    }

    void msx2_system::sync_cassette_control() noexcept {
        cassette.set_motor_on(common::msx_cassette_motor_from_ppi(ppi_control, ppi_c));
        cassette.set_output_high(common::msx_cassette_output_high_from_ppi(ppi_control, ppi_c));
    }

    void msx2_system::sync_psg_outputs() noexcept {
        const std::uint8_t port_b = common::msx_psg_effective_port_b_latch(
            psg.read_reg(chips::audio::ssg::reg_mixer),
            psg.read_reg(chips::audio::ssg::reg_port_b));
        mouse_ports[0].set_pin8((port_b & 0x10U) != 0U);
        mouse_ports[1].set_pin8((port_b & 0x20U) != 0U);
    }

    std::uint8_t msx2_system::read_slot(std::uint8_t slot, std::uint8_t subslot,
                                        std::uint16_t address) noexcept {
        if (address == 0xFFFFU && expanded_slot[slot & 0x03U]) {
            return static_cast<std::uint8_t>(secondary_slot[slot & 0x03U] ^ 0xFFU);
        }

        const std::uint8_t handoff_cart_slot = plain_32k_handoff_cart_slot(slot, subslot, address);
        if (handoff_cart_slot != k_no_cartridge_slot) {
            return read_cartridge(handoff_cart_slot, address);
        }

        if ((slot & 0x03U) == 0U && subslot == 0U) {
            if (address < 0x8000U && address < bios.size()) {
                return bios[address];
            }
            if (address >= 0x8000U && address < 0xC000U && !logo_rom.empty()) {
                const std::size_t offset = static_cast<std::size_t>(address - 0x8000U);
                return offset < logo_rom.size() ? logo_rom[offset] : 0xFFU;
            }
            return address < bios.size() ? bios[address] : 0xFFU;
        }
        if (!sub_bios.empty() && slot_selected(slot, subslot, sub_bios_primary_slot,
                                               sub_bios_secondary_slot)) {
            return address < sub_bios.size() ? sub_bios[address] : 0xFFU;
        }
        if ((slot & 0x03U) == 1U && subslot == 0U) {
            return read_cartridge(0U, address);
        }
        if (slot_selected(slot, subslot, disk_primary_slot, disk_secondary_slot) && disk_active) {
            if (fdc_mmio_selected(address)) {
                return read_fdc_mmio(address);
            }
            if (address >= 0x4000U && address < 0x8000U) {
                const std::size_t offset = static_cast<std::size_t>(address - 0x4000U);
                return offset < disk_rom.size() ? disk_rom[offset] : 0xFFU;
            }
            return 0xFFU;
        }
        if (!cartridge2.empty() &&
            slot_selected(slot, subslot, cartridge2_primary_slot, cartridge2_secondary_slot)) {
            return read_cartridge(1U, address);
        }
        if (slot_selected(slot, subslot, ram_primary_slot, ram_secondary_slot)) {
            return read_ram(address);
        }
        return 0xFFU;
    }

    void msx2_system::write_slot(std::uint8_t slot, std::uint8_t subslot, std::uint16_t address,
                                 std::uint8_t value) noexcept {
        if (address == 0xFFFFU && expanded_slot[slot & 0x03U]) {
            secondary_slot[slot & 0x03U] = value;
            return;
        }

        if ((slot & 0x03U) == 1U && subslot == 0U) {
            write_cartridge(0U, address, value);
            return;
        }
        if (slot_selected(slot, subslot, disk_primary_slot, disk_secondary_slot) && disk_active) {
            if (fdc_mmio_selected(address)) {
                write_fdc_mmio(address, value);
            }
            return;
        }
        if (!cartridge2.empty() &&
            slot_selected(slot, subslot, cartridge2_primary_slot, cartridge2_secondary_slot)) {
            write_cartridge(1U, address, value);
            return;
        }
        if (slot_selected(slot, subslot, ram_primary_slot, ram_secondary_slot)) {
            write_ram(address, value);
        }
    }

    std::uint8_t msx2_system::cpu_read(std::uint16_t address) noexcept {
        const std::uint8_t slot = slot_for_page(address);
        return read_slot(slot, secondary_for_page(slot, address), address);
    }

    void msx2_system::cpu_write(std::uint16_t address, std::uint8_t value) noexcept {
        const std::uint8_t slot = slot_for_page(address);
        write_slot(slot, secondary_for_page(slot, address), address, value);
    }

    std::uint8_t msx2_system::io_read(std::uint16_t port) noexcept {
        const std::uint8_t p = static_cast<std::uint8_t>(port & 0xFFU);
        switch (p) {
        case 0x7C:
        case 0x7D:
            return 0xFFU;
        case 0xD0:
        case 0xD1:
        case 0xD2:
        case 0xD3:
            return disk_active ? fdc.read_register(static_cast<std::uint8_t>(p - 0xD0U)) : 0xFFU;
        case 0xD4:
            // MSX2 I/O-port FDC status: bit7 = INTRQ-or-not-busy, bit6 = DRQ.
            return disk_active
                       ? static_cast<std::uint8_t>(((fdc.intrq() || !fdc.busy()) ? 0x80U : 0U) |
                                                   (fdc.drq() ? 0x40U : 0U))
                       : 0xFFU;
        case 0xD9:
            return kanji.read_data(0U);
        case 0xDB:
            return kanji.read_data(1U);
        case 0x98:
            return vdp.data_read();
        case 0x99:
            return vdp.status_read();
        case 0xA2:
            return read_psg_data();
        case 0xA8:
            return primary_slot;
        case 0xA9:
            if (common::msx_ppi_port_b_output(ppi_control)) {
                return ppi_b;
            }
            return common::msx_ppi_port_c_lower_output(ppi_control)
                       ? keyboard_rows[ppi_c & 0x0FU]
                       : 0xFFU;
        case 0xAA:
            return common::msx_ppi_port_c_read(ppi_control, ppi_c);
        case 0xB5:
            return rtc.data_read();
        case 0xFC:
        case 0xFD:
        case 0xFE:
        case 0xFF:
            return ram_segment[p - 0xFCU];
        default:
            return 0xFFU;
        }
    }

    void msx2_system::io_write(std::uint16_t port, std::uint8_t value) noexcept {
        const std::uint8_t p = static_cast<std::uint8_t>(port & 0xFFU);
        switch (p) {
        case 0x7C:
            if (msx_music_active) {
                music.write_address(value);
            }
            break;
        case 0x7D:
            if (msx_music_active) {
                music.write_data(value);
            }
            break;
        case 0xD0:
        case 0xD1:
        case 0xD2:
        case 0xD3:
            if (disk_active) {
                fdc.write_register(static_cast<std::uint8_t>(p - 0xD0U), value);
            }
            break;
        case 0xD4: {
            if (!disk_active) {
                break;
            }
            const common::msx_fdc_drive_control control =
                common::msx_fdc_decode_drive_control(value);
            // Canonical wd1793: set drive/side via the memory register; no motor model.
            fdc.write_memory_register(0x05U, control.drive);
            fdc.write_memory_register(0x04U, control.side);
            break;
        }
        case 0xD8:
            kanji.write_address(0U, false, value);
            break;
        case 0xD9:
            kanji.write_address(0U, true, value);
            break;
        case 0xDA:
            kanji.write_address(1U, false, value);
            break;
        case 0xDB:
            kanji.write_address(1U, true, value);
            break;
        case 0x98:
            vdp.data_write(value);
            break;
        case 0x99:
            vdp.ctrl_write(value);
            break;
        case 0x9A:
            vdp.palette_write(value);
            break;
        case 0x9B:
            vdp.indirect_reg_write(value);
            break;
        case 0xA0:
            psg.address(value);
            break;
        case 0xA1:
            psg.write(value);
            sync_psg_outputs();
            break;
        case 0xA8:
            ppi_a = value;
            sync_ppi_outputs();
            break;
        case 0xA9:
            ppi_b = value;
            break;
        case 0xAA:
            ppi_c = value;
            sync_cassette_control();
            break;
        case 0xAB:
            if ((value & 0x80U) != 0U) {
                ppi_control = value;
                sync_ppi_outputs();
            } else {
                const std::uint8_t bit = static_cast<std::uint8_t>((value >> 1U) & 0x07U);
                const std::uint8_t mask = static_cast<std::uint8_t>(1U << bit);
                ppi_c = (value & 0x01U) != 0U ? static_cast<std::uint8_t>(ppi_c | mask)
                                              : static_cast<std::uint8_t>(ppi_c & ~mask);
                sync_cassette_control();
            }
            break;
        case 0xB4:
            rtc.select(value);
            break;
        case 0xB5:
            rtc.data_write(value);
            break;
        case 0xFC:
        case 0xFD:
        case 0xFE:
        case 0xFF:
            if (!ram.empty()) {
                ram_segment[p - 0xFCU] =
                    common::msx_ram_mapper_latch_value(value, ram_mapper_segment_count(*this));
            }
            break;
        default:
            break;
        }
    }

    std::span<std::uint8_t> msx2_system::battery_ram() noexcept {
        if (!cart_sram.empty()) {
            return cart_sram;
        }
        if (fmpac_sram_active) {
            return fmpac_sram;
        }
        if (!cart2_sram.empty()) {
            return cart2_sram;
        }
        return {};
    }

    void msx2_system::save_state(chips::state_writer& writer) const {
        writer.u32(k_state_version);
        writer.u8(primary_slot);
        writer.u8(ppi_a);
        writer.bytes(secondary_slot);
        writer.u8(ppi_c);
        writer.u8(ppi_control);
        writer.u8(ppi_b);
        writer.bytes(keyboard_rows);
        writer.bytes(joystick_ports);
        save_mouse_ports(writer, mouse_ports);
        psg.save_state(writer);
        cassette.save_state(writer);
        writer.bytes(ram_segment);
        writer.bytes(ascii8_bank);
        writer.bytes(ascii16_bank);
        writer.bytes(konami_bank);
        writer.bytes(cart_sram);
        korean_mapper.save_state(writer);
        kanji.save_state(writer);
        scc.save_state(writer);
        rtc.save_state(writer);
        fdc.save_state(writer);
        writer.boolean(disk_active);
        writer.boolean(msx_music_active);
        music.save_state(writer);
        writer.boolean(fmpac_sram_active);
        writer.bytes(fmpac_sram);
        writer.bytes(fmpac_unlock_latch);
        writer.bytes(cart2_ascii8_bank);
        writer.bytes(cart2_ascii16_bank);
        writer.bytes(cart2_konami_bank);
        writer.bytes(cart2_sram);
        korean_mapper2.save_state(writer);
        writer.bytes(std::span<const std::uint8_t>(ram));
    }

    void msx2_system::load_state(chips::state_reader& reader) {
        const std::uint32_t version = reader.u32();
        if (version == 0U || version > k_state_version) {
            reader.fail();
            return;
        }
        primary_slot = reader.u8();
        if (version >= 18U) {
            ppi_a = reader.u8();
        } else {
            ppi_a = primary_slot;
        }
        if (version >= 3U) {
            reader.bytes(secondary_slot);
        } else {
            secondary_slot = {};
        }
        ppi_c = reader.u8();
        ppi_control = reader.u8();
        if (version >= 17U) {
            ppi_b = reader.u8();
        } else {
            ppi_b = 0xFFU;
        }
        reader.bytes(keyboard_rows);
        if (version >= 8U) {
            reader.bytes(joystick_ports);
        } else {
            joystick_ports = {0x3FU, 0x3FU};
        }
        if (version >= 20U) {
            load_mouse_ports(reader, mouse_ports);
        } else {
            mouse_ports = {};
        }
        if (version >= 19U) {
            psg.load_state(reader);
        } else {
            psg.reset(chips::reset_kind::power_on);
        }
        sync_psg_outputs();
        if (version >= 11U) {
            cassette.load_state(reader);
        } else {
            const bool legacy_input = version >= 9U ? reader.boolean() : true;
            cassette.reset(chips::reset_kind::power_on);
            sync_cassette_control();
            cassette.set_input_high(legacy_input);
        }
        reader.bytes(ram_segment);
        const std::size_t segment_count = ram_mapper_segment_count(*this);
        for (std::uint8_t& page : ram_segment) {
            page = common::msx_ram_mapper_latch_value(page, segment_count);
        }
        reader.bytes(ascii8_bank);
        reader.bytes(ascii16_bank);
        if (version >= 2U) {
            reader.bytes(konami_bank);
            if (version >= 12U) {
                reader.bytes(cart_sram);
            }
            if (version >= 13U) {
                korean_mapper.load_state(reader);
            } else {
                korean_mapper.reset(chips::reset_kind::power_on);
            }
            if (version >= 10U) {
                kanji.load_state(reader);
            } else {
                kanji.reset(chips::reset_kind::power_on);
            }
            if (version >= 4U) {
                scc.load_state(reader);
            } else {
                std::array<std::uint8_t, 0x100> legacy_scc_register{};
                reader.bytes(legacy_scc_register);
                scc.reset(chips::reset_kind::power_on);
                for (std::size_t i = 0; i < legacy_scc_register.size(); ++i) {
                    scc.write(static_cast<std::uint16_t>(i), legacy_scc_register[i]);
                }
            }
        } else {
            konami_bank = {0U, 1U, 2U, 3U};
            korean_mapper.reset(chips::reset_kind::power_on);
            kanji.reset(chips::reset_kind::power_on);
            scc.reset(chips::reset_kind::power_on);
        }
        if (version >= 6U) {
            rtc.load_state(reader);
        } else {
            rtc.reset(chips::reset_kind::power_on);
        }
        if (version >= 7U) {
            fdc.load_state(reader);
        } else {
            fdc.reset(chips::reset_kind::power_on);
        }
        if (version >= 16U) {
            disk_active = reader.boolean();
        } else {
            disk_active = fdc.mounted() || !disk_rom.empty();
        }
        if (version >= 5U) {
            msx_music_active = reader.boolean();
            music.load_state(reader);
        } else {
            music.reset(chips::reset_kind::power_on);
        }
        if (version >= 14U) {
            fmpac_sram_active = reader.boolean();
            reader.bytes(fmpac_sram);
            reader.bytes(fmpac_unlock_latch);
        } else {
            fmpac_sram_active = false;
            fmpac_sram.fill(0xFFU);
            fmpac_unlock_latch.fill(0xFFU);
        }
        if (version >= 15U) {
            reader.bytes(cart2_ascii8_bank);
            reader.bytes(cart2_ascii16_bank);
            reader.bytes(cart2_konami_bank);
            reader.bytes(cart2_sram);
            korean_mapper2.load_state(reader);
        } else {
            cart2_ascii8_bank = {0U, 1U, 2U, 3U};
            cart2_ascii16_bank = {0U, 1U};
            cart2_konami_bank = {0U, 1U, 2U, 3U};
            korean_mapper2.reset(chips::reset_kind::power_on);
        }
        reader.bytes(std::span<std::uint8_t>(ram));
    }

    std::unique_ptr<msx2_system> assemble_msx2(std::span<const std::uint8_t> bios,
                                               std::span<const std::uint8_t> cartridge,
                                               const msx2_config& config) {
        auto sys = std::make_unique<msx2_system>();
        msx2_system* s = sys.get();

        s->bios.assign(bios.begin(), bios.end());
        s->sub_bios.assign(config.sub_bios.begin(), config.sub_bios.end());
        s->logo_rom.assign(config.logo_rom.begin(), config.logo_rom.end());
        s->disk_rom.assign(config.disk_rom.begin(), config.disk_rom.end());
        s->kanji.attach_rom(config.kanji_rom);
        s->cartridge.assign(cartridge.begin(), cartridge.end());
        s->cartridge2.assign(config.cartridge2.begin(), config.cartridge2.end());
        s->ram.resize(rounded_ram_size(config.ram_size));
        s->cart_mapper = resolve_cartridge_mapper(config.cartridge_mapper, s->cartridge);
        s->cart2_mapper = resolve_cartridge_mapper(config.cartridge2_mapper, s->cartridge2);
        s->cartridge_lower_handoff =
            common::msx_plain_32k_lower_handoff_required(s->cartridge);
        s->cartridge2_lower_handoff =
            common::msx_plain_32k_lower_handoff_required(s->cartridge2);
        s->korean_mapper.set_variant(korean_variant(s->cart_mapper));
        s->korean_mapper.attach_rom(s->cartridge);
        s->korean_mapper.reset(chips::reset_kind::power_on);
        s->korean_mapper2.set_variant(korean_variant(s->cart2_mapper));
        s->korean_mapper2.attach_rom(s->cartridge2);
        s->korean_mapper2.reset(chips::reset_kind::power_on);
        if (!s->cartridge.empty()) {
            s->cart_sram.assign(cart_sram_size(s->cart_mapper), 0xFFU);
        }
        if (!s->cartridge2.empty()) {
            s->cart2_sram.assign(cart_sram_size(s->cart2_mapper), 0xFFU);
        }
        s->msx_music_active = config.msx_music;
        s->fmpac_sram_active = config.fmpac_sram;
        s->fmpac_sram.fill(0xFFU);
        s->fmpac_unlock_latch.fill(0xFFU);
        s->ppi_a = 0x00U;
        s->ppi_b = 0xFFU;
        s->keyboard_rows.fill(0xFFU);
        s->joystick_ports = {0x3FU, 0x3FU};
        s->secondary_slot = {};
        s->expanded_slot = {false, false, false, false};
        s->ram_primary_slot = static_cast<std::uint8_t>(config.ram_primary_slot & 0x03U);
        s->ram_secondary_slot = static_cast<std::uint8_t>(config.ram_secondary_slot & 0x03U);
        s->sub_bios_primary_slot =
            static_cast<std::uint8_t>(config.sub_bios_primary_slot & 0x03U);
        s->sub_bios_secondary_slot =
            static_cast<std::uint8_t>(config.sub_bios_secondary_slot & 0x03U);
        s->disk_primary_slot = static_cast<std::uint8_t>(config.disk_primary_slot & 0x03U);
        s->disk_secondary_slot = static_cast<std::uint8_t>(config.disk_secondary_slot & 0x03U);
        s->cartridge2_primary_slot =
            static_cast<std::uint8_t>(config.cartridge2_primary_slot & 0x03U);
        s->cartridge2_secondary_slot =
            static_cast<std::uint8_t>(config.cartridge2_secondary_slot & 0x03U);
        s->disk_active = config.disk_enabled || !s->disk_rom.empty() || !config.disk_image.empty();
        std::uint8_t expanded_slots =
            static_cast<std::uint8_t>(config.expanded_primary_slots & 0x0FU);
        if (!s->sub_bios.empty() && s->sub_bios_secondary_slot != 0U) {
            expanded_slots =
                static_cast<std::uint8_t>(expanded_slots | (1U << s->sub_bios_primary_slot));
        }
        if (s->ram_secondary_slot != 0U) {
            expanded_slots =
                static_cast<std::uint8_t>(expanded_slots | (1U << s->ram_primary_slot));
        }
        if (s->disk_active && s->disk_secondary_slot != 0U) {
            expanded_slots =
                static_cast<std::uint8_t>(expanded_slots | (1U << s->disk_primary_slot));
        }
        if (!s->cartridge2.empty() && s->cartridge2_secondary_slot != 0U) {
            expanded_slots =
                static_cast<std::uint8_t>(expanded_slots | (1U << s->cartridge2_primary_slot));
        }
        for (std::size_t slot = 0; slot < s->expanded_slot.size(); ++slot) {
            s->expanded_slot[slot] = (expanded_slots & (1U << slot)) != 0U;
        }
        s->ram_segment = common::msx_initial_ram_mapper_pages(ram_mapper_segment_count(*s));
        s->ascii8_bank = {0U, 1U, 2U, 3U};
        s->ascii16_bank = {0U, 1U};
        s->konami_bank = {0U, 1U, 2U, 3U};
        s->cart2_ascii8_bank = {0U, 1U, 2U, 3U};
        s->cart2_ascii16_bank = {0U, 1U};
        s->cart2_konami_bank = {0U, 1U, 2U, 3U};
        s->scc.reset(chips::reset_kind::power_on);
        s->rtc.reset(chips::reset_kind::power_on);
        s->cassette.set_cycles_per_second(chips::storage::msx_cassette::default_cycles_per_second);
        s->cassette.reset(chips::reset_kind::power_on);
        s->sync_cassette_control();
        if (!config.cassette_image.empty() && s->cassette.load_cas(config.cassette_image)) {
            s->cassette.set_play(true);
            s->sync_cassette_control();
        }
        s->fdc.reset(chips::reset_kind::power_on);
        if (!config.disk_image.empty()) {
            (void)s->fdc.mount_dsk(config.disk_image, config.disk_write_protected);
        }

        s->vdp.set_pal(config.video_region == mnemos::video_region::pal);
        s->vdp.set_irq_callback([s](bool asserted) { s->cpu.set_irq_line(asserted); });

        // The AY/YM2149 receives the Z80 clock divided by two, and its own
        // synthesis core steps at the chip's /16 internal SSG divider.
        s->psg.set_clock_divider(32);
        s->scc.set_clock_divider(chips::audio::scc::default_clock_divider);
        s->music.set_clock_divider(72);

        s->bus.map_mmio(
            0x0000U, 0x10000U,
            [s](std::uint32_t address) { return s->cpu_read(static_cast<std::uint16_t>(address)); },
            [s](std::uint32_t address, std::uint8_t value) {
                s->cpu_write(static_cast<std::uint16_t>(address), value);
            },
            0);

        s->cpu.set_port_in([s](std::uint16_t port) { return s->io_read(port); });
        s->cpu.set_port_out(
            [s](std::uint16_t port, std::uint8_t value) { s->io_write(port, value); });
        s->cpu.attach_bus(s->bus);
        s->cpu.reset(chips::reset_kind::power_on);
        return sys;
    }

} // namespace mnemos::manifests::msx2
