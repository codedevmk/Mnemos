#include "msx2_system.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace mnemos::manifests::msx2 {

    namespace {
        constexpr std::uint32_t k_state_version = 9U;

        [[nodiscard]] std::size_t rounded_ram_size(std::size_t requested) noexcept {
            constexpr std::size_t min_size = msx2_system::page_size * 4U;
            const std::size_t base = std::max(requested, min_size);
            return ((base + msx2_system::page_size - 1U) / msx2_system::page_size) *
                   msx2_system::page_size;
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

        [[nodiscard]] std::uint16_t mirror_konami_address(std::uint16_t address) noexcept {
            if (address < 0x2000U) {
                return static_cast<std::uint16_t>(0x8000U + address);
            }
            if (address < 0x4000U) {
                return static_cast<std::uint16_t>(0xA000U + (address - 0x2000U));
            }
            if (address >= 0xC000U && address < 0xE000U) {
                return static_cast<std::uint16_t>(0x4000U + (address - 0xC000U));
            }
            if (address >= 0xE000U) {
                return static_cast<std::uint16_t>(0x6000U + (address - 0xE000U));
            }
            return address;
        }

        [[nodiscard]] std::uint32_t konami_window(std::uint16_t address) noexcept {
            return static_cast<std::uint32_t>((address - 0x4000U) >> 13U);
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

        std::uint8_t value = 0x3FU;
        if (up) {
            value = static_cast<std::uint8_t>(value & ~0x01U);
        }
        if (down) {
            value = static_cast<std::uint8_t>(value & ~0x02U);
        }
        if (left) {
            value = static_cast<std::uint8_t>(value & ~0x04U);
        }
        if (right) {
            value = static_cast<std::uint8_t>(value & ~0x08U);
        }
        if (trigger_a) {
            value = static_cast<std::uint8_t>(value & ~0x10U);
        }
        if (trigger_b) {
            value = static_cast<std::uint8_t>(value & ~0x20U);
        }
        joystick_ports[static_cast<std::size_t>(port)] = value;
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

    std::uint8_t msx2_system::read_cartridge(std::uint16_t address) const noexcept {
        if (cartridge.empty()) {
            return 0xFFU;
        }

        switch (cart_mapper) {
        case msx2_cartridge_mapper::konami:
        case msx2_cartridge_mapper::konami_scc: {
            address = mirror_konami_address(address);
            if (address < 0x4000U || address >= 0xC000U) {
                return 0xFFU;
            }
            if (scc_register_selected(address)) {
                return scc.read(address);
            }
            const std::uint32_t window = konami_window(address);
            const std::uint32_t offset = address & 0x1FFFU;
            return read_paged(cartridge, ascii8_page_size, konami_bank[window], offset);
        }
        case msx2_cartridge_mapper::ascii8: {
            if (address < 0x4000U || address >= 0xC000U) {
                return 0xFFU;
            }
            const std::uint32_t window = (address - 0x4000U) / ascii8_page_size;
            const std::uint32_t offset = address & 0x1FFFU;
            return read_paged(cartridge, ascii8_page_size, ascii8_bank[window], offset);
        }
        case msx2_cartridge_mapper::ascii16: {
            if (address < 0x4000U || address >= 0xC000U) {
                return 0xFFU;
            }
            const std::uint32_t window = (address - 0x4000U) / ascii16_page_size;
            const std::uint32_t offset = address & 0x3FFFU;
            return read_paged(cartridge, ascii16_page_size, ascii16_bank[window], offset);
        }
        case msx2_cartridge_mapper::plain:
        default: {
            if (address < 0x4000U || address >= 0xC000U) {
                return 0xFFU;
            }
            const std::size_t offset = static_cast<std::size_t>(address - 0x4000U);
            return offset < cartridge.size() ? cartridge[offset] : 0xFFU;
        }
        }
    }

    void msx2_system::write_cartridge(std::uint16_t address, std::uint8_t value) noexcept {
        switch (cart_mapper) {
        case msx2_cartridge_mapper::konami_scc:
            address = mirror_konami_address(address);
            if (scc_register_selected(address)) {
                scc.write(address, value);
            } else if (address >= 0x5000U && address < 0x5800U) {
                konami_bank[0] = static_cast<std::uint8_t>(value & 0x3FU);
            } else if (address >= 0x7000U && address < 0x7800U) {
                konami_bank[1] = static_cast<std::uint8_t>(value & 0x3FU);
            } else if (address >= 0x9000U && address < 0x9800U) {
                konami_bank[2] = static_cast<std::uint8_t>(value & 0x3FU);
            } else if (address >= 0xB000U && address < 0xB800U) {
                konami_bank[3] = static_cast<std::uint8_t>(value & 0x3FU);
            }
            break;
        case msx2_cartridge_mapper::konami:
            address = mirror_konami_address(address);
            if (address >= 0x6000U && address < 0x8000U) {
                konami_bank[1] = value;
            } else if (address >= 0x8000U && address < 0xA000U) {
                konami_bank[2] = value;
            } else if (address >= 0xA000U && address < 0xC000U) {
                konami_bank[3] = value;
            }
            break;
        case msx2_cartridge_mapper::ascii8:
            if (address >= 0x6000U && address < 0x6800U) {
                ascii8_bank[0] = value;
            } else if (address >= 0x6800U && address < 0x7000U) {
                ascii8_bank[1] = value;
            } else if (address >= 0x7000U && address < 0x7800U) {
                ascii8_bank[2] = value;
            } else if (address >= 0x7800U && address < 0x8000U) {
                ascii8_bank[3] = value;
            }
            break;
        case msx2_cartridge_mapper::ascii16:
            if (address >= 0x6000U && address < 0x7000U) {
                ascii16_bank[0] = value;
            } else if (address >= 0x7000U && address < 0x8000U) {
                ascii16_bank[1] = value;
            }
            break;
        case msx2_cartridge_mapper::plain:
        default:
            break;
        }
    }

    bool msx2_system::scc_register_selected(std::uint16_t address) const noexcept {
        return cart_mapper == msx2_cartridge_mapper::konami_scc &&
               (konami_bank[2] & 0x3FU) == 0x3FU && address >= 0x9800U && address < 0xA000U;
    }

    bool msx2_system::fdc_mmio_selected(std::uint16_t address) const noexcept {
        if (disk_rom.empty()) {
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
            return fdc.read_memory_status();
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
            fdc.set_side(static_cast<std::uint8_t>(value & 0x01U));
            break;
        case 0x05U:
            fdc.select_drive(static_cast<std::uint8_t>(value & 0x03U));
            fdc.set_motor(value != 0U);
            break;
        case 0x06U:
        case 0x07U:
        default:
            break;
        }
    }

    std::uint8_t msx2_system::read_psg_data() const noexcept {
        if (psg.selected_register() == chips::audio::ssg::reg_port_a) {
            const std::uint8_t port =
                (psg.read_reg(chips::audio::ssg::reg_port_b) & 0x40U) != 0U ? 1U : 0U;
            const std::uint8_t cassette =
                cassette_input_high ? static_cast<std::uint8_t>(0x80U) : std::uint8_t{0U};
            return static_cast<std::uint8_t>(joystick_ports[port] | 0x40U | cassette);
        }
        return psg.read();
    }

    std::uint8_t msx2_system::read_slot(std::uint8_t slot, std::uint8_t subslot,
                                        std::uint16_t address) noexcept {
        if (address == 0xFFFFU && expanded_slot[slot & 0x03U]) {
            return static_cast<std::uint8_t>(secondary_slot[slot & 0x03U] ^ 0xFFU);
        }

        switch (slot & 0x03U) {
        case 0:
            if (subslot == 0U) {
                return address < bios.size() ? bios[address] : 0xFFU;
            }
            if (subslot == 1U) {
                return address < sub_bios.size() ? sub_bios[address] : 0xFFU;
            }
            return 0xFFU;
        case 1:
            return read_cartridge(address);
        case 2:
            if (fdc_mmio_selected(address)) {
                return read_fdc_mmio(address);
            }
            if (address >= 0x4000U && address < 0x8000U) {
                const std::size_t offset = static_cast<std::size_t>(address - 0x4000U);
                return offset < disk_rom.size() ? disk_rom[offset] : 0xFFU;
            }
            return 0xFFU;
        case 3:
            return read_ram(address);
        default:
            return 0xFFU;
        }
    }

    void msx2_system::write_slot(std::uint8_t slot, std::uint8_t /*subslot*/, std::uint16_t address,
                                 std::uint8_t value) noexcept {
        if (address == 0xFFFFU && expanded_slot[slot & 0x03U]) {
            secondary_slot[slot & 0x03U] = value;
            return;
        }

        switch (slot & 0x03U) {
        case 1:
            write_cartridge(address, value);
            break;
        case 2:
            if (fdc_mmio_selected(address)) {
                write_fdc_mmio(address, value);
            }
            break;
        case 3:
            write_ram(address, value);
            break;
        case 0:
        default:
            break;
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
            return fdc.read_register(static_cast<std::uint8_t>(p - 0xD0U));
        case 0xD4:
            return fdc.read_io_status();
        case 0x98:
            return vdp.data_read();
        case 0x99:
            return vdp.ctrl_read();
        case 0xA1:
        case 0xA2:
            return read_psg_data();
        case 0xA8:
            return primary_slot;
        case 0xA9:
            return keyboard_rows[ppi_c & 0x0FU];
        case 0xAA:
            return ppi_c;
        case 0xB5:
            return rtc.read_data();
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
            fdc.write_register(static_cast<std::uint8_t>(p - 0xD0U), value);
            break;
        case 0xD4: {
            std::uint8_t drive = static_cast<std::uint8_t>((value >> 1U) & 0x01U);
            if ((value & 0x01U) != 0U) {
                drive = 0U;
            } else if ((value & 0x02U) != 0U) {
                drive = 1U;
            }
            fdc.select_drive(drive);
            fdc.set_side(static_cast<std::uint8_t>(((value >> 4U) | (value >> 2U)) & 0x01U));
            fdc.set_motor((value & 0x80U) != 0U || value == 0U || (value & 0x03U) != 0U);
            break;
        }
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
            vdp.register_indirect_write(value);
            break;
        case 0xA0:
            psg.address(value);
            break;
        case 0xA1:
        case 0xA2:
            psg.write(value);
            break;
        case 0xA8:
            primary_slot = value;
            break;
        case 0xAA:
            ppi_c = value;
            break;
        case 0xAB:
            if ((value & 0x80U) != 0U) {
                ppi_control = value;
            } else {
                const std::uint8_t bit = static_cast<std::uint8_t>((value >> 1U) & 0x07U);
                const std::uint8_t mask = static_cast<std::uint8_t>(1U << bit);
                ppi_c = (value & 0x01U) != 0U ? static_cast<std::uint8_t>(ppi_c | mask)
                                              : static_cast<std::uint8_t>(ppi_c & ~mask);
            }
            break;
        case 0xB4:
            rtc.write_address(value);
            break;
        case 0xB5:
            rtc.write_data(value);
            break;
        case 0xFC:
        case 0xFD:
        case 0xFE:
        case 0xFF:
            if (!ram.empty()) {
                const std::uint8_t segment_count =
                    static_cast<std::uint8_t>(ram.size() / page_size);
                ram_segment[p - 0xFCU] =
                    segment_count != 0U ? static_cast<std::uint8_t>(value % segment_count) : 0U;
            }
            break;
        default:
            break;
        }
    }

    void msx2_system::save_state(chips::state_writer& writer) const {
        writer.u32(k_state_version);
        writer.u8(primary_slot);
        writer.bytes(secondary_slot);
        writer.u8(ppi_c);
        writer.u8(ppi_control);
        writer.bytes(keyboard_rows);
        writer.bytes(joystick_ports);
        writer.boolean(cassette_input_high);
        writer.bytes(ram_segment);
        writer.bytes(ascii8_bank);
        writer.bytes(ascii16_bank);
        writer.bytes(konami_bank);
        scc.save_state(writer);
        rtc.save_state(writer);
        fdc.save_state(writer);
        writer.boolean(msx_music_active);
        music.save_state(writer);
        writer.bytes(std::span<const std::uint8_t>(ram));
    }

    void msx2_system::load_state(chips::state_reader& reader) {
        const std::uint32_t version = reader.u32();
        if (version == 0U || version > k_state_version) {
            reader.fail();
            return;
        }
        primary_slot = reader.u8();
        if (version >= 3U) {
            reader.bytes(secondary_slot);
        } else {
            secondary_slot = {};
        }
        ppi_c = reader.u8();
        ppi_control = reader.u8();
        reader.bytes(keyboard_rows);
        if (version >= 8U) {
            reader.bytes(joystick_ports);
        } else {
            joystick_ports = {0x3FU, 0x3FU};
        }
        cassette_input_high = version >= 9U ? reader.boolean() : true;
        reader.bytes(ram_segment);
        reader.bytes(ascii8_bank);
        reader.bytes(ascii16_bank);
        if (version >= 2U) {
            reader.bytes(konami_bank);
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
        if (version >= 5U) {
            msx_music_active = reader.boolean();
            music.load_state(reader);
        } else {
            music.reset(chips::reset_kind::power_on);
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
        s->disk_rom.assign(config.disk_rom.begin(), config.disk_rom.end());
        s->cartridge.assign(cartridge.begin(), cartridge.end());
        s->ram.resize(rounded_ram_size(config.ram_size));
        s->cart_mapper = config.cartridge_mapper;
        s->msx_music_active = config.msx_music;
        s->keyboard_rows.fill(0xFFU);
        s->joystick_ports = {0x3FU, 0x3FU};
        s->cassette_input_high = true;
        s->secondary_slot = {};
        s->expanded_slot = {false, false, false, false};
        s->expanded_slot[0] = !s->sub_bios.empty();
        s->ram_segment = {0U, 1U, 2U, 3U};
        s->ascii8_bank = {0U, 1U, 2U, 3U};
        s->ascii16_bank = {0U, 1U};
        s->konami_bank = {0U, 1U, 2U, 3U};
        s->scc.reset(chips::reset_kind::power_on);
        s->rtc.reset(chips::reset_kind::power_on);
        s->fdc.reset(chips::reset_kind::power_on);
        if (!config.disk_image.empty()) {
            (void)s->fdc.mount(config.disk_image, config.disk_write_protected);
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
