#include "taito_gnet_system.hpp"

#include <utility>

namespace mnemos::manifests::taito_gnet {
    namespace {
        constexpr std::uint32_t k_state_version = 5U;
        constexpr std::uint32_t k_config_id = 0x0002U;

        [[nodiscard]] std::uint8_t read_mirrored(std::span<const std::uint8_t> data,
                                                 std::uint32_t offset) noexcept {
            if (data.empty()) {
                return 0xFFU;
            }
            return data[static_cast<std::size_t>(offset) % data.size()];
        }

        void write_mirrored(std::span<std::uint8_t> data, std::uint32_t offset,
                            std::uint8_t value) noexcept {
            if (!data.empty()) {
                data[static_cast<std::size_t>(offset) % data.size()] = value;
            }
        }

        [[nodiscard]] std::uint8_t read_u16_lane(std::uint16_t value,
                                                 std::uint32_t lane) noexcept {
            return lane < 2U ? static_cast<std::uint8_t>(value >> (lane * 8U)) : 0x00U;
        }

        [[nodiscard]] std::uint8_t read_u32_lane(std::uint32_t value,
                                                 std::uint32_t lane) noexcept {
            return lane < 4U ? static_cast<std::uint8_t>(value >> (lane * 8U)) : 0x00U;
        }

        void write_u16_lane(std::uint16_t& target, std::uint32_t lane,
                            std::uint8_t value) noexcept {
            if (lane >= 2U) {
                return;
            }
            const std::uint16_t mask = static_cast<std::uint16_t>(0xFFU << (lane * 8U));
            target = static_cast<std::uint16_t>((target & ~mask) |
                                                (static_cast<std::uint16_t>(value) <<
                                                 (lane * 8U)));
        }

        void acknowledge_u16_lane(std::uint16_t& target, std::uint32_t lane,
                                  std::uint8_t value) noexcept {
            if (lane >= 2U) {
                return;
            }
            const std::uint16_t mask = static_cast<std::uint16_t>(0xFFU << (lane * 8U));
            const std::uint16_t keep = static_cast<std::uint16_t>(value) << (lane * 8U);
            target = static_cast<std::uint16_t>((target & ~mask) | ((target & mask) & keep));
        }

        constexpr std::uint32_t k_dma_control_direction_from_ram = 1U << 0U;
        constexpr std::uint32_t k_dma_control_step_decrement = 1U << 1U;
        constexpr std::uint32_t k_dma_control_sync_shift = 9U;
        constexpr std::uint32_t k_dma_control_sync_mask = 0x3U << k_dma_control_sync_shift;
        constexpr std::uint32_t k_dma_control_start = 1U << 24U;
        constexpr std::uint32_t k_dma_control_trigger = 1U << 28U;
        constexpr std::uint32_t k_dma_interrupt_channel_enable_base = 16U;
        constexpr std::uint32_t k_dma_interrupt_master_enable = 1U << 23U;
        constexpr std::uint32_t k_dma_interrupt_channel_flag_base = 24U;
        constexpr std::uint32_t k_dma_interrupt_master_flag = 1U << 31U;
        constexpr std::uint16_t k_irq_dma = 0x0008U;
        constexpr std::uint16_t k_irq_root_timer0 = 0x0010U;
        constexpr std::uint16_t k_root_timer_mode_reset_on_target = 1U << 3U;
        constexpr std::uint16_t k_root_timer_mode_irq_on_target = 1U << 4U;
        constexpr std::uint16_t k_root_timer_mode_irq_on_overflow = 1U << 5U;
        constexpr std::uint16_t k_root_timer_mode_reached_target = 1U << 11U;
        constexpr std::uint16_t k_root_timer_mode_reached_overflow = 1U << 12U;
        constexpr std::uint32_t k_main_ram_dma_mask = 0x001FFFFCU;
        constexpr std::uint32_t k_linked_list_end = 0x00FFFFFFU;
        constexpr std::uint32_t k_max_dma_words =
            static_cast<std::uint32_t>(taito_gnet_system::main_ram_bytes / sizeof(std::uint32_t));
        constexpr std::uint32_t k_max_dma_packets = 4096U;

        void write_u32_lane(std::uint32_t& target, std::uint32_t lane,
                            std::uint8_t value) noexcept {
            if (lane >= 4U) {
                return;
            }
            const std::uint32_t shift = lane * 8U;
            target = (target & ~(0xFFU << shift)) | (static_cast<std::uint32_t>(value) << shift);
        }

        [[nodiscard]] std::uint32_t read_bus32_le(topology::bus& bus,
                                                  std::uint32_t address) noexcept {
            return static_cast<std::uint32_t>(bus.read16_le(address)) |
                   (static_cast<std::uint32_t>(bus.read16_le(address + 2U)) << 16U);
        }

        void write_bus32_le(topology::bus& bus, std::uint32_t address,
                            std::uint32_t value) noexcept {
            bus.write16_le(address, static_cast<std::uint16_t>(value));
            bus.write16_le(address + 2U, static_cast<std::uint16_t>(value >> 16U));
        }

        [[nodiscard]] std::uint32_t bounded_dma_word_count(std::uint32_t requested) noexcept {
            if (requested == 0U) {
                requested = 0x10000U;
            }
            return requested > k_max_dma_words ? k_max_dma_words : requested;
        }

        [[nodiscard]] std::uint32_t dma_block_word_count(
            const taito_gnet_system::dma_channel& channel) noexcept {
            const std::uint32_t sync =
                (channel.control & k_dma_control_sync_mask) >> k_dma_control_sync_shift;
            const std::uint32_t block_size = bounded_dma_word_count(channel.block & 0xFFFFU);
            if (sync == 1U) {
                std::uint32_t block_count = (channel.block >> 16U) & 0xFFFFU;
                if (block_count == 0U) {
                    block_count = 0x10000U;
                }
                if (block_count > (k_max_dma_words / block_size)) {
                    return k_max_dma_words;
                }
                return block_size * block_count;
            }
            return block_size;
        }

        [[nodiscard]] bool root_timer_crossed_target(std::uint16_t old_counter,
                                                     std::uint16_t new_counter,
                                                     bool overflowed,
                                                     std::uint16_t target) noexcept {
            return overflowed ? (target > old_counter || target <= new_counter)
                              : (old_counter < target && new_counter >= target);
        }
    } // namespace

    taito_gnet_system::taito_gnet_system() {
        pcmcia_registers[pcmcia_interrupt_control_register] = pcmcia_card_reset_bit;
    }

    void taito_gnet_system::step_instructions(std::uint32_t count) {
        for (std::uint32_t n = 0; n < count; ++n) {
            const int cycles = cpu.step_instruction();
            clock_root_timers(cycles > 0 ? static_cast<std::uint32_t>(cycles) : 0U);
        }
    }

    std::span<const std::uint8_t>
    taito_gnet_system::flash_card_data(std::size_t index) const noexcept {
        if (index >= flash_cards.size()) {
            return {};
        }
        return std::span<const std::uint8_t>{flash_cards[index].media.data};
    }

    void taito_gnet_system::save_state(chips::state_writer& writer) const {
        writer.u32(k_state_version);
        writer.u8(control);
        writer.u16(control2);
        writer.u8(control3);
        writer.bytes(pcmcia_registers);
        writer.u8(pcmcia_register_index);
        writer.boolean(pcmcia_reset_asserted);
        writer.bytes(scratchpad);
        for (const std::uint32_t value : memory_control) {
            writer.u32(value);
        }
        writer.u32(ram_size);
        writer.u32(cache_control);
        writer.bytes(gpu_vram);
        writer.u32(gpu_gp0);
        writer.u32(gpu_gp1);
        writer.u32(gpu_status);
        writer.u8(gpu_gp0_fifo_count);
        for (const std::uint32_t command : gpu_gp0_fifo) {
            writer.u32(command);
        }
        writer.u16(irq_status);
        writer.u16(irq_mask);
        for (const dma_channel& channel : dma_channels) {
            writer.u32(channel.base);
            writer.u32(channel.block);
            writer.u32(channel.control);
        }
        writer.u32(dma_control);
        writer.u32(dma_interrupt);
        for (const root_timer& timer : root_timers) {
            writer.u16(timer.counter);
            writer.u16(timer.mode);
            writer.u16(timer.target);
        }
        cpu.save_state(writer);
        writer.bytes(main_ram);
        writer.bytes(firm_flash);
        writer.bytes(zoom_program_flash);
        for (const auto& wave : wave_flash) {
            writer.bytes(wave);
        }
        writer.u32(static_cast<std::uint32_t>(flash_cards.size()));
        for (const gnet_flash_card_image& card : flash_cards) {
            writer.blob(std::span<const std::uint8_t>{card.media.data});
        }
    }

    void taito_gnet_system::load_state(chips::state_reader& reader) {
        if (reader.u32() != k_state_version) {
            reader.fail();
            return;
        }
        control = reader.u8();
        control2 = reader.u16();
        control3 = reader.u8();
        reader.bytes(pcmcia_registers);
        pcmcia_register_index = reader.u8();
        pcmcia_reset_asserted = reader.boolean();
        reader.bytes(scratchpad);
        for (std::uint32_t& value : memory_control) {
            value = reader.u32();
        }
        ram_size = reader.u32();
        cache_control = reader.u32();
        reader.bytes(gpu_vram);
        gpu_gp0 = reader.u32();
        gpu_gp1 = reader.u32();
        gpu_status = reader.u32();
        gpu_gp0_fifo_count = reader.u8();
        if (gpu_gp0_fifo_count > gpu_gp0_fifo.size()) {
            reader.fail();
            return;
        }
        for (std::uint32_t& command : gpu_gp0_fifo) {
            command = reader.u32();
        }
        irq_status = reader.u16();
        irq_mask = reader.u16();
        for (dma_channel& channel : dma_channels) {
            channel.base = reader.u32();
            channel.block = reader.u32();
            channel.control = reader.u32();
        }
        dma_control = reader.u32();
        dma_interrupt = reader.u32();
        for (root_timer& timer : root_timers) {
            timer.counter = reader.u16();
            timer.mode = reader.u16();
            timer.target = reader.u16();
        }
        cpu.load_state(reader);
        reader.bytes(main_ram);
        reader.bytes(firm_flash);
        reader.bytes(zoom_program_flash);
        for (auto& wave : wave_flash) {
            reader.bytes(wave);
        }
        const std::uint32_t card_count = reader.u32();
        if (card_count != flash_cards.size()) {
            reader.fail();
            return;
        }
        refresh_cpu_interrupt_line();
        for (gnet_flash_card_image& card : flash_cards) {
            std::vector<std::uint8_t> bytes = reader.blob();
            if (bytes.size() != card.media.data.size()) {
                reader.fail();
                return;
            }
            card.media.data = std::move(bytes);
        }
    }

    std::uint8_t taito_gnet_system::read_flash_window(std::uint32_t address) const noexcept {
        const std::uint32_t window_offset = address - flash_window_base;
        const std::uint32_t selected_bank =
            (((control & 0x04U) != 0U) ? 1U : 0U) | (jp1_high_bank ? 2U : 0U);
        const std::uint32_t flash_offset = selected_bank * flash_bank_stride + window_offset;

        if (flash_offset < 0x00200000U) {
            return firm_flash[flash_offset];
        }
        if (flash_offset >= 0x00200000U && flash_offset < 0x00300000U) {
            return read_pcmcia_memory(flash_offset - 0x00200000U);
        }
        if (flash_offset >= 0x00300000U && flash_offset < 0x00380000U) {
            return zoom_program_flash[flash_offset - 0x00300000U];
        }

        if (flash_offset >= 0x08000000U && flash_offset < 0x08600000U) {
            const std::uint32_t wave_offset = flash_offset - 0x08000000U;
            const std::size_t index = wave_offset / wave_flash_bytes;
            return wave_flash[index][wave_offset % wave_flash_bytes];
        }
        if (flash_offset >= 0x10000000U && flash_offset < 0x10100000U) {
            return read_mirrored(std::span<const std::uint8_t>{bios},
                                 flash_offset - 0x10000000U);
        }
        if (flash_offset >= 0x10100000U && flash_offset < 0x10200000U) {
            return read_mirrored(std::span<const std::uint8_t>{zoom_program_flash},
                                 flash_offset - 0x10100000U);
        }
        if (flash_offset >= 0x10200000U && flash_offset < 0x10400000U) {
            return firm_flash[flash_offset - 0x10200000U];
        }

        return 0xFFU;
    }

    void taito_gnet_system::write_flash_window(std::uint32_t address,
                                               std::uint8_t value) noexcept {
        const std::uint32_t window_offset = address - flash_window_base;
        const std::uint32_t selected_bank =
            (((control & 0x04U) != 0U) ? 1U : 0U) | (jp1_high_bank ? 2U : 0U);
        const std::uint32_t flash_offset = selected_bank * flash_bank_stride + window_offset;

        if (flash_offset < 0x00200000U) {
            firm_flash[flash_offset] = value;
            return;
        }
        if (flash_offset >= 0x00200000U && flash_offset < 0x00300000U) {
            write_pcmcia_memory(flash_offset - 0x00200000U, value);
            return;
        }
        if (flash_offset >= 0x00300000U && flash_offset < 0x00380000U) {
            zoom_program_flash[flash_offset - 0x00300000U] = value;
            return;
        }

        if (flash_offset >= 0x08000000U && flash_offset < 0x08600000U) {
            const std::uint32_t wave_offset = flash_offset - 0x08000000U;
            const std::size_t index = wave_offset / wave_flash_bytes;
            wave_flash[index][wave_offset % wave_flash_bytes] = value;
            return;
        }
        if (flash_offset >= 0x10100000U && flash_offset < 0x10200000U) {
            write_mirrored(std::span<std::uint8_t>{zoom_program_flash},
                           flash_offset - 0x10100000U, value);
            return;
        }
        if (flash_offset >= 0x10200000U && flash_offset < 0x10400000U) {
            firm_flash[flash_offset - 0x10200000U] = value;
        }
    }

    std::uint8_t taito_gnet_system::read_pcmcia_memory(std::uint32_t offset) const noexcept {
        if (flash_cards.empty()) {
            return 0xFFU;
        }
        const auto& data = flash_cards.front().media.data;
        return offset < data.size() ? data[offset] : 0xFFU;
    }

    void taito_gnet_system::write_pcmcia_memory(std::uint32_t offset,
                                                std::uint8_t value) noexcept {
        if (!flash_cards.empty() && offset < flash_cards.front().media.data.size()) {
            flash_cards.front().media.data[offset] = value;
        }
    }

    std::uint8_t taito_gnet_system::read_pcmcia_io(std::uint32_t address) const noexcept {
        const std::uint32_t offset = address - pcmcia_io_base;
        if (offset == pcmcia_index_offset) {
            return pcmcia_register_index;
        }
        if (offset == pcmcia_data_offset) {
            return pcmcia_registers[pcmcia_register_index];
        }
        return read_pcmcia_memory(offset);
    }

    void taito_gnet_system::write_pcmcia_io(std::uint32_t address,
                                            std::uint8_t value) noexcept {
        const std::uint32_t offset = address - pcmcia_io_base;
        if (offset == pcmcia_index_offset) {
            pcmcia_register_index = value;
            return;
        }
        if (offset == pcmcia_data_offset) {
            pcmcia_registers[pcmcia_register_index] = value;
            if (pcmcia_register_index == pcmcia_interrupt_control_register) {
                pcmcia_reset_asserted = (value & pcmcia_card_reset_bit) == 0U;
            }
            return;
        }
        write_pcmcia_memory(offset, value);
    }

    std::uint8_t taito_gnet_system::read_control(std::uint32_t address) const noexcept {
        switch (address) {
        case control_address:
            return control;
        case control2_address:
            return static_cast<std::uint8_t>(control2);
        case control2_address + 1U:
            return static_cast<std::uint8_t>(control2 >> 8U);
        case control3_address:
            return control3;
        case config_id_address:
            return static_cast<std::uint8_t>(k_config_id);
        case config_id_address + 1U:
            return static_cast<std::uint8_t>(k_config_id >> 8U);
        default:
            return 0xFFU;
        }
    }

    void taito_gnet_system::write_control(std::uint32_t address, std::uint8_t value) noexcept {
        switch (address) {
        case control_address:
            control = value;
            break;
        case control2_address:
            control2 = static_cast<std::uint16_t>((control2 & 0xFF00U) | value);
            break;
        case control2_address + 1U:
            control2 = static_cast<std::uint16_t>((control2 & 0x00FFU) |
                                                  (static_cast<std::uint16_t>(value) << 8U));
            break;
        case control3_address:
            control3 = value;
            break;
        default:
            break;
        }
    }

    std::uint8_t taito_gnet_system::read_memory_control(std::uint32_t address) const noexcept {
        if (address >= memory_control_base && address < memory_control_base + memory_control_bytes) {
            const std::uint32_t offset = address - memory_control_base;
            return read_u32_lane(memory_control[offset / 4U], offset % 4U);
        }
        if (address >= ram_size_address && address < ram_size_address + 4U) {
            return read_u32_lane(ram_size, address - ram_size_address);
        }
        if (address >= cache_control_address && address < cache_control_address + 4U) {
            return read_u32_lane(cache_control, address - cache_control_address);
        }
        return 0xFFU;
    }

    void taito_gnet_system::write_memory_control(std::uint32_t address,
                                                 std::uint8_t value) noexcept {
        if (address >= memory_control_base && address < memory_control_base + memory_control_bytes) {
            const std::uint32_t offset = address - memory_control_base;
            write_u32_lane(memory_control[offset / 4U], offset % 4U, value);
            return;
        }
        if (address >= ram_size_address && address < ram_size_address + 4U) {
            write_u32_lane(ram_size, address - ram_size_address, value);
            return;
        }
        if (address >= cache_control_address && address < cache_control_address + 4U) {
            write_u32_lane(cache_control, address - cache_control_address, value);
        }
    }

    std::uint8_t taito_gnet_system::read_gpu(std::uint32_t address) const noexcept {
        if (address >= gpu_gp0_address && address < gpu_gp0_address + 4U) {
            return read_u32_lane(gpu_gp0, address - gpu_gp0_address);
        }
        if (address >= gpu_gp1_address && address < gpu_gp1_address + 4U) {
            return read_u32_lane(gpu_status, address - gpu_gp1_address);
        }
        return 0xFFU;
    }

    void taito_gnet_system::write_gpu(std::uint32_t address, std::uint8_t value) noexcept {
        if (address >= gpu_gp0_address && address < gpu_gp0_address + 4U) {
            const std::uint32_t lane = address - gpu_gp0_address;
            write_u32_lane(gpu_gp0, lane, value);
            if (lane == 3U) {
                push_gpu_gp0(gpu_gp0);
            }
            return;
        }
        if (address >= gpu_gp1_address && address < gpu_gp1_address + 4U) {
            const std::uint32_t lane = address - gpu_gp1_address;
            write_u32_lane(gpu_gp1, lane, value);
            if (lane == 3U) {
                apply_gpu_gp1(gpu_gp1);
            }
        }
    }

    void taito_gnet_system::push_gpu_gp0(std::uint32_t value) noexcept {
        gpu_gp0 = value;
        if (gpu_gp0_fifo_count < gpu_gp0_fifo.size()) {
            gpu_gp0_fifo[gpu_gp0_fifo_count] = value;
            ++gpu_gp0_fifo_count;
            return;
        }
        for (std::size_t i = 1U; i < gpu_gp0_fifo.size(); ++i) {
            gpu_gp0_fifo[i - 1U] = gpu_gp0_fifo[i];
        }
        gpu_gp0_fifo.back() = value;
    }

    void taito_gnet_system::apply_gpu_gp1(std::uint32_t value) noexcept {
        const std::uint8_t command = static_cast<std::uint8_t>(value >> 24U);
        if (command == 0x00U) {
            gpu_gp0 = 0U;
            gpu_status = gpu_reset_status;
            gpu_gp0_fifo.fill(0U);
            gpu_gp0_fifo_count = 0U;
        }
    }

    void taito_gnet_system::request_interrupt(std::uint16_t mask) noexcept {
        irq_status = static_cast<std::uint16_t>(irq_status | mask);
        refresh_cpu_interrupt_line();
    }

    void taito_gnet_system::refresh_cpu_interrupt_line() noexcept {
        cpu.set_external_interrupt_line((irq_status & irq_mask) != 0U);
    }

    std::uint8_t taito_gnet_system::read_irq(std::uint32_t address) const noexcept {
        const std::uint32_t offset = address - irq_status_address;
        if (offset < 4U) {
            return read_u16_lane(irq_status, offset);
        }
        if (offset < 8U) {
            return read_u16_lane(irq_mask, offset - 4U);
        }
        return 0xFFU;
    }

    void taito_gnet_system::write_irq(std::uint32_t address, std::uint8_t value) noexcept {
        const std::uint32_t offset = address - irq_status_address;
        if (offset < 4U) {
            acknowledge_u16_lane(irq_status, offset, value);
        } else if (offset < 8U) {
            write_u16_lane(irq_mask, offset - 4U, value);
        }
        refresh_cpu_interrupt_line();
    }

    std::uint8_t taito_gnet_system::read_dma(std::uint32_t address) const noexcept {
        const std::uint32_t offset = address - dma_register_base;
        if (offset < 0x70U) {
            const std::uint32_t channel_index = offset / 0x10U;
            const std::uint32_t channel_offset = offset % 0x10U;
            if (channel_index >= dma_channels.size()) {
                return 0xFFU;
            }
            const dma_channel& channel = dma_channels[channel_index];
            if (channel_offset < 4U) {
                return read_u32_lane(channel.base, channel_offset);
            }
            if (channel_offset >= 4U && channel_offset < 8U) {
                return read_u32_lane(channel.block, channel_offset - 4U);
            }
            if (channel_offset >= 8U && channel_offset < 12U) {
                return read_u32_lane(channel.control, channel_offset - 8U);
            }
            return 0x00U;
        }
        if (offset >= 0x70U && offset < 0x74U) {
            return read_u32_lane(dma_control, offset - 0x70U);
        }
        if (offset >= 0x74U && offset < 0x78U) {
            return read_u32_lane(dma_interrupt, offset - 0x74U);
        }
        return 0x00U;
    }

    void taito_gnet_system::write_dma(std::uint32_t address, std::uint8_t value) noexcept {
        const std::uint32_t offset = address - dma_register_base;
        if (offset < 0x70U) {
            const std::uint32_t channel_index = offset / 0x10U;
            const std::uint32_t channel_offset = offset % 0x10U;
            if (channel_index >= dma_channels.size()) {
                return;
            }
            dma_channel& channel = dma_channels[channel_index];
            if (channel_offset < 4U) {
                write_u32_lane(channel.base, channel_offset, value);
            } else if (channel_offset >= 4U && channel_offset < 8U) {
                write_u32_lane(channel.block, channel_offset - 4U, value);
            } else if (channel_offset >= 8U && channel_offset < 12U) {
                const std::uint32_t lane = channel_offset - 8U;
                write_u32_lane(channel.control, lane, value);
                if (lane == 3U) {
                    execute_dma_channel(channel_index);
                }
            }
            return;
        }
        if (offset >= 0x70U && offset < 0x74U) {
            write_u32_lane(dma_control, offset - 0x70U, value);
        } else if (offset >= 0x74U && offset < 0x78U) {
            write_u32_lane(dma_interrupt, offset - 0x74U, value);
        }
    }

    void taito_gnet_system::execute_dma_channel(std::uint32_t channel_index) noexcept {
        if (channel_index >= dma_channels.size()) {
            return;
        }
        dma_channel& channel = dma_channels[channel_index];
        if ((channel.control & k_dma_control_start) == 0U) {
            return;
        }

        switch (channel_index) {
        case 2U:
            execute_gpu_dma(channel);
            break;
        case 6U:
            execute_otc_dma(channel);
            break;
        default:
            break;
        }
        complete_dma_channel(channel_index);
    }

    void taito_gnet_system::execute_gpu_dma(dma_channel& channel) noexcept {
        if ((channel.control & k_dma_control_direction_from_ram) == 0U) {
            return;
        }

        const std::uint32_t sync =
            (channel.control & k_dma_control_sync_mask) >> k_dma_control_sync_shift;
        if (sync == 2U) {
            std::uint32_t address = channel.base & k_main_ram_dma_mask;
            for (std::uint32_t packet = 0U; packet < k_max_dma_packets; ++packet) {
                const std::uint32_t header = read_bus32_le(bus, address);
                const std::uint32_t word_count = (header >> 24U) & 0xFFU;
                for (std::uint32_t word = 0U; word < word_count; ++word) {
                    address = (address + 4U) & k_main_ram_dma_mask;
                    push_gpu_gp0(read_bus32_le(bus, address));
                }
                const std::uint32_t next = header & 0x00FFFFFFU;
                if (next == k_linked_list_end) {
                    break;
                }
                address = next & k_main_ram_dma_mask;
            }
            return;
        }

        std::uint32_t address = channel.base & k_main_ram_dma_mask;
        const std::uint32_t step =
            (channel.control & k_dma_control_step_decrement) != 0U ? 0xFFFFFFFCU : 4U;
        const std::uint32_t word_count = dma_block_word_count(channel);
        for (std::uint32_t word = 0U; word < word_count; ++word) {
            push_gpu_gp0(read_bus32_le(bus, address));
            address = (address + step) & k_main_ram_dma_mask;
        }
    }

    void taito_gnet_system::execute_otc_dma(dma_channel& channel) noexcept {
        std::uint32_t address = channel.base & k_main_ram_dma_mask;
        const std::uint32_t word_count = bounded_dma_word_count(channel.block & 0xFFFFU);
        for (std::uint32_t word = 0U; word < word_count; ++word) {
            const bool last = word + 1U == word_count;
            const std::uint32_t next = last ? k_linked_list_end : ((address - 4U) & k_main_ram_dma_mask);
            write_bus32_le(bus, address, next);
            address = next & k_main_ram_dma_mask;
        }
    }

    void taito_gnet_system::complete_dma_channel(std::uint32_t channel_index) noexcept {
        if (channel_index >= dma_channels.size()) {
            return;
        }
        dma_channels[channel_index].control &= ~(k_dma_control_start | k_dma_control_trigger);

        const std::uint32_t channel_flag = 1U << (k_dma_interrupt_channel_flag_base + channel_index);
        dma_interrupt |= channel_flag;
        const std::uint32_t channel_enable =
            1U << (k_dma_interrupt_channel_enable_base + channel_index);
        if ((dma_interrupt & k_dma_interrupt_master_enable) != 0U &&
            (dma_interrupt & channel_enable) != 0U) {
            dma_interrupt |= k_dma_interrupt_master_flag;
            request_interrupt(k_irq_dma);
        }
    }

    std::uint8_t taito_gnet_system::read_root_timer(std::uint32_t address) const noexcept {
        const std::uint32_t offset = address - root_timer_base;
        const std::uint32_t index = offset / 0x10U;
        const std::uint32_t timer_offset = offset % 0x10U;
        if (index >= root_timers.size()) {
            return 0xFFU;
        }
        const root_timer& timer = root_timers[index];
        if (timer_offset < 2U) {
            return read_u16_lane(timer.counter, timer_offset);
        }
        if (timer_offset >= 4U && timer_offset < 6U) {
            return read_u16_lane(timer.mode, timer_offset - 4U);
        }
        if (timer_offset >= 8U && timer_offset < 10U) {
            return read_u16_lane(timer.target, timer_offset - 8U);
        }
        return 0x00U;
    }

    void taito_gnet_system::write_root_timer(std::uint32_t address,
                                             std::uint8_t value) noexcept {
        const std::uint32_t offset = address - root_timer_base;
        const std::uint32_t index = offset / 0x10U;
        const std::uint32_t timer_offset = offset % 0x10U;
        if (index >= root_timers.size()) {
            return;
        }
        root_timer& timer = root_timers[index];
        if (timer_offset < 2U) {
            write_u16_lane(timer.counter, timer_offset, value);
        } else if (timer_offset >= 4U && timer_offset < 6U) {
            write_u16_lane(timer.mode, timer_offset - 4U, value);
        } else if (timer_offset >= 8U && timer_offset < 10U) {
            write_u16_lane(timer.target, timer_offset - 8U, value);
        }
    }

    void taito_gnet_system::clock_root_timers(std::uint32_t cycles) noexcept {
        if (cycles == 0U) {
            return;
        }
        for (std::size_t index = 0U; index < root_timers.size(); ++index) {
            root_timer& timer = root_timers[index];
            const std::uint16_t old_counter = timer.counter;
            const std::uint32_t advanced = static_cast<std::uint32_t>(old_counter) + cycles;
            const auto new_counter = static_cast<std::uint16_t>(advanced);
            const bool overflowed = advanced > 0xFFFFU;
            const bool reached_target =
                root_timer_crossed_target(old_counter, new_counter, overflowed, timer.target);

            timer.counter = new_counter;
            const auto timer_irq_mask = static_cast<std::uint16_t>(k_irq_root_timer0 << index);
            if (reached_target) {
                timer.mode = static_cast<std::uint16_t>(
                    timer.mode | k_root_timer_mode_reached_target);
                if ((timer.mode & k_root_timer_mode_irq_on_target) != 0U) {
                    request_interrupt(timer_irq_mask);
                }
                if ((timer.mode & k_root_timer_mode_reset_on_target) != 0U) {
                    timer.counter = 0U;
                }
            }
            if (overflowed) {
                timer.mode = static_cast<std::uint16_t>(
                    timer.mode | k_root_timer_mode_reached_overflow);
                if ((timer.mode & k_root_timer_mode_irq_on_overflow) != 0U) {
                    request_interrupt(timer_irq_mask);
                }
            }
        }
    }

    std::unique_ptr<taito_gnet_system> assemble_taito_gnet(taito_gnet_config config) {
        if (config.bios.empty() ||
            config.bios.size() > taito_gnet_system::boot_rom_max_bytes ||
            config.flash_cards.empty()) {
            return nullptr;
        }

        auto sys = std::make_unique<taito_gnet_system>();
        sys->bios = std::move(config.bios);
        sys->flash_cards = std::move(config.flash_cards);
        sys->jp1_high_bank = config.jp1_high_bank;

        sys->bus.map_ram(0x00000000U, std::span<std::uint8_t>{sys->main_ram});
        sys->bus.map_ram(taito_gnet_system::scratchpad_base,
                         std::span<std::uint8_t>{sys->scratchpad});
        sys->bus.map_rom(taito_gnet_system::boot_rom_base,
                         std::span<const std::uint8_t>{sys->bios});
        sys->bus.map_mmio(
            taito_gnet_system::flash_window_base, taito_gnet_system::flash_window_bytes,
            [sys = sys.get()](std::uint32_t address) { return sys->read_flash_window(address); },
            [sys = sys.get()](std::uint32_t address, std::uint8_t value) {
                sys->write_flash_window(address, value);
            });
        sys->bus.map_mmio(
            taito_gnet_system::pcmcia_io_base, taito_gnet_system::pcmcia_io_bytes,
            [sys = sys.get()](std::uint32_t address) { return sys->read_pcmcia_io(address); },
            [sys = sys.get()](std::uint32_t address, std::uint8_t value) {
                sys->write_pcmcia_io(address, value);
            });
        sys->bus.map_mmio(
            taito_gnet_system::control3_address, 4U,
            [sys = sys.get()](std::uint32_t address) { return sys->read_control(address); },
            [sys = sys.get()](std::uint32_t address, std::uint8_t value) {
                sys->write_control(address, value);
            });
        sys->bus.map_mmio(
            taito_gnet_system::control_address, 4U,
            [sys = sys.get()](std::uint32_t address) { return sys->read_control(address); },
            [sys = sys.get()](std::uint32_t address, std::uint8_t value) {
                sys->write_control(address, value);
            });
        sys->bus.map_mmio(
            taito_gnet_system::control2_address, 4U,
            [sys = sys.get()](std::uint32_t address) { return sys->read_control(address); },
            [sys = sys.get()](std::uint32_t address, std::uint8_t value) {
                sys->write_control(address, value);
            });
        sys->bus.map_mmio(
            taito_gnet_system::memory_control_base, taito_gnet_system::memory_control_bytes,
            [sys = sys.get()](std::uint32_t address) {
                return sys->read_memory_control(address);
            },
            [sys = sys.get()](std::uint32_t address, std::uint8_t value) {
                sys->write_memory_control(address, value);
            });
        sys->bus.map_mmio(
            taito_gnet_system::ram_size_address, 4U,
            [sys = sys.get()](std::uint32_t address) {
                return sys->read_memory_control(address);
            },
            [sys = sys.get()](std::uint32_t address, std::uint8_t value) {
                sys->write_memory_control(address, value);
            });
        sys->bus.map_mmio(
            taito_gnet_system::cache_control_address, 4U,
            [sys = sys.get()](std::uint32_t address) {
                return sys->read_memory_control(address);
            },
            [sys = sys.get()](std::uint32_t address, std::uint8_t value) {
                sys->write_memory_control(address, value);
            });
        sys->bus.map_mmio(
            taito_gnet_system::gpu_gp0_address, 8U,
            [sys = sys.get()](std::uint32_t address) { return sys->read_gpu(address); },
            [sys = sys.get()](std::uint32_t address, std::uint8_t value) {
                sys->write_gpu(address, value);
            });
        sys->bus.map_mmio(
            taito_gnet_system::irq_status_address, 8U,
            [sys = sys.get()](std::uint32_t address) { return sys->read_irq(address); },
            [sys = sys.get()](std::uint32_t address, std::uint8_t value) {
                sys->write_irq(address, value);
            });
        sys->bus.map_mmio(
            taito_gnet_system::dma_register_base, taito_gnet_system::dma_register_bytes,
            [sys = sys.get()](std::uint32_t address) { return sys->read_dma(address); },
            [sys = sys.get()](std::uint32_t address, std::uint8_t value) {
                sys->write_dma(address, value);
            });
        sys->bus.map_mmio(
            taito_gnet_system::root_timer_base,
            taito_gnet_system::root_timer_register_bytes,
            [sys = sys.get()](std::uint32_t address) {
                return sys->read_root_timer(address);
            },
            [sys = sys.get()](std::uint32_t address, std::uint8_t value) {
                sys->write_root_timer(address, value);
            });
        sys->bus.map_mmio(
            taito_gnet_system::config_id_address, 4U,
            [sys = sys.get()](std::uint32_t address) { return sys->read_control(address); },
            [sys = sys.get()](std::uint32_t address, std::uint8_t value) {
                sys->write_control(address, value);
            });
        sys->cpu.attach_bus(sys->bus);
        sys->cpu.reset(chips::reset_kind::power_on);
        return sys;
    }

    std::unique_ptr<taito_gnet_system>
    assemble_taito_gnet_from_package(std::vector<std::uint8_t> bios,
                                     std::span<const std::uint8_t> package_bytes,
                                     std::uint64_t max_card_bytes) {
        auto cards = load_gnet_flash_cards(package_bytes, max_card_bytes);
        if (!cards) {
            return nullptr;
        }

        taito_gnet_config config;
        config.bios = std::move(bios);
        config.flash_cards = std::move(*cards);
        return assemble_taito_gnet(std::move(config));
    }

} // namespace mnemos::manifests::taito_gnet
