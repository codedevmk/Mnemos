#include "msx_system.hpp"

#include <algorithm>
#include <cstring>

namespace mnemos::manifests::msx {

    namespace {
        constexpr std::uint32_t k_state_version = 13U;
        constexpr std::uint8_t k_no_cartridge_slot = 0xFFU;
        constexpr std::size_t k_kanji_level_size = 0x20000U;
        constexpr std::size_t k_kanji_bytes_per_character = 32U;
        constexpr std::size_t k_ascii8_sram8_size = 0x2000U;
        constexpr std::size_t k_ascii16_sram2_size = 0x0800U;

        [[nodiscard]] std::uint8_t clamp_row(std::uint8_t value) noexcept {
            return static_cast<std::uint8_t>(value & 0x0FU);
        }

        [[nodiscard]] std::size_t bank_count_8k(std::span<const std::uint8_t> rom) noexcept {
            if (rom.empty()) {
                return 0U;
            }
            const std::size_t banks = (rom.size() + 0x1FFFU) / 0x2000U;
            return std::min<std::size_t>(banks, 0x100U);
        }

        [[nodiscard]] std::size_t bank_count_16k(std::span<const std::uint8_t> rom) noexcept {
            if (rom.empty()) {
                return 0U;
            }
            const std::size_t banks = (rom.size() + 0x3FFFU) / 0x4000U;
            return std::min<std::size_t>(banks, 0x100U);
        }

        [[nodiscard]] bool is_disk_mmio_address(std::uint16_t address) noexcept {
            return address >= 0x4000U && address < 0xC000U && (address & 0x3FF8U) == 0x3FF8U;
        }

        [[nodiscard]] bool is_korean_mapper(msx_cartridge_mapper mapper) noexcept {
            return mapper == msx_cartridge_mapper::korean_msx ||
                   mapper == msx_cartridge_mapper::korean_msx_nemesis;
        }

        [[nodiscard]] std::size_t cart_sram_size(msx_cartridge_mapper mapper) noexcept {
            switch (mapper) {
            case msx_cartridge_mapper::ascii8_sram8:
                return k_ascii8_sram8_size;
            case msx_cartridge_mapper::ascii16_sram2:
                return k_ascii16_sram2_size;
            default:
                return 0U;
            }
        }

        [[nodiscard]] chips::mapper::korean_msx_mapper::variant
        korean_variant(msx_cartridge_mapper mapper) noexcept {
            return mapper == msx_cartridge_mapper::korean_msx_nemesis
                       ? chips::mapper::korean_msx_mapper::variant::nemesis
                       : chips::mapper::korean_msx_mapper::variant::msx;
        }
    } // namespace

    void msx_system::set_key(int row, int bit, bool pressed) noexcept {
        if (row < 0 || row >= static_cast<int>(keyboard_rows.size()) || bit < 0 || bit >= 8) {
            return;
        }
        const auto mask = static_cast<std::uint8_t>(1U << bit);
        auto& row_ref = keyboard_rows[static_cast<std::size_t>(row)];
        if (pressed) {
            row_ref &= static_cast<std::uint8_t>(~mask);
        } else {
            row_ref |= mask;
        }
    }

    void msx_system::apply_joystick(int port, const peripheral::controller_state& state) noexcept {
        if (port < 0 || port >= static_cast<int>(joystick_rows.size())) {
            return;
        }
        std::uint8_t bits = 0x3FU;
        if (state.up) {
            bits &= static_cast<std::uint8_t>(~0x01U);
        }
        if (state.down) {
            bits &= static_cast<std::uint8_t>(~0x02U);
        }
        if (state.left) {
            bits &= static_cast<std::uint8_t>(~0x04U);
        }
        if (state.right) {
            bits &= static_cast<std::uint8_t>(~0x08U);
        }
        if (state.a || state.b) {
            bits &= static_cast<std::uint8_t>(~0x10U);
        }
        if (state.x || state.y) {
            bits &= static_cast<std::uint8_t>(~0x20U);
        }
        joystick_rows[static_cast<std::size_t>(port)] = bits;
    }

    bool msx_system::primary_slot_expanded(std::uint8_t primary) const noexcept {
        return primary < 4U && (expanded_primary_slots & (1U << primary)) != 0U;
    }

    msx_system::slot_decode msx_system::selected_slot(std::uint16_t address) const noexcept {
        const unsigned page = static_cast<unsigned>(address >> 14U);
        const auto primary =
            static_cast<std::uint8_t>((primary_slot_select >> (page * 2U)) & 0x03U);
        const auto secondary = static_cast<std::uint8_t>(
            primary_slot_expanded(primary)
                ? static_cast<std::uint8_t>((secondary_slot_select[primary] >> (page * 2U)) & 0x03U)
                : 0U);
        return {.primary = primary, .secondary = secondary};
    }

    msx_system::slot_decode msx_system::page3_slot() const noexcept {
        const auto primary = static_cast<std::uint8_t>((primary_slot_select >> 6U) & 0x03U);
        const auto secondary = static_cast<std::uint8_t>(
            primary_slot_expanded(primary)
                ? static_cast<std::uint8_t>((secondary_slot_select[primary] >> 6U) & 0x03U)
                : 0U);
        return {.primary = primary, .secondary = secondary};
    }

    bool msx_system::is_bios_slot(slot_decode slot) const noexcept {
        return slot.primary == 0U && slot.secondary == 0U;
    }

    std::uint8_t msx_system::cart_slot_index(slot_decode slot) const noexcept {
        if (slot.primary == 1U && slot.secondary == 0U) {
            return 0U;
        }
        if (!cartridge2.empty() && slot.primary == cartridge2_primary_slot &&
            slot.secondary == cartridge2_secondary_slot) {
            return 1U;
        }
        return k_no_cartridge_slot;
    }

    bool msx_system::is_disk_slot(slot_decode slot) const noexcept {
        return disk_enabled && slot.primary == disk_primary_slot &&
               slot.secondary == disk_secondary_slot;
    }

    bool msx_system::is_ram_slot(slot_decode slot) const noexcept {
        return slot.primary == ram_primary_slot && slot.secondary == ram_secondary_slot;
    }

    std::uint8_t msx_system::read_ram(std::uint16_t address) const noexcept {
        if (!mapped_ram.empty()) {
            const std::size_t segment_count = mapped_ram.size() / 0x4000U;
            if (segment_count != 0U) {
                const std::size_t page = address >> 14U;
                const std::size_t segment =
                    static_cast<std::size_t>(ram_mapper_page[page]) % segment_count;
                const std::size_t phys = (segment << 14U) | (address & 0x3FFFU);
                return phys < mapped_ram.size() ? mapped_ram[phys] : 0xFFU;
            }
        }
        return ram[address];
    }

    void msx_system::write_ram(std::uint16_t address, std::uint8_t value) noexcept {
        if (!mapped_ram.empty()) {
            const std::size_t segment_count = mapped_ram.size() / 0x4000U;
            if (segment_count != 0U) {
                const std::size_t page = address >> 14U;
                const std::size_t segment =
                    static_cast<std::size_t>(ram_mapper_page[page]) % segment_count;
                const std::size_t phys = (segment << 14U) | (address & 0x3FFFU);
                if (phys < mapped_ram.size()) {
                    mapped_ram[phys] = value;
                }
                return;
            }
        }
        ram[address] = value;
    }

    std::uint8_t msx_system::read_disk(std::uint16_t address) noexcept {
        if (is_disk_mmio_address(address)) {
            return fdc.read_memory_register(static_cast<std::uint8_t>(address & 0x07U));
        }
        if (address >= 0x4000U && address < 0x8000U && !disk_rom.empty()) {
            const std::size_t offset = static_cast<std::size_t>(address - 0x4000U);
            return offset < disk_rom.size() ? disk_rom[offset] : 0xFFU;
        }
        return 0xFFU;
    }

    void msx_system::write_disk(std::uint16_t address, std::uint8_t value) noexcept {
        if (is_disk_mmio_address(address)) {
            fdc.write_memory_register(static_cast<std::uint8_t>(address & 0x07U), value);
        }
    }

    std::uint8_t msx_system::read_cart_8k(std::span<const std::uint8_t> rom, std::uint8_t bank,
                                          std::uint16_t offset) const noexcept {
        const std::size_t count = bank_count_8k(rom);
        if (count == 0U) {
            return 0xFFU;
        }
        const std::size_t phys =
            ((static_cast<std::size_t>(bank) % count) << 13U) | (offset & 0x1FFFU);
        return phys < rom.size() ? rom[phys] : 0xFFU;
    }

    std::uint8_t msx_system::read_cart_16k(std::span<const std::uint8_t> rom, std::uint8_t bank,
                                           std::uint16_t offset) const noexcept {
        const std::size_t count = bank_count_16k(rom);
        if (count == 0U) {
            return 0xFFU;
        }
        const std::size_t phys =
            ((static_cast<std::size_t>(bank) % count) << 14U) | (offset & 0x3FFFU);
        return phys < rom.size() ? rom[phys] : 0xFFU;
    }

    std::uint8_t msx_system::read_cart(std::uint8_t slot_index, std::uint16_t address) noexcept {
        const bool primary_cart = slot_index == 0U;
        const auto active_mapper = primary_cart ? mapper : cartridge2_mapper;
        const auto& rom = primary_cart ? cartridge : cartridge2;
        const auto& bank8 = primary_cart ? cart_8k_bank : cart2_8k_bank;
        const auto& bank16 = primary_cart ? cart_16k_bank : cart2_16k_bank;
        const auto& sram = primary_cart ? cart_sram : cart2_sram;
        const bool scc_window = primary_cart ? scc_window_enabled : scc2_window_enabled;
        auto& korean = primary_cart ? korean_mapper : korean_mapper2;

        if (active_mapper == msx_cartridge_mapper::konami_scc && scc_window && address >= 0x9800U &&
            address < 0xA000U) {
            return scc.read(address);
        }
        if (primary_cart && fmpac_sram_enabled && fmpac_sram_unlocked() && address >= 0x4000U &&
            address < 0x6000U) {
            return fmpac_sram[static_cast<std::size_t>(address - 0x4000U)];
        }
        if (is_korean_mapper(active_mapper)) {
            return korean.cpu_read(address);
        }
        if (active_mapper == msx_cartridge_mapper::ascii8_sram8 && !sram.empty() &&
            address >= 0x8000U && address < 0xC000U) {
            const auto slot = static_cast<std::size_t>((address - 0x4000U) >> 13U);
            if ((bank8[slot] & 0x80U) != 0U) {
                return sram[static_cast<std::size_t>(address & 0x1FFFU) % sram.size()];
            }
        }
        if (active_mapper == msx_cartridge_mapper::ascii16_sram2 && !sram.empty() &&
            address >= 0x8000U && address < 0xC000U && (bank16[1] & 0x10U) != 0U) {
            return sram[static_cast<std::size_t>(address & 0x07FFU) % sram.size()];
        }
        if (rom.empty()) {
            return 0xFFU;
        }
        if (address < 0x4000U || address >= 0xC000U) {
            if (active_mapper == msx_cartridge_mapper::plain && rom.size() > 0x8000U) {
                return rom[address % rom.size()];
            }
            return 0xFFU;
        }

        switch (active_mapper) {
        case msx_cartridge_mapper::ascii8:
        case msx_cartridge_mapper::ascii8_sram8:
        case msx_cartridge_mapper::konami:
        case msx_cartridge_mapper::konami_scc: {
            const auto slot = static_cast<std::size_t>((address - 0x4000U) >> 13U);
            const std::uint8_t bank = active_mapper == msx_cartridge_mapper::ascii8_sram8
                                          ? static_cast<std::uint8_t>(bank8[slot] & 0x7FU)
                                          : bank8[slot];
            return read_cart_8k(rom, bank, address);
        }
        case msx_cartridge_mapper::ascii16:
        case msx_cartridge_mapper::ascii16_sram2: {
            const auto slot = static_cast<std::size_t>((address - 0x4000U) >> 14U);
            const std::uint8_t bank = active_mapper == msx_cartridge_mapper::ascii16_sram2
                                          ? static_cast<std::uint8_t>(bank16[slot] & 0x0FU)
                                          : bank16[slot];
            return read_cart_16k(rom, bank, address);
        }
        case msx_cartridge_mapper::plain:
        default: {
            const std::size_t phys = static_cast<std::size_t>(address - 0x4000U);
            return phys < rom.size() ? rom[phys] : 0xFFU;
        }
        }
    }

    void msx_system::write_cart(std::uint8_t slot_index, std::uint16_t address,
                                std::uint8_t value) noexcept {
        const bool primary_cart = slot_index == 0U;
        auto& active_mapper = primary_cart ? mapper : cartridge2_mapper;
        auto& bank8 = primary_cart ? cart_8k_bank : cart2_8k_bank;
        auto& bank16 = primary_cart ? cart_16k_bank : cart2_16k_bank;
        auto& sram = primary_cart ? cart_sram : cart2_sram;
        auto& scc_window = primary_cart ? scc_window_enabled : scc2_window_enabled;
        auto& korean = primary_cart ? korean_mapper : korean_mapper2;

        if (primary_cart && fmpac_sram_enabled && address >= 0x4000U && address < 0x6000U) {
            const auto offset = static_cast<std::size_t>(address - 0x4000U);
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
        if (active_mapper == msx_cartridge_mapper::ascii8_sram8 && !sram.empty() &&
            address >= 0x8000U && address < 0xC000U) {
            const auto slot = static_cast<std::size_t>((address - 0x4000U) >> 13U);
            if ((bank8[slot] & 0x80U) != 0U) {
                sram[static_cast<std::size_t>(address & 0x1FFFU) % sram.size()] = value;
                return;
            }
        }
        if (active_mapper == msx_cartridge_mapper::ascii16_sram2 && !sram.empty() &&
            address >= 0x8000U && address < 0xC000U && (bank16[1] & 0x10U) != 0U) {
            sram[static_cast<std::size_t>(address & 0x07FFU) % sram.size()] = value;
            return;
        }

        switch (active_mapper) {
        case msx_cartridge_mapper::korean_msx:
        case msx_cartridge_mapper::korean_msx_nemesis:
            korean.cpu_write(address, value);
            break;
        case msx_cartridge_mapper::ascii8:
        case msx_cartridge_mapper::ascii8_sram8:
            if (address >= 0x6000U && address < 0x8000U) {
                const auto reg = static_cast<std::size_t>((address - 0x6000U) >> 11U);
                if (reg < bank8.size()) {
                    bank8[reg] = value;
                }
            }
            break;
        case msx_cartridge_mapper::ascii16:
        case msx_cartridge_mapper::ascii16_sram2:
            if (address >= 0x6000U && address < 0x6800U) {
                bank16[0] = value;
            } else if (address >= 0x7000U && address < 0x7800U) {
                bank16[1] = value;
            }
            break;
        case msx_cartridge_mapper::konami:
            if (address >= 0x6000U && address < 0x8000U) {
                bank8[1] = value;
            } else if (address >= 0x8000U && address < 0xA000U) {
                bank8[2] = value;
            } else if (address >= 0xA000U && address < 0xC000U) {
                bank8[3] = value;
            }
            break;
        case msx_cartridge_mapper::konami_scc:
            if (address >= 0x9800U && address < 0xA000U && scc_window) {
                scc.write(address, value);
            } else if (address >= 0x5000U && address < 0x5800U) {
                bank8[0] = value;
            } else if (address >= 0x7000U && address < 0x7800U) {
                bank8[1] = value;
            } else if (address >= 0x9000U && address < 0x9800U) {
                bank8[2] = value;
                scc_window = (value & 0x3FU) == 0x3FU;
            } else if (address >= 0xB000U && address < 0xB800U) {
                bank8[3] = value;
            }
            break;
        case msx_cartridge_mapper::plain:
        default:
            break;
        }
    }

    bool msx_system::fmpac_sram_unlocked() const noexcept {
        return fmpac_unlock_latch[0] == 0x4DU && fmpac_unlock_latch[1] == 0x69U;
    }

    std::uint8_t msx_system::read_kanji(std::size_t level) noexcept {
        if (level >= kanji_char_address.size()) {
            return 0xFFU;
        }
        const std::size_t counter = kanji_byte_counter[level] & 0x1FU;
        const std::size_t offset = (level * k_kanji_level_size) +
                                   (static_cast<std::size_t>(kanji_char_address[level] & 0x0FFFU) *
                                    k_kanji_bytes_per_character) +
                                   counter;
        kanji_byte_counter[level] = static_cast<std::uint8_t>((counter + 1U) & 0x1FU);
        return offset < kanji_rom.size() ? kanji_rom[offset] : 0xFFU;
    }

    void msx_system::write_kanji_address(std::size_t level, bool upper,
                                         std::uint8_t value) noexcept {
        if (level >= kanji_char_address.size()) {
            return;
        }
        const auto bits = static_cast<std::uint16_t>(value & 0x3FU);
        if (upper) {
            kanji_char_address[level] = static_cast<std::uint16_t>(
                ((bits << 6U) | (kanji_char_address[level] & 0x003FU)) & 0x0FFFU);
        } else {
            kanji_char_address[level] =
                static_cast<std::uint16_t>((kanji_char_address[level] & 0x0FC0U) | bits);
        }
        kanji_byte_counter[level] = 0U;
    }

    std::uint8_t msx_system::read_memory(std::uint16_t address) noexcept {
        const slot_decode page3 = page3_slot();
        if (address == 0xFFFFU && primary_slot_expanded(page3.primary)) {
            return static_cast<std::uint8_t>(~secondary_slot_select[page3.primary]);
        }

        const slot_decode slot = selected_slot(address);
        if (is_bios_slot(slot)) {
            if (address < bios.size()) {
                return bios[address];
            }
            return 0xFFU;
        }
        if (is_disk_slot(slot)) {
            return read_disk(address);
        }
        const std::uint8_t cart_slot = cart_slot_index(slot);
        if (cart_slot != k_no_cartridge_slot) {
            return read_cart(cart_slot, address);
        }
        if (is_ram_slot(slot)) {
            return read_ram(address);
        }
        return 0xFFU;
    }

    void msx_system::write_memory(std::uint16_t address, std::uint8_t value) noexcept {
        const slot_decode page3 = page3_slot();
        if (address == 0xFFFFU && primary_slot_expanded(page3.primary)) {
            secondary_slot_select[page3.primary] = value;
            return;
        }

        const slot_decode slot = selected_slot(address);
        if (is_disk_slot(slot)) {
            write_disk(address, value);
            return;
        }
        const std::uint8_t cart_slot = cart_slot_index(slot);
        if (cart_slot != k_no_cartridge_slot) {
            write_cart(cart_slot, address, value);
            return;
        }
        if (is_ram_slot(slot)) {
            write_ram(address, value);
        }
    }

    std::uint8_t msx_system::psg_port_a() const noexcept {
        const bool port2 = (psg.read_reg(chips::audio::ssg::reg_port_b) & 0x40U) != 0U;
        const std::uint8_t joy = joystick_rows[port2 ? 1U : 0U] & 0x3FU;
        const std::uint8_t cassette_in = cassette.input_high() ? 0x80U : 0x00U;
        return static_cast<std::uint8_t>(joy | 0x40U | cassette_in);
    }

    void msx_system::sync_cassette_control() noexcept {
        cassette.set_motor_on((ppi_c & 0x10U) == 0U);
        cassette.set_output_high((ppi_c & 0x20U) != 0U);
    }

    bool msx_system::msx2_video() const noexcept { return video_model == msx_video_model::v9938; }

    chips::ivideo& msx_system::active_video() noexcept {
        return msx2_video() ? static_cast<chips::ivideo&>(vdp2) : static_cast<chips::ivideo&>(vdp);
    }

    const chips::ivideo& msx_system::active_video() const noexcept {
        return msx2_video() ? static_cast<const chips::ivideo&>(vdp2)
                            : static_cast<const chips::ivideo&>(vdp);
    }

    chips::frame_buffer_view msx_system::framebuffer() const noexcept {
        return active_video().framebuffer();
    }

    std::uint8_t msx_system::read_io(std::uint16_t port) noexcept {
        const auto p = static_cast<std::uint8_t>(port & 0xFFU);
        switch (p) {
        case 0x7CU:
        case 0x7DU:
            return 0xFFU;
        case 0xD0U:
            return disk_enabled ? fdc.read_register(0U) : 0xFFU;
        case 0xD1U:
            return disk_enabled ? fdc.read_register(1U) : 0xFFU;
        case 0xD2U:
            return disk_enabled ? fdc.read_register(2U) : 0xFFU;
        case 0xD3U:
            return disk_enabled ? fdc.read_register(3U) : 0xFFU;
        case 0xD4U:
            return disk_enabled ? fdc.read_control_register() : 0xFFU;
        case 0xD9U:
            return read_kanji(0U);
        case 0xDBU:
            return read_kanji(1U);
        case 0x98U:
            return msx2_video() ? vdp2.data_read() : vdp.data_read();
        case 0x99U:
            return msx2_video() ? vdp2.status_read() : vdp.status_read();
        case 0xA2U:
            return psg.selected_register() == chips::audio::ssg::reg_port_a ? psg_port_a()
                                                                            : psg.read();
        case 0xA8U:
            return primary_slot_select;
        case 0xA9U:
            return keyboard_rows[clamp_row(ppi_c)];
        case 0xAAU:
            return ppi_c;
        case 0xB5U:
            return rtc_enabled ? rtc.data_read() : 0xFFU;
        case 0xFCU:
        case 0xFDU:
        case 0xFEU:
        case 0xFFU:
            return mapped_ram.empty() ? 0xFFU
                                      : ram_mapper_page[static_cast<std::size_t>(p - 0xFCU)];
        default:
            return 0xFFU;
        }
    }

    void msx_system::write_io(std::uint16_t port, std::uint8_t value) noexcept {
        const auto p = static_cast<std::uint8_t>(port & 0xFFU);
        switch (p) {
        case 0x7CU:
            if (fm_music_enabled) {
                fm.write_address(value);
            }
            break;
        case 0x7DU:
            if (fm_music_enabled) {
                fm.write_data(value);
            }
            break;
        case 0xD0U:
            if (disk_enabled) {
                fdc.write_register(0U, value);
            }
            break;
        case 0xD1U:
            if (disk_enabled) {
                fdc.write_register(1U, value);
            }
            break;
        case 0xD2U:
            if (disk_enabled) {
                fdc.write_register(2U, value);
            }
            break;
        case 0xD3U:
            if (disk_enabled) {
                fdc.write_register(3U, value);
            }
            break;
        case 0xD4U:
            if (disk_enabled) {
                fdc.write_control_register(value);
            }
            break;
        case 0xD8U:
            write_kanji_address(0U, false, value);
            break;
        case 0xD9U:
            write_kanji_address(0U, true, value);
            break;
        case 0xDAU:
            write_kanji_address(1U, false, value);
            break;
        case 0xDBU:
            write_kanji_address(1U, true, value);
            break;
        case 0x98U:
            if (msx2_video()) {
                vdp2.data_write(value);
            } else {
                vdp.data_write(value);
            }
            break;
        case 0x99U:
            if (msx2_video()) {
                vdp2.ctrl_write(value);
            } else {
                vdp.ctrl_write(value);
            }
            break;
        case 0x9AU:
            if (msx2_video()) {
                vdp2.palette_write(value);
            }
            break;
        case 0x9BU:
            if (msx2_video()) {
                vdp2.indirect_reg_write(value);
            }
            break;
        case 0xA0U:
            psg.address(value);
            break;
        case 0xA1U:
            psg.write(value);
            break;
        case 0xA8U:
            primary_slot_select = value;
            break;
        case 0xAAU:
            ppi_c = value;
            sync_cassette_control();
            break;
        case 0xABU:
            if ((value & 0x80U) == 0U) {
                const auto bit = static_cast<std::uint8_t>((value >> 1U) & 0x07U);
                const auto mask = static_cast<std::uint8_t>(1U << bit);
                if ((value & 0x01U) != 0U) {
                    ppi_c |= mask;
                } else {
                    ppi_c &= static_cast<std::uint8_t>(~mask);
                }
                sync_cassette_control();
            }
            break;
        case 0xB4U:
            if (rtc_enabled) {
                rtc.select(value);
            }
            break;
        case 0xB5U:
            if (rtc_enabled) {
                rtc.data_write(value);
            }
            break;
        case 0xFCU:
        case 0xFDU:
        case 0xFEU:
        case 0xFFU:
            if (!mapped_ram.empty()) {
                ram_mapper_page[static_cast<std::size_t>(p - 0xFCU)] = value;
            }
            break;
        default:
            break;
        }
    }

    std::span<const std::uint8_t> msx_system::work_ram() const noexcept {
        if (!mapped_ram.empty()) {
            return mapped_ram;
        }
        return ram;
    }

    std::span<std::uint8_t> msx_system::battery_ram() noexcept {
        if (!cart_sram.empty()) {
            return cart_sram;
        }
        if (fmpac_sram_enabled) {
            return fmpac_sram;
        }
        if (!cart2_sram.empty()) {
            return cart2_sram;
        }
        return {};
    }

    void msx_system::save_state(chips::state_writer& writer) const {
        writer.u32(k_state_version);
        writer.bytes(ram);
        writer.bytes(mapped_ram);
        writer.u8(primary_slot_select);
        writer.bytes(secondary_slot_select);
        writer.bytes(ram_mapper_page);
        for (const std::uint16_t address : kanji_char_address) {
            writer.u16(address);
        }
        writer.bytes(kanji_byte_counter);
        writer.u8(ppi_c);
        writer.bytes(keyboard_rows);
        writer.bytes(joystick_rows);
        writer.bytes(cart_8k_bank);
        writer.bytes(cart_16k_bank);
        writer.boolean(scc_window_enabled);
        writer.bytes(cart2_8k_bank);
        writer.bytes(cart2_16k_bank);
        writer.boolean(scc2_window_enabled);
        writer.bytes(cart_sram);
        writer.bytes(cart2_sram);
        korean_mapper.save_state(writer);
        korean_mapper2.save_state(writer);
        scc.save_state(writer);
        writer.boolean(fm_music_enabled);
        fm.save_state(writer);
        writer.boolean(fmpac_sram_enabled);
        writer.bytes(fmpac_sram);
        writer.bytes(fmpac_unlock_latch);
        writer.boolean(rtc_enabled);
        rtc.save_state(writer);
        cassette.save_state(writer);
        writer.boolean(disk_enabled);
        fdc.save_state(writer);
        writer.u8(static_cast<std::uint8_t>(video_model));
    }

    void msx_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != k_state_version) {
            reader.fail();
            return;
        }
        reader.bytes(ram);
        reader.bytes(mapped_ram);
        primary_slot_select = reader.u8();
        reader.bytes(secondary_slot_select);
        reader.bytes(ram_mapper_page);
        for (std::uint16_t& address : kanji_char_address) {
            address = static_cast<std::uint16_t>(reader.u16() & 0x0FFFU);
        }
        reader.bytes(kanji_byte_counter);
        ppi_c = reader.u8();
        reader.bytes(keyboard_rows);
        reader.bytes(joystick_rows);
        reader.bytes(cart_8k_bank);
        reader.bytes(cart_16k_bank);
        scc_window_enabled = reader.boolean();
        reader.bytes(cart2_8k_bank);
        reader.bytes(cart2_16k_bank);
        scc2_window_enabled = reader.boolean();
        reader.bytes(cart_sram);
        reader.bytes(cart2_sram);
        korean_mapper.load_state(reader);
        korean_mapper2.load_state(reader);
        scc.load_state(reader);
        fm_music_enabled = reader.boolean();
        fm.load_state(reader);
        fmpac_sram_enabled = reader.boolean();
        reader.bytes(fmpac_sram);
        reader.bytes(fmpac_unlock_latch);
        rtc_enabled = reader.boolean();
        rtc.load_state(reader);
        cassette.load_state(reader);
        disk_enabled = reader.boolean();
        fdc.load_state(reader);
        const std::uint8_t model = reader.u8();
        video_model = model == static_cast<std::uint8_t>(msx_video_model::v9938)
                          ? msx_video_model::v9938
                          : msx_video_model::tms9918a;
    }

    std::unique_ptr<msx_system> assemble_msx(std::span<const std::uint8_t> bios,
                                             std::span<const std::uint8_t> cartridge,
                                             const msx_config& config) {
        return assemble_msx(bios, cartridge, std::span<const std::uint8_t>{},
                            std::span<const std::uint8_t>{}, std::span<const std::uint8_t>{},
                            std::span<const std::uint8_t>{}, config);
    }

    std::unique_ptr<msx_system> assemble_msx(std::span<const std::uint8_t> bios,
                                             std::span<const std::uint8_t> cartridge,
                                             std::span<const std::uint8_t> disk_rom,
                                             std::span<const std::uint8_t> disk_image,
                                             const msx_config& config) {
        return assemble_msx(bios, cartridge, std::span<const std::uint8_t>{}, disk_rom, disk_image,
                            std::span<const std::uint8_t>{}, config);
    }

    std::unique_ptr<msx_system>
    assemble_msx(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> cartridge,
                 std::span<const std::uint8_t> disk_rom, std::span<const std::uint8_t> disk_image,
                 std::span<const std::uint8_t> kanji_rom, const msx_config& config) {
        return assemble_msx(bios, cartridge, std::span<const std::uint8_t>{}, disk_rom, disk_image,
                            kanji_rom, config);
    }

    std::unique_ptr<msx_system>
    assemble_msx(std::span<const std::uint8_t> bios, std::span<const std::uint8_t> cartridge,
                 std::span<const std::uint8_t> cartridge2, std::span<const std::uint8_t> disk_rom,
                 std::span<const std::uint8_t> disk_image, std::span<const std::uint8_t> kanji_rom,
                 const msx_config& config) {
        auto sys = std::make_unique<msx_system>();
        msx_system* s = sys.get();

        const std::size_t bios_n = std::min<std::size_t>(bios.size(), s->bios.size());
        if (bios_n > 0U) {
            std::memcpy(s->bios.data(), bios.data(), bios_n);
        }
        s->cartridge.assign(cartridge.begin(), cartridge.end());
        s->cartridge2.assign(cartridge2.begin(), cartridge2.end());
        s->disk_rom.assign(disk_rom.begin(), disk_rom.end());
        s->kanji_rom.assign(kanji_rom.begin(), kanji_rom.end());
        s->video_model = config.video_model;
        s->mapper = config.cartridge_mapper;
        s->cartridge2_mapper = config.cartridge2_mapper;
        s->korean_mapper.set_variant(korean_variant(s->mapper));
        s->korean_mapper.attach_rom(s->cartridge);
        s->korean_mapper.reset(chips::reset_kind::power_on);
        s->korean_mapper2.set_variant(korean_variant(s->cartridge2_mapper));
        s->korean_mapper2.attach_rom(s->cartridge2);
        s->korean_mapper2.reset(chips::reset_kind::power_on);
        if (!s->cartridge.empty()) {
            s->cart_sram.assign(cart_sram_size(s->mapper), 0xFFU);
        }
        if (!s->cartridge2.empty()) {
            s->cart2_sram.assign(cart_sram_size(s->cartridge2_mapper), 0xFFU);
        }
        s->keyboard_rows.fill(0xFFU);
        s->expanded_primary_slots =
            static_cast<std::uint8_t>(config.expanded_primary_slots & 0x0FU);
        s->ram_primary_slot = static_cast<std::uint8_t>(config.ram_primary_slot & 0x03U);
        s->ram_secondary_slot = static_cast<std::uint8_t>(config.ram_secondary_slot & 0x03U);
        s->rtc_enabled = config.rtc_enabled;
        s->fm_music_enabled = config.fm_music_enabled;
        s->fmpac_sram_enabled = config.fmpac_sram_enabled;
        s->fmpac_sram.fill(0xFFU);
        s->fmpac_unlock_latch.fill(0xFFU);
        s->disk_enabled = config.disk_enabled || !s->disk_rom.empty() || !disk_image.empty();
        s->disk_primary_slot = static_cast<std::uint8_t>(config.disk_primary_slot & 0x03U);
        s->disk_secondary_slot = static_cast<std::uint8_t>(config.disk_secondary_slot & 0x03U);
        s->cartridge2_primary_slot =
            static_cast<std::uint8_t>(config.cartridge2_primary_slot & 0x03U);
        s->cartridge2_secondary_slot =
            static_cast<std::uint8_t>(config.cartridge2_secondary_slot & 0x03U);
        if (s->disk_enabled && s->disk_secondary_slot != 0U) {
            s->expanded_primary_slots =
                static_cast<std::uint8_t>(s->expanded_primary_slots | (1U << s->disk_primary_slot));
        }
        if (!s->cartridge2.empty() && s->cartridge2_secondary_slot != 0U) {
            s->expanded_primary_slots = static_cast<std::uint8_t>(
                s->expanded_primary_slots | (1U << s->cartridge2_primary_slot));
        }
        if (!disk_image.empty()) {
            (void)s->fdc.mount_dsk(disk_image);
        } else {
            s->fdc.reset(chips::reset_kind::power_on);
        }
        if (config.ram_mapper_segments != 0U) {
            const auto segment_count =
                static_cast<std::size_t>(std::max<std::uint8_t>(config.ram_mapper_segments, 4U));
            s->mapped_ram.assign(segment_count * 0x4000U, 0U);
        }
        s->primary_slot_select = 0x00U; // BIOS slot selected for all pages at reset
        s->ppi_c = 0xF0U;
        s->sync_cassette_control();
        s->psg.set_clock_divider(32); // MSX PSG input clock is Z80/2, then SSG /16.
        s->scc.set_clock_divider(32); // SCC native sample clock is the bus clock / 32.
        s->fm.set_clock_divider(72);  // MSX-MUSIC OPLL native rate is 3.579545 MHz / 72.
        s->rtc.set_cycles_per_second(chips::peripheral::rp5c01::default_cycles_per_second);
        s->cassette.set_cycles_per_second(chips::storage::msx_cassette::default_cycles_per_second);
        s->vdp.set_pal(config.video_region == mnemos::video_region::pal);
        s->vdp2.set_pal(config.video_region == mnemos::video_region::pal);
        s->vdp.set_irq_callback([s](bool asserted) { s->cpu.set_irq_line(asserted); });
        s->vdp2.set_irq_callback([s](bool asserted) { s->cpu.set_irq_line(asserted); });

        s->bus.map_mmio(
            0x0000U, 0x10000U,
            [s](std::uint32_t a) { return s->read_memory(static_cast<std::uint16_t>(a)); },
            [s](std::uint32_t a, std::uint8_t v) {
                s->write_memory(static_cast<std::uint16_t>(a), v);
            },
            0);
        s->cpu.set_port_in([s](std::uint16_t port) { return s->read_io(port); });
        s->cpu.set_port_out(
            [s](std::uint16_t port, std::uint8_t value) { s->write_io(port, value); });
        s->cpu.attach_bus(s->bus);
        s->cpu.reset(chips::reset_kind::power_on);
        return sys;
    }

} // namespace mnemos::manifests::msx
